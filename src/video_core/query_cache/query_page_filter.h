// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>

#include "common/common_types.h"

namespace VideoCommon {

/// Pages that ever held a cached query, hashed into bits: a set bit may belong to another page,
/// a clear one means no query was ever cached in the page. Read without the cache's lock.
class QueryPageFilter {
public:
    /// Called under the cache's lock, before the page's entry is added.
    void Mark(u64 page) noexcept {
        words[Word(page)].fetch_or(Bit(page), std::memory_order_release);
    }

    /// Whether a page of [page_begin, page_end] may hold queries. Long ranges always may.
    [[nodiscard]] bool MayHold(u64 page_begin, u64 page_end) const noexcept {
        if (page_end - page_begin >= 64) {
            return true;
        }
        for (u64 page = page_begin; page <= page_end; ++page) {
            if ((words[Word(page)].load(std::memory_order_acquire) & Bit(page)) != 0) {
                return true;
            }
        }
        return false;
    }

private:
    static constexpr size_t NumWords = 1024;

    static size_t Word(u64 page) noexcept {
        return static_cast<size_t>((page >> 6) % NumWords);
    }
    static u64 Bit(u64 page) noexcept {
        return u64{1} << (page & 63);
    }

    std::array<std::atomic<u64>, NumWords> words{};
};

} // namespace VideoCommon
