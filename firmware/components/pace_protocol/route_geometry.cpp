// SPDX-License-Identifier: MIT
#include "route_fields.hpp"
#include <algorithm>

namespace pace::json_detail {
namespace {
bool delta(std::string_view text, std::size_t &at, std::int64_t &value) {
    std::uint64_t encoded = 0;
    for (unsigned shift = 0; shift <= 30; shift += 5) {
        if (at >= text.size() || text[at] < 63 || text[at] > 126)
            return false;
        const auto byte = static_cast<unsigned>(text[at++]) - 63;
        encoded |= static_cast<std::uint64_t>(byte & 31) << shift;
        if (!(byte & 32)) {
            value = (encoded & 1) ? -static_cast<std::int64_t>((encoded >> 1) + 1)
                                  : static_cast<std::int64_t>(encoded >> 1);
            return true;
        }
    }
    return false;
}
} // namespace

bool decode_polyline(const cJSON *value, unsigned precision, RouteGeometry &geometry) {
    if (!cJSON_IsString(value) || (precision != 5 && precision != 6))
        return false;
    const std::string_view text(value->valuestring);
    if (text.empty() || text.size() > 16384)
        return false;
    // Two bounded passes retain endpoints while reducing a long overview to fixed storage.
    unsigned total = 0;
    geometry = {};
    for (unsigned pass = 0; pass < 2; ++pass) {
        std::size_t at = 0;
        std::int64_t latitude = 0, longitude = 0;
        unsigned index = 0, selected = 0;
        const unsigned kept = std::min(total, kMaxRoutePoints);
        while (at < text.size()) {
            std::int64_t dlat, dlon;
            if (!delta(text, at, dlat) || !delta(text, at, dlon))
                return false;
            latitude += dlat;
            longitude += dlon;
            const int multiplier = precision == 5 ? 10 : 1;
            if (latitude * multiplier < -90000000 || latitude * multiplier > 90000000 ||
                longitude * multiplier < -180000000 || longitude * multiplier > 180000000)
                return false;
            if (pass && selected < kept && index == selected * (total - 1) / (kept - 1)) {
                geometry.points[selected++] = {static_cast<std::int32_t>(latitude * multiplier),
                                               static_cast<std::int32_t>(longitude * multiplier)};
            }
            ++index;
        }
        if (index < 2)
            return false;
        if (!pass)
            total = index;
        else
            geometry.count = selected;
    }
    return true;
}
} // namespace pace::json_detail
