// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"

namespace VideoCommon {

class UsageTracker {
    // PAGE_SHIFT is a macro on FreeBSD
    static constexpr size_t BUFFER_BYTES_PER_BITSHIFT = 6;
    static constexpr size_t BUFFER_PAGE_SHIFT = 6 + BUFFER_BYTES_PER_BITSHIFT;
    static constexpr size_t BUFFER_PAGE_BYTES = 1 << BUFFER_PAGE_SHIFT;
public:
    explicit UsageTracker(size_t size) {
        const size_t num_pages = (size >> BUFFER_PAGE_SHIFT) + 1;
        pages.resize(num_pages, 0ULL);
    }

    void Reset() noexcept {
        // Only the pages tracked since the last reset: the runtime resets every idle buffer each
        // frame, most of them untouched or barely touched.
        if (dirty_begin < dirty_end) {
            std::fill(pages.begin() + dirty_begin, pages.begin() + dirty_end, 0ULL);
        }
        dirty_begin = std::numeric_limits<size_t>::max();
        dirty_end = 0;
        recent.fill({~u64{0}, 0});
    }

    void Track(u64 offset, u64 size) noexcept {
        // Tracking is idempotent until Reset: skip ranges tracked just before. Draws bind the
        // same (often multi-megabyte) storage buffers again and again, and each Track wrote a
        // word per 4 KiB of the range.
        for (const auto& [recent_offset, recent_size] : recent) {
            if (recent_offset == offset && recent_size == size) {
                return;
            }
        }
        recent[recent_cursor++ % recent.size()] = {offset, size};
        const size_t page = offset >> BUFFER_PAGE_SHIFT;
        const size_t page_end = (offset + size) >> BUFFER_PAGE_SHIFT;
        if (page_end < page || page_end >= pages.size()) {
            return;
        }
        dirty_begin = std::min(dirty_begin, page);
        dirty_end = std::max(dirty_end, page_end + 1);
        TrackPage(page, offset, size);
        if (page == page_end) {
            return;
        }
        for (size_t i = page + 1; i < page_end; i++) {
            pages[i] = ~u64{0};
        }
        const size_t offset_end = offset + size;
        const size_t offset_end_page_aligned = Common::AlignDown(offset_end, BUFFER_PAGE_BYTES);
        TrackPage(page_end, offset_end_page_aligned, offset_end - offset_end_page_aligned);
    }

    [[nodiscard]] bool IsUsed(u64 offset, u64 size) const noexcept {
        const size_t page = offset >> BUFFER_PAGE_SHIFT;
        const size_t page_end = (offset + size) >> BUFFER_PAGE_SHIFT;
        if (page_end < page || page_end >= pages.size()) {
            return false;
        }
        if (IsPageUsed(page, offset, size)) {
            return true;
        }
        if (page == page_end) {
            // Checking the tail below would count the page from its start, before the range.
            return false;
        }
        for (size_t i = page + 1; i < page_end; i++) {
            if (pages[i] != 0) {
                return true;
            }
        }
        const size_t offset_end = offset + size;
        const size_t offset_end_page_aligned = Common::AlignDown(offset_end, BUFFER_PAGE_BYTES);
        return IsPageUsed(page_end, offset_end_page_aligned, offset_end - offset_end_page_aligned);
    }

private:
    /// Mask of the 64-byte blocks of a page that a range touches, clamped to the page end. Counting
    /// whole blocks from the first one lost the last block of a range ending inside it (a 16-byte
    /// write tracked nothing), and an empty range shifted by 64.
    static u64 PageMask(u64 offset, u64 size) noexcept {
        const size_t offset_in_page = offset % BUFFER_PAGE_BYTES;
        const size_t first_bit = offset_in_page >> BUFFER_BYTES_PER_BITSHIFT;
        const size_t bytes = std::min<size_t>(size, BUFFER_PAGE_BYTES - offset_in_page);
        const size_t last_bit = (offset_in_page + bytes - 1) >> BUFFER_BYTES_PER_BITSHIFT;
        const size_t num_bits = last_bit - first_bit + 1;
        return (~u64{0} >> (64 - num_bits)) << first_bit;
    }

    void TrackPage(u64 page, u64 offset, u64 size) noexcept {
        if (size != 0) {
            pages[page] |= PageMask(offset, size);
        }
    }

    bool IsPageUsed(u64 page, u64 offset, u64 size) const noexcept {
        return size != 0 && (pages[page] & PageMask(offset, size)) != 0;
    }

private:
    std::vector<u64> pages;
    /// Ranges tracked last, (offset, size).
    std::array<std::pair<u64, u64>, 4> recent{{{~u64{0}, 0}, {~u64{0}, 0}, {~u64{0}, 0}, {~u64{0}, 0}}};
    size_t recent_cursor{};
    /// Pages [dirty_begin, dirty_end) hold every bit set since the last reset.
    size_t dirty_begin{std::numeric_limits<size_t>::max()};
    size_t dirty_end{};
};

} // namespace VideoCommon
