// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/literals.h"
#include "video_core/buffer_cache/buffer_cache_base.h"

using namespace Common::Literals;
using VideoCommon::NativeComputeStorageSize;

TEST_CASE("Native compute bindings keep their bound without a larger size word",
          "[video_core][storage_bindings]") {
    constexpr u32 Mapping = static_cast<u32>(32_MiB);
    // A parameter after the pointer, like Fortune's Weave's texture handle.
    REQUIRE(NativeComputeStorageSize(Mapping, 266) == 8_MiB);
    REQUIRE(NativeComputeStorageSize(Mapping, 0) == 8_MiB);
    REQUIRE(NativeComputeStorageSize(Mapping, 8_MiB) == 8_MiB);
    // Not a size: high bits set, or past the mapping.
    REQUIRE(NativeComputeStorageSize(Mapping, (u64{1} << 32) | 16_MiB) == 8_MiB);
    REQUIRE(NativeComputeStorageSize(Mapping, 33_MiB) == 8_MiB);
    // A mapping smaller than the bound.
    REQUIRE(NativeComputeStorageSize(static_cast<u32>(1_MiB), 2_MiB) == 1_MiB);
}

TEST_CASE("Native compute bindings grow through a larger size word within the mapping",
          "[video_core][storage_bindings]") {
    // Mario Kart World's lighting binding (q69).
    REQUIRE(NativeComputeStorageSize(0x102f240, 0x102edc0) == 0x102edc0);
    REQUIRE(NativeComputeStorageSize(static_cast<u32>(32_MiB), 32_MiB) == 32_MiB);
}

TEST_CASE("Grown native compute bindings stay within 128 MiB once aligned to pages",
          "[video_core][storage_bindings]") {
    const u32 size = NativeComputeStorageSize(static_cast<u32>(1_GiB), 512_MiB);
    // The start aligned down by up to a storage buffer alignment (256 bytes) and the end rounded up
    // to a 4 KiB page.
    REQUIRE(u64{size} + 256 + 4_KiB <= 128_MiB);
    REQUIRE(size > 120_MiB);
}
