// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Buffer cache downloads against guest CPU writes collected and not applied yet. The fixture is
// a trimmed port of Lichii's buffer_cache_ports.cpp (#194, #397).

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "common/page_table.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/gpu.h"
#include "video_core/gpu_split.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace {

struct Runtime;

struct Buffer : VideoCommon::BufferBase {
    Buffer(Runtime&, VideoCommon::NullBufferParams params) : BufferBase{params} {}
    Buffer(Runtime&, DAddr address, u32 size, bool mapped)
        : BufferBase{address, size}, bytes(size), host_mapped{mapped} {}

    void MarkUsage(u64 offset, u64 size) {
        REQUIRE(offset + size <= bytes.size());
    }
    std::span<u8> Mapped() {
        return host_mapped ? std::span{bytes} : std::span<u8>{};
    }
    u64 LastUsageTick() const {
        return 0;
    }

    std::vector<u8> bytes;
    bool host_mapped{};
};

struct Staging {
    Buffer* buffer;
    u64 offset;
    std::span<u8> mapped_span;
};

struct Runtime {
    bool CanReportMemoryUsage() const {
        return false;
    }
    u64 GetDeviceLocalMemory() const {
        return 0;
    }
    u64 GetDeviceMemoryUsage() const {
        return 0;
    }
    u32 GetStorageBufferAlignment() const {
        return 256;
    }
    u32 GetUniformBufferAlignment() const {
        return 1;
    }
    bool IsFree(u64) const {
        return true;
    }
    u64 CurrentTick() const {
        return 0;
    }
    void TickFrame(auto&) {}
    void WaitSubmittedTick(u64) {}
    bool CanReorderUpload(const Buffer&, std::span<VideoCommon::BufferCopy>) const {
        return false;
    }
    Staging DownloadStagingBuffer(u64 size, bool = false) {
        auto& staging = stagings.emplace_back(std::make_unique<Buffer>(*this, 0, size + 128, false));
        return {staging.get(), 128, std::span{staging->bytes}.subspan(128)};
    }
    Staging UploadStagingBuffer(u64 size) {
        return DownloadStagingBuffer(size);
    }
    void FreeDeferredStagingBuffer(Staging&) {}
    void PreCopyBarrier() {}
    void PostCopyBarrier() {}
    void CopyBuffer(Buffer* dst, Buffer& src, std::span<const VideoCommon::BufferCopy> copies,
                    bool, bool = false) {
        for (const auto& copy : copies) {
            REQUIRE(copy.src_offset + copy.size <= src.bytes.size());
            REQUIRE(copy.dst_offset + copy.size <= dst->bytes.size());
            pending.push_back([dst, &src, copy] {
                std::memcpy(dst->bytes.data() + copy.dst_offset, src.bytes.data() + copy.src_offset,
                            copy.size);
            });
        }
    }
    void CopyBuffer(Buffer& dst, Buffer* src, std::span<const VideoCommon::BufferCopy> copies, bool,
                    bool = false) {
        for (const auto& copy : copies) {
            std::memcpy(dst.bytes.data() + copy.dst_offset, src->bytes.data() + copy.src_offset,
                        copy.size);
        }
    }
    void CopyBuffer(Buffer& dst, Buffer& src, std::span<const VideoCommon::BufferCopy> copies, bool,
                    bool = false) {
        CopyBuffer(dst, &src, copies, true);
    }
    void ClearBuffer(Buffer& dst, u32 offset, size_t size, u32 value) {
        REQUIRE(offset + size <= dst.bytes.size());
        std::memset(dst.bytes.data() + offset, static_cast<u8>(value), size);
    }
    void Finish() {
        for (auto& copy : pending) {
            copy();
        }
        pending.clear();
    }
    void Wait(u64) {}
    void FinishForHostRead() {
        Finish();
    }

    std::vector<std::unique_ptr<Buffer>> stagings;
    std::vector<std::function<void()>> pending;
};

struct Policy {
    using Runtime = ::Runtime;
    using Buffer = ::Buffer;
    using Async_Buffer = Staging;
    using MemoryTracker = VideoCommon::MemoryTrackerBase<Tegra::MaxwellDeviceMemoryManager>;
    static constexpr bool IS_OPENGL = false;
    static constexpr bool HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS = false;
    static constexpr bool HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT = false;
    static constexpr bool NEEDS_BIND_UNIFORM_INDEX = false;
    static constexpr bool NEEDS_BIND_STORAGE_INDEX = false;
    static constexpr bool USE_MEMORY_MAPS = true;
    static constexpr bool SEPARATE_IMAGE_BUFFER_BINDINGS = false;
    static constexpr bool USE_MEMORY_MAPS_FOR_UPLOADS = true;
};

using Cache = VideoCommon::BufferCache<Policy>;

} // namespace

namespace VideoCommon {

struct BufferCacheTestAccess {
    static BufferId Add(Cache& cache, DAddr address, u32 size, bool mapped = false) {
        const auto id = cache.slot_buffers.insert(cache.runtime, address, size, mapped);
        cache.Register(id);
        cache.memory_tracker.ForEachUploadRange(address, size, [](auto, auto) {});
        return id;
    }
    static ::Buffer& Get(Cache& cache, BufferId id) {
        return cache.slot_buffers[id];
    }
    /// What a GPU write to the range leaves behind, downloaded at the next fence.
    static void GpuWrite(Cache& cache, DAddr address, u64 size) {
        cache.memory_tracker.MarkRegionAsGpuModified(address, size);
        cache.gpu_modified_ranges.Add(address, size);
        cache.uncommitted_gpu_modified_ranges.Add(address, size);
    }
};

} // namespace VideoCommon

