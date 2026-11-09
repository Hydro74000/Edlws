// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include "common/common_types.h"
#include "shader_recompiler/stage.h"
#include "video_core/shader_environment.h"

namespace {

constexpr std::array<char, 8> Magic{'y', 'u', 'z', 'u', 'c', 'a', 'c', 'h'};

template <typename T>
void Put(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

// A compute record as FileEnvironment::Deserialize reads it: one environment with one word of
// code and no recorded values, then a 4-byte key.
std::string ComputeRecord(u32 key) {
    std::string record;
    Put(record, u32{1});
    Put(record, u64{sizeof(u64)});
    for (int count = 0; count < 5; ++count) {
        Put(record, u64{0});
    }
    for (int field = 0; field < 5; ++field) {
        Put(record, u32{0});
    }
    Put(record, u32{1});
    Put(record, Shader::Stage::Compute);
    Put(record, u64{0});
    Put(record, std::array<u32, 3>{1, 1, 1});
    Put(record, u32{0});
    Put(record, key);
    return record;
}

std::string Header(u32 version) {
    std::string header(Magic.data(), Magic.size());
    Put(header, version);
    return header;
}

struct TempCache {
    TempCache() {
        std::random_device random;
        directory = std::filesystem::temp_directory_path() /
                    fmt::format("eden_pipeline_cache_upgrade_{:08x}", random());
        std::filesystem::create_directories(directory);
        path = directory / "vulkan.bin";
    }
    ~TempCache() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    void Write(const std::string& contents) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    std::string Read() const {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::vector<u32> Load(u32 expected, u32 min_compatible,
                          VideoCommon::CacheUpgrade* upgrade) const {
        std::vector<u32> keys;
        VideoCommon::LoadPipelines(
            std::stop_token{}, path, expected,
            [&keys](std::ifstream& file, VideoCommon::FileEnvironment) {
                u32 key{};
                file.read(reinterpret_cast<char*>(&key), sizeof(key));
                keys.push_back(key);
            },
            [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {}, min_compatible,
            upgrade);
        return keys;
    }

    std::filesystem::path directory;
    std::filesystem::path path;
};

} // Anonymous namespace

TEST_CASE("An older compatible pipeline cache is loaded and written back under the new version",
          "[video_core]") {
    TempCache cache;
    const std::string first = ComputeRecord(7);
    const std::string second = ComputeRecord(9);
    cache.Write(Header(20) + first + second);

    VideoCommon::CacheUpgrade upgrade;
    REQUIRE(cache.Load(21, 20, &upgrade) == std::vector<u32>{7, 9});
    REQUIRE(upgrade.from_version == 20);
    REQUIRE(upgrade.records == std::vector<std::string>{first, second});
    REQUIRE(upgrade.is_compute == std::vector<bool>{true, true});
    REQUIRE(std::filesystem::exists(cache.path));

    // The first record failed to translate again: only the second is written back.
    const std::array<const std::string*, 1> kept{&upgrade.records[1]};
    VideoCommon::RewritePipelines(cache.path, 21, upgrade.from_version, kept);
    REQUIRE(cache.Read() == Header(21) + second);
    REQUIRE(std::distance(std::filesystem::directory_iterator(cache.directory),
                          std::filesystem::directory_iterator()) == 1);

    VideoCommon::CacheUpgrade again;
    REQUIRE(cache.Load(21, 20, &again) == std::vector<u32>{9});
    REQUIRE(again.from_version == 0);
    REQUIRE(again.records.empty());
}

TEST_CASE("Pipeline caches outside the compatible versions are deleted", "[video_core]") {
    TempCache cache;
    for (const u32 version : {19U, 22U}) {
        cache.Write(Header(version) + ComputeRecord(7));
        VideoCommon::CacheUpgrade upgrade;
        REQUIRE(cache.Load(21, 20, &upgrade).empty());
        REQUIRE(upgrade.from_version == 0);
        REQUIRE(!std::filesystem::exists(cache.path));
    }
    // Without an upgrade to fill (OpenGL), an older version is deleted as before.
    cache.Write(Header(20) + ComputeRecord(7));
    REQUIRE(cache.Load(21, 20, nullptr).empty());
    REQUIRE(!std::filesystem::exists(cache.path));
}

TEST_CASE("A cut-short older pipeline cache keeps its whole records", "[video_core]") {
    TempCache cache;
    const std::string first = ComputeRecord(7);
    const std::string second = ComputeRecord(9);
    cache.Write(Header(20) + first + second.substr(0, second.size() / 2));

    VideoCommon::CacheUpgrade upgrade;
    REQUIRE(cache.Load(21, 20, &upgrade) == std::vector<u32>{7});
    REQUIRE(upgrade.records == std::vector<std::string>{first});
    REQUIRE(std::filesystem::exists(cache.path));

    // A cut-short cache of the current version is deleted as before.
    cache.Write(Header(21) + first + second.substr(0, second.size() / 2));
    VideoCommon::CacheUpgrade current;
    REQUIRE(cache.Load(21, 20, &current) == std::vector<u32>{7});
    REQUIRE(!std::filesystem::exists(cache.path));
}
