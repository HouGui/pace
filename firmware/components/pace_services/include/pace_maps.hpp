// SPDX-License-Identifier: MIT
#pragma once
#include "pace/map.hpp"
#include <cstddef>

namespace pace::services {
inline constexpr std::size_t kMapTileBytes = 65536;

struct MapTile {
    std::array<unsigned char, kMapTileBytes> bytes{};
    std::size_t size{};
};

struct MapFrame {
    std::array<MapTile, 4> tiles{};
    MapViewport view{};
    RouteGeometry geometry{};
    int route{-1};
    std::int64_t route_fetched_ms{};
    bool ready{};
};
struct MapWorker;
bool png_tile(const unsigned char *bytes, std::size_t size);
} // namespace pace::services
