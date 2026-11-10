// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/nx2.h"

TEST_CASE("Nx2SemaphoreMirror finds every region, past the first 32", "[video_core][nx2]") {
    constexpr u64 base = 0x2106000000;
    constexpr u64 count = 140;
    const auto start = [](u64 index) { return base + index * 0x3000; };
    Tegra::Nx2ClearSemaphoreRegions();
    for (u64 index = 0; index < count; ++index) {
        Tegra::Nx2AddSemaphoreRegion(start(index));
    }
    Tegra::Nx2AddSemaphoreRegion(start(0));
    CHECK(Tegra::g_nx2_semaphore_regions.size() == count);
    for (u64 index = 0; index < count; ++index) {
        CAPTURE(index);
        CHECK(Tegra::Nx2SemaphoreMirror(start(index) + 0x1010) == start(index) + 0x10);
        CHECK(Tegra::Nx2SemaphoreMirror(start(index) + 0x10) == 0);
        CHECK(Tegra::Nx2SemaphoreMirror(start(index) + 0x2000) == 0);
    }

    Tegra::Nx2RemoveSemaphoreRegion(start(count - 1));
    CHECK(Tegra::Nx2SemaphoreMirror(start(count - 1) + 0x1010) == 0);
    CHECK(Tegra::Nx2SemaphoreMirror(start(count - 2) + 0x1010) == start(count - 2) + 0x10);
    Tegra::Nx2ClearSemaphoreRegions();
    CHECK(Tegra::Nx2SemaphoreMirror(start(40) + 0x1010) == 0);
}
