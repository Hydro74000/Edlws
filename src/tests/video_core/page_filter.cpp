// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <random>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "video_core/page_filter.h"

TEST_CASE("PageFilter answers like the set of marked pages", "[video_core]") {
    VideoCommon::PageFilter filter;
    std::set<u64> marked;
    std::mt19937_64 rng{0xf117e2};
    constexpr u64 Page = 4096;
    constexpr u64 Span = u64{1} << 26; // a 64 MiB window, pages hit often enough to collide
    const auto random_range = [&] {
        const u64 address = (u64{1} << 33) + rng() % Span;
        const u64 size = 1 + rng() % (Page * (rng() % 4 == 0 ? 300 : 3));
        return std::pair{address, size};
    };
    for (int step = 0; step < 4000; ++step) {
        const auto [address, size] = random_range();
        filter.Mark(address, size);
        for (u64 page = address / Page; page <= (address + size - 1) / Page; ++page) {
            marked.insert(page);
        }
    }
    for (int step = 0; step < 200000; ++step) {
        const auto [address, size] = random_range();
        const auto it = marked.lower_bound(address / Page);
        const bool expected = it != marked.end() && *it <= (address + size - 1) / Page;
        REQUIRE(filter.Any(address, size) == expected);
    }
    CHECK(!filter.Any(0x1000, 0));
}

TEST_CASE("PageFilter reports ranges past its space as possibly holding objects",
          "[video_core]") {
    VideoCommon::PageFilter filter;
    constexpr u64 Past = u64{1} << 36;
    filter.Mark(Past + 0x1000, 0x1000);
    CHECK(filter.Any(Past, 0x1000));
    // Marking past the space doesn't spill into it.
    CHECK(!filter.Any(0, Past));
}
