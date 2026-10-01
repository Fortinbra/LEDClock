#pragma once

#include <cstdint>

namespace animation {

// Flowing full-panel rainbow; phase advances the flow.
void draw_gradient(int32_t phase);
// Begins a randomly chosen animation, never the same one twice in a row.
void start_random(uint64_t now_us);
// Draws the next frame when due. Returns false once the animation has finished.
bool step(uint64_t now_us, const char* time_text, bool colon_on);

}  // namespace animation
