// SPDX-License-Identifier: MIT
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "services_internal.hpp"

namespace pace::services {
esp_err_t receive_upload(BodySink sink, void *target, void *source) {
    auto &runtime = *static_cast<Runtime *>(source);
    const esp_err_t ready = ESP_OK;
    xQueueOverwrite(runtime.upload_result, &ready);
    const auto deadline = esp_timer_get_time() / 1000 + 300000;
    UploadChunk chunk;
    for (;;) {
        esp_task_wdt_reset();
        if (esp_timer_get_time() / 1000 >= deadline)
            return ESP_ERR_TIMEOUT;
        if (xQueueReceive(runtime.upload_chunks, &chunk, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;
        if (chunk.size < 0)
            return ESP_FAIL;
        if (chunk.size == 0)
            return ESP_OK;
        if (chunk.size > static_cast<int>(sizeof(chunk.bytes)))
            return ESP_ERR_INVALID_SIZE;
        const auto result = sink(chunk.bytes, chunk.size, target);
        if (result != ESP_OK)
            return result;
    }
}
} // namespace pace::services
