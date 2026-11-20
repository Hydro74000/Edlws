// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <boost/container/small_vector.hpp>

#include "common/common_types.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/surface.h"
#include "video_core/texture_cache/types.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

using Shader::Backend::SPIRV::NUM_TEXTURE_AND_IMAGE_SCALING_WORDS;

[[nodiscard]] inline std::optional<PixelFormat> PixelFormatFromImageFormat(
    Shader::ImageFormat format) {
    switch (format) {
    case Shader::ImageFormat::Typeless:
        return std::nullopt;
    case Shader::ImageFormat::R8_UINT:
        return PixelFormat::R8_UINT;
    case Shader::ImageFormat::R8_SINT:
        return PixelFormat::R8_SINT;
    case Shader::ImageFormat::R16_UINT:
        return PixelFormat::R16_UINT;
    case Shader::ImageFormat::R16_SINT:
        return PixelFormat::R16_SINT;
    case Shader::ImageFormat::R32_UINT:
        return PixelFormat::R32_UINT;
    case Shader::ImageFormat::R32G32_UINT:
        return PixelFormat::R32G32_UINT;
    case Shader::ImageFormat::R32G32B32A32_UINT:
        return PixelFormat::R32G32B32A32_UINT;
    }
    return std::nullopt;
}

[[nodiscard]] inline VkDeviceSize DescriptorSizeForType(const Device& device,
                                                        VkDescriptorType type) {
    const auto& props = device.DescriptorBufferProperties();
    const bool robust = device.IsRobustBufferAccessEnabled();
    switch (type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return robust ? props.robustUniformBufferDescriptorSize : props.uniformBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return robust ? props.robustStorageBufferDescriptorSize : props.storageBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return robust ? props.robustUniformTexelBufferDescriptorSize
                      : props.uniformTexelBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return robust ? props.robustStorageTexelBufferDescriptorSize
                      : props.storageTexelBufferDescriptorSize;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return props.combinedImageSamplerDescriptorSize;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return props.storageImageDescriptorSize;
    default:
        return 0;
    }
}

struct DescriptorBufferBinding {
    VkDescriptorType type;
    u32 count;
    VkDeviceSize offset;
    VkDeviceSize stride;
};

struct DescriptorBufferLayout {
    VkDeviceSize size{};
    boost::container::small_vector<DescriptorBufferBinding, 32> bindings;

    [[nodiscard]] bool Empty() const noexcept {
        return bindings.empty();
    }
};

inline void WriteDescriptorBuffer(const Device& device, const DescriptorBufferLayout& layout,
                                  const DescriptorUpdateEntry* payload, u8* host) {
    const vk::Device& dev = device.GetLogical();
    for (const DescriptorBufferBinding& binding : layout.bindings) {
        for (u32 index = 0; index < binding.count; ++index) {
            const DescriptorUpdateEntry& entry = *(payload++);
            const VkDescriptorAddressInfoEXT address_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                .pNext = nullptr,
                .address = entry.address.address,
                .range = entry.address.range,
                .format = entry.address.format,
            };
            VkDescriptorGetInfoEXT get_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                .pNext = nullptr,
                .type = binding.type,
                .data{},
            };
            switch (binding.type) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                get_info.data.pUniformBuffer = &address_info;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                get_info.data.pStorageBuffer = &address_info;
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                get_info.data.pUniformTexelBuffer = &address_info;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                get_info.data.pStorageTexelBuffer = &address_info;
                break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                get_info.data.pCombinedImageSampler = &entry.image;
                break;
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                get_info.data.pStorageImage = &entry.image;
                break;
            default:
                continue;
            }
            dev.GetDescriptorEXT(get_info, binding.stride,
                                 host + binding.offset + index * binding.stride);
        }
    }
}

[[nodiscard]] inline u32 NumDescriptorEntries(const Shader::Info& info) {
    return Shader::NumDescriptors(info.constant_buffer_descriptors) +
           Shader::NumDescriptors(info.storage_buffers_descriptors) +
           Shader::NumDescriptors(info.texture_buffer_descriptors) +
           Shader::NumDescriptors(info.image_buffer_descriptors) +
           Shader::NumDescriptors(info.texture_descriptors) +
           Shader::NumDescriptors(info.image_descriptors);
}

/// Most descriptor sets a graphics pipeline binds: the pre-rasterization stages, then the fragment
/// stage.
inline constexpr size_t NUM_GRAPHICS_DESCRIPTOR_SETS = 2;

