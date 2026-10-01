#include "animations.hpp"

#include <cstdio>
#include <cstring>

#include "display.hpp"
#include "pico/rand.h"
#include "text.hpp"

namespace animation {
namespace {

constexpr uint64_t kFrameUs = 33000;

enum class Kind { RainbowWave, Sparkle, Rain, Wipe, Inverted, Count };

struct Info {
    const char* name;
    uint64_t duration_us;
};

constexpr Info kInfo[] = {
    {"rainbow wave", 5000000},
    {"sparkle", 5000000},
    {"rain", 6000000},
    {"wipe", 3000000},
    {"inverted time", 8000000},
};
constexpr int kKindCount = static_cast<int>(Kind::Count);
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == kKindCount, "one Info per animation");

struct Drop {
    int16_t head;  // quarter-pixel units, may start above the panel
    uint8_t speed;
    int32_t hue;
};

constexpr int kRainTail = 4;

Kind kind = Kind::RainbowWave;
int last_kind = -1;
uint64_t started_us = 0;
uint64_t next_frame_us = 0;
int32_t phase = 0;

uint8_t sparkle_level[display::kPixelCount];
int32_t sparkle_hue[display::kPixelCount];
Drop drops[display::kWidth];

int32_t random_below(int32_t limit) {
    return static_cast<int32_t>(get_rand_32() % static_cast<uint32_t>(limit));
}

void reset_drop(Drop& drop) {
    drop.head = static_cast<int16_t>(-4 * (1 + random_below(12)));
    drop.speed = static_cast<uint8_t>(1 + random_below(3));
    drop.hue = phase + random_below(256);
}

void draw_sparkle() {
    for (int i = 0; i < display::kPixelCount; ++i) {
        sparkle_level[i] = static_cast<uint8_t>(sparkle_level[i] * 7 / 8);
    }
    for (int n = 0; n < 3; ++n) {
        const int i = random_below(display::kPixelCount);
        sparkle_level[i] = 255;
        sparkle_hue[i] = random_below(display::kHueSteps);
    }
    for (int i = 0; i < display::kPixelCount; ++i) {
        if (sparkle_level[i] != 0) {
            display::set_pixel(i % display::kWidth, i / display::kWidth,
                               display::dim(display::hue_to_rgb(sparkle_hue[i]), sparkle_level[i]));
        }
    }
}

void draw_rain() {
    for (int x = 0; x < display::kWidth; ++x) {
        Drop& drop = drops[x];
        drop.head = static_cast<int16_t>(drop.head + drop.speed);
        const int head_y = drop.head / 4;
        if (head_y - kRainTail >= display::kHeight) {
            reset_drop(drop);
            continue;
        }
        for (int t = 0; t <= kRainTail; ++t) {
            const auto level = static_cast<uint8_t>(255 - t * (255 / (kRainTail + 1)));
            display::set_pixel(x, head_y - t, display::dim(display::hue_to_rgb(drop.hue), level));
        }
    }
}

void draw_wipe(uint64_t elapsed_us, uint64_t duration_us) {
    // Fill left-to-right during the first half, then clear left-to-right.
    const int position = static_cast<int>(elapsed_us * 2 * display::kWidth / duration_us);
    for (int x = 0; x < display::kWidth; ++x) {
        const bool lit = position < display::kWidth ? x < position : x >= position - display::kWidth;
        if (!lit) {
            continue;
        }
        for (int y = 0; y < display::kHeight; ++y) {
            display::set_pixel(x, y, display::hue_to_rgb(phase + x * 24 + y * 12));
        }
    }
}

void draw_inverted(const char* time_text, bool colon_on) {
    draw_gradient(phase);
    const int x = (display::kWidth - text::width(time_text)) / 2;
    text::for_each_pixel(time_text, x, [colon_on](int index, int px, int py) {
        if (index == 2 && !colon_on) {
            return;
        }
        display::set_pixel(px, py, {0, 0, 0});
    });
}

}  // namespace

void draw_gradient(int32_t gradient_phase) {
    for (int y = 0; y < display::kHeight; ++y) {
        for (int x = 0; x < display::kWidth; ++x) {
            display::set_pixel(x, y, display::hue_to_rgb(gradient_phase + x * 48 + y * 24));
        }
    }
}

void start_random(uint64_t now_us) {
    int choice = random_below(kKindCount);
    if (choice == last_kind) {
        choice = (choice + 1 + random_below(kKindCount - 1)) % kKindCount;
    }
    last_kind = choice;
    kind = static_cast<Kind>(choice);
    started_us = now_us;
    next_frame_us = 0;
    phase = random_below(display::kHueSteps);
    std::memset(sparkle_level, 0, sizeof(sparkle_level));
    for (Drop& drop : drops) {
        reset_drop(drop);
    }
    std::printf("Animation: %s\n", kInfo[choice].name);
}

bool step(uint64_t now_us, const char* time_text, bool colon_on) {
    const uint64_t elapsed_us = now_us - started_us;
    const uint64_t duration_us = kInfo[static_cast<int>(kind)].duration_us;
    if (elapsed_us >= duration_us) {
        return false;
    }
    if (now_us < next_frame_us) {
        return true;
    }
    next_frame_us = now_us + kFrameUs;

    display::clear();
    switch (kind) {
    case Kind::RainbowWave:
        draw_gradient(phase);
        phase += 12;
        break;
    case Kind::Sparkle:
        draw_sparkle();
        break;
    case Kind::Rain:
        draw_rain();
        break;
    case Kind::Wipe:
        draw_wipe(elapsed_us, duration_us);
        break;
    case Kind::Inverted:
        draw_inverted(time_text, colon_on);
        phase += 6;
        break;
    case Kind::Count:
        break;
    }
    display::show();
    return true;
}

}  // namespace animation
