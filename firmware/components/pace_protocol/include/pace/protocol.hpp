// SPDX-License-Identifier: MIT
#pragma once
#include "cJSON.h"
#include "pace/model.hpp"
#include <cstddef>
#include <cstdint>

namespace pace {
enum class FeedResult {
    ok,
    invalid,
    access_denied,
    reauthorization_required,
    capacity_exceeded,
    unsupported_preferences,
    temporarily_unavailable,
    invalid_request
};
const char *feed_result_text(FeedResult result);

// Strict byte/depth/UTF-8/duplicate-key checks wrap the SDK parser.
class Json {
  public:
    Json(const char *bytes, std::size_t size, std::size_t limit, unsigned max_depth = 8,
         bool provider_text = false);
    ~Json();
    Json(const Json &) = delete;
    Json &operator=(const Json &) = delete;

    cJSON *get() const { return root_; }

  private:
    cJSON *root_{};
};

// Output is scratch: only publish it after ok. Never parse into the active snapshot.
FeedResult parse_feed(const char *bytes, std::size_t size, const Preferences &requested,
                      Snapshot &scratch);
bool parse_settings(const char *bytes, std::size_t size, Settings &result);
inline constexpr std::size_t kSettingsMaxBytes = 8192;
// Protected setup only: null replaces a saved secret and means "keep" on save.
bool redact_settings(const char *bytes, std::size_t size, char *out, std::size_t capacity);
bool restore_settings_secrets(const char *edit, std::size_t size, const char *stored,
                              std::size_t stored_size, char *out, std::size_t capacity,
                              std::size_t &written);
bool https_url(const char *url);
bool google_script_url(const char *url);
bool google_content_url(const char *url);
bool parse_weather(const char *bytes, std::size_t size, UtcSeconds now,
                   OutdoorTemperatureSample &sample);
} // namespace pace
