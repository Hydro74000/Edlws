// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "core/device_memory.h"
#include "video_core/host1x/gpu_device_memory_manager.h"

TEST_CASE("Device space freed and mapped again every frame stays in place", "[device_memory]") {
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    // A range freed and mapped again each frame (Yoshi demo, 1.5 MiB). The free runs on the GPU
    // thread, so the next frame's allocation can come first.
    constexpr size_t size = 0x180000;
    DAddr previous = device_memory.Allocate(size);
    const DAddr lowest = previous;
    DAddr highest = previous;
    for (int frame = 0; frame < 1000; ++frame) {
        const DAddr next = device_memory.Allocate(size);
        device_memory.Free(previous, size);
        previous = next;
        highest = (std::max)(highest, next);
    }
    REQUIRE(highest - lowest <= 2 * size);

    // Another size doesn't take the freed block.
    const DAddr other = device_memory.Allocate(0x20000);
    REQUIRE(other > highest);
    device_memory.Free(previous, size);
    REQUIRE(device_memory.Allocate(size) == previous);
}

TEST_CASE("Kept device blocks go back to the allocator when it runs out", "[device_memory]") {
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    // A 1 GiB block between two others fills the space; once it is freed, 512 MiB fits only in it.
    constexpr size_t freed_size = 1ULL << 30;
    REQUIRE(device_memory.Allocate(0x20000) != 0);
    const DAddr freed = device_memory.Allocate(freed_size);
    REQUIRE(freed != 0);
    const size_t rest =
        (1ULL << Tegra::MaxwellDeviceMemoryManager::AS_BITS) - 1 - freed - freed_size;
    REQUIRE(device_memory.Allocate(rest) != 0);
    device_memory.Free(freed, freed_size);
    REQUIRE(device_memory.Allocate(512ULL << 20) == freed);
}

TEST_CASE("An allocation fits once every kept device block is given back", "[device_memory]") {
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    const DAddr first = device_memory.Allocate(1ULL << 30);
    REQUIRE(first != 0);
    device_memory.Free(first, 1ULL << 30);
    REQUIRE(device_memory.Allocate(63ULL << 30) == first);
}

TEST_CASE("A device allocation larger than the space fails after kept blocks are given back",
          "[device_memory]") {
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    const DAddr first = device_memory.Allocate(1ULL << 30);
    REQUIRE(first != 0);
    device_memory.Free(first, 1ULL << 30);
    REQUIRE(device_memory.Allocate(65ULL << 30) == 0);
    REQUIRE(device_memory.Allocate(1ULL << 30) == first);
}
