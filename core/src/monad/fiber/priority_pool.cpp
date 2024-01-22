#include <monad/fiber/priority_pool.hpp>

#include <boost/fiber/protected_fixedsize_stack.hpp>

#include <memory>
#include <mutex>
#include <thread>
#include <utility>

MONAD_FIBER_NAMESPACE_BEGIN

PriorityPool::PriorityPool(unsigned const n_threads, unsigned const n_fibers)
{
    threads_.reserve(n_threads);
    for (unsigned i = 0; i < n_threads; ++i) {
        auto thread = std::thread([this] {
            boost::fibers::use_scheduling_algorithm<PriorityAlgorithm>(queue_);
            std::unique_lock<boost::fibers::mutex> lock{mutex_};
            cv_.wait(lock, [this] { return done_; });
        });
        threads_.push_back(std::move(thread));
    }

    fibers_.reserve(n_fibers);
    auto thread = std::thread([this, n_fibers] {
        boost::fibers::use_scheduling_algorithm<PriorityAlgorithm>(queue_);
        for (unsigned i = 0; i < n_fibers; ++i) {
            auto *const properties = new PriorityProperties{nullptr};
            boost::fibers::fiber fiber{
                static_cast<boost::fibers::fiber_properties *>(properties),
                std::allocator_arg,
                boost::fibers::protected_fixedsize_stack{8 * 1024 * 1024},
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
    });
    thread.join();
}

PriorityPool::~PriorityPool()
{
    channel_.close();

    while (fibers_.size()) {
        auto &fiber = fibers_.back();
        fiber.join();
        fibers_.pop_back();
    }

    {
        std::unique_lock<boost::fibers::mutex> lock{mutex_};
        done_ = true;
    }

    cv_.notify_all();

    while (threads_.size()) {
        auto &thread = threads_.back();
        thread.join();
        threads_.pop_back();
    }
}

MONAD_FIBER_NAMESPACE_END
