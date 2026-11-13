// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/gpu.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace {

constexpr u64 Page = 0x1000;
constexpr GPUVAddr Base = 0x10000000;

/// Bytes from gpu_addr, up to size, whose device addresses follow each other: a page walk.
size_t Expected(const Tegra::MemoryManager& gpu, GPUVAddr gpu_addr, size_t size) {
    const std::optional<DAddr> first = gpu.GpuToCpuAddress(gpu_addr);
    if (!first) {
        return 0;
    }
    GPUVAddr page = (gpu_addr & ~(Page - 1)) + Page;
    while (page < gpu_addr + size) {
        const std::optional<DAddr> device = gpu.GpuToCpuAddress(page);
        if (!device || *device != *first + (page - gpu_addr)) {
            return page - gpu_addr;
        }
        page += Page;
    }
    return size;
}

bool AllAnswersMatch(const Tegra::MemoryManager& gpu) {
    static constexpr std::array<size_t, 4> sizes{0x100, 0x3000, 0x40000, 0x800000};
    for (GPUVAddr address = Base - 0x2000; address < Base + 0x300000; address += 0x700) {
        for (const size_t size : sizes) {
            const size_t got = gpu.MaxContinuousRange(address, size);
            const size_t want = Expected(gpu, address, size);
            if (got != want) {
                UNSCOPED_INFO("address " << std::hex << address << " size " << size << ": "
                                         << got << " instead of " << want);
                return false;
            }
        }
    }
    return true;
}

} // Anonymous namespace

TEST_CASE("MaxContinuousRange answers like a page walk, before and after a remap",
          "[video_core][memory_manager]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::GPU host_gpu{system, false, false};
    Null::RasterizerNull rasterizer{host_gpu};
    Tegra::MemoryManager gpu{system, device};
    gpu.BindRasterizer(&rasterizer);

    // One run of 512 KiB, a jump in device memory, a run up to a hole of one page, then a run
    // inside one 64 KiB page made of two separate allocations.
    gpu.Map(Base, 0x100000, 0x80000, Tegra::PTEKind::PITCH, false);
    gpu.Map(Base + 0x80000, 0x400000, 0x100000, Tegra::PTEKind::PITCH, false);
    gpu.Map(Base + 0x181000, 0x501000, 0x7000, Tegra::PTEKind::PITCH, false);
    gpu.Map(Base + 0x188000, 0x900000, 0x8000, Tegra::PTEKind::PITCH, false);
    gpu.Map(Base + 0x190000, 0x908000, 0x170000, Tegra::PTEKind::PITCH, false);
    REQUIRE(AllAnswersMatch(gpu));

    // A remap must not leave answers from the old page tables behind.
    gpu.Unmap(Base + 0x40000, 0x2000);
    gpu.Map(Base + 0x80000, 0x180000, 0x80000, Tegra::PTEKind::PITCH, false);
    REQUIRE(AllAnswersMatch(gpu));
}

TEST_CASE("Device unmaps wait for deferred page count changes", "[video_core][memory_manager]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::GPU host_gpu{system, false, false};
    Null::RasterizerNull rasterizer{host_gpu};
    device.BindInterface(&rasterizer);

    device.BeginDeferredCachedUpdates();
    std::atomic<bool> unmapped{};
    std::thread unmapper{[&] {
        device.Unmap(0x10000, 0x1000);
        unmapped = true;
    }};
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    CHECK_FALSE(unmapped);
    device.EndDeferredCachedUpdates();
    unmapper.join();
    REQUIRE(unmapped);
}
