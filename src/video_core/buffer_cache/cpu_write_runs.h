// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include "common/common_types.h"

namespace VideoCommon {

/// Guest CPU writes collected and not applied yet, as sorted disjoint [begin, end) runs: a
/// download must not write the GPU's older copy over them.
class CpuWriteRuns {
public:
    void Add(DAddr begin, u64 size) {
        if (size != 0) {
            runs.emplace_back(begin, begin + size);
        }
    }

    /// Sorts and merges what Add collected.
    void Finish() {
        if (runs.size() < 2) {
            return;
        }
        std::ranges::sort(runs);
        size_t last = 0;
        for (size_t i = 1; i < runs.size(); ++i) {
            if (runs[i].first <= runs[last].second) {
                runs[last].second = std::max(runs[last].second, runs[i].second);
            } else {
                runs[++last] = runs[i];
            }
        }
        runs.resize(last + 1);
    }

    [[nodiscard]] bool Empty() const noexcept {
        return runs.empty();
    }

    /// Calls `func(begin, end)` for each part of [addr, addr + size) that no run covers.
    template <typename Func>
    void ForEachUncovered(DAddr addr, u64 size, Func&& func) const {
        const DAddr end = addr + size;
        auto it = std::ranges::upper_bound(runs, addr, {}, &Run::second);
        DAddr cursor = addr;
        for (; it != runs.end() && it->first < end; ++it) {
            if (it->first > cursor) {
                func(cursor, it->first);
            }
            cursor = std::max(cursor, it->second);
        }
        if (cursor < end) {
            func(cursor, end);
        }
    }

private:
    using Run = std::pair<DAddr, DAddr>;
    std::vector<Run> runs;
};

} // namespace VideoCommon
