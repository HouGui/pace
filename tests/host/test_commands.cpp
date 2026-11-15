// SPDX-License-Identifier: MIT
#include "services_internal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace {
unsigned checks{}, queued{}, watchdogs{};
bool full{};
std::int64_t clock_ms{};
std::deque<pace::services::UploadChunk> chunks;
std::size_t consumed{};
int received_result{};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                                        \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
} // namespace

std::int64_t esp_timer_get_time() { return clock_ms * 1000; }

esp_err_t esp_task_wdt_reset() {
    ++watchdogs;
    return ESP_OK;
}

int xQueueSend(QueueHandle_t, const void *, TickType_t) {
    if (full)
        return pdFALSE;
    ++queued;
    return pdTRUE;
}

int xQueueOverwrite(QueueHandle_t, const void *result) {
    received_result = *static_cast<const int *>(result);
    return pdTRUE;
}

int xQueueReceive(QueueHandle_t, void *out, TickType_t wait) {
    if (chunks.empty()) {
        clock_ms += wait;
        return pdFALSE;
    }
    *static_cast<pace::services::UploadChunk *>(out) = chunks.front();
    chunks.pop_front();
    return pdTRUE;
}

int main() {
    using namespace pace::services;
    Runtime runtime;
    CHECK(!request(runtime, Command::sync));
    runtime.configured = true;
    CHECK(request(runtime, Command::sync) && runtime.sync_requested);
    CHECK(request(runtime, Command::sync) && queued == 0);
    runtime.sync_requested = false;
    runtime.sync_in_progress = true;
    CHECK(request(runtime, Command::sync) && !runtime.sync_requested);
    CHECK(request(runtime, Command::update) && queued == 1 && runtime.update_requested);
    CHECK(request(runtime, Command::update) && queued == 1);
    CHECK(!request(runtime, Command::close_setup) && queued == 1);
    runtime.update_requested = false;
    full = true;
    CHECK(!request(runtime, Command::update) && !runtime.update_requested);
    CHECK(!request(runtime, Command::setup));
    full = false;
    CHECK(request(runtime, Command::setup) && queued == 2);
    const auto sink = [](const char *, std::size_t size, void *) {
        consumed += size;
        return ESP_OK;
    };
    UploadChunk chunk;
    chunk.size = 2048;
    chunks.push_back(chunk);
    chunks.push_back(chunk);
    chunk.size = 100;
    chunks.push_back(chunk);
    chunk.size = 0;
    chunks.push_back(chunk);
    CHECK(receive_upload(sink, nullptr, &runtime) == ESP_OK);
    CHECK(consumed == 4196 && received_result == ESP_OK && watchdogs == 4);
    chunk.size = -1;
    chunks.push_back(chunk);
    CHECK(receive_upload(sink, nullptr, &runtime) == ESP_FAIL);
    chunk.size = 2049;
    chunks.push_back(chunk);
    CHECK(receive_upload(sink, nullptr, &runtime) == ESP_ERR_INVALID_SIZE);
    chunk.size = 1;
    chunks.push_back(chunk);
    CHECK(receive_upload([](const char *, std::size_t, void *) { return ESP_FAIL; }, nullptr,
                         &runtime) == ESP_FAIL);
    clock_ms = 0;
    CHECK(receive_upload(sink, nullptr, &runtime) == ESP_ERR_TIMEOUT && clock_ms == 300000);
    std::printf("PASS: command coalescing, upload backpressure and deadline (%u assertions)\n",
                checks);
}
