# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(tests
    audio_core/adpcm_decode.cpp
    video_core/shader_object_pool.cpp
    video_core/join_whole_copy.cpp
    core/file_sys_parsers.cpp
    core/vfs_vector.cpp
    common/bit_field.cpp
    common/bit_util.cpp
    common/cityhash.cpp
    common/container_hash.cpp
    common/fibers.cpp
    common/game_presets.cpp
    common/fs_replace_dir.cpp
    common/host_memory.cpp
    common/multi_level_page_table.cpp
    common/page_table.cpp
    common/param_package.cpp
    common/range_map.cpp
    common/range_set_subtract.cpp
    common/ring_buffer.cpp
    common/scratch_buffer.cpp
    common/slot_vector.cpp
    common/thread_worker_cancel.cpp
    common/undefined_fix.cpp
    common/unique_function.cpp
    common/zstd_compression.cpp
    core/core_timing.cpp
    core/nup_zstd_read.cpp
    core/ldn.cpp
    core/malformed_containers.cpp
    core/memory_range.cpp
    core/internal_network/socket_proxy.cpp
    core/nup_writer.cpp
    core/priority_queue.cpp
    core/internal_network/network.cpp
    video_core/memory_tracker.cpp
    video_core/invalidation_accumulator.cpp
    video_core/syncpoint_mirror.cpp
    video_core/nx2_gpu_mapping.cpp
    video_core/texture_handles.cpp
    video_core/maxwell_fmz.cpp
    video_core/h264_packet.cpp
    video_core/vertex_repack.cpp
    video_core/ampere_lowering.cpp
    video_core/spirv_software_matrix.cpp
    video_core/ampere_geometry.cpp
    video_core/ampere_i2f.cpp
    video_core/ampere_returns.cpp
    video_core/ampere_guarded_ulea.cpp
    video_core/ampere_compute_lowering.cpp
    video_core/native_macro_fifo.cpp
    video_core/native_qmd.cpp
    video_core/framebuffer_limits.cpp
    video_core/nx2_semaphore_ledger.cpp
    video_core/nx2_semaphore_mirror.cpp
    video_core/nx2_written_pages.cpp
    video_core/memory_manager_ranges.cpp
    video_core/maxwell_cb_words.cpp
    video_core/uniform_stream_reuse.cpp
    video_core/usage_tracker.cpp
    video_core/shader_invalidation.cpp
    video_core/descriptor_table.cpp
    video_core/global_address_tracking.cpp
    video_core/split_fence_wait.cpp
    video_core/pipeline_cache_upgrade.cpp
    video_core/spirv_atomic_retry.cpp
    video_core/depth_compare_type.cpp
    video_core/global_atomic_routing.cpp
    video_core/nvn_storage_descriptor.cpp
    video_core/ampere_f2i.cpp
    video_core/ampere_ldc.cpp
    video_core/ampere_vote.cpp
    video_core/shader_attributes.cpp
    video_core/indirect_reset.cpp
    video_core/block_linear_gob_copy.cpp
    video_core/cpu_write_runs.cpp
    video_core/buffer_cache_downloads.cpp
    video_core/page_filter.cpp
    video_core/query_page_filter.cpp
    video_core/texture_alias_ticks.cpp
    input_common/calibration_configuration_job.cpp
    network/packet.cpp
    qt_common/game_discovery.cpp
)

create_target_directory_groups(tests)

target_link_libraries(tests PRIVATE common core input_common video_core)
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)

add_test(NAME tests COMMAND tests)

# needed for vma
if (NOT MSVC)
    target_compile_options(tests PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-conversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>)
endif()
