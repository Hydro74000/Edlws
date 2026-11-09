// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <random>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/nx2.h"

namespace {

using Ledger = Tegra::Nx2SemaphoreLedger;

// The ledger is one table for the process: each test takes its own words.
constexpr u64 Base = 0x7e0000000000ULL;

} // Anonymous namespace

TEST_CASE("Nx2SemaphoreLedger: nothing queued for a word", "[video_core][nx2]") {
    auto& ledger = Ledger::Get();
    REQUIRE(ledger.Check(Base, [] { return 0U; }, [](u32) { return true; }) ==
            Ledger::Outcome::NothingQueued);
}

TEST_CASE("Nx2SemaphoreLedger: queued releases, applied in order, meet an acquire",
          "[video_core][nx2]") {
    auto& ledger = Ledger::Get();
    constexpr u64 address = Base + 0x10;
    u32 word = 3;
    ledger.Queue(address, Ledger::Op::Add, 2);
    ledger.Queue(address, Ledger::Op::Max, 4);
    ledger.Queue(address, Ledger::Op::Write, 9);
    const auto check = [&](auto meets) { return ledger.Check(address, [&] { return word; }, meets); };
    // 3 -> 5 -> 5 -> 9
    CHECK(check([](u32 v) { return v >= 5; }) == Ledger::Outcome::Meets);
    CHECK(check([](u32 v) { return v == 9; }) == Ledger::Outcome::Meets);
    CHECK(check([](u32 v) { return v == 4; }) == Ledger::Outcome::Misses);
    CHECK(check([](u32 v) { return v > 9; }) == Ledger::Outcome::Misses);
    // Each release runs with its write: the predictions stay the same.
    ledger.Apply(address, [&] { word = Ledger::Reduce(Ledger::Op::Add, word, 2); });
    CHECK(word == 5);
    CHECK(check([](u32 v) { return v == 9; }) == Ledger::Outcome::Meets);
    CHECK(check([](u32 v) { return v == 6; }) == Ledger::Outcome::Misses);
    ledger.Apply(address, [&] { word = Ledger::Reduce(Ledger::Op::Max, word, 4); });
    ledger.Apply(address, [&] { word = 9; });
    CHECK(check([](u32) { return true; }) == Ledger::Outcome::NothingQueued);
}

TEST_CASE("Nx2SemaphoreLedger: a dropped release leaves the word", "[video_core][nx2]") {
    auto& ledger = Ledger::Get();
    constexpr u64 address = Base + 0x20;
    ledger.Queue(address, Ledger::Op::Write, 7);
    ledger.Drop(address);
    CHECK(ledger.Check(address, [] { return 0U; }, [](u32 v) { return v == 7; }) ==
          Ledger::Outcome::NothingQueued);
}

TEST_CASE("Nx2SemaphoreLedger: the reductions match the semaphore encodings", "[video_core][nx2]") {
    CHECK(Ledger::Reduce(Ledger::Op::Add, 5, 3) == 8);
    CHECK(Ledger::Reduce(Ledger::Op::Min, 5, 3) == 3);
    CHECK(Ledger::Reduce(Ledger::Op::Max, 5, 3) == 5);
    CHECK(Ledger::Reduce(Ledger::Op::Inc, 5, 5) == 0);
    CHECK(Ledger::Reduce(Ledger::Op::Inc, 4, 5) == 5);
    CHECK(Ledger::Reduce(Ledger::Op::Dec, 0, 5) == 5);
    CHECK(Ledger::Reduce(Ledger::Op::Dec, 6, 5) == 5);
    CHECK(Ledger::Reduce(Ledger::Op::Dec, 3, 5) == 2);
    CHECK(Ledger::Reduce(Ledger::Op::And, 6, 3) == 2);
    CHECK(Ledger::Reduce(Ledger::Op::Or, 6, 3) == 7);
    CHECK(Ledger::Reduce(Ledger::Op::Xor, 6, 3) == 5);
    CHECK(Ledger::Reduce(Ledger::Op::Write, 6, 3) == 3);
    CHECK(Ledger::Reduce(Ledger::Op::MinSigned, 0xffffffff, 1) == 0xffffffff);
    CHECK(Ledger::Reduce(Ledger::Op::MaxSigned, 0xffffffff, 1) == 1);
}

TEST_CASE("Nx2SemaphoreLedger: QMD release reductions keep their signed format", "[video_core][nx2]") {
    CHECK(Ledger::Reduction(0, true) == Ledger::Op::Add);
    CHECK(Ledger::Reduction(1, false) == Ledger::Op::Min);
    CHECK(Ledger::Reduction(1, true) == Ledger::Op::MinSigned);
    CHECK(Ledger::Reduction(2, false) == Ledger::Op::Max);
    CHECK(Ledger::Reduction(2, true) == Ledger::Op::MaxSigned);
    CHECK(Ledger::Reduction(7, true) == Ledger::Op::Xor);
}

TEST_CASE("Nx2SemaphoreLedger: a release running during a check is never counted twice",
          "[video_core][nx2]") {
    // The fencing thread applies adds while the GPU thread checks. The word ends at N: a check
    // that read the word after a release ran but counted that release again would predict N + 1.
    auto& ledger = Ledger::Get();
    constexpr u64 address = Base + 0x30;
    constexpr u32 N = 200000;
    std::atomic<u32> word{0};
    for (u32 i = 0; i < N; ++i) {
        ledger.Queue(address, Ledger::Op::Add, 1);
    }
    std::atomic<bool> done{false};
    std::thread applier([&] {
        for (u32 i = 0; i < N; ++i) {
            ledger.Apply(address, [&] { word.store(word.load() + 1); });
        }
        done = true;
    });
    u32 checks{};
    u32 wrong{};
    while (!done) {
        ++checks;
        if (ledger.Check(address, [&] { return word.load(); }, [](u32 v) { return v > N; }) ==
            Ledger::Outcome::Meets) {
            ++wrong;
        }
    }
    applier.join();
    CHECK(checks > 0);
    CHECK(wrong == 0);
    CHECK(word.load() == N);
    CHECK(ledger.Check(address, [&] { return word.load(); }, [](u32) { return true; }) ==
          Ledger::Outcome::NothingQueued);
}
