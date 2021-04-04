//
// Created by maxlundin on 4/3/21.
//


#include <gtest/gtest.h>
#include <iostream>

#include "TaskQueue.h"


/*
 * probability based test
 * may fail
 */
TEST(distribution, check_distribution) {
    constexpr size_t queue_size = 6;
    constexpr size_t iter_count = 1000000;

    std::vector<size_t> distribution(queue_size, 0);
    std::vector<double> distribution_normalized(queue_size, 0);


    for (size_t i = 0; i < iter_count; ++i) {
        distribution[QueueDistribution<queue_size>::generate()]++;
    }

    std::transform(distribution.begin(), distribution.end(), distribution_normalized.begin(),
                   [](size_t x) { return (1. * x) / iter_count; });

    for (size_t i = 0; i < queue_size; ++i) {
        ASSERT_NEAR(distribution_normalized[i], 1. * (queue_size - i) / QueueDistribution<queue_size>::max_random_val,
                    0.05);
    }
}

TEST(thread_pool, create_zero) {
    ASSERT_THROW(TaskQueue<8> queue(0), std::runtime_error);
}

TEST(thread_pool, create_empty) {
    TaskQueue<8> queue(4);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

TEST(thread_pool, execute_void_task) {
    TaskQueue<8> queue(4);
    auto x = queue.enqueue([] {}, 0);
    queue.execute(x);
}

TEST(thread_pool, execute_no_void_task) {
    TaskQueue<8> queue(4);
    auto x = queue.enqueue([] { return 4; }, 0);
    ASSERT_EQ(queue.execute(x), 4);
}

TEST(thread_pool, execute_not_pure_task) {
    TaskQueue<8> queue(4);
    int result = 1;
    auto x = queue.enqueue([&result] { return result++; }, 0);
    ASSERT_EQ(queue.execute(x), 1);
}

TEST(thread_pool, execute_same_task_twice) {
    TaskQueue<8> queue(4);
    std::atomic_int result = 1;
    auto func = [&result] { return result++; };
    auto handler1 = queue.enqueue(func, 0);
    auto handler2 = queue.enqueue(func, 0);
    auto res1 = queue.execute(handler1);
    auto res2 = queue.execute(handler2);
    ASSERT_TRUE((res1 == 1 && res2 == 2) || (res1 == 2 && res2 == 1));
}

TEST(thread_pool, get_result_twice) {
    TaskQueue<8> queue(4);
    std::atomic_int result = 1;
    auto func = [&result] { return result++; };
    auto handler = queue.enqueue(func, 0);
    auto res1 = queue.execute(handler);
    ASSERT_THROW(queue.execute(handler), std::runtime_error);
    ASSERT_TRUE(res1 == 1);
}

TEST(thread_pool, one_thread_w_intern_call) {
    TaskQueue<8> queue(1);
    std::atomic_int semaphore{0};
    auto func = [&queue, &semaphore] {
        semaphore.store(1);
        auto handle = queue.enqueue([] { return 42; }, 0);
        return queue.execute(handle);
    };
    auto handler = queue.enqueue(func, 0);
    while (semaphore.load() == 0) {} // wait until worker thread takes our task
    auto res1 = queue.execute(handler);
    ASSERT_TRUE(res1 == 42);
}

TEST(thread_pool, self_execute) {
    TaskQueue<8> queue(1);
    std::atomic_int semaphore{0};
    std::mutex m;
    TaskQueue<8>::Handler<int> handler;
    {
        std::scoped_lock<std::mutex> sc_lk(m);
        auto func = [&m, &semaphore] {
            semaphore.store(1);
            std::scoped_lock<std::mutex> sc_lock(m);
            return 13;
        };
        handler = queue.enqueue(func, 0);
        while (semaphore.load() == 0) {} // wait until worker thread takes our task

        auto handler1 = queue.enqueue([] { return 42; }, 0);
        auto res1 = queue.execute(handler1); // only we can execute. worker thread is now waiting for mutex
        ASSERT_EQ(res1, 42);
    }
    auto res = queue.execute(handler);
    ASSERT_EQ(res, 13);
}

namespace {
    void add(TaskQueue<8> &queue) {
        std::vector<TaskQueue<8>::Handler<int>> handlers;
        size_t task_number = 10000;
        handlers.reserve(task_number);
        for (size_t i = 0; i < task_number; ++i) {
            handlers.push_back(queue.enqueue([]() { return 4; }, rand() % 8));
        }
        for (auto &elem : handlers) {
            ASSERT_EQ(queue.execute(elem), 4);
        }
    }
}

TEST(thread_pool, multiple_tasks) {
    TaskQueue<8> queue(4);
    std::thread th1(add, std::ref(queue));
    std::thread th2(add, std::ref(queue));
    std::thread th3(add, std::ref(queue));
    std::thread th4(add, std::ref(queue));
    th1.join();
    th2.join();
    th3.join();
    th4.join();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}