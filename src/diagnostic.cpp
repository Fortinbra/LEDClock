#include "diagnostic.hpp"

#include <cstdio>

#include "display.hpp"
#include "pico/time.h"

namespace diagnostic {
namespace {

constexpr uint32_t kWalkStepMs = 60;
constexpr uint32_t kHoldMs = 6000;
constexpr display::Rgb kRed{255, 0, 0};
constexpr display::Rgb kGreen{0, 255, 0};
constexpr display::Rgb kBlue{0, 0, 255};
constexpr display::Rgb kWhite{255, 255, 255};

enum class Stage { StrandWalk, Corners, Channels };

Stage stage = Stage::StrandWalk;
int step = 0;
absolute_time_t next_step;

}  // namespace

void poll() {
    if (!time_reached(next_step) || display::busy()) {
        return;
    }

    display::clear();
    switch (stage) {
    case Stage::StrandWalk:
        if (step == 0) {
            std::printf("Diagnostic: strand walk; red marks strand index 0, white is the moving index\n");
        }
        if (step % display::kHeight == 0) {
            std::printf("  strand index %d\n", step);
        }
        display::set_strand_pixel(0, kRed);
        display::set_strand_pixel(step, kWhite);
        next_step = make_timeout_time_ms(kWalkStepMs);
        if (++step == display::kPixelCount) {
            stage = Stage::Corners;
            step = 0;
        }
        break;

    case Stage::Corners: {
        const int right = display::kWidth - 1;
        const int bottom = display::kHeight - 1;
        std::printf("Diagnostic: corners via mapping; expect top-left RED (strand %d), top-right GREEN (%d), "
                    "bottom-left BLUE (%d), bottom-right WHITE (%d)\n",
                    display::strand_index(0, 0), display::strand_index(right, 0), display::strand_index(0, bottom),
                    display::strand_index(right, bottom));
        display::set_pixel(0, 0, kRed);
        display::set_pixel(right, 0, kGreen);
        display::set_pixel(0, bottom, kBlue);
        display::set_pixel(right, bottom, kWhite);
        next_step = make_timeout_time_ms(kHoldMs);
        stage = Stage::Channels;
        break;
    }

    case Stage::Channels:
        std::printf("Diagnostic: colour order; expect left third RED, middle GREEN, right third BLUE\n");
        for (int x = 0; x < display::kWidth; ++x) {
            const display::Rgb color = x < 11 ? kRed : x < 21 ? kGreen : kBlue;
            for (int y = 0; y < display::kHeight; ++y) {
                display::set_pixel(x, y, color);
            }
        }
        next_step = make_timeout_time_ms(kHoldMs);
        stage = Stage::StrandWalk;
        break;
    }
    display::show();
}

}  // namespace diagnostic
