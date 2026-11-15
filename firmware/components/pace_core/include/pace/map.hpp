// SPDX-License-Identifier: MIT
#pragma once
#include "pace/traffic.hpp"

namespace pace {
inline constexpr int kMapWidth = 256, kMapHeight = 160;

struct MapViewport {
    int zoom{};
    std::int64_t left{}, top{};
    int first_x{}, first_y{}, columns{}, rows{};
};

struct MapPoint {
    int x{}, y{};
};

bool map_viewport(const RouteGeometry &geometry, MapViewport &out);
MapPoint map_point(const MapViewport &view, const GeoPoint &point);
int wrapped_tile_x(int x, int zoom);
} // namespace pace
