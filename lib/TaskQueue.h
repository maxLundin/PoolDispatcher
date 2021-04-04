//
// Created by maxlundin on 4/2/21.
//
#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <future>
#include <list>
#include <memory>
#include <mutex>

#include "PoolDispatcher.h"
#include "QueueDistribution.h"

template<size_t queue_size = 8>
class TaskQueue {

    static_assert(queue_size >= 1);

    struct TaskBase {
    protected:
        std::shared_ptr<std::atomic_bool> task_taken_promise;
        size_t priority;

        explicit TaskBase(size_t priority) : priority(priority),
                                             task_taken_promise(std::make_shared<std::atomic_bool>(false)) {}

    public:
        size_t get_priority() {
            return priority;
        }

        std::shared_ptr<std::atomic_bool> get_task_taken_future() {
            return task_taken_promise;
        }

        void start() {
            task_taken_promise->store(true, std::memory_order_acquire);
        }

        virtual void execute() = 0;

        virtual ~TaskBase() = default;
    };

    template<typename ReturnType>
    struct Task final : TaskBase {

        Task(std::function<ReturnType()> function, size_t priority) : TaskBase(priority),
                                                                      function(std::move(function)) {}

        Task(Task const &) = delete;

        Task(Task &&) noexcept = default;

        std::future<ReturnType> get_res_future() {
            return res_promise.get_future();
        }

        void execute() final {
            if constexpr (std::is_same_v<ReturnType, void>) {
                try {
                    function();
                    res_promise.set_value();
                } catch (...) {
                    res_promise.set_value();
                    throw;
                }
            } else {
                try {
                    res_promise.set_value(function());
                } catch (...) {
                    try {
                        res_promise.set_exception(std::current_exception());
                    } catch (...) {
                        std::cerr << "Promise thrown" << std::endl;
                        exit(1);
                    }
                }
            }
        }

    private:
        std::function<ReturnType()> function;
        std::promise<ReturnType> res_promise;

        friend class TaskQueue;
    };

    using internal_queue_t = std::list<std::unique_ptr<TaskBase>>;

    struct HandlerBase {
    protected:

        HandlerBase() = default;

        std::shared_ptr<std::atomic_bool> task_taken_future;
        size_t priority{};

        explicit HandlerBase(size_t priority,
                             std::shared_ptr<std::atomic_bool> task_taken) : priority(priority),
                                                                             task_taken_future(std::move(task_taken)) {}

    public:

        HandlerBase(HandlerBase &&other) noexcept : task_taken_future(std::move(other.task_taken_future)),
                                           priority(other.priority) {}

        HandlerBase &operator=(HandlerBase &&other) noexcept = default;

        [[nodiscard]] size_t get_priority() const {
            return priority;
        }

        [[nodiscard]] bool started() const {
            return task_taken_future->load();
        }

        virtual ~HandlerBase() = default;
    };

    static void sleep() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    static void thread_run(TaskQueue &task_queue) {
        if (task_queue.total_elements.load() == 0) {
            sleep();
            return;
        }
        size_t priority = QueueDistribution<queue_size>::generate();

        while ((*task_queue.sizes[priority]).load() == 0 && task_queue.total_elements.load() != 0) {
            priority++;
            priority %= queue_size;
        }

        if (task_queue.total_elements.load() == 0) {
            sleep();
            return;
        }

        if ((*task_queue.sizes[priority]) != 0) {
            std::unique_ptr<TaskBase> elem;
            {
                std::scoped_lock<std::mutex> queue_lock(task_queue.queues_mutexes[priority]);
                if (*task_queue.sizes[priority] == 0) {
                    return;
                }
                elem = std::move(task_queue.queues[priority].front());
                task_queue.queues[priority].pop_front();
                (*task_queue.sizes[priority])--;
                task_queue.total_elements--;
                elem->start();
            }
            elem->execute();
        } else {
            sleep();
        }
    }

    constexpr static std::array<std::unique_ptr<std::atomic_size_t>, queue_size> gen_counters() {
        std::array<std::unique_ptr<std::atomic_size_t>, queue_size> sizes_proto;
        for (auto &elem: sizes_proto) {
            elem = std::make_unique<std::atomic_size_t>(0);
        }
        return sizes_proto;
    }

public:
    template<typename ReturnType>
    struct Handler final : HandlerBase {

        Handler() : expired(true) {
        }

        explicit Handler(Task<ReturnType> &task) : HandlerBase(task.get_priority(), task.get_task_taken_future()),
                                                   res_future(task.get_res_future()) {}

        Handler(Handler const &) = delete;

        Handler(Handler &&) noexcept = default;

        Handler &operator=(Handler &&other) noexcept = default;

        ReturnType get_result() {
            return res_future.get();
        }

    private:
        std::future<ReturnType> res_future;
        typename internal_queue_t::iterator iterator;
        bool expired{false};

        friend class TaskQueue;
    };

    template<typename Function>
    auto enqueue(Function function, size_t priority) {
        using ReturnType = decltype(function());
        assert(priority < queue_size);
        auto task = std::unique_ptr<TaskBase>(new Task<ReturnType>(function, priority));
        Handler<ReturnType> handler(*((Task<ReturnType> *) task.get()));
        {
            std::scoped_lock<std::mutex> queue_lock(queues_mutexes[priority]);
            queues[priority].push_back(std::move(task));
            (*sizes[priority])++;
            handler.iterator = queues[priority].end();
            handler.iterator--;
            total_elements++;
        }
        return std::move(handler);
    }

    template<typename ReturnType>
    ReturnType execute(Handler<ReturnType> &handler) {
        if (handler.expired) {
            throw std::runtime_error("Execute on expired handler");
        }
        handler.expired = true;
        size_t priority = handler.get_priority();
        if (handler.started()) {
            return handler.get_result();
        }

        std::unique_ptr<TaskBase> task;
        {
            std::scoped_lock<std::mutex> queue_lock(queues_mutexes[priority]);
            if (!handler.started()) {
                task = std::move(*handler.iterator);
                queues[priority].erase(handler.iterator);
                (*sizes[priority])--;
                task->start();
                total_elements--;
            } else {
                return handler.get_result();
            }
        }
        return ((Task<ReturnType> *) task.get())->function();
    }

    explicit TaskQueue(uint32_t thread_count) : sizes(gen_counters()),
                                                pool([this]() { thread_run(*this); }, thread_count) {
    }

    TaskQueue(TaskQueue const &) = delete;

    TaskQueue(TaskQueue &&) noexcept = default;

private:
    std::atomic_size_t total_elements{};
    std::array<internal_queue_t, queue_size> queues;
    std::array<std::unique_ptr<std::atomic_size_t>, queue_size> sizes;
    std::array<std::mutex, queue_size> queues_mutexes;
    PoolDispatcher pool;
};