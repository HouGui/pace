// SPDX-License-Identifier: MIT
#include "pace/traffic_protocol.hpp"
#include "route_fields.hpp"
#include <cstdio>

namespace pace {
using namespace json_detail;

bool make_transit_url(const TrafficSettings &settings, const RouteSettings &route,
                      Language language, char *out, std::size_t capacity) {
    double from_lat, from_lon, to_lat, to_lon;
    if (route.mode != TravelMode::transit || !routing_endpoint(settings.transit_endpoint.data()) ||
        !route_coordinates(route.origin.data(), from_lat, from_lon) ||
        !route_coordinates(route.destination.data(), to_lat, to_lon))
        return false;
    const auto size = std::snprintf(
        out, capacity,
        "%s?fromPlace=%.6f,%.6f&toPlace=%.6f,%.6f&transitModes=TRANSIT&directModes="
        "&preTransitModes=WALK&postTransitModes=WALK&timetableView=false&arriveBy=false"
        "&detailedLegs=false&detailedTransfers=false&withFares=false&language=%s",
        settings.transit_endpoint.data(), from_lat, from_lon, to_lat, to_lon,
        language == Language::german ? "de" : "en");
    return size > 0 && static_cast<std::size_t>(size) < capacity;
}

namespace {
bool utc_time(const cJSON *value, std::int64_t &out) {
    if (!cJSON_IsString(value))
        return false;
    const std::string_view s(value->valuestring);
    // MOTIS serializes UTC instants, e.g. 2030-03-08T10:30:00Z.
    if (s.size() != 20 || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z')
        return false;
    const auto day = date_number(s.substr(0, 10));
    for (unsigned i : {11, 12, 14, 15, 17, 18})
        if (s[i] < '0' || s[i] > '9')
            return false;
    const int hour = (s[11] - '0') * 10 + s[12] - '0';
    const int minute = (s[14] - '0') * 10 + s[15] - '0';
    const int second = (s[17] - '0') * 10 + s[18] - '0';
    if (day < 0 || hour > 23 || minute > 59 || second > 59)
        return false;
    out = day * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

bool place(const cJSON *value, RouteGeometry &geometry) {
    const auto *lat = field(value, "lat");
    const auto *lon = field(value, "lon");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || lat->valuedouble < -90 ||
        lat->valuedouble > 90 || lon->valuedouble < -180 || lon->valuedouble > 180 ||
        geometry.count >= geometry.points.size())
        return false;
    const GeoPoint point{static_cast<std::int32_t>(std::round(lat->valuedouble * 1000000)),
                         static_cast<std::int32_t>(std::round(lon->valuedouble * 1000000))};
    if (!geometry.count || geometry.points[geometry.count - 1].latitude_e6 != point.latitude_e6 ||
        geometry.points[geometry.count - 1].longitude_e6 != point.longitude_e6)
        geometry.points[geometry.count++] = point;
    return true;
}

bool alerts(const cJSON *object, RouteResult &result) {
    const auto *items = field(object, "alerts");
    if (!items)
        return true;
    int count;
    if (!array(items, 8, count))
        return false;
    for (int i = 0; i < count; ++i) {
        const auto *item = cJSON_GetArrayItem(items, i);
        if (!append(result.warnings, field(item, "headerText"), " · "))
            return false;
    }
    return true;
}

bool itinerary(const cJSON *item, RouteResult &result, std::int64_t &arrival, bool &cancelled) {
    std::int64_t duration;
    int count;
    const auto *legs = field(item, "legs");
    if (!integer(field(item, "duration"), 0, 604800, duration) ||
        !utc_time(field(item, "startTime"), result.departure_utc) ||
        !utc_time(field(item, "endTime"), arrival) || arrival < result.departure_utc ||
        arrival - result.departure_utc != duration || !array(legs, 32, count) || !count)
        return false;
    result.seconds = duration;
    result.distance_known = false; // MOTIS distances cover walking legs only.
    result.geometry.schematic = true;
    bool transit = false;
    for (int i = 0; i < count; ++i) {
        const auto *leg = cJSON_GetArrayItem(legs, i);
        std::array<char, 32> mode{};
        bool realtime, is_cancelled = false;
        if (!string(field(leg, "mode"), mode) || !boolean(field(leg, "realTime"), realtime) ||
            (field(leg, "cancelled") && !boolean(field(leg, "cancelled"), is_cancelled)) ||
            !place(field(leg, "from"), result.geometry) ||
            !place(field(leg, "to"), result.geometry) || !alerts(leg, result) ||
            !alerts(field(leg, "from"), result) || !alerts(field(leg, "to"), result))
            return false;
        cancelled = cancelled || is_cancelled;
        for (const auto *endpoint : {field(leg, "from"), field(leg, "to")}) {
            if (const auto *value = field(endpoint, "cancelled")) {
                bool stop_cancelled;
                if (!boolean(value, stop_cancelled))
                    return false;
                cancelled = cancelled || stop_cancelled;
            }
        }
        if (!std::strcmp(mode.data(), "WALK"))
            continue;
        transit = true;
        result.realtime = result.realtime || realtime;
        const auto *name = field(leg, "displayName");
        if (!name || (cJSON_IsString(name) && !name->valuestring[0]))
            name = field(leg, "routeShortName");
        if (!name || (cJSON_IsString(name) && !name->valuestring[0]))
            name = field(leg, "mode");
        if (!append(result.summary, name, " > "))
            return false;
    }
    return transit;
}
} // namespace

RouteResult parse_transit_route(const char *bytes, std::size_t size) {
    RouteResult result;
    result.status = RouteStatus::invalid;
    Json json(bytes, size, kRouteResponseLimit, 12, true);
    const auto *root = json.get();
    const auto *items = field(root, "itineraries");
    int count;
    if (!array(items, 8, count))
        return result;
    result.status = RouteStatus::no_route;
    std::int64_t earliest_arrival = INT64_MAX;
    for (int i = 0; i < count; ++i) {
        RouteResult candidate;
        std::int64_t arrival;
        bool cancelled = false;
        if (!itinerary(cJSON_GetArrayItem(items, i), candidate, arrival, cancelled)) {
            result = {};
            result.status = RouteStatus::invalid;
            return result;
        }
        if (!cancelled && arrival < earliest_arrival) {
            earliest_arrival = arrival;
            result = candidate;
            result.status = RouteStatus::ok;
        }
    }
    return result;
}
} // namespace pace
