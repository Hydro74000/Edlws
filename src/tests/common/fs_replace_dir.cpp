// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/fs/fs.h"

namespace fs = std::filesystem;

namespace {

struct TempDir {
    TempDir() {
        path = fs::temp_directory_path() /
               ("edelweiss-replace-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path path;
};

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path} << text;
}

std::string Read(const fs::path& path) {
    std::string text;
    std::getline(std::ifstream{path}, text);
    return text;
}

} // namespace

TEST_CASE("ReplaceDirContents swaps the contents", "[fs]") {
    TempDir t;
    Write(t.path / "old.bin", "old");
    Write(t.path / "both.bin", "old");
    Write(t.path / "old_dir/file", "old");
    const auto staging = t.path / ".staging";
    Write(staging / "new.bin", "new");
    Write(staging / "both.bin", "new");
    Write(staging / "new_dir/file", "new");

    REQUIRE(Common::FS::ReplaceDirContents(t.path, staging));

    REQUIRE(Read(t.path / "new.bin") == "new");
    REQUIRE(Read(t.path / "both.bin") == "new");
    REQUIRE(Read(t.path / "new_dir/file") == "new");
    REQUIRE_FALSE(fs::exists(t.path / "old.bin"));
    REQUIRE_FALSE(fs::exists(t.path / "old_dir"));
    REQUIRE_FALSE(fs::exists(staging));
    REQUIRE_FALSE(fs::exists(t.path / ".staging.old"));
}

TEST_CASE("ReplaceDirContents takes a directory written with a trailing separator", "[fs]") {
    TempDir t;
    Write(t.path / "old.bin", "old");
    const auto dir = t.path.string() + "/";
    const auto staging = fs::path{dir} / ".staging";
    Write(staging / "new.bin", "new");

    REQUIRE(Common::FS::ReplaceDirContents(dir, staging));

    REQUIRE(Read(t.path / "new.bin") == "new");
    REQUIRE_FALSE(fs::exists(t.path / "old.bin"));
}

TEST_CASE("ReplaceDirContents puts everything back when it fails halfway", "[fs]") {
    TempDir t;
    Write(t.path / "old.bin", "old");
    const auto staging = t.path / ".staging";
    Write(staging / "new.bin", "new");
    // Can't land on the staging directory itself, which isn't empty. Permissions wouldn't do, as
    // tests can run as root.
    Write(staging / ".staging", "in the way");

    REQUIRE_FALSE(Common::FS::ReplaceDirContents(t.path, staging));

    REQUIRE(Read(t.path / "old.bin") == "old");
    REQUIRE(Read(staging / "new.bin") == "new");
    REQUIRE(Read(staging / ".staging") == "in the way");
    REQUIRE_FALSE(fs::exists(t.path / "new.bin"));
    REQUIRE_FALSE(fs::exists(t.path / ".staging.old"));
}

TEST_CASE("ReplaceDirContents touches nothing without room for the old contents", "[fs]") {
    TempDir t;
    Write(t.path / "old.bin", "old");

    REQUIRE_FALSE(Common::FS::ReplaceDirContents(t.path, t.path / "missing" / ".staging"));

    REQUIRE(Read(t.path / "old.bin") == "old");
}
