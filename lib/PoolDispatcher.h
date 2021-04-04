//
// Created by maxlundin on 3/31/21.
//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

class PoolDispatcher {
private:
    template<typename Runnable>
    struct thread_run {

        explicit thread_run(Runnable run) : runnable(run) {}

        void operator()(std::atomic_bool const &cancellation) {
            while (!cancellation.load()) {
                runnable();
            }
        }

    private:
        std::function<void()> runnable;
    };

public:

    template<typename Runnable>
    explicit PoolDispatcher(Runnable run, uint32_t threads_c) {
        if (!threads_c) {
            throw std::runtime_error("Can't have 0 threads");
        }
        threads.reserve(threads_c);
        cancellers.reserve(threads_c);
        for (uint32_t i = 0; i < threads_c; ++i) {
            cancellers.push_back(std::make_unique<std::atomic_bool>(false));
            threads.emplace_back(thread_run(run), std::ref(*cancellers[i]));
        }
    }

    PoolDispatcher(PoolDispatcher const &) = delete;

    PoolDispatcher(PoolDispatcher &&) = default;

    ~PoolDispatcher() {
        for (auto &canceller : cancellers) {
            canceller->store(true, std::memory_order_release);
        }
        for (auto &thread : threads) {
            thread.join();
        }
    }

private:
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<std::atomic_bool>> cancellers;
};