/// Whether the fragment stage has a set of its own. Only pipeline libraries need it, each holding
/// its own stage's set; elsewhere every stage stays in set 0, which keeps its descriptor transport
/// (push descriptors or descriptor buffer).
[[nodiscard]] inline bool SplitsGraphicsDescriptorSets(const Device& device) {
    return device.IsExtGraphicsPipelineLibraryEnabled();
}

/// The descriptor set of a graphics stage's resources, numbered from binding 0 in each set, so a
/// stage's descriptors don't depend on the other set's stages. Stages of one set are consecutive,
/// so each set's descriptors are one range of the draw's descriptor payload.
[[nodiscard]] constexpr u32 GraphicsDescriptorSet(size_t stage_index, bool split_sets) noexcept {
    return split_sets && stage_index == 4 ? 1 : 0;
}

/// First rescaling bit of a set's textures and images: each set has its own share of the words.
[[nodiscard]] constexpr u32 GraphicsTextureScalingBase(u32 set, bool split_sets) noexcept {
    return set * Shader::Backend::SPIRV::NUM_TEXTURE_SCALING_WORDS * 32 /
           (split_sets ? NUM_GRAPHICS_DESCRIPTOR_SETS : size_t{1});
}
[[nodiscard]] constexpr u32 GraphicsImageScalingBase(u32 set, bool split_sets) noexcept {
    return set * Shader::Backend::SPIRV::NUM_IMAGE_SCALING_WORDS * 32 /
           (split_sets ? NUM_GRAPHICS_DESCRIPTOR_SETS : size_t{1});
}

class DescriptorLayoutBuilder {
public:
    DescriptorLayoutBuilder(const Device& device_) : device{&device_} {}

    bool Empty() const noexcept {
        return bindings.empty();
    }

    bool CanUsePushDescriptor() const noexcept {
        if (!device->IsKhrPushDescriptorSupported() ||
            num_descriptors > device->MaxPushDescriptors()) {
            return false;
        }
        return !device->IsExtDescriptorBufferSupported() ||
               device->DescriptorBufferProperties().bufferlessPushDescriptors;
    }

    bool CanUseDescriptorBuffer() const noexcept {
        const auto& props = device->DescriptorBufferProperties();
        if (!device->IsExtDescriptorBufferSupported() || bindings.empty() ||
            !props.combinedImageSamplerDescriptorSingleArray) {
            return false;
        }
        return !props.bufferlessPushDescriptors || !CanUsePushDescriptor();
    }

    DescriptorBufferLayout MakeDescriptorBufferLayout(VkDescriptorSetLayout layout) const {
        DescriptorBufferLayout result;
        if (!layout) {
            return result;
        }
        const vk::Device& dev = device->GetLogical();
        result.size = dev.GetDescriptorSetLayoutSizeEXT(layout);
        result.bindings.reserve(bindings.size());
        for (const VkDescriptorSetLayoutBinding& entry : bindings) {
            result.bindings.push_back(DescriptorBufferBinding{
                .type = entry.descriptorType,
                .count = entry.descriptorCount,
                .offset = dev.GetDescriptorSetLayoutBindingOffsetEXT(layout, entry.binding),
                .stride = DescriptorSizeForType(*device, entry.descriptorType),
            });
        }
        return result;
    }

    /// An empty builder makes no layout, unless allow_empty: a set below a set with resources
    /// needs one in the pipeline layout.
    vk::DescriptorSetLayout CreateDescriptorSetLayout(bool use_push_descriptor,
                                                      bool use_descriptor_buffer = false,
                                                      bool allow_empty = false) const {
        if (bindings.empty() && !allow_empty) {
            return nullptr;
        }
        VkDescriptorSetLayoutCreateFlags flags = 0;
        if (use_push_descriptor) {
            flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        }
        if (use_descriptor_buffer) {
            flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        }
        boost::container::small_vector<VkDescriptorBindingFlags, 32> binding_flags;
        VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_ci{};
        const void* pnext = nullptr;
        if (!use_push_descriptor && device->IsDescriptorBindingPartiallyBoundSupported()) {
            binding_flags.assign(bindings.size(), VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
            binding_flags_ci = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .pNext = nullptr,
                .bindingCount = static_cast<u32>(binding_flags.size()),
                .pBindingFlags = binding_flags.data(),
            };
            pnext = &binding_flags_ci;
        }
        return device->GetLogical().CreateDescriptorSetLayout({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = pnext,
            .flags = flags,
            .bindingCount = static_cast<u32>(bindings.size()),
            .pBindings = bindings.data(),
        });
    }

