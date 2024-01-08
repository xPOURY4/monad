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
    PriorityPool(unsigned n_threads, unsigned n_fibers);

    ~PriorityPool();

    void submit(uint64_t const priority, std::function<void()> const task)
    {
        channel_.push({priority, task});
    }
};

MONAD_FIBER_NAMESPACE_END
