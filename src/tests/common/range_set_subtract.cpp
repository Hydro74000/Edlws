// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/range_sets.h"
#include "common/range_sets.inc"

namespace {

constexpr u64 SPACE = 512;

std::array<bool, SPACE> Contents(const Common::RangeSet<u64>& set) {
    std::array<bool, SPACE> contents{};
    set.ForEach([&](u64 begin, u64 end) {
        for (u64 address = begin; address < end; ++address) {
            contents[address] = true;
        }
    });
    return contents;
}

} // Anonymous namespace

TEST_CASE("RangeSet::Subtract leaves neighbours of the range alone", "[range_sets]") {
    Common::RangeSet<u64> set;
    set.Add(100, 50);
    set.Subtract(150, 10);
    set.Subtract(90, 10);
    set.Subtract(0, 0);
    set.Subtract(120, 0);
    std::array<bool, SPACE> expected{};
    for (u64 address = 100; address < 150; ++address) {
        expected[address] = true;
    }
    REQUIRE(Contents(set) == expected);
    set.Subtract(149, 1);
    set.Subtract(90, 11);
    expected[149] = false;
    expected[100] = false;
    REQUIRE(Contents(set) == expected);

    Common::RangeSet<u64> empty;
    empty.Subtract(0, SPACE);
    REQUIRE(empty.Empty());
}

TEST_CASE("RangeSet::Subtract matches a per-address model", "[range_sets]") {
    std::mt19937_64 rng{0x0400486024DEC000ULL};
    Common::RangeSet<u64> set;
    std::array<bool, SPACE> model{};
    for (int step = 0; step < 20000; ++step) {
        const u64 base = rng() % SPACE;
        const u64 size = rng() % (SPACE - base + 1);
        const bool add = rng() % 3 == 0;
        if (add) {
            set.Add(base, size);
        } else {
            set.Subtract(base, size);
        }
        for (u64 address = base; address < base + size; ++address) {
            model[address] = add;
        }
        REQUIRE(Contents(set) == model);
    }
}

TEST_CASE("RangeSet stays sorted and joined, and its lookups match a per-address model",
          "[range_sets]") {
    std::mt19937_64 rng{0x0400486024DEC001ULL};
    Common::RangeSet<u64> set;
    std::array<bool, SPACE> model{};
    for (int step = 0; step < 20000; ++step) {
        // Mostly small ranges, so that the set holds many.
        const u64 base = rng() % SPACE;
        const u64 limit = rng() % 8 == 0 ? SPACE - base : (std::min<u64>)(16, SPACE - base);
        const u64 size = rng() % (limit + 1);
        const bool add = rng() % 2 == 0;
        if (add) {
            set.Add(base, size);
        } else {
            set.Subtract(base, size);
        }
        for (u64 address = base; address < base + size; ++address) {
            model[address] = add;
        }

        u64 previous_end = 0;
        bool first = true;
        set.ForEach([&](u64 begin, u64 end) {
            REQUIRE(begin < end);
            // Touching ranges would have joined.
            REQUIRE((first || previous_end < begin));
            previous_end = end;
            first = false;
        });
        REQUIRE(set.Empty() == first);

        const u64 query = rng() % SPACE;
        const u64 query_size = rng() % (SPACE - query + 1);
        bool expected_hit = false;
        std::array<bool, SPACE> expected{};
        for (u64 address = query; address < query + query_size; ++address) {
            expected[address] = model[address];
            expected_hit = expected_hit || model[address];
        }
        REQUIRE(set.Intersects(query, query_size) == expected_hit);
        std::array<bool, SPACE> visited{};
        set.ForEachInRange(query, query_size, [&](u64 begin, u64 end) {
            REQUIRE(begin >= query);
            REQUIRE(end <= query + query_size);
            REQUIRE(begin < end);
            for (u64 address = begin; address < end; ++address) {
                visited[address] = true;
            }
        });
        REQUIRE(visited == expected);
    }
}

TEST_CASE("RangeSet joins touching ranges and splits on a cut", "[range_sets]") {
    const auto count = [](const Common::RangeSet<u64>& set) {
        int ranges = 0;
        set.ForEach([&](u64, u64) { ++ranges; });
        return ranges;
    };
    Common::RangeSet<u64> set;
    set.Add(10, 10);
    set.Add(30, 10);
    REQUIRE(count(set) == 2);
    set.Add(20, 10); // Touches both.
    REQUIRE(count(set) == 1);
    set.Subtract(20, 5);
    REQUIRE(count(set) == 2);
    set.Add(5, 0);
    set.Add(50, 0);
    REQUIRE(count(set) == 2);
    set.Subtract(0, 100);
    REQUIRE(set.Empty());
}

TEST_CASE("RangeSet holding thousands of ranges matches a per-address model", "[range_sets]") {
    // Tiny ranges over a wide space: the set spreads over many blocks, which split, empty and
    // join as ranges come and go, as with the buffer cache's GPU-written ranges.
    constexpr u64 Wide = 16384;
    std::mt19937_64 rng{0x040074A01BF12000ULL};
    Common::RangeSet<u64> set;
    std::vector<bool> model(Wide);
    size_t most_ranges = 0;
    for (int step = 0; step < 20000; ++step) {
        const u64 base = rng() % Wide;
        const u64 limit = rng() % 1024 == 0 ? Wide - base : (std::min<u64>)(4, Wide - base);
        const u64 size = rng() % (limit + 1);
        // Adding more often than removing fills the set, the wide removals empty runs of blocks.
        const bool add = rng() % 8 < 5;
        if (add) {
            set.Add(base, size);
        } else {
            set.Subtract(base, size);
        }
        for (u64 address = base; address < base + size; ++address) {
            model[address] = add;
        }

        std::vector<bool> contents(Wide);
        size_t ranges = 0;
        u64 previous_end = 0;
        set.ForEach([&](u64 begin, u64 end) {
            REQUIRE(begin < end);
            REQUIRE((ranges == 0 || previous_end < begin));
            previous_end = end;
            ++ranges;
            for (u64 address = begin; address < end; ++address) {
                contents[address] = true;
            }
        });
        most_ranges = (std::max)(most_ranges, ranges);
        REQUIRE(contents == model);
        REQUIRE(set.Empty() == (ranges == 0));

        const u64 query = rng() % Wide;
        const u64 query_size = rng() % ((std::min<u64>)(Wide - query, 2048) + 1);
        bool expected_hit = false;
        for (u64 address = query; address < query + query_size; ++address) {
            expected_hit = expected_hit || model[address];
        }
        REQUIRE(set.Intersects(query, query_size) == expected_hit);
        u64 visited = 0;
        set.ForEachInRange(query, query_size, [&](u64 begin, u64 end) {
            REQUIRE(begin >= query);
            REQUIRE(end <= query + query_size);
            for (u64 address = begin; address < end; ++address) {
                REQUIRE(model[address]);
            }
            visited += end - begin;
        });
        u64 expected_visited = 0;
        for (u64 address = query; address < query + query_size; ++address) {
            expected_visited += model[address] ? 1 : 0;
        }
        REQUIRE(visited == expected_visited);
    }
    // Several blocks' worth at the peak.
    REQUIRE(most_ranges > 1000);

    // Moving hands every range over and leaves the source empty and usable.
    Common::RangeSet<u64> moved{std::move(set)};
    REQUIRE(set.Empty());
    set.Add(10, 5);
    REQUIRE(set.Intersects(12, 1));
    moved.Clear();
    REQUIRE(moved.Empty());
}