    vk::DescriptorUpdateTemplate CreateTemplate(VkDescriptorSetLayout descriptor_set_layout,
                                                VkPipelineLayout pipeline_layout,
                                                bool use_push_descriptor, u32 set = 0) const {
        if (entries.empty()) {
            return nullptr;
        }
        const VkDescriptorUpdateTemplateType type =
            use_push_descriptor ? VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR
                                : VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
        return device->GetLogical().CreateDescriptorUpdateTemplate({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .descriptorUpdateEntryCount = static_cast<u32>(entries.size()),
            .pDescriptorUpdateEntries = entries.data(),
            .templateType = type,
            .descriptorSetLayout = descriptor_set_layout,
            .pipelineBindPoint =
                is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
            .pipelineLayout = pipeline_layout,
            .set = set,
        });
    }

    vk::PipelineLayout CreatePipelineLayout(VkDescriptorSetLayout descriptor_set_layout) const {
        return CreatePipelineLayout(bindings.empty() ? std::span<const VkDescriptorSetLayout>{}
                                                     : std::span{&descriptor_set_layout, 1});
    }

    vk::PipelineLayout CreatePipelineLayout(std::span<const VkDescriptorSetLayout> set_layouts,
                                            VkPipelineLayoutCreateFlags flags = 0) const {
        using Shader::Backend::SPIRV::RenderAreaLayout;
        using Shader::Backend::SPIRV::RescalingLayout;
        const u32 size_offset = is_compute ? sizeof(RescalingLayout::down_factor) : 0u;
        const VkPushConstantRange range{
            .stageFlags = static_cast<VkShaderStageFlags>(
                is_compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS),
            .offset = 0,
            .size = static_cast<u32>(sizeof(RescalingLayout)) - size_offset +
                    static_cast<u32>(sizeof(RenderAreaLayout)),
        };
        return device->GetLogical().CreatePipelineLayout({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = flags,
            .setLayoutCount = static_cast<u32>(set_layouts.size()),
            .pSetLayouts = set_layouts.empty() ? nullptr : set_layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        });
    }

    void Add(const Shader::Info& info, VkShaderStageFlags stage) {
        is_compute |= (stage & VK_SHADER_STAGE_COMPUTE_BIT) != 0;

        Add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stage, info.constant_buffer_descriptors);
        Add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stage, info.storage_buffers_descriptors);
        Add(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, stage, info.texture_buffer_descriptors);
        Add(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, stage, info.image_buffer_descriptors);
        Add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stage, info.texture_descriptors);
        Add(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stage, info.image_descriptors);
    }

private:
    template <typename Descriptors>
    void Add(VkDescriptorType type, VkShaderStageFlags stage, const Descriptors& descriptors) {
        const size_t num{descriptors.size()};
        for (size_t i = 0; i < num; ++i) {
            bindings.push_back({
                .binding = binding,
                .descriptorType = type,
                .descriptorCount = descriptors[i].count,
                .stageFlags = stage,
                .pImmutableSamplers = nullptr,
            });
            entries.push_back({
                .dstBinding = binding,
                .dstArrayElement = 0,
                .descriptorCount = descriptors[i].count,
                .descriptorType = type,
                .offset = offset,
                .stride = sizeof(DescriptorUpdateEntry),
            });
            ++binding;
            num_descriptors += descriptors[i].count;
            offset += sizeof(DescriptorUpdateEntry) * descriptors[i].count;
        }
    }

    const Device* device{};
    bool is_compute{};
    boost::container::small_vector<VkDescriptorSetLayoutBinding, 32> bindings;
    boost::container::small_vector<VkDescriptorUpdateTemplateEntry, 32> entries;
    u32 binding{};
    u32 num_descriptors{};
    size_t offset{};
};

class RescalingPushConstant {
public:
    explicit RescalingPushConstant() noexcept {}

    // Past the end of its set's words, a texture or image has no bit: the shader sees it as not
    // rescaled.
    void PushTexture(bool is_rescaled) noexcept {
        if (texture_ptr == texture_end) {
            return;
        }
        *texture_ptr |= is_rescaled ? texture_bit : 0u;
        texture_bit <<= 1u;
        if (texture_bit == 0u) {
            texture_bit = 1u;
            ++texture_ptr;
        }
    }

