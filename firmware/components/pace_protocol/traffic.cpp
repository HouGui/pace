// SPDX-License-Identifier: MIT
#include "pace/traffic_protocol.hpp"
#include "route_fields.hpp"
#include <cstdio>

namespace pace {
using namespace json_detail;

bool make_route_request(const RouteSettings &route, Language language, char *out,
                        std::size_t capacity) {
    auto *root = cJSON_CreateObject();
    if (!root || capacity > 4096) {
        cJSON_Delete(root);
        return false;
    }
    auto *origin = cJSON_AddObjectToObject(root, "origin");
    auto *destination = cJSON_AddObjectToObject(root, "destination");
    bool ok =
        origin && destination && cJSON_AddStringToObject(origin, "address", route.origin.data()) &&
        cJSON_AddStringToObject(destination, "address", route.destination.data()) &&
        cJSON_AddStringToObject(root, "travelMode",
                                route.mode == TravelMode::car ? "DRIVE" : "TRANSIT") &&
        cJSON_AddStringToObject(root, "languageCode", language == Language::german ? "de" : "en");
    if (route.mode == TravelMode::car)
        ok = ok && cJSON_AddStringToObject(root, "routingPreference", "TRAFFIC_AWARE");
    ok = ok && cJSON_PrintPreallocated(root, out, static_cast<int>(capacity), false);
    cJSON_Delete(root);
    return ok;
}

namespace {
bool duration(const cJSON *value, std::uint32_t &seconds) {
    if (!cJSON_IsString(value))
        return false;
    const std::string_view s(value->valuestring);
    std::size_t i = 0;
    std::uint32_t whole = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        whole = whole * 10 + s[i++] - '0';
        if (whole > 604800)
            return false;
    }
    if (i == 0)
        return false;
    if (i < s.size() && s[i] == '.') {
        const auto start = ++i;
        bool fraction = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            fraction = fraction || s[i] != '0';
            ++i;
        }
        if (i == start || i - start > 9)
            return false;
        whole += fraction; // Round up only a nonzero fractional second.
    }
    if (i + 1 != s.size() || s[i] != 's' || whole > 604800)
        return false;
    seconds = whole;
    return true;
}

} // namespace

RouteResult parse_route(const char *bytes, std::size_t size) {
    RouteResult result;
    result.status = RouteStatus::invalid;
    Json json(bytes, size, kRouteResponseLimit, 10);
    const auto *root = json.get();
    const auto *routes = field(root, "routes");
    // Protobuf JSON omits an empty repeated field when no route exists.
    if (cJSON_IsObject(root) && !field(root, "error") &&
        ((!routes && !root->child) || (cJSON_IsArray(routes) && !routes->child))) {
        result.status = RouteStatus::no_route;
        return result;
    }
    int count;
    if (!array(routes, 1, count) || count != 1)
        return result;
    const auto *route = cJSON_GetArrayItem(routes, 0);
    std::int64_t meters = 0;
    if (!duration(field(route, "duration"), result.seconds) ||
        (field(route, "distanceMeters") &&
         !integer(field(route, "distanceMeters"), 0, 50000000, meters)))
        return result;
    result.meters = meters;
    if (const auto *description = field(route, "description"))
        if (!string(description, result.summary, true))
            return result;
    if (const auto *warnings = field(route, "warnings")) {
        if (!array(warnings, 8, count))
            return result;
        for (int i = 0; i < count; ++i)
            if (!append(result.warnings, cJSON_GetArrayItem(warnings, i), " · "))
                return result;
    }
    if (const auto *legs = field(route, "legs")) {
        if (!array(legs, 1, count))
            return result;
        std::array<char, 193> lines{};
        for (int i = 0; i < count; ++i) {
            const auto *leg = cJSON_GetArrayItem(legs, i);
            if (!cJSON_IsObject(leg))
                return result;
            const auto *steps = field(leg, "steps");
            if (!steps)
                continue;
            int step_count;
            if (!array(steps, 64, step_count))
                return result;
            for (int step = 0; step < step_count; ++step) {
                const auto *item = cJSON_GetArrayItem(steps, step);
                if (!cJSON_IsObject(item))
                    return result;
                auto *details = field(item, "transitDetails");
                if (!details)
                    continue;
                const auto *line = field(details, "transitLine");
                auto *name = field(line, "nameShort");
                if (!name || (cJSON_IsString(name) && !name->valuestring[0]))
                    name = field(line, "name");
                if (!append(lines, name, " > "))
                    return result;
            }
        }
        if (lines[0])
            result.summary = lines;
    }
    result.status = RouteStatus::ok;
    return result;
}
} // namespace pace
