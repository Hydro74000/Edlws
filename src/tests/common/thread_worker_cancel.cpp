// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <memory>
#include <stop_token>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "common/thread_worker.h"

// Cancelling leaves queued tasks behind, so only the count of stopped workers ends the wait. A
// worker that counted itself without the queue mutex could slip between the waiter's check and
// its sleep, and the wait never returned (cancelling a shader load hung).
TEST_CASE("Cancelling a thread worker always wakes its waiter", "[common][thread_worker]") {
    constexpr size_t NumWorkers = 2;
    struct State {
        std::atomic<bool> release{};
        std::atomic<bool> returned{};
        std::atomic<size_t> started{};
    };
    for (int iteration = 0; iteration < 200; ++iteration) {
        auto* const worker = new Common::ThreadWorker(NumWorkers, "CancelTest");
        const auto state = std::make_shared<State>();
        for (size_t i = 0; i < NumWorkers + 2; ++i) {
            worker->QueueWork([state] {
                ++state->started;
                while (!state->release.load()) {
                    std::this_thread::yield();
                }
            });
        }
        while (state->started.load() < NumWorkers) {
            std::this_thread::yield();
        }
        std::stop_source stop;
        std::thread waiter([worker, state, token = stop.get_token()] {
            worker->WaitForRequests(token);
            state->returned.store(true);
        });
        stop.request_stop();
        state->release.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!state->returned.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        if (!state->returned.load()) {
            // The waiter is stuck inside the worker: leave both behind rather than hang the run.
            waiter.detach();
            FAIL("WaitForRequests never returned after a cancel");
        }
        waiter.join();
        delete worker;
    }
}