    void PushImage(bool is_rescaled) noexcept {
        if (image_ptr == image_end) {
            return;
        }
        *image_ptr |= is_rescaled ? image_bit : 0u;
        image_bit <<= 1u;
        if (image_bit == 0u) {
            image_bit = 1u;
            ++image_ptr;
        }
    }

    /// Moves to the words of a graphics descriptor set's stages.
    void SeekSet(u32 set, bool split_sets) noexcept {
        if (set == current_set) {
            return;
        }
        current_set = set;
        const u32 texture_base{GraphicsTextureScalingBase(set, split_sets)};
        texture_ptr = words.data() + texture_base / 32;
        texture_end = words.data() + GraphicsTextureScalingBase(set + 1, split_sets) / 32;
        texture_bit = 1u << (texture_base % 32);
        u32* const images{words.data() + Shader::Backend::SPIRV::NUM_TEXTURE_SCALING_WORDS};
        const u32 image_base{GraphicsImageScalingBase(set, split_sets)};
        image_ptr = images + image_base / 32;
        image_end = images + GraphicsImageScalingBase(set + 1, split_sets) / 32;
        image_bit = 1u << (image_base % 32);
    }

    const std::array<u32, NUM_TEXTURE_AND_IMAGE_SCALING_WORDS>& Data() const noexcept {
        return words;
    }

private:
    std::array<u32, NUM_TEXTURE_AND_IMAGE_SCALING_WORDS> words{};
    u32* texture_ptr{words.data()};
    u32* texture_end{words.data() + Shader::Backend::SPIRV::NUM_TEXTURE_SCALING_WORDS};
    u32* image_ptr{words.data() + Shader::Backend::SPIRV::NUM_TEXTURE_SCALING_WORDS};
    u32* image_end{words.data() + Shader::Backend::SPIRV::NUM_TEXTURE_AND_IMAGE_SCALING_WORDS};
    u32 texture_bit{1u};
    u32 image_bit{1u};
    /// None yet: the first SeekSet installs its set's bounds, and compute keeps the whole words.
    u32 current_set{~0u};
};

class RenderAreaPushConstant {
public:
    bool uses_render_area{};
    std::array<f32, 4> words{};
};

inline void PushImageDescriptors(TextureCache& texture_cache,
                                 GuestDescriptorQueue& guest_descriptor_queue,
                                 const Shader::Info& info, RescalingPushConstant& rescaling,
                                 const VideoCommon::SamplerId*& samplers,
                                 const VideoCommon::ImageViewInOut*& views) {
    const u32 num_texture_buffers = Shader::NumDescriptors(info.texture_buffer_descriptors);
    const u32 num_image_buffers = Shader::NumDescriptors(info.image_buffer_descriptors);
    views += num_texture_buffers;
    views += num_image_buffers;
    for (const auto& desc : info.texture_descriptors) {
        bool is_rescaled{};
        for (u32 index = 0; index < desc.count; ++index) {
            const VideoCommon::ImageViewId image_view_id{(views++)->id};
            const VideoCommon::SamplerId sampler_id{*(samplers++)};
            ImageView& image_view{texture_cache.GetImageView(image_view_id)};
            VkImageView vk_image_view{image_view.Handle(desc.type)};
            if (vk_image_view == VK_NULL_HANDLE) {
                const VkImageView null_image_view{texture_cache.GetImageView(VideoCommon::NULL_IMAGE_VIEW_ID).Handle(desc.type)};
                if (null_image_view != VK_NULL_HANDLE) vk_image_view = null_image_view;
            }
            Sampler& sampler{texture_cache.GetSampler(sampler_id)};
            guest_descriptor_queue.AddSampledImage(vk_image_view,
                                                   sampler.HandleFor(image_view, desc.is_depth));
            const bool element_rescaled{texture_cache.IsRescaling(image_view)};
            is_rescaled |= element_rescaled;
        }
        rescaling.PushTexture(is_rescaled);
    }
    for (const auto& desc : info.image_descriptors) {
        bool is_rescaled{};
        for (u32 index = 0; index < desc.count; ++index) {
            ImageView& image_view{texture_cache.GetImageView((views++)->id)};
            if (desc.is_written) {
                texture_cache.MarkStorageWrite(image_view);
            }
            const VkImageView vk_image_view{image_view.StorageView(desc.type, desc.format)};
            guest_descriptor_queue.AddImage(vk_image_view);
            const bool element_rescaled{texture_cache.IsRescaling(image_view)};
            is_rescaled |= element_rescaled;
        }
        rescaling.PushImage(is_rescaled);
    }
}

} // namespace Vulkan
