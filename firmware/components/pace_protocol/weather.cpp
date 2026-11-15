// SPDX-License-Identifier: MIT
#include "json_fields.hpp"

namespace pace {
using namespace json_detail;

bool parse_weather(const char *bytes, std::size_t size, UtcSeconds now,
                   OutdoorTemperatureSample &sample) {
    Json json(bytes, size, 16384);
    auto *current = field(json.get(), "current");
    auto *temp = field(current, "temperature_2m");
    UtcSeconds source;
    auto *unit = field(field(json.get(), "current_units"), "temperature_2m");
    if (!cJSON_IsNumber(temp) || temp->valuedouble < -100 || temp->valuedouble > 70 ||
        !timestamp(field(current, "time"), source) || source > now + 300 || now - source > 21600 ||
        !cJSON_IsString(unit) || std::strcmp(unit->valuestring, "°C"))
        return false;
    sample.celsius = temp->valuedouble;
    sample.source_time_utc = source;
    sample.valid = true;
    return true;
}
} // namespace pace
