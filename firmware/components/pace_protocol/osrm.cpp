// SPDX-License-Identifier: MIT
#include "pace/traffic_protocol.hpp"
#include "route_fields.hpp"
#include <cstdio>

namespace pace {
using namespace json_detail;

bool make_osrm_url(const TrafficSettings &settings, const RouteSettings &route, char *out,
                   std::size_t capacity) {
    double from_lat, from_lon, to_lat, to_lon;
    if (route.mode != TravelMode::car || !routing_endpoint(settings.road_endpoint.data()) ||
        !route_coordinates(route.origin.data(), from_lat, from_lon) ||
        !route_coordinates(route.destination.data(), to_lat, to_lon))
        return false;
    auto endpoint = std::string_view(settings.road_endpoint.data());
    while (endpoint.back() == '/')
        endpoint.remove_suffix(1);
    const auto size = std::snprintf(
        out, capacity,
        "%.*s/route/v1/driving/%.6f,%.6f;%.6f,%.6f?overview=simplified&geometries=polyline6"
        "&steps=false&alternatives=false&generate_hints=false&skip_waypoints=true",
        static_cast<int>(endpoint.size()), endpoint.data(), from_lon, from_lat, to_lon, to_lat);
    return size > 0 && static_cast<std::size_t>(size) < capacity;
}

RouteResult parse_osrm_route(const char *bytes, std::size_t size) {
    RouteResult result;
    result.status = RouteStatus::invalid;
    Json json(bytes, size, kRouteResponseLimit, 10);
    const auto *root = json.get();
    std::array<char, 32> code{};
    if (!string(field(root, "code"), code))
        return result;
    if (!std::strcmp(code.data(), "NoRoute") || !std::strcmp(code.data(), "NoSegment")) {
        result.status = RouteStatus::no_route;
        return result;
    }
    int count;
    const auto *routes = field(root, "routes");
    if (std::strcmp(code.data(), "Ok") || !array(routes, 1, count) || count != 1)
        return result;
    const auto *route = cJSON_GetArrayItem(routes, 0);
    const auto *seconds = field(route, "duration");
    const auto *meters = field(route, "distance");
    if (!cJSON_IsNumber(seconds) || seconds->valuedouble < 0 || seconds->valuedouble > 604800 ||
        !cJSON_IsNumber(meters) || meters->valuedouble < 0 || meters->valuedouble > 50000000 ||
        !decode_polyline(field(route, "geometry"), 6, result.geometry))
        return result;
    result.seconds = static_cast<std::uint32_t>(std::ceil(seconds->valuedouble));
    result.meters = static_cast<std::uint32_t>(std::ceil(meters->valuedouble));
    const auto *legs = field(route, "legs");
    if (!array(legs, 1, count) || count != 1 ||
        !string(field(cJSON_GetArrayItem(legs, 0), "summary"), result.summary, true))
        return result;
    result.status = RouteStatus::ok;
    return result;
}
} // namespace pace
