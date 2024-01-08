#pragma once

#include <monad/fiber/config.hpp>
#include <monad/fiber/priority_algorithm.hpp>
#include <monad/fiber/priority_properties.hpp>
#include <monad/fiber/priority_queue.hpp>
#include <monad/fiber/priority_task.hpp>

#include <boost/fiber/buffered_channel.hpp>
#include <boost/fiber/condition_variable.hpp>
#include <boost/fiber/mutex.hpp>
#include <boost/fiber/operations.hpp>
#include <boost/fiber/properties.hpp>

#include <mutex>
#include <thread>
#include <utility>

MONAD_FIBER_NAMESPACE_BEGIN

class PriorityPool final
{
    PriorityQueue queue_{};

    boost::fibers::mutex mutex_{};
    boost::fibers::condition_variable cv_{};
    std::vector<std::thread> threads_{};

    boost::fibers::buffered_channel<PriorityTask> channel_{1024};

    std::vector<boost::fibers::fiber> fibers_{};

public:
    PriorityPool(unsigned const n_threads, unsigned const n_fibers)
    {
        threads_.reserve(n_threads);
        for (unsigned i = 0; i < n_threads; ++i) {
            auto thread = std::thread([this] {
                boost::fibers::use_scheduling_algorithm<PriorityAlgorithm>(
                    queue_);
                std::unique_lock<boost::fibers::mutex> lock{mutex_};
                cv_.wait(lock);
            });
            threads_.push_back(std::move(thread));
        }

        fibers_.reserve(n_fibers);
        for (unsigned i = 0; i < n_fibers; ++i) {
            auto *const properties = new PriorityProperties{nullptr};
            boost::fibers::fiber fiber{
                static_cast<boost::fibers::fiber_properties *>(properties),
                [this, properties] {
                    PriorityTask task;
                    while (channel_.pop(task) ==
                           boost::fibers::channel_op_status::success) {
                        properties->set_priority(task.priority);
                        boost::this_fiber::yield();
                        task.task();
                        properties->set_priority(0);
                    }
                }};
            fibers_.push_back(std::move(fiber));
        }
    }

    ~PriorityPool()
    {
        channel_.close();

        while (fibers_.size()) {
            auto &fiber = fibers_.back();
            fiber.join();
            fibers_.pop_back();
        }

        cv_.notify_all();
        while (threads_.size()) {
            auto &thread = threads_.back();
            thread.join();
            threads_.pop_back();
        }
    }

    void submit(uint64_t const priority, std::function<void()> const task)
    {
        channel_.push({priority, task});
    }
};

MONAD_FIBER_NAMESPACE_END
