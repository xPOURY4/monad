#include <monad/fiber/priority_queue.hpp>

#include <monad/core/likely.h>
#include <monad/fiber/config.hpp>

#include <mutex>

MONAD_FIBER_NAMESPACE_BEGIN

bool PriorityQueue::empty() const
{
    std::unique_lock<std::mutex> const lock{mutex_};
    return queue_.empty();
}

context *PriorityQueue::pop()
{
    std::unique_lock<std::mutex> const lock{mutex_};
    if (MONAD_UNLIKELY(queue_.empty())) {
        return nullptr;
    }
    context *const ctx = queue_.top();
    queue_.pop();
    return ctx;
}

void PriorityQueue::push(context *const ctx)
{
    std::unique_lock<std::mutex> const lock{mutex_};
    queue_.push(ctx);
}

MONAD_FIBER_NAMESPACE_END
