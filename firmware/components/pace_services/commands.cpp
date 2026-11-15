// SPDX-License-Identifier: MIT
#include "pace_services.hpp"

namespace pace::services {
bool request(Runtime &runtime, Command command) {
    if (command == Command::sync) {
        if (!runtime.configured)
            return false;
        if (!runtime.sync_in_progress)
            runtime.sync_requested = true;
        return true;
    }
    if (runtime.update_requested && command != Command::update)
        return false;
    if (command == Command::update && runtime.update_requested.exchange(true))
        return true;
    const bool queued = xQueueSend(runtime.commands, &command, 0) == pdTRUE;
    if (!queued && command == Command::update)
        runtime.update_requested = false;
    return queued;
}

} // namespace pace::services
