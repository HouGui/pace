// SPDX-License-Identifier: MIT
#include "json_fields.hpp"
#include "pace/traffic_protocol.hpp"
#include <cstdio>

namespace pace {
using namespace json_detail;

bool routing_endpoint(const char *url) {
    return https_url(url) && std::strlen(url) <= 256 && !std::strchr(url, '?');
}

bool route_coordinates(const char *text, double &latitude, double &longitude) {
    // Deliberately accept decimal degrees only, not exponents, NaN or URL syntax.
    for (const char c : std::string_view(text))
        if (!(c >= '0' && c <= '9') && c != '-' && c != '+' && c != '.' && c != ',' && c != ' ')
            return false;
    char extra;
    return std::sscanf(text, " %lf , %lf %c", &latitude, &longitude, &extra) == 2 &&
           std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90 &&
           latitude <= 90 && longitude >= -180 && longitude <= 180;
}

namespace {
bool contact_valid(const char *text) {
    const std::string_view s(text);
    if (s.empty())
        return false;
    for (const unsigned char c : s)
        if (c <= 0x20 || c >= 0x7f || c == '(' || c == ')')
            return false;
    if (https_url(text))
        return true;
    const auto at = s.find('@');
    return at != s.npos && at > 0 && s.find('@', at + 1) == s.npos &&
           s.find('.', at + 1) != s.npos && s.back() != '.';
}
} // namespace

bool parse_traffic_settings(const cJSON *root, TrafficSettings &out) {
    TrafficSettings settings;
    if (!root) {
        out = settings;
        return true;
    }
    bool enabled;
    if (!cJSON_IsObject(root) || !boolean(field(root, "enabled"), enabled))
        return false;
    if (!enabled) {
        out = settings;
        return true;
    }
    // Legacy enabled settings used Google exclusively. Never change their provider silently.
    settings.provider = RoutingProvider::google_routes;
    if (const auto *provider = field(root, "provider")) {
        std::array<char, 16> name{};
        if (!string(provider, name))
            return false;
        if (!std::strcmp(name.data(), "open"))
            settings.provider = RoutingProvider::open;
        else if (std::strcmp(name.data(), "google_routes"))
            return false;
    }
    if (settings.provider == RoutingProvider::google_routes) {
        if (!string(field(root, "api_key"), settings.api_key))
            return false;
        for (const char c : std::string_view(settings.api_key.data()))
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '-' || c == '_'))
                return false;
    } else {
        std::strcpy(settings.road_endpoint.data(), kDefaultOsrmEndpoint);
        std::strcpy(settings.transit_endpoint.data(), kDefaultTransitEndpoint);
        std::strcpy(settings.tile_endpoint.data(), kDefaultTileEndpoint);
        if (const auto *value = field(root, "road_endpoint"))
            if (!string(value, settings.road_endpoint) ||
                !routing_endpoint(settings.road_endpoint.data()))
                return false;
        if (const auto *value = field(root, "transit_endpoint"))
            if (!string(value, settings.transit_endpoint) ||
                !routing_endpoint(settings.transit_endpoint.data()))
                return false;
        if (const auto *value = field(root, "tile_endpoint"))
            if (!string(value, settings.tile_endpoint) ||
                !routing_endpoint(settings.tile_endpoint.data()))
                return false;
        if (const auto *contact = field(root, "contact"))
            if (!string(contact, settings.contact, true) ||
                (settings.contact[0] && !contact_valid(settings.contact.data())))
                return false;
    }
    int count;
    if (!array(field(root, "routes"), kMaxRoutes, count) || count == 0)
        return false;
    for (int i = 0; i < count; ++i) {
        auto *item = cJSON_GetArrayItem(field(root, "routes"), i);
        auto &route = settings.routes[i];
        std::array<char, 8> mode{};
        if (!string(field(item, "name"), route.name) ||
            !string(field(item, "origin"), route.origin) ||
            !string(field(item, "destination"), route.destination) ||
            !string(field(item, "mode"), mode) ||
            (std::strcmp(mode.data(), "car") && std::strcmp(mode.data(), "transit")))
            return false;
        route.mode = mode[0] == 'c' ? TravelMode::car : TravelMode::transit;
        if (settings.provider == RoutingProvider::open) {
            double latitude, longitude;
            if (!route_coordinates(route.origin.data(), latitude, longitude) ||
                !route_coordinates(route.destination.data(), latitude, longitude) ||
                (route.mode == TravelMode::transit && !settings.contact[0]))
                return false;
        }
    }
    settings.count = count;
    out = settings;
    return true;
}
} // namespace pace
