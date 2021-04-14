//
// Created by maxlundin on 3/31/21.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

class PoolDispatcher {
private:
    void joinThreads() {
        std::runtime_error *curError = nullptr;
        for (auto &thread : threads) {
            try {
                thread.join();
            } catch (std::system_error &error) {
                curError = &error;
            }
        }
        if (curError) {
            throw *curError;
        }
    }

public:
    using thread_id_t = int32_t;

    template<typename Runnable>
    explicit
    PoolDispatcher(Runnable run, thread_id_t threads_c) : canceller(std::make_unique<std::atomic_bool>(false)) {
        if (!threads_c) {
            throw std::runtime_error("Can't have 0 threads");
        }
        threads.reserve(threads_c);

        auto const runnable = [run](std::atomic_bool const &cancellation) {
            while (!cancellation.load(std::memory_order_acquire)) {
                run();
            }
        };

        for (thread_id_t i = 0; i < threads_c; ++i) {
            try {
                threads.emplace_back(runnable, std::ref(*canceller));
            } catch (std::system_error &error) {
                joinThreads();
                throw;
            }
        }
    }

    PoolDispatcher(PoolDispatcher const &) = delete;

    PoolDispatcher(PoolDispatcher &&) = default;

    ~PoolDispatcher() {
        canceller->store(true, std::memory_order_release);
        joinThreads();
    }

private:
    std::vector<std::thread> threads;
    std::unique_ptr<std::atomic_bool> canceller;
};


