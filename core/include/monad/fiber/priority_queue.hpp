#pragma once

#include <monad/core/assert.h>
#include <monad/core/likely.h>
#include <monad/fiber/config.hpp>
#include <monad/fiber/properties.hpp>

#include <boost/fiber/context.hpp>

#include <mutex>
#include <queue>
#include <vector>

MONAD_FIBER_NAMESPACE_BEGIN

using boost::fibers::context;

class PriorityQueue final
{
    struct Compare
    {
        static constexpr uint64_t get_priority(context const *const ctx)
        {
            auto const *const properties =
                static_cast<fiber_properties const *>(ctx->get_properties());
            MONAD_ASSERT(properties); // TODO debug assert
            return properties->getPriority();
        }

        constexpr bool operator()(context *const ctx1, context *const ctx2)
        {
            return get_priority(ctx1) > get_priority(ctx2);
        }
    };

    std::priority_queue<context *, std::vector<context *>, Compare>
        queue_{}; // TODO intrusive
    std::mutex mutex_{}; // TODO spinlock / concurrent

public:
    context *pop();

    void push(context *);
};

MONAD_FIBER_NAMESPACE_END
