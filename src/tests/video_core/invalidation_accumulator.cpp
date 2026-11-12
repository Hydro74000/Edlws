// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "video_core/invalidation_accumulator.h"

namespace {

std::vector<std::pair<GPUVAddr, size_t>> Ranges(VideoCommon::InvalidationAccumulator& accumulator) {
    std::vector<std::pair<GPUVAddr, size_t>> ranges;
    (void)accumulator.InvalidateAll(
        [&ranges](GPUVAddr address, size_t size) { ranges.emplace_back(address, size); });
    return ranges;
}

} // namespace

TEST_CASE("A word past its block's start invalidates the whole block", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x1054, 4);
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 0x1040);
    CHECK(ranges[0].second == 0x20);
}

TEST_CASE("Words written from mid-block on are all invalidated", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    for (GPUVAddr address = 0x2054; address < 0x2084; address += 4) {
        accumulator.Add(address, 4);
    }
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 0x2040);
    CHECK(ranges[0].second == 0x60);
}

TEST_CASE("A write spanning blocks covers each block it touches", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x3018, 0x30);
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 0x3000);
    CHECK(ranges[0].second == 0x60);
}

TEST_CASE("Writes in separate blocks give separate ranges", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x4004, 4);
    accumulator.Add(0x5034, 4);
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0] == std::pair<GPUVAddr, size_t>{0x4000, 0x20});
    CHECK(ranges[1] == std::pair<GPUVAddr, size_t>{0x5020, 0x20});
}

TEST_CASE("Writes before or over the current range grow it", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x6040, 0x20);
    accumulator.Add(0x6020, 0x20); // touching, before
    accumulator.Add(0x6010, 0x40); // overlapping
    accumulator.Add(0x6060, 4);    // touching, after
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0] == std::pair<GPUVAddr, size_t>{0x6000, 0x80});
}

TEST_CASE("Ranges set aside that overlap or touch are invalidated as one",
          "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    accumulator.Add(0x7000, 0x20);
    accumulator.Add(0x9000, 0x20);
    accumulator.Add(0x7020, 0x20); // touches the first range, set aside
    accumulator.Add(0x9000, 0x40); // over the second
    accumulator.Add(0x8000, 4);
    accumulator.Add(0x7010, 4);
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 3);
    CHECK(ranges[0] == std::pair<GPUVAddr, size_t>{0x7000, 0x40});
    CHECK(ranges[1] == std::pair<GPUVAddr, size_t>{0x8000, 0x20});
    CHECK(ranges[2] == std::pair<GPUVAddr, size_t>{0x9000, 0x40});
    CHECK(!accumulator.InvalidateAll([](GPUVAddr, size_t) {}));
}

TEST_CASE("Very many ranges set aside go through as they came", "[invalidation_accumulator]") {
    VideoCommon::InvalidationAccumulator accumulator;
    for (GPUVAddr i = 0; i < 300; ++i) {
        accumulator.Add(0x100000 - i * 0x100, 4);
    }
    const auto ranges = Ranges(accumulator);
    REQUIRE(ranges.size() == 300);
    CHECK(ranges.front() == std::pair<GPUVAddr, size_t>{0x100000, 0x20});
    CHECK(ranges.back() == std::pair<GPUVAddr, size_t>{0x100000 - 299 * 0x100, 0x20});
}
