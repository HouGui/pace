// SPDX-License-Identifier: MIT
#pragma once
#include "pace/protocol.hpp"

namespace pace {
// Provider URLs and JSON stay at the protocol/transport boundary.
inline constexpr const char *kDefaultOsrmEndpoint = "https://routing.openstreetmap.de/routed-car";
inline constexpr const char *kDefaultTransitEndpoint = "https://api.transitous.org/api/v6/plan";
inline constexpr const char *kDefaultTileEndpoint = "https://tile.openstreetmap.org";
inline constexpr std::size_t kRouteResponseLimit = 32768;
inline constexpr const char *kRouteFields =
    "routes.duration,routes.distanceMeters,routes.description,routes.warnings,"
    "routes.legs.steps.transitDetails.transitLine.nameShort,"
    "routes.legs.steps.transitDetails.transitLine.name";
bool parse_traffic_settings(const cJSON *root, TrafficSettings &out);
bool route_coordinates(const char *text, double &latitude, double &longitude);
bool routing_endpoint(const char *url);
bool make_osrm_url(const TrafficSettings &settings, const RouteSettings &route, char *out,
                   std::size_t capacity);
RouteResult parse_osrm_route(const char *bytes, std::size_t size);
bool make_transit_url(const TrafficSettings &settings, const RouteSettings &route,
                      Language language, char *out, std::size_t capacity);
RouteResult parse_transit_route(const char *bytes, std::size_t size);
bool make_route_request(const RouteSettings &route, Language language, char *out,
                        std::size_t capacity);
RouteResult parse_route(const char *bytes, std::size_t size);
} // namespace pace
