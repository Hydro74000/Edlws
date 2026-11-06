// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "qt_common/game_list/discovery.h"

namespace fs = std::filesystem;

namespace {

struct TempRoot {
    TempRoot() {
        path = fs::temp_directory_path() /
               ("edelweiss-discovery-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(path);
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path path;
};

void Touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream{path}.put('x');
}

void Dump(const fs::path& path) {
    Touch(path / "exefs/main");
    Touch(path / "exefs/main.npdm");
}

bool Supported(const fs::path& path) {
    return path.filename() == "main" || path.extension() == ".nsp";
}

auto Discover(const fs::path& path, bool deep) {
    return GameListDiscovery::Discover(path, deep, Supported, [] { return false; });
}

} // namespace

TEST_CASE("Game discovery finds loose dumps and leaves their RomFS alone", "[game_list]") {
    TempRoot t;
    const auto game = t.path / fs::path{u8"MKW-\u00e9glantine"};
    Dump(game / "base");
    Dump(game / "updates/0100000000000800");
    for (unsigned i = 0; i < 400; ++i) {
        Touch(game / "base/romfs/assets" / std::to_string(i) / "false-game.nsp");
    }
    Touch(t.path / "standalone.nsp");
    Touch(t.path / "unrelated/romfs/actual-game.nsp");

    const auto deep = Discover(t.path, true);
    REQUIRE(deep.files.size() == 4);
    REQUIRE(deep.skipped_romfs == 1);
    REQUIRE(deep.errors == 0);
    REQUIRE(deep.directories.size() < 20);
    REQUIRE(Discover(t.path, false).files.size() == 3);
    REQUIRE(Discover(game / "base", false).files.size() == 1);
    REQUIRE(Discover(game / "base/exefs", false).files.size() == 1);
    REQUIRE(Discover(t.path / "missing", true).files.empty());

    Touch(t.path / "incomplete/exefs/main");
    Touch(t.path / "incomplete/romfs/actual-game.nsp");
    REQUIRE(Discover(t.path, true).files.size() == 6);

    unsigned calls{};
    const auto cancelled =
        GameListDiscovery::Discover(t.path, true, Supported, [&] { return ++calls > 3; });
    REQUIRE(calls < 10);
    REQUIRE(cancelled.directories.size() <= 1);
}
