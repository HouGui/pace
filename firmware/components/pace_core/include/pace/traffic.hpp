// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>

namespace pace {
inline constexpr unsigned kMaxRoutes = 4;
inline constexpr unsigned kMaxRoutePoints = 96;
enum class TravelMode { car, transit };
enum class RoutingProvider { open, google_routes };
enum class RouteStatus { pending, ok, no_route, access_denied, quota, unavailable, invalid };

struct RouteSettings {
    std::array<char, 49> name{};
    std::array<char, 129> origin{}, destination{};
    TravelMode mode{TravelMode::car};
};

struct TrafficSettings {
    RoutingProvider provider{RoutingProvider::open};
    std::array<char, 257> road_endpoint{}, transit_endpoint{};
    std::array<char, 257> tile_endpoint{};
    std::array<char, 129> contact{};
    std::array<char, 129> api_key{};
    std::array<RouteSettings, kMaxRoutes> routes{};
    unsigned count{};
};

struct GeoPoint {
    std::int32_t latitude_e6{}, longitude_e6{};
};

struct RouteGeometry {
    std::array<GeoPoint, kMaxRoutePoints> points{};
    unsigned count{};
    bool schematic{}; // Transit stop connections, not street-level navigation.
};

struct RouteResult {
    RouteStatus status{RouteStatus::pending};
    std::uint32_t seconds{}, meters{};
    bool distance_known{true};
    std::int64_t departure_utc{};
    bool realtime{};
    RouteGeometry geometry{};
    std::int64_t fetched_at_monotonic_ms{};
    std::array<char, 193> summary{};
    std::array<char, 385> warnings{};
};

inline bool route_current(const RouteResult &route, std::int64_t mono) {
    return route.status == RouteStatus::ok && mono >= route.fetched_at_monotonic_ms &&
           mono - route.fetched_at_monotonic_ms < 300000;
}

// Fetch only while visible. Reopening cannot exceed one request/route/minute.
struct TrafficSchedule {
    std::array<std::int64_t, kMaxRoutes> next{};
    std::array<std::int64_t, kMaxRoutes> cooldown{};
    std::int64_t next_request{};
    std::array<bool, kMaxRoutes> requested{};
    bool active{};

    int due(bool visible, bool ready, unsigned count, std::int64_t mono, bool automatic = true) {
        if (!visible) {
            active = false;
            return -1;
        }
        if (!active) {
            next.fill(0);
            requested.fill(false);
            active = true;
        }
        if (!ready || count > kMaxRoutes || mono < next_request)
            return -1;
        for (unsigned i = 0; i < count; ++i) {
            if ((automatic || !requested[i]) && mono >= next[i] && mono >= cooldown[i]) {
                next[i] = mono + 300000;
                cooldown[i] = mono + 60000;
                next_request = mono + 1000;
                requested[i] = true;
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};
} // namespace pace
