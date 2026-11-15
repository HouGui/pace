// SPDX-License-Identifier: MIT
#include "pace/map.hpp"
#include <algorithm>
#include <cmath>

namespace pace {
namespace {
constexpr double pi = 3.141592653589793;

double mercator_y(const GeoPoint &point) {
    const double latitude =
        std::clamp(point.latitude_e6 / 1000000.0, -85.05112878, 85.05112878) * pi / 180;
    return (1 - std::asinh(std::tan(latitude)) / pi) / 2;
}
} // namespace

bool map_viewport(const RouteGeometry &geometry, MapViewport &out) {
    if (geometry.count < 2 || geometry.count > kMaxRoutePoints)
        return false;
    double min_x = 2, max_x = -2, min_y = 1, max_y = 0;
    const double anchor = (geometry.points[0].longitude_e6 / 1000000.0 + 180) / 360;
    for (unsigned i = 0; i < geometry.count; ++i) {
        const auto &point = geometry.points[i];
        double x = (point.longitude_e6 / 1000000.0 + 180) / 360;
        x -= std::round(x - anchor);
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, mercator_y(point));
        max_y = std::max(max_y, mercator_y(point));
    }
    for (int zoom = 18; zoom >= 0; --zoom) {
        const auto world = std::int64_t{256} << zoom;
        if (zoom &&
            ((max_x - min_x) * world > kMapWidth - 32 || (max_y - min_y) * world > kMapHeight - 32))
            continue;
        out.zoom = zoom;
        out.left =
            static_cast<std::int64_t>(std::floor((min_x + max_x) * world / 2 - kMapWidth / 2));
        out.top = std::clamp<std::int64_t>(
            static_cast<std::int64_t>((min_y + max_y) * world / 2 - kMapHeight / 2), 0,
            world - kMapHeight);
        out.first_x = static_cast<int>(std::floor(out.left / 256.0));
        out.first_y = out.top / 256;
        out.columns =
            static_cast<int>(std::floor((out.left + kMapWidth - 1) / 256.0)) - out.first_x + 1;
        out.rows = (out.top + kMapHeight - 1) / 256 - out.first_y + 1;
        return out.columns * out.rows <= 4;
    }
    return false;
}

MapPoint map_point(const MapViewport &view, const GeoPoint &point) {
    const auto world = std::int64_t{256} << view.zoom;
    double x = (point.longitude_e6 / 1000000.0 + 180) / 360 * world;
    const double center = view.left + kMapWidth / 2;
    x -= std::round((x - center) / world) * world;
    return {static_cast<int>(std::round(x - view.left)),
            static_cast<int>(std::round(mercator_y(point) * world - view.top))};
}

int wrapped_tile_x(int x, int zoom) {
    const int columns = 1 << zoom;
    return (x % columns + columns) % columns;
}
} // namespace pace
