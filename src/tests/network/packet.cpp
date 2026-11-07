// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "network/packet.h"

using Network::Packet;

TEST_CASE("Packet reads back a vector", "[network]") {
    Packet packet;
    packet.Write(std::vector<u8>{1, 2, 3});
    std::vector<u8> out;
    packet.Read(out);
    REQUIRE(static_cast<bool>(packet));
    REQUIRE(out == std::vector<u8>{1, 2, 3});
}

TEST_CASE("Packet refuses a vector longer than what it holds", "[network]") {
    for (const u32 declared : {u32{5}, u32{1024}, u32{0xffffffff}}) {
        Packet packet;
        packet.Write(declared);
        packet.Write(u32{0});
        std::vector<u8> out{1, 2, 3};
        packet.Read(out);
        REQUIRE_FALSE(static_cast<bool>(packet));
        REQUIRE(out.empty());
    }
}

TEST_CASE("Packet takes an empty vector", "[network]") {
    Packet packet;
    packet.Write(std::vector<u8>{});
    std::vector<u8> out{1};
    packet.Read(out);
    REQUIRE(static_cast<bool>(packet));
    REQUIRE(out.empty());
}
