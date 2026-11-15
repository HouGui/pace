// SPDX-License-Identifier: MIT
#include "pace/traffic_protocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {
unsigned checks{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

std::string fixture(const char *name) {
    std::ifstream file(std::string(PACE_SOURCE_DIR) + "/tests/fixtures/" + name);
    CHECK(file.good());
    return {(std::istreambuf_iterator<char>(file)), {}};
}

pace::RouteResult parse(const cJSON *root, bool transit) {
    auto *bytes = cJSON_PrintUnformatted(root);
    auto result = transit ? pace::parse_transit_route(bytes, std::strlen(bytes))
                          : pace::parse_osrm_route(bytes, std::strlen(bytes));
    cJSON_free(bytes);
    return result;
}
} // namespace

int main() {
    using namespace pace;
    for (const bool transit : {false, true}) {
        const auto bytes = fixture(transit ? "transit-route.json" : "osrm-route.json");
        auto *root = cJSON_Parse(bytes.c_str());
        CHECK(root);
        auto result = parse(root, transit);
        CHECK(result.status == RouteStatus::ok);
        CHECK(result.geometry.count >= 2);
        CHECK(result.geometry.points[0].latitude_e6 == 48000000);
        CHECK(result.geometry.points[result.geometry.count - 1].longitude_e6 == 8200000);
        CHECK(result.geometry.schematic == transit);
        auto *routes = cJSON_GetObjectItem(root, transit ? "itineraries" : "routes");
        auto *item = cJSON_GetArrayItem(routes, 0);
        if (transit) {
            CHECK(result.seconds == 1800 && !result.distance_known && result.realtime);
            CHECK(result.departure_utc == 1899196200);
            CHECK(!std::strcmp(result.summary.data(), "Test Bus 1"));
            auto *leg = cJSON_GetArrayItem(cJSON_GetObjectItem(item, "legs"), 0);
            cJSON_ReplaceItemInObject(leg, "cancelled", cJSON_CreateTrue());
            CHECK(parse(root, true).status == RouteStatus::no_route);
            cJSON_ReplaceItemInObject(leg, "cancelled", cJSON_CreateFalse());
            auto *from = cJSON_GetObjectItem(leg, "from");
            cJSON_AddBoolToObject(from, "cancelled", true);
            CHECK(parse(root, true).status == RouteStatus::no_route);
            cJSON_DeleteItemFromObject(from, "cancelled");
            cJSON_ReplaceItemInObject(leg, "realTime", cJSON_CreateFalse());
            CHECK(!parse(root, true).realtime);
            cJSON_ReplaceItemInObject(leg, "realTime", cJSON_CreateTrue());
            cJSON_SetValuestring(cJSON_GetObjectItem(item, "endTime"), "2030-03-08T09:00:00Z");
            CHECK(parse(root, true).status == RouteStatus::invalid);
        } else {
            CHECK(result.seconds == 1235 && result.meters == 18501 && result.distance_known);
            const std::string many_points(400, '?'); // 200 valid zero-coordinate points.
            cJSON_SetValuestring(cJSON_GetObjectItem(item, "geometry"), many_points.c_str());
            CHECK(parse(root, false).geometry.count == kMaxRoutePoints);
            cJSON_SetValuestring(cJSON_GetObjectItem(item, "geometry"), "???");
            CHECK(parse(root, false).status == RouteStatus::invalid);
            cJSON_SetValuestring(cJSON_GetObjectItem(item, "geometry"), "~~~~~~~~~~~~~~");
            CHECK(parse(root, false).status == RouteStatus::invalid);
            cJSON_SetValuestring(cJSON_GetObjectItem(root, "code"), "NoSegment");
            CHECK(parse(root, false).status == RouteStatus::no_route);
            cJSON_SetValuestring(cJSON_GetObjectItem(root, "code"), "InvalidQuery");
            CHECK(parse(root, false).status == RouteStatus::invalid);
        }
        cJSON_Delete(root);
    }
    for (const auto bad :
         {"{}", "null", "[]", R"({"itineraries":[{}]})", R"({"itineraries":[],"itineraries":[]})"})
        CHECK(parse_transit_route(bad, std::strlen(bad)).status == RouteStatus::invalid);
    CHECK(parse_transit_route("{\"itineraries\":[]}", 18).status == RouteStatus::no_route);
    const std::string huge(kRouteResponseLimit + 1, ' ');
    CHECK(parse_transit_route(huge.data(), huge.size()).status == RouteStatus::invalid);
    CHECK(parse_osrm_route(huge.data(), huge.size()).status == RouteStatus::invalid);
    double lat, lon;
    for (const auto good : {"48,8", " -90 , +180 ", ".5, -.2"})
        CHECK(route_coordinates(good, lat, lon));
    for (const auto bad : {"91,8", "48,181", "48,8&secret=x", "nan,0", "1e1,2", "0x2,1", "1,2,3",
                           "", "1,", "1..2,3"})
        CHECK(!route_coordinates(bad, lat, lon));
    TrafficSettings settings;
    std::strcpy(settings.road_endpoint.data(), kDefaultOsrmEndpoint);
    std::strcpy(settings.transit_endpoint.data(), kDefaultTransitEndpoint);
    RouteSettings route;
    std::strcpy(route.origin.data(), "48,8");
    std::strcpy(route.destination.data(), "48.05,8.2");
    char url[1024];
    CHECK(make_osrm_url(settings, route, url, sizeof(url)));
    CHECK(std::strstr(url, "/8.000000,48.000000;8.200000,48.050000?"));
    CHECK(!make_osrm_url(settings, route, url, 10));
    route.mode = TravelMode::transit;
    CHECK(!make_osrm_url(settings, route, url, sizeof(url)));
    CHECK(make_transit_url(settings, route, Language::german, url, sizeof(url)));
    CHECK(std::strstr(url, "fromPlace=48.000000,8.000000") && std::strstr(url, "language=de"));
    for (const auto bad : {"http://example.org", "https://user:secret@example.org",
                           "https://example.org?key=x", "https://example.org/#fragment"})
        CHECK(!routing_endpoint(bad));
    auto *config = cJSON_Parse(
        R"({"enabled":true,"provider":"open","contact":"https://example.org","routes":[{"name":"Test journey","origin":"48,8","destination":"48.05,8.2","mode":"transit"}]})");
    CHECK(parse_traffic_settings(config, settings) && settings.provider == RoutingProvider::open);
    CHECK(settings.api_key[0] == '\0');
    CHECK(!std::strcmp(settings.tile_endpoint.data(), kDefaultTileEndpoint));
    cJSON_AddStringToObject(config, "tile_endpoint", "http://example.org");
    CHECK(!parse_traffic_settings(config, settings));
    cJSON_SetValuestring(cJSON_GetObjectItem(config, "tile_endpoint"), "https://example.org/tiles");
    CHECK(parse_traffic_settings(config, settings));
    cJSON_SetValuestring(cJSON_GetObjectItem(config, "contact"), "");
    CHECK(!parse_traffic_settings(config, settings));
    cJSON_SetValuestring(cJSON_GetObjectItem(config, "contact"), "test\r\nInjected: x");
    CHECK(!parse_traffic_settings(config, settings));
    cJSON_Delete(config);
    TrafficSchedule schedule;
    CHECK(schedule.due(true, true, 1, 0, false) == 0);
    CHECK(schedule.due(true, true, 1, 300000, false) == -1);
    CHECK(schedule.due(false, true, 1, 300000, false) == -1);
    CHECK(schedule.due(true, true, 1, 300000, false) == 0);
    CHECK(schedule.due(false, true, 1, 300001, false) == -1);
    CHECK(schedule.due(true, true, 1, 359999, false) == -1);
    CHECK(schedule.due(true, true, 1, 360000, false) == 0);
    std::printf("PASS: open routing, bounds, geometry and manual scheduling (%u assertions)\n",
                checks);
}
