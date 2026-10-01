#include "face.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "animations.hpp"
#include "display.hpp"
#include "pico/rand.h"
#include "pico/time.h"
#include "text.hpp"
#include "timekeeping.hpp"

namespace face {
namespace {

constexpr uint64_t kWaitFrameUs = 33000;
constexpr uint64_t kClockFrameUs = 100000;
constexpr uint64_t kScrollStepUs = 50000;
constexpr uint64_t kIpDurationUs = 30000000;
constexpr uint64_t kDateIntervalUs = 60000000;
constexpr uint32_t kAnimationMinGapS = 120;
constexpr uint32_t kAnimationMaxGapS = 300;
// Keeps the date from scrolling immediately after an animation finishes.
constexpr uint64_t kQuietAfterAnimationUs = 10000000;
constexpr uint64_t kHueRotationPeriodMs = 50000;
constexpr display::Rgb kUnsyncedColor{80, 80, 80};
constexpr display::Rgb kIpColor{0, 200, 255};
constexpr display::Rgb kOfflineColor{120, 0, 0};

// Character colours: base rotates with time, the steps are re-randomized every minute.
struct Palette {
    int32_t base;
    int32_t char_step;
    int32_t row_step;
};

enum class Mode { Waiting, Ip, Clock };
enum class Feature { Time, Date, Animation };

Mode mode = Mode::Waiting;
bool network_ok = true;
uint64_t next_frame_us = 0;
int32_t wait_phase = 0;

char ip_text[24];
uint64_t ip_until_us = 0;
int scroll_x = 0;

Feature feature = Feature::Time;
uint64_t next_date_us = kDateIntervalUs;
uint64_t next_animation_us = uint64_t{kAnimationMinGapS} * 1000000;
char date_text[16];
int64_t palette_minute = -1;
Palette palette{0, 256, 0};

int32_t random_between(int32_t low, int32_t high) {
    return low + static_cast<int32_t>(get_rand_32() % static_cast<uint32_t>(high - low + 1));
}

void randomize_palette() {
    palette.char_step = random_between(96, 320) * ((get_rand_32() & 1) != 0 ? 1 : -1);
    palette.row_step = random_between(-48, 48);
}

display::Rgb palette_color(int index, int y) {
    return display::hue_to_rgb(palette.base + index * palette.char_step + y * palette.row_step);
}

void draw_solid_text(const char* text, int x, display::Rgb color) {
    text::for_each_pixel(text, x, [color](int, int px, int py) { display::set_pixel(px, py, color); });
}

int centered(const char* text) {
    return (display::kWidth - text::width(text)) / 2;
}

void finish_frame() {
    if (!network_ok && mode == Mode::Clock) {
        display::set_pixel(display::kWidth - 1, 0, kOfflineColor);
    }
    display::show();
}

void render_waiting(uint64_t now) {
    if (now < next_frame_us) {
        return;
    }
    display::clear();
    animation::draw_gradient(wait_phase);
    wait_phase = (wait_phase + 8) % display::kHueSteps;
    next_frame_us = now + kWaitFrameUs;
    display::show();
}

void render_ip(uint64_t now) {
    if (now < next_frame_us) {
        return;
    }
    display::clear();
    draw_solid_text(ip_text, scroll_x, kIpColor);
    display::show();
    next_frame_us = now + kScrollStepUs;
    if (--scroll_x < -text::width(ip_text)) {
        scroll_x = display::kWidth;
    }
}

void schedule_next_animation(uint64_t now) {
    next_animation_us = now + uint64_t(random_between(kAnimationMinGapS, kAnimationMaxGapS)) * 1000000;
}

void render_clock(uint64_t now) {
    int64_t seconds = 0;
    std::tm local{};
    if (!timekeeping::now_unix(seconds) || !timekeeping::local_time(local)) {
        if (now >= next_frame_us) {
            display::clear();
            draw_solid_text("--:--", centered("--:--"), kUnsyncedColor);
            finish_frame();
            next_frame_us = now + kClockFrameUs;
        }
        return;
    }

    char time_text[8];
    std::snprintf(time_text, sizeof(time_text), "%02d:%02d", local.tm_hour, local.tm_min);
    const bool colon_on = seconds % 2 == 0;
    if (seconds / 60 != palette_minute) {
        palette_minute = seconds / 60;
        randomize_palette();
    }

    if (feature == Feature::Time) {
        if (now >= next_animation_us) {
            animation::start_random(now);
            feature = Feature::Animation;
        } else if (now >= next_date_us) {
            std::strftime(date_text, sizeof(date_text), "%Y-%m-%d", &local);
            scroll_x = display::kWidth;
            next_frame_us = 0;
            feature = Feature::Date;
        }
    }

    if (feature == Feature::Animation) {
        if (!animation::step(now, time_text, colon_on)) {
            feature = Feature::Time;
            schedule_next_animation(now);
            next_date_us = std::max(next_date_us, now + kQuietAfterAnimationUs);
            next_frame_us = 0;
        }
        return;
    }

    if (now < next_frame_us) {
        return;
    }
    palette.base = static_cast<int32_t>((now / 1000) % kHueRotationPeriodMs * display::kHueSteps / kHueRotationPeriodMs);
    display::clear();
    if (feature == Feature::Date) {
        text::for_each_pixel(date_text, scroll_x,
                             [](int index, int px, int py) { display::set_pixel(px, py, palette_color(index, py)); });
        next_frame_us = now + kScrollStepUs;
        if (--scroll_x < -text::width(date_text)) {
            feature = Feature::Time;
            next_date_us = now + kDateIntervalUs;
        }
    } else {
        text::for_each_pixel(time_text, centered(time_text), [colon_on](int index, int px, int py) {
            if (index == 2 && !colon_on) {
                return;
            }
            display::set_pixel(px, py, palette_color(index, py));
        });
        next_frame_us = now + kClockFrameUs;
    }
    finish_frame();
}

}  // namespace

void show_waiting() {
    mode = Mode::Waiting;
    next_frame_us = 0;
}

void show_ip(const char* address) {
    char text[sizeof(ip_text)];
    std::snprintf(text, sizeof(text), "IP %s", address);
    if (mode == Mode::Ip && std::strcmp(text, ip_text) == 0) {
        return;
    }
    std::memcpy(ip_text, text, sizeof(ip_text));
    mode = Mode::Ip;
    ip_until_us = time_us_64() + kIpDurationUs;
    scroll_x = display::kWidth;
    next_frame_us = 0;
}

void set_network_ok(bool ok) {
    network_ok = ok;
}

void poll() {
    if (display::busy()) {
        return;
    }
    const uint64_t now = time_us_64();
    switch (mode) {
    case Mode::Waiting:
        render_waiting(now);
        break;
    case Mode::Ip:
        render_ip(now);
        if (now >= ip_until_us) {
            mode = Mode::Clock;
            feature = Feature::Time;
            next_frame_us = 0;
            next_date_us = now + kDateIntervalUs;
            schedule_next_animation(now);
        }
        break;
    case Mode::Clock:
        render_clock(now);
        break;
    }
}

}  // namespace face
