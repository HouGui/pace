// SPDX-License-Identifier: MIT
#include "pace/protocol.hpp"
#include "pace/traffic_protocol.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>

int main(int argc, char **argv) {
    const bool weather = argc == 3 && !std::strcmp(argv[1], "--weather");
    const bool settings = argc == 3 && !std::strcmp(argv[1], "--settings");
    const bool osrm = argc == 3 && !std::strcmp(argv[1], "--osrm");
    const bool transit = argc == 3 && !std::strcmp(argv[1], "--transit");
    if (argc != 2 && !weather && !settings && !osrm && !transit) {
        std::fprintf(stderr,
                     "Usage: pace_feed_check feed.json | --weather weather.json | "
                     "--settings settings.json | --osrm route.json | --transit route.json\n");
        return 2;
    }
    std::ifstream file(argv[argc == 3 ? 2 : 1], std::ios::binary);
    if (!file)
        return 2;
    std::string bytes(65537, '\0');
    file.read(bytes.data(), bytes.size());
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    if (bytes.size() > 65536)
        return 1;
    if (osrm || transit) {
        const auto result = osrm ? pace::parse_osrm_route(bytes.data(), bytes.size())
                                 : pace::parse_transit_route(bytes.data(), bytes.size());
        const bool accepted =
            result.status == pace::RouteStatus::ok || result.status == pace::RouteStatus::no_route;
        std::printf("Route response %s\n", accepted ? "accepted" : "rejected");
        return accepted ? 0 : 1;
    }
    if (settings) {
        pace::Settings result;
        const bool accepted = pace::parse_settings(bytes.data(), bytes.size(), result);
        std::printf("Settings %s\n", accepted ? "accepted" : "rejected");
        return accepted ? 0 : 1;
    }
    if (weather) {
        pace::OutdoorTemperatureSample sample;
        const bool accepted =
            pace::parse_weather(bytes.data(), bytes.size(), std::time(nullptr), sample);
        std::printf("Weather response %s\n", accepted ? "accepted" : "rejected");
        return accepted ? 0 : 1;
    }
    pace::Json json(bytes.data(), bytes.size(), 65536);
    auto *root = json.get();
    pace::Preferences p;
    auto *zone = cJSON_GetObjectItemCaseSensitive(root, "time_zone");
    auto *locale = cJSON_GetObjectItemCaseSensitive(root, "locale");
    auto *cycle = cJSON_GetObjectItemCaseSensitive(root, "hour_cycle");
    if (!cJSON_IsString(zone) || !cJSON_IsString(locale) || !cJSON_IsString(cycle))
        return 1;
    std::snprintf(p.time_zone.data(), p.time_zone.size(), "%s", zone->valuestring);
    std::snprintf(p.locale.data(), p.locale.size(), "%s", locale->valuestring);
    p.twelve_hour = !std::strcmp(cycle->valuestring, "h12");
    static pace::Snapshot snapshot;
    const auto result = pace::parse_feed(bytes.data(), bytes.size(), p, snapshot);
    std::printf("%s\n", pace::feed_result_text(result));
    return result == pace::FeedResult::ok ? 0 : 1;
}
