// SPDX-License-Identifier: MIT
#pragma once
#include "pace/core.hpp"

namespace pace {
// Worker-owned timing; a manual request bypasses a retry deadline, not prerequisites.
struct SyncSchedule {
    static constexpr std::int64_t interval_ms = 10 * 60 * 1000;
    std::int64_t next{};
    unsigned failures{};

    bool due(std::int64_t now, bool manual) const { return manual || now >= next; }

    void complete(std::int64_t now, bool accepted, std::uint32_t entropy) {
        if (accepted) {
            failures = 0;
            next = now + interval_ms;
        } else {
            next = now + 1000 * retry_seconds(failures, entropy);
            if (failures < 6)
                ++failures;
        }
    }
};
} // namespace pace
