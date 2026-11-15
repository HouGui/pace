// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "pace/traffic_protocol.hpp"
#include "pace_http.hpp"
#include "services_internal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {
unsigned checks{}, calls{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
std::int64_t mono{};
int http_status{200};
std::string response =
    R"({"routes":[{"duration":"1200s","distanceMeters":18500,"description":"A 1"}]})";
pace::services::Runtime runtime;
pace::services::Health health;
char buffer[pace::kRouteResponseLimit];
esp_app_desc_t description{};
bool hide_during_request{};

pace::RouteResult parse(const char *json) { return pace::parse_route(json, std::strlen(json)); }
} // namespace

std::int64_t esp_timer_get_time() { return mono * 1000; }

const esp_app_desc_t *esp_app_get_description() { return &description; }

namespace pace::services {
esp_err_t fetch(const char *url, const char *body, bool redirect, char *out, std::size_t capacity,
                std::int64_t deadline, std::size_t &size, int &status,
                const RequestHeaders *headers) {
    ++calls;
    CHECK(!redirect && deadline == mono + 10000 && capacity == kRouteResponseLimit);
    if (runtime.settings.traffic.provider == RoutingProvider::google_routes) {
        CHECK(!std::strcmp(url, "https://routes.googleapis.com/directions/v2:computeRoutes"));
        CHECK(body && headers && !std::strcmp(headers->api_key, "synthetic-key"));
        CHECK(std::strstr(headers->field_mask, "routes.duration"));
        CHECK(!std::strstr(body, "synthetic-key"));
    } else {
        CHECK(!body && headers && !headers->api_key && !headers->field_mask);
        CHECK(std::strstr(headers->user_agent, "PACE/") == headers->user_agent);
        CHECK(std::strstr(url, runtime.settings.traffic.routes[0].mode == TravelMode::car
                                   ? "/route/v1/driving/"
                                   : "/api/v6/plan?"));
    }
    size = response.size();
    status = http_status;
    std::memcpy(out, response.data(), size);
    if (hide_during_request)
        runtime.traffic_visible = false;
    return status == 200 || (status == 400 && headers->read_bad_request_body) ? ESP_OK : ESP_FAIL;
}
} // namespace pace::services

