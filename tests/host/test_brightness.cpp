// SPDX-License-Identifier: MIT
#include "services_internal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
unsigned checks{}, writes{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
std::string stored;
bool fail_write{};
} // namespace

namespace pace::services {
esp_err_t load_blob(const char *, const char *key, char *out, std::size_t capacity,
                    std::size_t &size) {
    CHECK(!std::strcmp(key, "brightness"));
    size = stored.size();
    if (size > capacity)
        return ESP_ERR_INVALID_SIZE;
    std::memcpy(out, stored.data(), size);
    return ESP_OK;
}

esp_err_t save_blob(const char *, const char *key, const char *bytes, std::size_t size) {
    CHECK(!std::strcmp(key, "brightness"));
    ++writes;
    if (fail_write)
        return ESP_FAIL;
    stored.assign(bytes, size);
    return ESP_OK;
}
} // namespace pace::services

int main() {
    using namespace pace::services;
    Runtime runtime;
    Health health;
    CHECK(load_brightness(runtime) != ESP_OK && runtime.brightness == 100);
    runtime.brightness_pending = 40;
    save_brightness(runtime, health);
    CHECK(!health.brightness_error && runtime.brightness == 40 && writes == 1);
    runtime.brightness = 100;
    CHECK(load_brightness(runtime) == ESP_OK && runtime.brightness == 40);
    runtime.brightness_pending = 40;
    save_brightness(runtime, health);
    CHECK(writes == 1);
    fail_write = true;
    runtime.brightness_pending = 20;
    save_brightness(runtime, health);
    CHECK(health.brightness_error && runtime.brightness == 40);
    runtime.brightness_pending = 10;
    save_brightness(runtime, health);
    CHECK(health.brightness_error && writes == 2);
    stored = "B2x";
    CHECK(load_brightness(runtime) != ESP_OK && runtime.brightness == 40);
    stored = "B1";
    CHECK(load_brightness(runtime) != ESP_OK && runtime.brightness == 40);
    stored = "B1x";
    CHECK(load_brightness(runtime) != ESP_OK);
    std::printf("PASS: brightness persistence and failures (%u assertions)\n", checks);
}
