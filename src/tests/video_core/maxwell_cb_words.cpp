// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/page_table.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace {

using Tegra::Engines::Maxwell3D;

constexpr u32 CbSize = MAXWELL3D_REG_INDEX(const_buffer.size);
constexpr u32 CbAddressHigh = MAXWELL3D_REG_INDEX(const_buffer.address_high);
constexpr u32 CbAddressLow = MAXWELL3D_REG_INDEX(const_buffer.address_low);
constexpr u32 CbOffset = MAXWELL3D_REG_INDEX(const_buffer.offset);
constexpr u32 CbData = MAXWELL3D_REG_INDEX(const_buffer.buffer);
constexpr u32 ShadowControl = MAXWELL3D_REG_INDEX(shadow_ram_control);

constexpr GPUVAddr BufferAddress = 0x2000'0000;
constexpr DAddr DeviceA = 0x10000;
constexpr DAddr DeviceB = 0x20000;
constexpr u32 BufferSize = 0x1000;

/// Two 3D engines, each with its own address space mapping one constant buffer to its own device
/// memory: one takes the words through CallMethod, the other through CallConstBufferWords.
struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::GPU gpu{system, false, false};
    Null::RasterizerNull rasterizer{gpu};
    std::unique_ptr<Kernel::KProcess> process;
    Core::Asid asid{};
    u8* bytes{};
    std::unique_ptr<Tegra::MemoryManager> memory_a;
    std::unique_ptr<Tegra::MemoryManager> memory_b;
    std::unique_ptr<Maxwell3D> word_by_word;
    std::unique_ptr<Maxwell3D> batched;

    Fixture() {
        system.Initialize();
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(24, 12);
        bytes = physical.GetPointerFromRaw<u8>(0x10000);
        std::memset(bytes, 0, 0x20000);
        for (u64 page = 0; page < 32; ++page) {
            table.entries.GetAndFault(0x10 + page)
                .Store(false, Common::PageType::Memory, 1,
                       reinterpret_cast<uintptr_t>(bytes) - 0x10000);
        }
        process->GetMemory().SetCurrentPageTable(*process);
        asid = device.RegisterProcess(&process->GetMemory());
        device.BindInterface(&rasterizer);
        device.Map(0x10000, 0x10000, 0x20000, asid);
        memory_a = std::make_unique<Tegra::MemoryManager>(system, device);
        memory_b = std::make_unique<Tegra::MemoryManager>(system, device);
        memory_a->BindRasterizer(&rasterizer);
        memory_b->BindRasterizer(&rasterizer);
        memory_a->Map(BufferAddress, DeviceA, BufferSize, Tegra::PTEKind::PITCH, false);
        memory_b->Map(BufferAddress, DeviceB, BufferSize, Tegra::PTEKind::PITCH, false);
        word_by_word = std::make_unique<Maxwell3D>(*memory_a);
        batched = std::make_unique<Maxwell3D>(*memory_b);
        for (Maxwell3D* engine : {word_by_word.get(), batched.get()}) {
            engine->BindRasterizer(&rasterizer);
            VideoCommon::Dirty::SetupDirtyFlags(engine->dirty.tables);
            engine->dirty.flags.reset();
            Call(*engine, CbAddressHigh, static_cast<u32>(BufferAddress >> 32));
            Call(*engine, CbAddressLow, static_cast<u32>(BufferAddress));
            Call(*engine, CbSize, BufferSize);
        }
    }
    ~Fixture() {
        batched.reset();
        word_by_word.reset();
        memory_b.reset();
        memory_a.reset();
        device.Unmap(0x10000, 0x20000);
        device.UnregisterProcess(asid);
        system.Kernel().UnregisterKernelObject(process.get());
    }

    void Call(Maxwell3D& engine, u32 method, u32 argument) {
        engine.CallMethod(system, method, argument, true);
    }
    void Both(u32 method, u32 argument) {
        Call(*word_by_word, method, argument);
        Call(*batched, method, argument);
    }
    /// One row of words, as GpuSplit's back would see it.
    void Row(const std::vector<u32>& methods, const std::vector<u32>& arguments) {
        for (size_t i = 0; i < methods.size(); ++i) {
            Call(*word_by_word, methods[i], arguments[i]);
        }
        batched->CallConstBufferWords(system, methods.data(), arguments.data(),
                                      static_cast<u32>(methods.size()));
    }
    bool Same() const {
        return word_by_word->regs.reg_array == batched->regs.reg_array &&
               word_by_word->shadow_state.reg_array == batched->shadow_state.reg_array &&
               word_by_word->dirty.flags == batched->dirty.flags &&
               std::memcmp(bytes + (DeviceA - 0x10000), bytes + (DeviceB - 0x10000), BufferSize) ==
                   0;
    }
    bool Written() const {
        const u8* const a = bytes + (DeviceA - 0x10000);
        return std::any_of(a, a + BufferSize, [](u8 b) { return b != 0; });
    }
};

} // Anonymous namespace

TEST_CASE("A row of constant buffer words goes up as word by word", "[video_core][maxwell3d]") {
    Fixture f;
    std::mt19937 rng{42};
    const auto row = [&](u32 count) {
        std::vector<u32> methods(count), arguments(count);
        for (u32 i = 0; i < count; ++i) {
            methods[i] = CbData + rng() % 16;
            arguments[i] = static_cast<u32>(rng());
        }
        f.Row(methods, arguments);
    };
    SECTION("Rows of every length, offsets advancing") {
        f.Both(CbOffset, 0x40);
        for (u32 count = 1; count <= 64; count += 7) {
            row(count);
            REQUIRE(f.Same());
        }
        REQUIRE(f.Written());
    }
    SECTION("Shadow RAM replay writes the shadow values") {
        f.Both(CbOffset, 0);
        row(16);
        f.Both(ShadowControl, static_cast<u32>(Maxwell3D::Regs::ShadowRamControl::Replay));
        f.Both(CbOffset, 0x100);
        row(16);
        REQUIRE(f.Same());
        REQUIRE(f.Written());
    }
    SECTION("A row past the end of the buffer clips as word by word") {
        f.Both(CbOffset, BufferSize - 8);
        row(5);
        REQUIRE(f.Same());
    }
}
