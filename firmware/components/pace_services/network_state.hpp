// SPDX-License-Identifier: MIT
#pragma once
#include "services_internal.hpp"
#include <atomic>

namespace pace::services::network_detail {
// Only SDK callbacks/UI update these signals. The worker owns all other state.
extern std::atomic<bool> connected, synchronized, screen_ready;
extern std::atomic<std::int64_t> screen_progress;
void worker(void *context);
void make_demo(Snapshot &snapshot, const Preferences &preferences, UtcSeconds now);
} // namespace pace::services::network_detail
