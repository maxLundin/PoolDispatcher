//
// Created by maxlundin on 4/2/21.
//
#pragma once

#include "PoolDispatcher.h"
#include "QueueDistribution.h"

#include <cassert>
#include <future>
#include <list>
#include <memory>
#include <mutex>

template<size_t queue_size = 8>
class TaskQueue {

    static_assert(queue_size >= 1);

    class TaskBase {
    protected:
        std::shared_ptr<std::atomic_bool> task_taken_promise;

        explicit TaskBase() : task_taken_promise(std::make_shared<std::atomic_bool>(false)) {}

    public:

        std::shared_ptr<std::atomic_bool> get_task_taken_future() {
            return task_taken_promise;
        }

        void set_start() {
            task_taken_promise->store(true, std::memory_order_release);
        }

        virtual void execute() = 0;

        virtual ~TaskBase() = default;
    };

    template<typename ReturnType>
    class Task final : TaskBase {
    public:
        Task(std::function<ReturnType()> function) : function(std::move(function)) {}

        Task(Task const &) = delete;

        Task &operator=(Task const &) = delete;

        Task(Task &&) noexcept = default;

        Task &operator=(Task &&) noexcept = default;

        std::future<ReturnType> get_res_future() {
            return res_promise.get_future();
        }

        void execute() final {
            if constexpr (std::is_same_v<ReturnType, void>) {
                try {
                    function();
                    res_promise.set_value();
                } catch (...) {
                    try {
                        res_promise.set_exception(std::current_exception());
                    } catch (...) {
                        std::cerr << "Promise thrown" << std::endl;
                        exit(1);
                    }
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

    static void sleep(uint8_t &sleep_count) {
        sleep_count = std::max(sleep_count + 1, 127);
        if (sleep_count > 10) {
            std::this_thread::sleep_for(std::chrono::microseconds(50 * sleep_count));
        }
    }

    static void thread_run(TaskQueue &task_queue, uint8_t &sleep_counter) {
        if (task_queue.total_elements.load() == 0) {
            sleep(sleep_counter);
            return;
        }
        size_t priority = QueueDistribution<queue_size>::generate();

        while ((*task_queue.sizes[priority]).load() == 0 && task_queue.total_elements.load() != 0) {
            priority++;
            priority %= queue_size;
        }

        if (task_queue.total_elements.load() == 0) {
            sleep(sleep_counter);
            return;
        }
        sleep_counter = 0;
        if ((*task_queue.sizes[priority]) != 0) {
            std::unique_ptr<TaskBase> elem;
            {
                std::scoped_lock<std::mutex> queue_lock(task_queue.queues_mutexes[priority]);
                if (*task_queue.sizes[priority] == 0) {
                    return;
                }
                elem = std::move(task_queue.queues[priority].back());
                task_queue.queues[priority].pop_back();
                (*task_queue.sizes[priority])--;
                elem->set_start();
            }
            task_queue.total_elements--;
            elem->execute();
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
    struct Handler {

        Handler() : expired(true) {
        }

        explicit Handler(Task<ReturnType> &task, uint32_t priority) : task_taken_future(task.get_task_taken_future()),
                                                                      priority(priority),
                                                                      res_future(task.get_res_future()) {}

        Handler(Handler const &) = delete;

        Handler(Handler &&) noexcept = default;

        Handler &operator=(Handler const &other) = delete;

        Handler &operator=(Handler &&other) noexcept = default;

        ReturnType get_result() {
            return res_future.get();
        }

        [[nodiscard]] size_t get_priority() const {
            return priority;
        }

        [[nodiscard]] bool is_started() const {
            return task_taken_future->load(std::memory_order_acquire);
        }

    private:
        typename internal_queue_t::iterator iterator;
        std::shared_ptr<std::atomic_bool> task_taken_future;
        size_t priority{};
        std::future<ReturnType> res_future;
        bool expired{false};


        friend class TaskQueue;
    };

private:

    template<typename ReturnType>
    static void check_expired(Handler<ReturnType> &handler, std::string const &function) {
        if (handler.expired)
            throw std::runtime_error("Operation on invalid handler: " + function);
    }

public:
    explicit TaskQueue(uint32_t thread_count) : sleep_counter(thread_count, 0), sizes(gen_counters()),
                                                pool([this](PoolDispatcher::thread_id_t t_id) {
                                                    thread_run(*this, sleep_counter[t_id]);
                                                }, thread_count) {
    }

    template<typename Function>
    auto enqueue(Function function, size_t priority = queue_size - 1) {
        using ReturnType = decltype(function());
        using Task_t = Task<ReturnType>;
        assert(priority < queue_size);
        auto task = std::unique_ptr<TaskBase>(new Task_t(std::move(function)));
        Handler<ReturnType> handler(static_cast<Task_t &>(*task), priority);
        {
            std::scoped_lock<std::mutex> queue_lock(queues_mutexes[priority]);
            queues[priority].push_front(std::move(task));
            handler.iterator = queues[priority].begin();
            (*sizes[priority])++;
        }
        total_elements++;
        return handler;
    }

    template<typename ReturnType>
    ReturnType execute(Handler<ReturnType> &handler) {
        check_expired(handler, "execute");
        handler.expired = true;
        size_t priority = handler.get_priority();
        if (handler.is_started()) {
            wait_result:
            return handler.get_result();
        }

        std::unique_ptr<TaskBase> task;
        {
            std::scoped_lock<std::mutex> queue_lock(queues_mutexes[priority]);
            if (handler.is_started()) {
                goto wait_result;
            }
            task = std::move(*handler.iterator);
            queues[priority].erase(handler.iterator);
            task->set_start();
            (*sizes[priority])--;
        }
        total_elements--;
        return ((Task<ReturnType> *) task.get())->function();
    }

    template<typename ReturnType>
    void set_priority(Handler<ReturnType> &handler, uint32_t new_priority) {
        check_expired(handler, "set_priority");
        size_t priority = handler.get_priority();
        if (handler.is_started() || priority == new_priority) {
            return;
        }
        {
            std::lock(queues_mutexes[priority], queues_mutexes[new_priority]);
            std::scoped_lock<std::mutex> queue_lock_from(std::adopt_lock, queues_mutexes[priority]);
            std::scoped_lock<std::mutex> queue_lock_to(std::adopt_lock, queues_mutexes[new_priority]);
            if (handler.is_started()) {
                return;
            }
            auto task = std::move(*handler.iterator);
            queues[priority].erase(handler.iterator);
            queues[new_priority].push_front(std::move(task));
            handler.iterator = queues[new_priority].begin();
            (*sizes[new_priority])++;
            (*sizes[priority])--;
        }
        handler.priority = new_priority;
    }

    template<typename ReturnType>
    void delete_task(Handler<ReturnType> &handler) {
        check_expired(handler, "delete_task");
        handler.expired = true;
        if (handler.is_started()) {
            return;
        }
        size_t priority = handler.get_priority();
        {
            std::scoped_lock<std::mutex> queue_lock(queues_mutexes[priority]);
            if (handler.is_started()) {
                return;
            }
            queues[priority].erase(handler.iterator);
            (*sizes[priority])--;
        }
        total_elements--;
    }

    bool empty() {
        return total_elements == 0;
    }

    TaskQueue(TaskQueue const &) = delete;

    TaskQueue(TaskQueue &&) noexcept = default;

private:
    std::atomic_size_t total_elements{};
    std::array<internal_queue_t, queue_size> queues;
    std::vector<uint8_t> sleep_counter;
    std::array<std::unique_ptr<std::atomic_size_t>, queue_size> sizes;
    std::array<std::mutex, queue_size> queues_mutexes;
    PoolDispatcher pool;
};