int main() {
    using namespace pace;
    CHECK(parse("{}").status == RouteStatus::no_route);
    CHECK(parse(R"({"routes":[]})").status == RouteStatus::no_route);
    for (auto json : {"null", "[]", R"({"error":{}})", R"({"routes":[{}]})",
                      R"({"routes":[{"duration":"-1s"}]})", R"({"routes":[{"duration":"1.s"}]})",
                      R"({"routes":[{"duration":"604801s"}]})",
                      R"({"routes":[{"duration":"1s","legs":[null]}]})",
                      R"({"routes":[{"duration":"1s","legs":[{"steps":[null]}]}]})",
                      R"({"routes":[{"duration":"1s","distanceMeters":-1}]})",
                      R"({"routes":[{"duration":"1s","duration":"2s"}]})"})
        CHECK(parse(json).status == RouteStatus::invalid);
    auto route = parse(response.c_str());
    CHECK(route.status == RouteStatus::ok && route.seconds == 1200 && route.meters == 18500);
    CHECK(parse(R"({"routes":[{"duration":"60.000s"}]})").seconds == 60);
    CHECK(parse(R"({"routes":[{"duration":"60.001s"}]})").seconds == 61);
    route = parse(R"({"routes":[{"duration":"1234s","legs":[{"steps":[{},
       {"transitDetails":{"transitLine":{"nameShort":"S 1","name":"Train"}}},
       {"transitDetails":{"transitLine":{"name":"Bus 12"}}}]}],"warnings":["Use footpath"]}]})");
    CHECK(route.status == RouteStatus::ok);
    CHECK(!std::strcmp(route.summary.data(), "S 1 > Bus 12"));
    CHECK(!std::strcmp(route.warnings.data(), "Use footpath"));
    route.fetched_at_monotonic_ms = 100;
    CHECK(route_current(route, 100) && route_current(route, 300099));
    CHECK(!route_current(route, 99) && !route_current(route, 300100));
    const std::string huge =
        "{\"routes\":[{\"duration\":\"1s\",\"description\":\"" + std::string(193, 'x') + "\"}]}";
    CHECK(parse(huge.c_str()).status == RouteStatus::invalid);
    const std::string nested = "[[[[[[[[[0]]]]]]]]]";
    Json calendar_limit(nested.data(), nested.size(), 16384);
    Json traffic_limit(nested.data(), nested.size(), 16384, 10);
    CHECK(!calendar_limit.get() && traffic_limit.get());
    const std::string too_deep = "[[[[[[[[[[[0]]]]]]]]]]]";
    Json rejected_depth(too_deep.data(), too_deep.size(), 16384, 10);
    CHECK(!rejected_depth.get());
    const std::string too_large(kRouteResponseLimit + 1, ' ');
    CHECK(parse(too_large.c_str()).status == RouteStatus::invalid);
    const std::string long_warning =
        "{\"routes\":[{\"duration\":\"1s\",\"warnings\":[\"" + std::string(385, 'x') + "\"]}]}";
    CHECK(parse(long_warning.c_str()).status == RouteStatus::invalid);

    RouteSettings setting;
    std::strcpy(setting.origin.data(), "Park \"West\", Teststadt");
    std::strcpy(setting.destination.data(), "Büro");
    char body[2048];
    CHECK(make_route_request(setting, Language::german, body, sizeof(body)));
    Json request(body, std::strlen(body), sizeof(body));
    CHECK(request.get());
    CHECK(!std::strcmp(cJSON_GetObjectItem(request.get(), "travelMode")->valuestring, "DRIVE"));
    CHECK(!std::strcmp(
        cJSON_GetObjectItem(cJSON_GetObjectItem(request.get(), "origin"), "address")->valuestring,
        setting.origin.data()));
    setting.mode = TravelMode::transit;
    CHECK(make_route_request(setting, Language::english, body, sizeof(body)));
    CHECK(!std::strstr(body, "routingPreference") && std::strstr(body, "TRANSIT"));
    CHECK(!make_route_request(setting, Language::english, body, 5));

    std::ifstream file(PACE_SOURCE_DIR "/docs/device.example.json");
    std::string config((std::istreambuf_iterator<char>(file)), {});
    auto *root = cJSON_Parse(config.c_str());
    cJSON_DeleteItemFromObject(root, "traffic");
    auto *traffic = cJSON_AddObjectToObject(root, "traffic");
    cJSON_AddBoolToObject(traffic, "enabled", true);
    cJSON_AddStringToObject(traffic, "api_key", "synthetic-key");
    auto *routes = cJSON_AddArrayToObject(traffic, "routes");
    auto *item = cJSON_Parse(
        R"({"name":"Work","origin":"Test Park","destination":"Test Office","mode":"car"})");
    cJSON_AddItemToArray(routes, item);
    const auto settings_ok = [&] {
        auto *text = cJSON_PrintUnformatted(root);
        const auto accepted = parse_settings(text, std::strlen(text), runtime.settings);
        cJSON_free(text);
        return accepted;
    };
    CHECK(settings_ok() && runtime.settings.traffic.count == 1);
    for (unsigned i = 1; i < 4; ++i)
        cJSON_AddItemToArray(routes, cJSON_Duplicate(item, true));
    CHECK(settings_ok() && runtime.settings.traffic.count == 4);
    cJSON_AddItemToArray(routes, cJSON_Duplicate(item, true));
    CHECK(!settings_ok());
    cJSON_DeleteItemFromArray(routes, 4);
    cJSON_SetValuestring(cJSON_GetObjectItem(traffic, "api_key"), "bad\r\nkey");
    CHECK(!settings_ok());
    cJSON_SetValuestring(cJSON_GetObjectItem(traffic, "api_key"), "synthetic-key");
    cJSON_SetValuestring(cJSON_GetObjectItem(item, "mode"), "bike");
    CHECK(!settings_ok());
    cJSON_SetValuestring(cJSON_GetObjectItem(item, "mode"), "transit");
    CHECK(settings_ok());
    cJSON_Delete(root);

    TrafficSchedule schedule;
    CHECK(schedule.due(false, true, 4, 0) == -1);
    CHECK(schedule.due(true, false, 4, 0) == -1);
    for (int i = 0; i < 4; ++i) {
        CHECK(schedule.due(true, true, 4, i * 1000) == i);
        CHECK(schedule.due(true, true, 4, i * 1000 + 999) == -1);
    }
    CHECK(schedule.due(true, true, 4, 299999) == -1);
    CHECK(schedule.due(true, true, 4, 300000) == 0);
    CHECK(schedule.due(false, true, 4, 300000) == -1);
    CHECK(schedule.due(true, true, 1, 359999) == -1);
    CHECK(schedule.due(true, true, 1, 360000) == 0);

    schedule = {};
    runtime.settings.traffic.count = 1;
    health.calendar = FeedResult::ok;
    health.outside.celsius = 12.0;
    health.network = health.time_trusted = true;
    services::poll_traffic(runtime, health, schedule, buffer, mono);
    CHECK(calls == 0);
    runtime.traffic_visible = true;
    services::poll_traffic(runtime, health, schedule, buffer, mono);
    CHECK(calls == 1 && health.routes[0].status == RouteStatus::ok);
    for (const auto status : {403, 429, 500}) {
        mono += 300000;
        http_status = status;
        services::poll_traffic(runtime, health, schedule, buffer, mono);
        CHECK(health.routes[0].status == (status == 403   ? RouteStatus::access_denied
                                          : status == 429 ? RouteStatus::quota
                                                          : RouteStatus::unavailable));
        CHECK(health.routes[0].seconds == 0 && health.calendar == FeedResult::ok &&
              health.outside.celsius == 12.0);
    }
    runtime.traffic_visible = false;
    services::poll_traffic(runtime, health, schedule, buffer, mono);
    CHECK(health.routes[0].status == RouteStatus::pending);
    CHECK(calls == 4);
    runtime.settings.traffic.provider = RoutingProvider::open;
    std::strcpy(runtime.settings.traffic.road_endpoint.data(), kDefaultOsrmEndpoint);
    std::strcpy(runtime.settings.traffic.transit_endpoint.data(), kDefaultTransitEndpoint);
    std::strcpy(runtime.settings.traffic.contact.data(), "https://example.org");
    auto &open_route = runtime.settings.traffic.routes[0];
    std::strcpy(open_route.origin.data(), "48,8");
    std::strcpy(open_route.destination.data(), "48.05,8.2");
    for (const auto mode : {TravelMode::car, TravelMode::transit}) {
        open_route.mode = mode;
        std::ifstream fixture(std::string(PACE_SOURCE_DIR) + "/tests/fixtures/" +
                              (mode == TravelMode::car ? "osrm-route.json" : "transit-route.json"));
        response.assign(std::istreambuf_iterator<char>(fixture), {});
        http_status = 200;
        schedule = {};
        runtime.traffic_visible = true;
        const auto before = calls;
        services::poll_traffic(runtime, health, schedule, buffer, mono);
        CHECK(calls == before + 1 && health.routes[0].status == RouteStatus::ok);
        CHECK(health.routes[0].geometry.count >= 2);
        mono += 300000;
        services::poll_traffic(runtime, health, schedule, buffer, mono);
        CHECK(calls == before + 1); // Open providers never poll continuously.
        runtime.traffic_visible = false;
        services::poll_traffic(runtime, health, schedule, buffer, mono);
        CHECK(health.routes[0].geometry.count == 0);
        runtime.traffic_visible = true;
        hide_during_request = true;
        services::poll_traffic(runtime, health, schedule, buffer, mono);
        CHECK(health.routes[0].status == RouteStatus::pending);
        hide_during_request = false;
    }
    open_route.mode = TravelMode::car;
    schedule = {};
    runtime.traffic_visible = true;
    response = R"({"code":"NoRoute"})";
    http_status = 400;
    services::poll_traffic(runtime, health, schedule, buffer, mono);
    CHECK(health.routes[0].status == RouteStatus::no_route);
    CHECK(health.calendar == FeedResult::ok && health.outside.celsius == 12.0);
    std::printf("PASS: traffic translation, scheduling and worker (%u assertions)\n", checks);
}
