#include "display.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/time.h"
// pioasm emits C-style designated initializers, which -Wpedantic rejects before C++20.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wc++20-extensions"
#include "ws2812.pio.h"
#pragma GCC diagnostic pop

namespace display {
namespace {

#ifdef PLASMA2350_DATA_PIN
constexpr uint kDataPin = PLASMA2350_DATA_PIN;
#else
constexpr uint kDataPin = 15;
#endif

struct Layout {
    bool column_major;  // strand runs along columns rather than rows
    bool serpentine;    // every other column/row runs in the opposite direction
    bool flip_x;
    bool flip_y;
};
// Verified on hardware: strand starts top-left and runs down column 0, then zig-zags column by column.
constexpr Layout kLayout{true, true, false, false};

enum class PowerMode { Usb, External };
constexpr PowerMode kPowerMode = PowerMode::Usb;
constexpr uint8_t kUsbMaxBrightness = 128;  // 50 %
constexpr uint8_t kExternalMaxBrightness = 255;
constexpr uint8_t kRequestedBrightness = 64;  // 25 %
constexpr uint32_t kUsbLedBudgetMa = 400;
constexpr uint32_t kExternalLedBudgetMa = 4000;
// Conservative full-on draw of one WS2812B colour channel.
constexpr uint32_t kChannelFullScaleMa = 20;

constexpr uint32_t kBitRateHz = 800000;
constexpr float kGamma = 2.2f;
// 30 us per pixel plus the >280 us latch gap and FIFO drain.
constexpr uint64_t kFrameTimeUs = kPixelCount * 30 + 400;

Rgb frame[kPixelCount];
uint32_t output[kPixelCount];
uint8_t gamma_table[256];

PIO pio = nullptr;
uint state_machine = 0;
int dma_channel = -1;
uint64_t ready_at_us = 0;

}  // namespace

Rgb hue_to_rgb(int32_t hue) {
    hue %= kHueSteps;
    if (hue < 0) {
        hue += kHueSteps;
    }
    const auto rising = static_cast<uint8_t>(hue & 0xff);
    const auto falling = static_cast<uint8_t>(255 - rising);
    switch (hue >> 8) {
    case 0: return {255, rising, 0};
    case 1: return {falling, 255, 0};
    case 2: return {0, 255, rising};
    case 3: return {0, falling, 255};
    case 4: return {rising, 0, 255};
    default: return {255, 0, falling};
    }
}

Rgb dim(Rgb color, uint8_t level) {
    return {static_cast<uint8_t>(color.r * level / 255), static_cast<uint8_t>(color.g * level / 255),
            static_cast<uint8_t>(color.b * level / 255)};
}

bool init() {
    for (int i = 0; i < 256; ++i) {
        gamma_table[i] = static_cast<uint8_t>(std::lround(std::pow(i / 255.0f, kGamma) * 255.0f));
    }

    uint offset = 0;
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &state_machine, &offset, kDataPin, 1,
                                                          true)) {
        return false;
    }
    pio_gpio_init(pio, kDataPin);
    pio_sm_set_consecutive_pindirs(pio, state_machine, kDataPin, 1, true);
    pio_sm_config config = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, kDataPin);
    sm_config_set_out_shift(&config, false, true, 24);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    const uint32_t cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    sm_config_set_clkdiv(&config, static_cast<float>(clock_get_hz(clk_sys)) / static_cast<float>(kBitRateHz * cycles_per_bit));
    pio_sm_init(pio, state_machine, offset, &config);
    pio_sm_set_enabled(pio, state_machine, true);

    dma_channel = dma_claim_unused_channel(false);
    if (dma_channel < 0) {
        return false;
    }
    dma_channel_config dma_config = dma_channel_get_default_config(static_cast<uint>(dma_channel));
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio, state_machine, true));
    dma_channel_configure(static_cast<uint>(dma_channel), &dma_config, &pio->txf[state_machine], output, kPixelCount,
                          false);

    clear();
    return show();
}

void clear() {
    std::fill(std::begin(frame), std::end(frame), Rgb{0, 0, 0});
}

int strand_index(int x, int y) {
    if (kLayout.flip_x) {
        x = kWidth - 1 - x;
    }
    if (kLayout.flip_y) {
        y = kHeight - 1 - y;
    }
    if (kLayout.column_major) {
        const int position = kLayout.serpentine && (x & 1) != 0 ? kHeight - 1 - y : y;
        return x * kHeight + position;
    }
    const int position = kLayout.serpentine && (y & 1) != 0 ? kWidth - 1 - x : x;
    return y * kWidth + position;
}

void set_pixel(int x, int y, Rgb color) {
    if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
        frame[strand_index(x, y)] = color;
    }
}

void set_strand_pixel(int index, Rgb color) {
    if (index >= 0 && index < kPixelCount) {
        frame[index] = color;
    }
}

bool busy() {
    return dma_channel < 0 || time_us_64() < ready_at_us;
}

bool show() {
    if (busy()) {
        return false;
    }

    const uint32_t brightness =
        std::min(kRequestedBrightness, kPowerMode == PowerMode::Usb ? kUsbMaxBrightness : kExternalMaxBrightness);
    const uint32_t budget_ma = kPowerMode == PowerMode::Usb ? kUsbLedBudgetMa : kExternalLedBudgetMa;

    uint32_t channel_sum = 0;
    for (int i = 0; i < kPixelCount; ++i) {
        const uint32_t r = gamma_table[frame[i].r] * brightness / 255;
        const uint32_t g = gamma_table[frame[i].g] * brightness / 255;
        const uint32_t b = gamma_table[frame[i].b] * brightness / 255;
        channel_sum += r + g + b;
        output[i] = (r << 16) | (g << 8) | b;
    }

    const uint32_t estimated_ma = static_cast<uint32_t>(uint64_t{channel_sum} * kChannelFullScaleMa / 255);
    const uint32_t scale = estimated_ma > budget_ma ? budget_ma * 256 / estimated_ma : 256;
    for (uint32_t& pixel : output) {
        const uint32_t r = ((pixel >> 16) & 0xff) * scale / 256;
        const uint32_t g = ((pixel >> 8) & 0xff) * scale / 256;
        const uint32_t b = (pixel & 0xff) * scale / 256;
        pixel = (g << 24) | (r << 16) | (b << 8);  // GRB, left-aligned for the 24-bit PIO shift
    }

    dma_channel_transfer_from_buffer_now(static_cast<uint>(dma_channel), output, kPixelCount);
    ready_at_us = time_us_64() + kFrameTimeUs;
    return true;
}

}  // namespace display
