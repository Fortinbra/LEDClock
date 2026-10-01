#pragma once

#include <cstdint>

namespace text {

constexpr int kTop = 1;
constexpr int kGlyphHeight = 7;

struct Glyph {
    char c;
    uint8_t width;
    uint8_t columns[5];  // bit 0 is the top row
};

const Glyph* find(char c);
int width(const char* text);

// Calls visit(char_index, x, y) for every lit pixel, with the text's left edge at x.
template <typename Visit>
void for_each_pixel(const char* text, int x, Visit&& visit) {
    for (int index = 0; text[index] != '\0'; ++index) {
        const Glyph* glyph = find(text[index]);
        if (glyph == nullptr) {
            continue;
        }
        for (int column = 0; column < glyph->width; ++column) {
            for (int row = 0; row < kGlyphHeight; ++row) {
                if ((glyph->columns[column] >> row) & 1) {
                    visit(index, x + column, kTop + row);
                }
            }
        }
        x += glyph->width + 1;
    }
}

}  // namespace text
