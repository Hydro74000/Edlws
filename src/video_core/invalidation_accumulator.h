// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include "common/common_types.h"

namespace VideoCommon {

class InvalidationAccumulator {
public:
    InvalidationAccumulator() = default;
    ~InvalidationAccumulator() = default;

    void Add(GPUVAddr address, size_t size) noexcept {
        const GPUVAddr end_address = start_address + accumulated_size;
        if (address >= start_address && address + size <= end_address) {
            return;
        }
        // From the start of the first block the write touches to the end of the last one.
        const GPUVAddr first_block = address & atomicity_mask;
        const GPUVAddr blocks_end = (address + size + atomicity_size_mask) & atomicity_mask;
        if (start_address == 0) {
            start_address = first_block;
            accumulated_size = blocks_end - first_block;
        } else if (first_block <= end_address && blocks_end >= start_address) {
            // Overlapping or touching the current range, before or after it: grow it.
            start_address = (std::min)(start_address, first_block);
            accumulated_size = (std::max)(end_address, blocks_end) - start_address;
        } else {
            buffer.emplace_back(start_address, accumulated_size);
            start_address = first_block;
            accumulated_size = blocks_end - first_block;
        }
    }

    template <typename F>
    [[nodiscard]] bool InvalidateAll(F&& f) noexcept {
        if (start_address == 0) {
            return false;
        }
        if (buffer.empty()) {
            f(start_address, accumulated_size);
        } else if (buffer.size() >= MaxMergedRanges) {
            // Sorting thousands of ranges (Donkey Kong Bananza has such flushes, with few
            // overlaps) costs more than the calls it saves.
            for (const auto& [address, size] : buffer) {
                f(address, size);
            }
            f(start_address, accumulated_size);
            buffer.clear();
        } else {
            // Constant buffer writes come back to earlier ranges (a third of the ranges of a
            // flush overlapped or touched another in a Mario Kart World race): ranges that
            // overlap or touch are invalidated as one, the same bytes in fewer calls.
            buffer.emplace_back(start_address, accumulated_size);
            std::sort(buffer.begin(), buffer.end());
            GPUVAddr merged_start = buffer.front().first;
            GPUVAddr merged_end = merged_start + buffer.front().second;
            for (size_t i = 1; i < buffer.size(); ++i) {
                const auto [address, size] = buffer[i];
                if (address <= merged_end) {
                    merged_end = (std::max)(merged_end, address + size);
                    continue;
                }
                f(merged_start, merged_end - merged_start);
                merged_start = address;
                merged_end = address + size;
            }
            f(merged_start, merged_end - merged_start);
            buffer.clear();
        }
        start_address = 0;
        accumulated_size = 0;
        return true;
    }

private:
    static constexpr size_t MaxMergedRanges = 256;
    static constexpr size_t atomicity_bits = 5;
    static constexpr size_t atomicity_size = 1ULL << atomicity_bits;
    static constexpr size_t atomicity_size_mask = atomicity_size - 1;
    static constexpr size_t atomicity_mask = ~atomicity_size_mask;
    std::vector<std::pair<VAddr, size_t>> buffer;
    GPUVAddr start_address = 0;
    size_t accumulated_size = 0;
};

} // namespace VideoCommon
