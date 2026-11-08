// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/shader_info.h"
#include "video_core/shader_handle_program.h"

namespace {

struct HandleTestMemory {
    std::map<GPUVAddr, u32> words;

    template <typename T>
    T Read(GPUVAddr address) const {
        const auto word = [&](GPUVAddr at) -> u64 {
            const auto it = words.find(at);
            return it != words.end() ? it->second : 0U;
        };
        if constexpr (sizeof(T) == 8) {
            return static_cast<T>(word(address) | (word(address + 4) << 32));
        } else {
            return static_cast<T>(word(address));
        }
    }
};

} // namespace

TEST_CASE("Storage handle words are read from the host-aligned buffer base",
          "[video_core][texture_handles]") {
    // The program's storage offsets count from the buffer address aligned down to the host storage
    // alignment (StorageOffset), where the buffer cache binds the buffer.
    constexpr GPUVAddr cbuf = 0x1000;
    HandleTestMemory memory;
    memory.words[cbuf + 0x10] = 0x2008; // the storage buffer address, 8 bytes past 16
    memory.words[cbuf + 0x14] = 0;
    memory.words[0x2008] = 0x11; // the handle the shader reads
    memory.words[0x2010] = 0x22;

    Shader::Info info;
    info.storage_buffers_descriptors.push_back(
        {.cbuf_index = 0, .cbuf_offset = 0x10, .count = 1, .is_written = false});
    // Offset 8 from the base aligned to 16, as StorageOffset gives it for a read at 0x2008.
    info.handle_programs.push_back({static_cast<u32>(Shader::HandleOp::Immediate), 8U,
                                    static_cast<u32>(Shader::HandleOp::Storage), 0U});
    const auto cbuf_address = [](u32 index) -> GPUVAddr { return index == 0 ? cbuf : 0; };

    info.handle_storage_alignment = 16;
    CHECK(VideoCommon::EvaluateTextureHandle(info, 0, memory, cbuf_address) == 0x11U);
    info.handle_storage_alignment = 4;
    CHECK(VideoCommon::EvaluateTextureHandle(info, 0, memory, cbuf_address) == 0x22U);
    info.handle_storage_alignment = 1;
    CHECK(VideoCommon::EvaluateTextureHandle(info, 0, memory, cbuf_address) == 0x22U);
}
