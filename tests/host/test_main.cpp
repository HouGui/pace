// SPDX-License-Identifier: MIT
#include "pace/core.hpp"
#include "pace/protocol.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {
unsigned checks = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

std::string fixture(const char *name) {
    std::ifstream file(std::string(PACE_SOURCE_DIR) + name);
    CHECK(file.good());
    return {(std::istreambuf_iterator<char>(file)), {}};
}

pace::Preferences prefs() {
    pace::Preferences p;
    std::strcpy(p.time_zone.data(), "Etc/UTC");
    std::strcpy(p.locale.data(), "en-GB");
    return p;
}

pace::Snapshot scratch;

void mutation(const std::string &good, void (*change)(cJSON *),
              pace::FeedResult expected = pace::FeedResult::invalid) {
    auto *json = cJSON_Parse(good.c_str());
    CHECK(json);
    change(json);
    char *text = cJSON_PrintUnformatted(json);
    CHECK(text);
    CHECK(pace::parse_feed(text, std::strlen(text), prefs(), scratch) == expected);
    cJSON_free(text);
    cJSON_Delete(json);
}

cJSON *item(cJSON *j, const char *name, int index = 0) {
    return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, name), index);
}
} // namespace

int main() {
    using namespace pace;
    CHECK(date_number("2024-02-29") >= 0);
    CHECK(date_number("2023-02-29") == -1);
    CHECK(date_number("2030-04-31") == -1);
    CHECK(date_number("2030-01-01junk") == -1);
    CHECK(date_number("2025-01-01") == date_number("2024-12-31") + 1);
    CHECK(valid_utf8("Réunion 東京"));
    CHECK(!valid_utf8("\xc0\xaf"));
    CHECK(!valid_utf8("\xed\xa0\x80"));
    CHECK(!valid_utf8("\xf4\x90\x80\x80"));
    CHECK(!valid_utf8("\xe2\x82"));
    CHECK(!valid_utf8("a\nb"));
    CHECK(night_active(23 * 60, 22 * 60, 7 * 60));
    CHECK(night_active(0, 22 * 60, 7 * 60));
    CHECK(!night_active(7 * 60, 22 * 60, 7 * 60));
    CHECK(!night_active(-1, 22 * 60, 7 * 60));
    CHECK(!night_active(600, 600, 600));
    CHECK(night_active(600, 500, 700));
    CHECK(std::fabs(display_temperature(25, true) - 77) < 0.001);
    std::uint8_t crc_input[] = {0xbe, 0xef};
    CHECK(sht_crc(crc_input, 2) == 0x92);
    std::array<std::uint8_t, 6> sensor = {0x80, 0, 0, 0x80, 0, 0};
    sensor[2] = sht_crc(sensor.data(), 2);
    sensor[5] = sht_crc(sensor.data() + 3, 2);
    float temp = 0;
    CHECK(decode_sht40(sensor, temp));
    CHECK(std::fabs(temp - 42.5013) < 0.01);
    sensor[5] ^= 1;
    CHECK(!decode_sht40(sensor, temp));
    CHECK(retry_seconds(100, 0) == 900);
    CHECK(retry_seconds(0, 0) == 15);
    CHECK(https_url("https://example.invalid/feed"));
    CHECK(!https_url("http://example.invalid"));
    CHECK(!https_url("https://user@example.invalid/"));
    CHECK(!https_url("https://example.invalid/#x"));
    CHECK(google_script_url("https://script.google.com/macros/s/example/exec"));
    CHECK(google_content_url("https://script.googleusercontent.com/macros/echo?x=1"));
    CHECK(!google_content_url("https://script.googleusercontent.com.evil.invalid/"));
    for (const char *bad :
         {"{\"a\":1,\"a\":2}", "{} trailing", "{\"a\":\"x\\u0000suffix\"}", "[[[[[[[[[0]]]]]]]]]",
          "{\"a\":\"\xc0\xaf\"}", "[01]", "[1.]", "[1e]", "[-01]", "{\v\"a\":1}"}) {
        Json j(bad, std::strlen(bad), 65536);
        CHECK(!j.get());
    }
    const auto good = fixture("/docs/interfaces/calendar-feed.example.json");
    CHECK(parse_feed(good.data(), good.size(), prefs(), scratch) == FeedResult::ok);
    const Snapshot s = scratch;
    mutation(good, [](auto *j) {
        auto *segments = cJSON_GetObjectItemCaseSensitive(j, "clock");
        auto *first = item(j, "clock");
        const auto start = cJSON_GetObjectItemCaseSensitive(first, "start_utc")->valuedouble;
        auto *middle = cJSON_Duplicate(first, true);
        auto *last = cJSON_Duplicate(first, true);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(first, "end_utc"), start + 82800);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(middle, "start_utc"), start + 82800);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(middle, "end_utc"), start + 84600);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(middle, "offset_seconds"), 7200);
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(last, "start_utc"), start + 84600);
        cJSON_AddItemToArray(segments, middle);
        cJSON_AddItemToArray(segments, last);
    });
    CHECK(s.event_count == 2);
    CHECK(s.day_count == 14);
    CHECK(s.events[0].location[0] == '\0');
    mutation(
        good,
        [](auto *j) { cJSON_AddStringToObject(item(j, "events"), "location", "München · Büro 2"); },
        FeedResult::ok);
    CHECK(std::strcmp(scratch.events[0].location.data(), "München · Büro 2") == 0);
    mutation(
        good, [](auto *j) { cJSON_AddStringToObject(item(j, "events"), "location", ""); },
        FeedResult::ok);
    mutation(good, [](auto *j) {
        cJSON_AddStringToObject(item(j, "events"), "location", std::string(193, 'x').c_str());
    });
    mutation(good, [](auto *j) {
        cJSON_AddStringToObject(item(j, "events"), "location", "room\nother");
    });
    mutation(good, [](auto *j) { cJSON_AddNumberToObject(item(j, "events"), "location", 1); });
    mutation(good, [](auto *j) { cJSON_AddNullToObject(item(j, "events"), "location"); });
    CHECK(current_day(s, s.days[0].start_utc) == 0);
    CHECK(current_day(s, s.days[0].end_utc) == 1);
    CHECK(current_day(s, s.days[13].end_utc) == -1);
    CHECK(freshness(&s, s.generated_at_utc, true) == Freshness::current);
    CHECK(freshness(&s, s.generated_at_utc + 1800, true) == Freshness::stale);
    CHECK(freshness(&s, s.days[8].start_utc, true) == Freshness::expired);
    CHECK(freshness(&s, s.generated_at_utc, false) == Freshness::time_unknown);
    CHECK(freshness(nullptr, 0, false) == Freshness::unavailable);
    CHECK(events_for_day(s, 2).count == 1);
    CHECK(events_for_day(s, 3).count == 0);
    CHECK(events_for_day(s, 14).count == 0);
    const auto t = std::get<TimedInterval>(s.events[0].interval);
    CHECK(highlight(s, s.events[0], t.start_utc, false) == Highlight::none);
    CHECK(highlight(s, s.events[0], t.start_utc, true) == Highlight::now);
    CHECK(highlight(s, s.events[0], t.end_utc, true) == Highlight::past);
    CHECK(highlight(s, s.events[0], t.start_utc - 1, true) == Highlight::next);
    Snapshot ties = s;
    ties.events[2] = ties.events[0];
    ties.event_count = 3;
    CHECK(highlight(ties, ties.events[2], t.start_utc - 1, true) == Highlight::next);
    TimedInterval overnight = t;
    overnight.start_utc = s.days[0].end_utc - 100;
    overnight.end_utc = s.days[1].start_utc + 100;
    Event e;
    e.interval = overnight;
    CHECK(overlaps(e, s.days[0]));
    CHECK(overlaps(e, s.days[1]));
    CHECK(!overlaps(e, s.days[2]));
    // Clock offset changes within a day: forward gap and backward fold.
    Snapshot dst;
    dst.clock_count = 2;
    dst.clock[0] = {1893974400, 1893978000, 3600};
    dst.clock[1] = {1893978000, 1894060800, 7200};
    CHECK(local_minute(dst, 1893977999) == 119);
    CHECK(local_minute(dst, 1893978000) == 180);
    dst.clock[0].offset_seconds = 7200;
    dst.clock[1].offset_seconds = 3600;
    CHECK(local_minute(dst, 1893977999) == 179);
    CHECK(local_minute(dst, 1893978000) == 120);
    CHECK(local_minute(dst, 1894060800) == -1);
    mutation(good, [](auto *j) {
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(j, "schema_version"), 1);
    });
    mutation(good, [](auto *j) {
        cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(j, "events"),
                             cJSON_Duplicate(item(j, "events"), true));
    });
    mutation(good, [](auto *j) {
        cJSON_AddStringToObject(item(j, "events"), "start_date", "2030-01-01");
    });
    mutation(good, [](auto *j) {
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(item(j, "days"), "end_utc"), 1);
    });
    mutation(good, [](auto *j) {
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(item(j, "clock"), "offset_seconds"),
                             3600);
    });
    mutation(good, [](auto *j) {
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(j, "generated_at_utc"), 1894003200.5);
    });
    mutation(good, [](auto *j) {
        cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(j, "time_zone"), "Europe/Berlin");
    });
    mutation(good, [](auto *j) {
        cJSON_SetValuestring(
            cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(item(j, "events"), "category_ids"),
                               0),
            "unknown");
    });
    mutation(
        good,
        [](auto *j) {
            cJSON_DeleteItemFromObjectCaseSensitive(j, "events");
            cJSON_AddArrayToObject(j, "events");
        },
        FeedResult::ok);
    const char error[] =
        "{\"schema_version\":2,\"ok\":false,\"error\":{\"code\":\"access_denied\"}}";
    CHECK(parse_feed(error, sizeof(error) - 1, prefs(), scratch) == FeedResult::access_denied);
    const auto config = fixture("/docs/device.example.json");
    Settings settings;
    CHECK(parse_settings(config.data(), config.size(), settings));
    CHECK(settings.demo);
    CHECK(settings.language == Language::english);
    auto *configured = cJSON_Parse(config.c_str());
    auto *ui = cJSON_GetObjectItemCaseSensitive(configured, "ui");
    auto parse_configured = [&]() {
        char *text = cJSON_PrintUnformatted(configured);
        const bool result = parse_settings(text, std::strlen(text), settings);
        cJSON_free(text);
        return result;
    };
    cJSON_AddStringToObject(ui, "language", "de");
    CHECK(parse_configured() && settings.language == Language::german);
    cJSON_ReplaceItemInObjectCaseSensitive(ui, "language", cJSON_CreateString("fr"));
    CHECK(!parse_configured());
    cJSON_DeleteItemFromObjectCaseSensitive(ui, "language");
    cJSON_ReplaceItemInObjectCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(configured, "preferences"), "locale",
        cJSON_CreateString("de-DE"));
    CHECK(parse_configured() && settings.language == Language::german);
    cJSON_AddStringToObject(ui, "language", "en");
    CHECK(parse_configured() && settings.language == Language::english);
    auto *weather_config = cJSON_GetObjectItemCaseSensitive(configured, "weather");
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "enabled", cJSON_CreateBool(true));
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "location_label",
                                           cJSON_CreateString("Berlin test"));
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "latitude", cJSON_CreateNumber(52.52));
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "longitude", cJSON_CreateNumber(13.405));
    CHECK(parse_configured() && settings.weather_enabled && settings.latitude == 52.52 &&
          settings.longitude == 13.405);
    CHECK(!std::strcmp(settings.location.data(), "Berlin test"));
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "latitude", cJSON_CreateNumber(91));
    CHECK(!parse_configured());
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "latitude", cJSON_CreateNumber(52.52));
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "longitude", cJSON_CreateNumber(-181));
    CHECK(!parse_configured());
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "longitude", cJSON_CreateNumber(13.405));
    cJSON_ReplaceItemInObjectCaseSensitive(weather_config, "location_label",
                                           cJSON_CreateString(""));
    CHECK(!parse_configured());
    cJSON_Delete(configured);
    CHECK(https_url("https://example.invalid:8443/feed"));
    CHECK(!https_url("https://example.invalid:65536/feed"));
    CHECK(!https_url("https://example.invalid:/feed"));
    CHECK(!https_url("https://example.invalid:0/feed"));
    mutation(good, [](auto *j) {
        cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(item(j, "days"), "date"),
                             "2030-02-30");
    });
    mutation(good, [](auto *j) {
        cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(j, "days"), 13);
    });
    mutation(good, [](auto *j) {
        cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(j, "clock"),
                             cJSON_Duplicate(item(j, "clock"), true));
    });
    mutation(good, [](auto *j) {
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(item(j, "events"), "end_utc"),
                             1894035600);
    });
    mutation(good, [](auto *j) {
        cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(item(j, "events"), "title"),
                             std::string(129, 'x').c_str());
    });
    // Seeded mutations exercise parser memory safety; exact rejection cases above
    // establish semantics. A mutation may legitimately remain a valid feed.
    std::uint32_t seed = 0x50414345;
    for (unsigned i = 0; i < 1000; ++i) {
        auto damaged = good;
        seed = 1664525 * seed + 1013904223;
        damaged[seed % damaged.size()] ^= static_cast<char>((seed >> 24) | 1);
        (void)parse_feed(damaged.data(), damaged.size(), prefs(), scratch);
    }
    for (std::size_t i = 0; i < good.size(); i += 17)
        CHECK(parse_feed(good.data(), i, prefs(), scratch) == FeedResult::invalid);
    const char weather[] = "{\"current\":{\"time\":1894003200,\"temperature_2m\":12.5},\"current_"
                           "units\":{\"temperature_2m\":\"°C\"}}";
    OutdoorTemperatureSample outside;
    CHECK(parse_weather(weather, sizeof(weather) - 1, 1894003200, outside));
    CHECK(!parse_weather(weather, sizeof(weather) - 1, 1894003200 + 21601, outside));
    CHECK(!parse_weather(weather, sizeof(weather) - 1, 1894003200 - 301, outside));
    CHECK(!parse_weather("{}", 2, 1894003200, outside));
    CHECK(outside.valid && outside.celsius == 12.5f && outside.source_time_utc == 1894003200);
    std::string bad_weather(weather);
    bad_weather.replace(bad_weather.find("°C"), std::strlen("°C"), "°F");
    CHECK(!parse_weather(bad_weather.data(), bad_weather.size(), 1894003200, outside));
    CHECK(outside.celsius == 12.5f);
    bad_weather = weather;
    bad_weather.replace(bad_weather.find("12.5"), 4, "null");
    CHECK(!parse_weather(bad_weather.data(), bad_weather.size(), 1894003200, outside));
    CHECK(outdoor_freshness(outside, 1894003200, 0, true) == Freshness::current);
    CHECK(outdoor_freshness(outside, 1894006800, 1000, true) == Freshness::stale);
    CHECK(outdoor_freshness(outside, 1894003200, 3600000, true) == Freshness::stale);
    CHECK(outdoor_freshness(outside, 1894024801, 1000, true) == Freshness::expired);
    CHECK(outdoor_freshness(outside, 1894003200, 21600001, true) == Freshness::expired);
    CHECK(outdoor_freshness(outside, 1894003200, 0, false) == Freshness::time_unknown);
    CHECK(outdoor_freshness(outside, 1894002899, 0, true) == Freshness::time_unknown);
    CHECK(indoor_available({20.0f, 1000, true}, 301000));
    CHECK(!indoor_available({20.0f, 1000, true}, 301001));
    CHECK(!indoor_available({20.0f, 1000, true}, 0));
    CHECK(!indoor_available({20.0f, 1000, false}, 1000));
    std::puts("PASS: domain, bounded parser, settings, sensor CRC and security policy");
    std::printf("%u assertions\n", checks);
}
