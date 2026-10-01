#pragma once

#include <cstdint>

namespace display {

constexpr int kWidth = 32;
constexpr int kHeight = 8;
constexpr int kPixelCount = kWidth * kHeight;
constexpr int32_t kHueSteps = 1536;

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Fully saturated colour on a 0..kHueSteps wheel; any integer wraps around.
Rgb hue_to_rgb(int32_t hue);
// Scales a colour by level / 255.
Rgb dim(Rgb color, uint8_t level);

bool init();
void clear();
// Logical coordinates: (0, 0) is the top-left pixel. Out-of-range writes are ignored.
void set_pixel(int x, int y, Rgb color);
// Raw strand position, bypassing the layout mapping; for wiring diagnostics.
void set_strand_pixel(int index, Rgb color);
int strand_index(int x, int y);
// True while a frame is still being clocked out.
bool busy();
// Applies gamma, brightness and current limits, then starts the transfer. Returns false if busy.
bool show();

}  // namespace display
