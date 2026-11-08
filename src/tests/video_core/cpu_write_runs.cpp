// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <random>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "core/gpu_dirty_memory_manager.h"
#include "video_core/buffer_cache/cpu_write_runs.h"

TEST_CASE("CPU write runs leave exactly the uncovered bytes", "[video_core][cpu_writes]") {
    constexpr u64 Size = 4096;
    std::mt19937 random{0x2604};
    for (u32 round = 0; round < 500; ++round) {
        VideoCommon::CpuWriteRuns runs;
        std::array<bool, Size> covered{};
        const u32 count = random() % 8;
        for (u32 i = 0; i < count; ++i) {
            const u64 begin = random() % Size;
            const u64 size = random() % (Size - begin + 1);
            runs.Add(begin, size);
            std::fill_n(covered.begin() + begin, size, true);
        }
        runs.Finish();
        const u64 begin = random() % Size;
        const u64 size = random() % (Size - begin + 1);
        std::array<bool, Size> written{};
        DAddr last_end = 0;
        runs.ForEachUncovered(begin, size, [&](DAddr part_begin, DAddr part_end) {
            REQUIRE(part_begin < part_end);
            REQUIRE(part_begin >= last_end);
            last_end = part_end;
            std::fill(written.begin() + part_begin, written.begin() + part_end, true);
        });
        for (u64 byte = 0; byte < Size; ++byte) {
            CAPTURE(round, byte, begin, size);
            const bool in_range = byte >= begin && byte < begin + size;
            REQUIRE(written[byte] == (in_range && !covered[byte]));
        }
    }
}

TEST_CASE("Collected CPU writes stay pending until a gather hands them out",
          "[video_core][cpu_writes]") {
    Core::GPUDirtyMemoryManager manager;
    REQUIRE_FALSE(manager.HasPending());
    manager.Collect(0x10040, 8);
    manager.Collect(0x10100, 0x40);
    manager.Collect(0x20800, 4);
    REQUIRE(manager.HasPending());
    std::vector<std::pair<PAddr, size_t>> pending;
    manager.ForEachPending([&](PAddr address, size_t size) { pending.emplace_back(address, size); });
    std::ranges::sort(pending);
    REQUIRE(pending == std::vector<std::pair<PAddr, size_t>>{
                           {0x10040, 0x40}, {0x10100, 0x40}, {0x20800, 0x40}});
    REQUIRE(manager.HasPending());

    std::vector<std::pair<PAddr, size_t>> gathered;
    std::function<void(PAddr, size_t)> gather(
        [&](PAddr address, size_t size) { gathered.emplace_back(address, size); });
    manager.Gather(gather);
    std::ranges::sort(gathered);
    REQUIRE(gathered == pending);
    REQUIRE_FALSE(manager.HasPending());
    pending.clear();
    manager.ForEachPending([&](PAddr address, size_t size) { pending.emplace_back(address, size); });
    REQUIRE(pending.empty());
}

TEST_CASE("Gathers hand out every write collected while others read the pending ones",
          "[video_core][cpu_writes]") {
    constexpr u64 Base = 0x100000;
    constexpr u64 Granules = 1024; // 64 KiB of 64-byte granules
    Core::GPUDirtyMemoryManager manager;
    std::array<bool, Granules> collected{};
    std::vector<bool> gathered(Granules);
    std::atomic<bool> done{};
    std::thread writer([&] {
        std::mt19937 random{0x397};
        for (u32 i = 0; i < 200000; ++i) {
            const u64 granule = random() % Granules;
            // Within one 2 KiB block, as the CPU write paths collect them.
            const u64 room = 32 - granule % 32;
            const u64 granules = 1 + random() % room;
            manager.Collect(Base + granule * 64, granules * 64 - random() % 64);
            std::fill_n(collected.begin() + granule, granules, true);
        }
        done = true;
    });
    std::function<void(PAddr, size_t)> gather([&](PAddr address, size_t size) {
        REQUIRE(address >= Base);
        REQUIRE(address + size <= Base + Granules * 64);
        for (u64 g = (address - Base) / 64; g < (address + size - Base) / 64; ++g) {
            gathered[g] = true;
        }
    });
    u64 pending_runs = 0;
    while (!done) {
        manager.ForEachPending([&](PAddr address, size_t size) {
            REQUIRE(address >= Base);
            REQUIRE(address + size <= Base + Granules * 64);
            ++pending_runs;
        });
        manager.Gather(gather);
    }
    writer.join();
    manager.Gather(gather);
    REQUIRE_FALSE(manager.HasPending());
    for (u64 g = 0; g < Granules; ++g) {
        CAPTURE(g);
        REQUIRE(gathered[g] == collected[g]);
    }
}
