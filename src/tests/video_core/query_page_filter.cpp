// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <random>
#include <set>
#include <catch2/catch_test_macros.hpp>

#include "video_core/query_cache/query_page_filter.h"

TEST_CASE("Query page filter never misses a marked page", "[video_core][query_cache]") {
    VideoCommon::QueryPageFilter filter;
    REQUIRE_FALSE(filter.MayHold(0, 0));
    REQUIRE_FALSE(filter.MayHold(0x12345, 0x12350));
    std::mt19937_64 random{0x9e3779b9};
    std::set<u64> marked;
    for (u32 i = 0; i < 300; ++i) {
        const u64 page = random() % (u64{1} << 28);
        filter.Mark(page);
        marked.insert(page);
    }
    u64 skipped = 0;
    for (u32 i = 0; i < 200000; ++i) {
        const u64 begin = i % 2 == 0 ? *std::next(marked.begin(), random() % marked.size()) - random() % 4
                                     : random() % (u64{1} << 28);
        const u64 end = begin + random() % 8;
        const auto it = marked.lower_bound(begin);
        const bool holds = it != marked.end() && *it <= end;
        const bool may = filter.MayHold(begin, end);
        CAPTURE(begin, end);
        REQUIRE((!holds || may));
        skipped += may ? 0 : 1;
    }
    // Most ranges far from the marked pages are told apart from them.
    REQUIRE(skipped > 90000);
    REQUIRE(filter.MayHold(5, 5 + 64));
}
