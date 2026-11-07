// SPDX-License-Identifier: GPL-3.0-or-later

#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "shader_recompiler/profile.h"

namespace {
class TextureEnvironment final : public Shader::Environment {
public:
    bool integer_format{false};

    u64 ReadInstruction(u32) override { return 0; }
    u32 ReadCbufValue(u32, u32) override { return 0; }
    Shader::TextureType ReadTextureType(u32) override { return Shader::TextureType::Color2D; }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override { return integer_format; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 15; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};
} // namespace

// Ocarina of Time binds a garbage descriptor (unknown format 33) to a TEX.DC. Reading it as an
// integer format gave the image an integer sampled type, which RADV's spirv_to_nir rejects.
TEST_CASE("Depth-compare samples keep a float image when the descriptor reads as integer",
          "[shader_recompiler][texture_pass]") {
    using namespace Shader;
    for (const bool depth : {false, true}) {
        for (const bool integer : {false, true}) {
            CAPTURE(depth, integer);
            TextureEnvironment env;
            env.integer_format = integer;
            ObjectPool<IR::Inst> pool;
            IR::Block block{pool};
            IR::Program program;
            program.stage = Stage::Fragment;
            program.blocks = {&block};
            program.post_order_blocks = {&block};
            program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                                   {{}, IR::AbstractSyntaxNode::Type::Return}};
            IR::IREmitter ir{block};
            IR::TextureInstInfo flags{};
            flags.type.Assign(TextureType::Color2D);
            flags.is_depth.Assign(depth ? 1 : 0);
            const auto coords = ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(0.0f));
            const IR::Value sample =
                depth ? IR::Value{ir.ImageSampleDrefImplicitLod(ir.Imm32(0), coords,
                                                                ir.Imm32(0.5f), {}, {}, {}, flags)}
                      : ir.ImageSampleExplicitLod(ir.Imm32(0), coords, ir.Imm32(0.0f), {}, flags);

            HostTranslateInfo host;
            host.ApplyDescriptorLimitPolicy();
            Optimization::TexturePass(env, program, host);
            REQUIRE(program.info.texture_descriptors.size() == 1);
            const bool expect_integer = integer && !depth;
            CHECK(program.info.texture_descriptors[0].is_integer == expect_integer);
            CHECK(sample.Inst()->Flags<IR::TextureInstInfo>().is_integer ==
                  static_cast<u32>(expect_integer));

            Profile profile;
            profile.supported_spirv = 0x00010500;
            const auto code = Backend::SPIRV::EmitSPIRV(profile, program);
            constexpr u32 OpTypeInt = 21;
            constexpr u32 OpTypeFloat = 22;
            constexpr u32 OpTypeImage = 25;
            constexpr u32 OpImageSampleDrefImplicitLod = 89;
            std::unordered_map<u32, u32> scalar_types;
            u32 image_sampled_type{};
            u32 dref_result_type{};
            for (size_t offset = 5; offset < code.size();) {
                const u32 size = code[offset] >> 16;
                REQUIRE(size != 0);
                REQUIRE(offset + size <= code.size());
                const u32 opcode = code[offset] & 0xffffU;
                if (opcode == OpTypeInt || opcode == OpTypeFloat) {
                    scalar_types.emplace(code[offset + 1], opcode);
                } else if (opcode == OpTypeImage) {
                    image_sampled_type = code[offset + 2];
                    CHECK(code[offset + 4] == static_cast<u32>(depth));
                } else if (opcode == OpImageSampleDrefImplicitLod) {
                    dref_result_type = code[offset + 1];
                }
                offset += size;
            }
            REQUIRE(image_sampled_type != 0);
            CHECK(scalar_types.at(image_sampled_type) == (expect_integer ? OpTypeInt : OpTypeFloat));
            if (depth) {
                CHECK(dref_result_type == image_sampled_type);
            }
        }
    }
}
