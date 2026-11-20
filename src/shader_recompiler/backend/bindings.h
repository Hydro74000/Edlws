// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace Shader::Backend {

struct Bindings {
    /// Descriptor set that the resources of the stage being emitted go to.
    u32 descriptor_set{};
    u32 unified{};
    u32 uniform_buffer{};
    u32 storage_buffer{};
    u32 texture{};
    u32 image{};
    u32 texture_scaling_index{};
    u32 image_scaling_index{};
    /// End of the rescaling bits the stage's set owns: textures and images past it are never
    /// seen as rescaled.
    u32 texture_scaling_end{~0u};
    u32 image_scaling_end{~0u};
};

} // namespace Shader::Backend
