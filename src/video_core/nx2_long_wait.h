// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdlib>
#include <source_location>
#include <string_view>

#include "common/logging.h"

namespace VideoCommon {

/// Logs a wait of the GPU threads long enough to show as a hitch (50 ms or more, or
/// EDEN_NX2_LONG_WAIT_MS), with what waited and the code that asked for it. Hitches where a
/// thread waits for another one leave nothing else in the log; this names the wait. Only for the
/// slow path of a wait: it reads the clock twice.
class LongWait {
public:
    LongWait(const char* what_, const std::source_location& where_)
        : what{what_}, where{where_}, start{std::chrono::steady_clock::now()} {}

    ~LongWait() {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= Threshold()) [[unlikely]] {
            std::string_view file{where.file_name()};
            if (const auto slash = file.find_last_of("/\\"); slash != std::string_view::npos) {
                file.remove_prefix(slash + 1);
            }
            LOG_INFO(HW_GPU, "{} took {} ms ({}:{})", what,
                     std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), file,
                     where.line());
        }
    }

    LongWait(const LongWait&) = delete;
    LongWait& operator=(const LongWait&) = delete;

private:
    static std::chrono::milliseconds Threshold() {
        static const std::chrono::milliseconds threshold{[] {
            const char* value = std::getenv("EDEN_NX2_LONG_WAIT_MS");
            return value != nullptr ? std::strtol(value, nullptr, 10) : 50L;
        }()};
        return threshold;
    }

    const char* what;
    std::source_location where;
    std::chrono::steady_clock::time_point start;
};

} // namespace VideoCommon
