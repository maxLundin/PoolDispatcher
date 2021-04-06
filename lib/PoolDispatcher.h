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
public:
    using thread_id_t = int32_t;
private:
    template<typename Runnable>
    struct thread_run {

        explicit thread_run(Runnable &run) : runnable(new Runnable(run)) {}

        void operator()(std::atomic_bool const &cancellation, thread_id_t t_id) {
            while (!cancellation.load(std::memory_order_acquire)) {
                (*runnable)(t_id);
            }
        }

    private:
        std::unique_ptr<Runnable> runnable;
    };

public:

    template<typename Runnable>
    explicit PoolDispatcher(Runnable run, thread_id_t threads_c) {
        if (!threads_c) {
            throw std::runtime_error("Can't have 0 threads");
        }
        threads.reserve(threads_c);
        cancellers.reserve(threads_c);
        for (thread_id_t i = 0; i < threads_c; ++i) {
            cancellers.push_back(std::make_unique<std::atomic_bool>(false));
            threads.emplace_back(thread_run(run), std::ref(*cancellers[i]), i);
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


