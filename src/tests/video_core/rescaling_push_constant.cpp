// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/pipeline_helper.h"

namespace {

using Shader::Backend::SPIRV::NUM_TEXTURE_SCALING_WORDS;
using Vulkan::RescalingPushConstant;

bool TextureBit(const RescalingPushConstant& rescaling, u32 bit) {
    return ((rescaling.Data()[bit / 32] >> (bit % 32)) & 1) != 0;
}

bool ImageBit(const RescalingPushConstant& rescaling, u32 bit) {
    return ((rescaling.Data()[NUM_TEXTURE_SCALING_WORDS + bit / 32] >> (bit % 32)) & 1) != 0;
}

} // Anonymous namespace

TEST_CASE("Pre-rasterization textures past their set's bits leave the fragment set's alone",
          "[video_core][rescaling]") {
    RescalingPushConstant rescaling;
    rescaling.SeekSet(0);
    for (u32 index = 0; index < 129; ++index) {
        rescaling.PushTexture(true);
    }
    REQUIRE(TextureBit(rescaling, 0));
    REQUIRE(TextureBit(rescaling, 127));
    REQUIRE_FALSE(TextureBit(rescaling, 128));
    rescaling.SeekSet(1);
    rescaling.PushTexture(false);
    rescaling.PushTexture(true);
    REQUIRE_FALSE(TextureBit(rescaling, 128));
    REQUIRE(TextureBit(rescaling, 129));
}

TEST_CASE("Images past their set's bits are dropped", "[video_core][rescaling]") {
    RescalingPushConstant rescaling;
    rescaling.SeekSet(0);
    for (u32 index = 0; index < 65; ++index) {
        rescaling.PushImage(true);
    }
    REQUIRE(ImageBit(rescaling, 63));
    REQUIRE_FALSE(ImageBit(rescaling, 64));
    rescaling.SeekSet(1);
    rescaling.PushImage(false);
    for (u32 index = 1; index < 65; ++index) {
        rescaling.PushImage(true);
    }
    REQUIRE_FALSE(ImageBit(rescaling, 64));
    REQUIRE(ImageBit(rescaling, 127));
}

TEST_CASE("Compute textures use every texture bit and no image bit", "[video_core][rescaling]") {
    RescalingPushConstant rescaling;
    for (u32 index = 0; index < NUM_TEXTURE_SCALING_WORDS * 32 + 1; ++index) {
        rescaling.PushTexture(true);
    }
    REQUIRE(TextureBit(rescaling, NUM_TEXTURE_SCALING_WORDS * 32 - 1));
    REQUIRE_FALSE(ImageBit(rescaling, 0));
}
