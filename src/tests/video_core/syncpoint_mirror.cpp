// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/host1x/syncpoint_manager.h"

using Tegra::Host1x::SyncpointManager;

TEST_CASE("The guest mirror sees each increment before it is published and its actions run",
          "[video_core][syncpoint]") {
    SyncpointManager manager;
    std::vector<u32> mirrored;
    bool published_early{};
    const int owner{};
    manager.SetGuestMirror(
        [&](u32 id, u32 value) {
            REQUIRE(id == 3);
            published_early |= manager.IsReadyGuest(id, value);
            mirrored.push_back(value);
        },
        &owner);
    u32 mirrored_at_action{};
    manager.RegisterGuestAction(3, 2, [&] { mirrored_at_action = mirrored.back(); });
    manager.IncrementGuest(3);
    manager.IncrementGuest(3);
    manager.IncrementHost(3);
    REQUIRE(mirrored == std::vector<u32>{1, 2});
    REQUIRE_FALSE(published_early);
    REQUIRE(mirrored_at_action == 2);

    // Only the owner drops it.
    const int someone_else{};
    manager.ClearGuestMirror(&someone_else);
    manager.IncrementGuest(3);
    REQUIRE(mirrored.size() == 3);
    manager.ClearGuestMirror(&owner);
    manager.IncrementGuest(3);
    REQUIRE(mirrored.size() == 3);
    REQUIRE(manager.GetGuestSyncpointValue(3) == 4);
}

TEST_CASE("Guest increments wait for a stable copy of the guest values", "[video_core][syncpoint]") {
    SyncpointManager manager;
    std::thread increment;
    u32 value_while_stable{};
    manager.WithGuestValuesStable([&] {
        increment = std::thread([&] { manager.IncrementGuest(0); });
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        value_while_stable = manager.GetGuestSyncpointValue(0);
    });
    increment.join();
    REQUIRE(value_while_stable == 0);
    REQUIRE(manager.GetGuestSyncpointValue(0) == 1);
}