namespace {

using Access = VideoCommon::BufferCacheTestAccess;

struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::GPU gpu{system, false, false};
    Null::RasterizerNull rasterizer{gpu};
    Runtime runtime;
    std::unique_ptr<Cache> cache;
    std::unique_ptr<Kernel::KProcess> process;
    Core::Asid asid{};
    u8* bytes{};
    std::vector<std::pair<DAddr, size_t>> cpu_writes;

    Fixture() {
        system.Initialize();
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(24, 12);
        bytes = physical.GetPointerFromRaw<u8>(0x10000);
        std::memset(bytes, 0, 0x40000);
        for (u64 page = 0; page < 64; ++page) {
            table.entries.GetAndFault(0x10 + page)
                .Store(false, Common::PageType::Memory, 1,
                       reinterpret_cast<uintptr_t>(bytes) - 0x10000);
        }
        process->GetMemory().SetCurrentPageTable(*process);
        asid = device.RegisterProcess(&process->GetMemory());
        device.BindInterface(&rasterizer);
        device.Map(0x10000, 0x10000, 0x40000, asid);
        cache = std::make_unique<Cache>(device, runtime);
        cache->SetPendingCpuWrites([this](const std::function<void(DAddr, size_t)>& callback) {
            for (const auto& [address, size] : cpu_writes) {
                callback(address, size);
            }
        });
    }
    ~Fixture() {
        cache.reset();
        device.Unmap(0x10000, 0x40000);
        device.UnregisterProcess(asid);
        system.Kernel().UnregisterKernelObject(process.get());
    }
    /// Guest memory at a device address (mapped from 0x10000).
    u8* Guest(DAddr address) {
        return bytes + (address - 0x10000);
    }
    /// A guest CPU write the GPU thread has not applied yet.
    void CpuWrite(DAddr address, size_t size, u8 value) {
        std::memset(Guest(address), value, size);
        cpu_writes.emplace_back(address, size);
    }
    bool GuestIs(DAddr begin, DAddr end, u8 value) {
        return std::all_of(Guest(begin), Guest(end), [value](u8 b) { return b == value; });
    }
};

} // namespace

TEST_CASE("Downloads keep CPU writes collected and not yet applied", "[video_core][cpu_writes]") {
    Fixture f;
    const auto staged = Access::Add(*f.cache, 0x20000, 0x10000);
    const auto mapped = Access::Add(*f.cache, 0x30000, 0x10000, true);
    std::ranges::fill(Access::Get(*f.cache, staged).bytes, 0x12);
    std::ranges::fill(Access::Get(*f.cache, mapped).bytes, 0x34);
    f.CpuWrite(0x20100, 0x10, 0xee);
    f.CpuWrite(0x30ff8, 0x10, 0xee);
    Access::GpuWrite(*f.cache, 0x20000, 0x2000);
    Access::GpuWrite(*f.cache, 0x30000, 0x2000);

    f.cache->DownloadMemory(0x20000, 0x2000);
    REQUIRE(f.GuestIs(0x20000, 0x20100, 0x12));
    REQUIRE(f.GuestIs(0x20100, 0x20110, 0xee));
    REQUIRE(f.GuestIs(0x20110, 0x22000, 0x12));

    f.cache->DownloadMemory(0x30000, 0x2000);
    REQUIRE(f.GuestIs(0x30000, 0x30ff8, 0x34));
    REQUIRE(f.GuestIs(0x30ff8, 0x31008, 0xee));
    REQUIRE(f.GuestIs(0x31008, 0x32000, 0x34));
}

TEST_CASE("Fence downloads keep CPU writes collected after the copy was taken",
          "[video_core][cpu_writes]") {
    Fixture f;
    const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
    std::ranges::fill(Access::Get(*f.cache, id).bytes, 0x56);
    Access::GpuWrite(*f.cache, 0x20000, 0x1000);
    f.cache->CommitAsyncFlushes();
    f.runtime.Finish();
    // Collected between the fence's copy and its pop, on the fencing thread.
    f.CpuWrite(0x20400, 0x40, 0xee);
    f.cache->PopAsyncFlushes();
    REQUIRE(f.GuestIs(0x20000, 0x20400, 0x56));
    REQUIRE(f.GuestIs(0x20400, 0x20440, 0xee));
    REQUIRE(f.GuestIs(0x20440, 0x21000, 0x56));
}

TEST_CASE("Applying collected CPU writes one by one keeps the ones still to apply",
          "[video_core][cpu_writes]") {
    Fixture f;
    const auto id = Access::Add(*f.cache, 0x20000, 0x10000);
    std::ranges::fill(Access::Get(*f.cache, id).bytes, 0x78);
    Access::GpuWrite(*f.cache, 0x20000, 0x1000);
    f.CpuWrite(0x20100, 0x40, 0xaa);
    f.CpuWrite(0x20800, 0x40, 0xbb);
    // What ApplyCpuWrites does: the whole batch out of the GPU-written ranges, then each write.
    const std::vector<std::pair<DAddr, u64>> batch(f.cpu_writes.begin(), f.cpu_writes.end());
    f.cpu_writes.clear();
    f.cache->ClaimCpuWrites(batch);
    VideoCommon::GpuSplit::MarkGpuThread();
    for (const auto& [address, size] : batch) {
        f.cache->WriteMemory(address, size);
    }
    REQUIRE(f.GuestIs(0x20000, 0x20100, 0x78));
    REQUIRE(f.GuestIs(0x20100, 0x20140, 0xaa));
    REQUIRE(f.GuestIs(0x20140, 0x20800, 0x78));
    REQUIRE(f.GuestIs(0x20800, 0x20840, 0xbb));
    REQUIRE(f.GuestIs(0x20840, 0x21000, 0x78));
}
