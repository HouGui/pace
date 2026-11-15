// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>

namespace pace::ui_detail {
// A packed RGB565 partial draw buffer: reverse rows and pixels in place.
// No second framebuffer, allocation, or byte-order conversion is needed.
inline void rotate_rgb565_180(std::uint8_t *pixels, std::size_t count) {
    for (std::size_t i = 0; i < count / 2; ++i) {
        const auto other = count - i - 1;
        for (unsigned byte = 0; byte < 2; ++byte) {
            const auto saved = pixels[2 * i + byte];
            pixels[2 * i + byte] = pixels[2 * other + byte];
            pixels[2 * other + byte] = saved;
        }
    }
}
} // namespace pace::ui_detail
