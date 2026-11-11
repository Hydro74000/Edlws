// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/nx2.h"

namespace {

// The table is one for the process: these tests use the words of the last 1 GiB of it.
constexpr u64 Base = (u64{1} << 36) - (u64{1} << 30);
constexpr u64 Page = u64{1} << Tegra::Nx2WrittenPageBits;

void Clear() {
    for (u64 index = (Base / Page) / 64; index < Tegra::g_nx2_gpu_written_pages.size(); ++index) {
        Tegra::g_nx2_gpu_written_pages[index].store(0, std::memory_order_relaxed);
    }
}

/// Pages in [first, last] marked, and none of the 200 pages around them.
bool OnlyPagesMarked(u64 first, u64 last) {
    for (u64 page = first - 200; page <= last + 200; ++page) {
        const bool marked = Tegra::Nx2IsGpuWritten(page * Page);
        if (marked != (page >= first && page <= last)) {
            return false;
        }
    }
    return true;
}

} // Anonymous namespace

TEST_CASE("Nx2MarkGpuWritten marks every page a range touches and no other",
          "[video_core][nx2]") {
    const u64 base_page = Base / Page + 1000;
    SECTION("Inside one word") {
        Clear();
        Tegra::Nx2MarkGpuWritten((base_page + 3) * Page + 0x10, 2 * Page);
        REQUIRE(OnlyPagesMarked(base_page + 3, base_page + 5));
    }
    SECTION("Across several words, unaligned at both ends") {
        Clear();
        const u64 first = (base_page / 64 + 1) * 64 - 5;
        Tegra::Nx2MarkGpuWritten(first * Page + 0x800, 200 * Page);
        REQUIRE(OnlyPagesMarked(first, first + 200));
    }
    SECTION("Whole words") {
        Clear();
        const u64 first = (base_page / 64 + 1) * 64;
        Tegra::Nx2MarkGpuWritten(first * Page, 128 * Page);
        REQUIRE(OnlyPagesMarked(first, first + 127));
    }
    SECTION("One byte") {
        Clear();
        Tegra::Nx2MarkGpuWritten(base_page * Page + Page - 1, 1);
        REQUIRE(OnlyPagesMarked(base_page, base_page));
    }
    SECTION("Clamped at the end of the table") {
        Clear();
        const u64 pages = Tegra::g_nx2_gpu_written_pages.size() * 64;
        Tegra::Nx2MarkGpuWritten((pages - 3) * Page, 10 * Page);
        for (u64 page = pages - 70; page < pages; ++page) {
            REQUIRE(Tegra::Nx2IsGpuWritten(page * Page) == (page >= pages - 3));
        }
    }
    Clear();
}
