// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/page_table.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"

// The ranges come from the guest (64-bit WaitIfEqual, the audio codec, sysmix, the gdbstub). One
// whose end wrapped past the address space skipped the page loop and was reported valid.
TEST_CASE("Virtual address ranges that wrap past the address space end are invalid",
          "[core][memory]") {
    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    Kernel::KProcess process{system.Kernel()};
    SCOPE_EXIT {
        system.Kernel().UnregisterKernelObject(&process);
    };
    auto& table = process.GetPageTable().GetImpl();
    table.Resize(20, 12);
    auto* page = system.DeviceMemory().buffer.BackingBasePointer() + 0x10000;
    table.entries.GetAndFault(4).Store(false, Common::PageType::Memory, 1,
                                       reinterpret_cast<uintptr_t>(page) - 0x4000);
    auto& memory = process.GetMemory();
    memory.SetCurrentPageTable(process);
    REQUIRE(memory.IsValidVirtualAddressRange(0x4000, 0x1000));
    REQUIRE(memory.IsValidVirtualAddressRange(0x4ff8, 8));
    REQUIRE_FALSE(memory.IsValidVirtualAddressRange(0x4ffc, 8));
    REQUIRE_FALSE(memory.IsValidVirtualAddressRange(~u64{0} - 3, 8));
    REQUIRE_FALSE(memory.IsValidVirtualAddressRange(0x4000, ~u64{0}));
}
