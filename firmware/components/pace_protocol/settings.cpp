// SPDX-License-Identifier: MIT
#include "json_fields.hpp"
#include "pace/traffic_protocol.hpp"
#include <memory>
#include <new>

namespace pace {
using namespace json_detail;

bool https_url(const char *url) {
    const std::string_view s(url);
    if (s.size() > 2048 || s.substr(0, 8) != "https://")
        return false;
    const auto end = s.find('/', 8);
    auto host = s.substr(8, end == s.npos ? s.size() - 8 : end - 8);
    const auto colon = host.find(':');
    if (colon != host.npos) {
        const auto port = host.substr(colon + 1);
        unsigned number = 0;
        if (port.empty() || port.size() > 5)
            return false;
        for (char c : port) {
            if (c < '0' || c > '9')
                return false;
            number = number * 10 + c - '0';
        }
        if (number == 0 || number > 65535)
            return false;
        host = host.substr(0, colon);
    }
    if (host.empty())
        return false;
    for (char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-'))
            return false;
    for (unsigned char c : s)
        if (c <= 0x20 || c >= 0x7f || c == '#' || c == '\\' || c == '@')
            return false;
    return true;
}

bool google_script_url(const char *url) {
    return https_url(url) && std::string_view(url).find("https://script.google.com/macros/s/") == 0;
}

bool google_content_url(const char *url) {
    return https_url(url) &&
           std::string_view(url).find("https://script.googleusercontent.com/") == 0;
}

bool parse_settings(const char *bytes, std::size_t size, Settings &out) {
    Json json(bytes, size, kSettingsMaxBytes);
    const auto *root = json.get();
    // Setup/startup only: one bounded allocation avoids stacking full settings
    // and traffic scratch on the HTTP server or startup task's limited stack.
    auto candidate = std::unique_ptr<Settings>(new (std::nothrow) Settings);
    if (!candidate)
        return false;
    auto &s = *candidate;
    if (!parse_traffic_settings(field(root, "traffic"), s.traffic))
        return false;
    std::int64_t value;
    if (!cJSON_IsObject(root) || !integer(field(root, "schema_version"), 1, 1, value))
        return false;
    if (const auto *updates = field(root, "updates")) {
        if (!cJSON_IsObject(updates) ||
            !string(field(updates, "manifest_url"), s.ota_manifest_url, true) ||
            (s.ota_manifest_url[0] && !https_url(s.ota_manifest_url.data())))
            return false;
    }
    auto *wifi = field(root, "wifi");
    auto *cal = field(root, "calendar");
    auto *p = field(root, "preferences");
    std::array<char, 24> profile{};
    std::array<char, 2> unit{};
    if (!string(field(wifi, "ssid"), s.ssid) ||
        !string(field(wifi, "password"), s.wifi_password, true) ||
        !string(field(cal, "transport_profile"), profile) ||
        !string(field(cal, "endpoint"), s.endpoint, true) ||
        !string(field(cal, "device_token"), s.device_token, true) ||
        !preferences(p, s.preferences) || !string(field(p, "temperature_unit"), unit))
        return false;
    const auto password_size = std::strlen(s.wifi_password.data());
    if (password_size && (password_size < 8 || password_size > 63))
        return false;
    s.demo = !std::strcmp(profile.data(), "demo");
    s.google_redirect = !std::strcmp(profile.data(), "google_apps_script");
    if (!s.demo && !s.google_redirect && std::strcmp(profile.data(), "generic_https"))
        return false;
    if (!s.demo && (!https_url(s.endpoint.data()) || std::strlen(s.device_token.data()) < 43))
        return false;
    if (s.google_redirect && !google_script_url(s.endpoint.data()))
        return false;
    if (unit[0] != 'C' && unit[0] != 'F')
        return false;
    s.fahrenheit = unit[0] == 'F';
    auto *w = field(root, "weather");
    if (!boolean(field(w, "enabled"), s.weather_enabled) ||
        !string(field(w, "location_label"), s.location, true))
        return false;
    if (s.weather_enabled) {
        auto *lat = field(w, "latitude");
        auto *lon = field(w, "longitude");
        if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || lat->valuedouble < -90 ||
            lat->valuedouble > 90 || lon->valuedouble < -180 || lon->valuedouble > 180 ||
            !s.location[0])
            return false;
        s.latitude = lat->valuedouble;
        s.longitude = lon->valuedouble;
    }
    auto *ui = field(root, "ui");
    if (const auto *rgb = field(ui, "secondary_color")) {
        if (!integer(rgb, 0, 0xffffff, value))
            return false;
        s.secondary_rgb = value;
    }
    // Existing settings inherit the label locale; explicit controls are independent.
    s.language = std::strncmp(s.preferences.locale.data(), "de", 2) == 0 &&
                         (s.preferences.locale[2] == '-' || s.preferences.locale[2] == '\0')
                     ? Language::german
                     : Language::english;
    if (const auto *language = field(ui, "language")) {
        std::array<char, 3> name{};
        if (!string(language, name) ||
            (std::strcmp(name.data(), "en") && std::strcmp(name.data(), "de")))
            return false;
        s.language = name[0] == 'd' ? Language::german : Language::english;
    }
    if (!boolean(field(ui, "night_blanking_enabled"), s.night_enabled) ||
        !integer(field(ui, "night_start"), 0, 1439, value))
        return false;
    s.night_start = value;
    if (!integer(field(ui, "night_end"), 0, 1439, value))
        return false;
    s.night_end = value;
    int n;
    auto *styles = field(ui, "category_styles");
    if (!array(styles, 8, n))
        return false;
    s.style_count = n;
    for (int i = 0; i < n; ++i) {
        auto *st = cJSON_GetArrayItem(styles, i);
        if (!string(field(st, "id"), s.styles[i].id) ||
            !integer(field(st, "rgb"), 0, 0xffffff, value))
            return false;
        s.styles[i].rgb = value;
        for (int k = 0; k < i; ++k)
            if (s.styles[i].id == s.styles[k].id || s.styles[i].rgb == s.styles[k].rgb)
                return false;
    }
    out = s;
    return true;
}

} // namespace pace
