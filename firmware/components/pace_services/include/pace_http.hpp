// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <cstddef>
#include <cstdint>

namespace pace::services {
using BodySink = esp_err_t (*)(const char *, std::size_t, void *);

struct RequestHeaders {
    const char *api_key{};
    const char *field_mask{};
    const char *user_agent{};
    bool read_bad_request_body{}; // OSRM reports no-route errors as bounded JSON with HTTP 400.
    const char *accept{"application/json"};
};

// Caller owns sink and its context for this synchronous, bounded operation.
esp_err_t transfer(const char *url, const char *post, bool google_redirect, std::size_t limit,
                   std::int64_t deadline_ms, BodySink sink, void *context, std::size_t &received,
                   int &status, const RequestHeaders *headers = nullptr);
esp_err_t fetch(const char *url, const char *post, bool google_redirect, char *out,
                std::size_t capacity, std::int64_t deadline_ms, std::size_t &size, int &status,
                const RequestHeaders *headers = nullptr);
} // namespace pace::services
