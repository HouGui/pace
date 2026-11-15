// SPDX-License-Identifier: MIT
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pace/traffic_protocol.hpp"
#include "pace_http.hpp"
#include "services_internal.hpp"
#include <cstdio>
#include <memory>
#include <new>

namespace pace::services {
namespace {
struct TrafficRequest {
    char body[2048], url[1024], agent[192];
};
} // namespace

void poll_traffic(Runtime &runtime, Health &health, TrafficSchedule &schedule, char *bytes,
                  std::int64_t mono) {
    const bool visible = runtime.traffic_visible && runtime.settings.traffic.count > 0;
    if (!visible && schedule.active) {
        health.routes = {};
        ++health.traffic_revision;
    }
    const auto index =
        schedule.due(visible,
                     health.network && health.time_trusted && !runtime.update_requested &&
                         !runtime.sync_requested,
                     runtime.settings.traffic.count, mono,
                     runtime.settings.traffic.provider == RoutingProvider::google_routes);
    if (index < 0)
        return;
    auto request = std::unique_ptr<TrafficRequest>(new (std::nothrow) TrafficRequest);
    if (!request) {
        health.routes[index].status = RouteStatus::unavailable;
        ++health.traffic_revision;
        return;
    }
    auto &body = request->body;
    std::size_t size = 0;
    int status = 0;
    const auto &settings = runtime.settings;
    const auto &route = settings.traffic.routes[index];
    const bool open = settings.traffic.provider == RoutingProvider::open;
    auto &url = request->url;
    auto &agent = request->agent;
    std::snprintf(agent, sizeof(agent), "PACE/%s%s%s%s", esp_app_get_description()->version,
                  settings.traffic.contact[0] ? " (" : "", settings.traffic.contact.data(),
                  settings.traffic.contact[0] ? ")" : "");
    RequestHeaders headers{settings.traffic.api_key.data(), kRouteFields};
    bool made_request;
    if (open) {
        headers = {nullptr, nullptr, agent, route.mode == TravelMode::car};
        made_request =
            route.mode == TravelMode::car
                ? make_osrm_url(settings.traffic, route, url, sizeof(url))
                : make_transit_url(settings.traffic, route, settings.language, url, sizeof(url));
    } else {
        std::snprintf(url, sizeof(url), "%s",
                      "https://routes.googleapis.com/directions/v2:computeRoutes");
        made_request = make_route_request(route, settings.language, body, sizeof(body));
    }
    const auto error = made_request
                           ? fetch(url, open ? nullptr : body, false, bytes, kRouteResponseLimit,
                                   mono + 10000, size, status, &headers)
                           : ESP_ERR_INVALID_ARG;
    RouteResult result;
    if (error == ESP_OK)
        result = !open                           ? parse_route(bytes, size)
                 : route.mode == TravelMode::car ? parse_osrm_route(bytes, size)
                                                 : parse_transit_route(bytes, size);
    else
        result.status = status == 401 || status == 403 ? RouteStatus::access_denied
                        : status == 429                ? RouteStatus::quota
                                                       : RouteStatus::unavailable;
    if (status != 200 && result.status == RouteStatus::ok)
        result.status = RouteStatus::invalid;
    // Diagnostic codes only: never log names, coordinates, URLs, contacts or keys.
    ESP_LOGI("traffic", "Request result: transport=%s http=%d route=%u", esp_err_to_name(error),
             status, unsigned(result.status));
    result.fetched_at_monotonic_ms = esp_timer_get_time() / 1000;
    // A screen change during TLS cancels publication; provider content is never persisted.
    if (runtime.traffic_visible)
        health.routes[index] = result;
    ++health.traffic_revision;
}
} // namespace pace::services
