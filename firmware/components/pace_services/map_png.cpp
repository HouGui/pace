// SPDX-License-Identifier: MIT
#include "pace_maps.hpp"
#include <cstring>

namespace pace::services {
namespace {
std::uint32_t be32(const unsigned char *p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
           p[3];
}
} // namespace

bool png_tile(const unsigned char *bytes, std::size_t size) {
    const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 45 || size > kMapTileBytes || std::memcmp(bytes, signature, sizeof(signature)) ||
        be32(bytes + 8) != 13 || std::memcmp(bytes + 12, "IHDR", 4) || be32(bytes + 16) != 256 ||
        be32(bytes + 20) != 256)
        return false;
    bool data = false;
    for (std::size_t at = 8; at + 12 <= size;) {
        const auto length = be32(bytes + at);
        if (length > size - at - 12)
            return false;
        const auto *type = bytes + at + 4;
        // No compressed text/profiles or animation. The UI also limits inflate output.
        bool allowed = false;
        for (const char *name : {"IHDR", "PLTE", "tRNS", "IDAT", "IEND", "sRGB", "gAMA", "cHRM",
                                 "pHYs", "bKGD", "tIME"})
            allowed = allowed || std::memcmp(type, name, 4) == 0;
        if (!allowed)
            return false;
        data = data || std::memcmp(type, "IDAT", 4) == 0;
        at += length + 12;
        if (!std::memcmp(type, "IEND", 4))
            return data && length == 0 && at == size;
    }
    return false;
}

} // namespace pace::services
