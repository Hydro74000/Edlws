// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "video_core/textures/decoders.h"

namespace {

// The software blitter copies a whole block linear surface onto one with blocks of another height
// GOB by GOB (CopyBlockLinearGobs) when neither has padding: that must give the bytes
// unswizzling the surface and swizzling it with the other block height would.
void CheckGobCopy(u32 bytes_per_pixel, u32 width, u32 height, u32 block_height,
                  u32 output_block_height) {
    const size_t size =
        Tegra::Texture::CalculateSize(true, bytes_per_pixel, width, height, 1, block_height, 0);
    REQUIRE(size == size_t{width} * height * bytes_per_pixel);
    REQUIRE(size == Tegra::Texture::CalculateSize(true, bytes_per_pixel, width, height, 1,
                                                  output_block_height, 0));
    std::vector<u8> tiled(size);
    u32 state = 0x12345678;
    for (auto& byte : tiled) {
        state = state * 1664525 + 1013904223;
        byte = static_cast<u8>(state >> 24);
    }
    std::vector<u8> linear(size);
    Tegra::Texture::UnswizzleSubrect(linear, tiled, bytes_per_pixel, width, height, 1, 0, 0, width,
                                     height, block_height, 0, width * bytes_per_pixel);
    std::vector<u8> expected(size);
    Tegra::Texture::SwizzleSubrect(expected, linear, bytes_per_pixel, width, height, 1, 0, 0, width,
                                   height, output_block_height, 0, width * bytes_per_pixel);
    std::vector<u8> copied(size);
    Tegra::Texture::CopyBlockLinearGobs(copied, tiled, width * bytes_per_pixel, height,
                                        block_height, output_block_height);
    REQUIRE(copied == expected);
}

} // Anonymous namespace

TEST_CASE("CopyBlockLinearGobs: same bytes as unswizzling and swizzling", "[video_core]") {
    CheckGobCopy(16, 256, 128, 4, 3); // RGBA32F from blocks of 16 GOBs to 8, as title 1's copies
    CheckGobCopy(16, 256, 128, 3, 4);
    CheckGobCopy(16, 128, 256, 5, 0);
    CheckGobCopy(4, 64, 64, 3, 3);
    CheckGobCopy(8, 32, 16, 1, 0);
    CheckGobCopy(1, 64, 8, 0, 0);
}
