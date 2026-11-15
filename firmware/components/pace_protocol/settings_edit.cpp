// SPDX-License-Identifier: MIT
#include "json_fields.hpp"

namespace pace {
namespace {
struct SecretField {
    const char *section;
    const char *name;
};

constexpr SecretField secrets[] = {{"wifi", "password"},
                                   {"calendar", "endpoint"},
                                   {"calendar", "device_token"},
                                   {"traffic", "api_key"},
                                   {"updates", "manifest_url"}};

bool same_field(const cJSON *edit, const cJSON *stored, const char *section, const char *name) {
    const auto *left = json_detail::field(json_detail::field(edit, section), name);
    const auto *right = json_detail::field(json_detail::field(stored, section), name);
    return cJSON_IsString(left) && cJSON_IsString(right) &&
           !std::strcmp(left->valuestring, right->valuestring);
}
} // namespace

bool redact_settings(const char *bytes, std::size_t size, char *out, std::size_t capacity) {
    Json json(bytes, size, kSettingsMaxBytes);
    if (!cJSON_IsObject(json.get()) || capacity > kSettingsMaxBytes)
        return false;
    for (const auto &secret : secrets) {
        auto *section = cJSON_GetObjectItemCaseSensitive(json.get(), secret.section);
        auto *value = cJSON_GetObjectItemCaseSensitive(section, secret.name);
        if (!value)
            continue; // Optional sections may be absent in older settings.
        if (!cJSON_IsString(value))
            return false;
        if (value->valuestring[0] &&
            !cJSON_ReplaceItemInObjectCaseSensitive(section, secret.name, cJSON_CreateNull()))
            return false;
    }
    return cJSON_PrintPreallocated(json.get(), out, capacity, false);
}

bool restore_settings_secrets(const char *edit, std::size_t size, const char *stored,
                              std::size_t stored_size, char *out, std::size_t capacity,
                              std::size_t &written) {
    written = 0;
    Json proposed(edit, size, kSettingsMaxBytes);
    Json previous(stored, stored_size, kSettingsMaxBytes);
    if (!cJSON_IsObject(proposed.get()) || capacity > kSettingsMaxBytes)
        return false;
    for (const auto &secret : secrets) {
        auto *section = cJSON_GetObjectItemCaseSensitive(proposed.get(), secret.section);
        const auto *value = json_detail::field(section, secret.name);
        if (!cJSON_IsNull(value))
            continue; // Explicit strings replace or clear; the settings parser validates them.
        const auto *saved =
            json_detail::field(json_detail::field(previous.get(), secret.section), secret.name);
        if (!cJSON_IsString(saved))
            return false;
        // Never forward a retained credential to a newly selected network or adapter.
        if (saved->valuestring[0] &&
            ((!std::strcmp(secret.section, "wifi") &&
              !same_field(proposed.get(), previous.get(), "wifi", "ssid")) ||
             (!std::strcmp(secret.name, "device_token") &&
              (!same_field(proposed.get(), previous.get(), "calendar", "endpoint") ||
               !same_field(proposed.get(), previous.get(), "calendar", "transport_profile")))))
            return false;
        if (!cJSON_ReplaceItemInObjectCaseSensitive(section, secret.name,
                                                    cJSON_CreateString(saved->valuestring)))
            return false;
    }
    if (!cJSON_PrintPreallocated(proposed.get(), out, capacity, false))
        return false;
    written = std::strlen(out);
    return true;
}
} // namespace pace
