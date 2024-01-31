#pragma once

#include <monad/mpt/config.hpp>

#include <monad/async/io_senders.hpp>
#include <monad/mpt/node.hpp>

MONAD_MPT_NAMESPACE_BEGIN

namespace detail
{
    template <class ResultType>
    inline Node::UniquePtr deserialize_node_from_receiver_result(
        ResultType buffer_, uint16_t buffer_off,
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *io_state)
    {
        MONAD_ASSERT(buffer_);
        Node::UniquePtr node;
        if constexpr (std::is_same_v<
                          std::decay_t<ResultType>,
                          typename monad::async::read_single_buffer_sender::
                              result_type>) {
            auto &buffer = std::move(buffer_).assume_value().get();
            MONAD_ASSERT(buffer.size() > 0);
            node = deserialize_node_from_buffer(
                (unsigned char *)buffer.data() + buffer_off,
                buffer.size() - buffer_off);
            buffer.reset();
        }
        else if constexpr (std::is_same_v<
                               std::decay_t<ResultType>,
                               typename monad::async::
                                   read_multiple_buffer_sender::result_type>) {
            auto &buffer = buffer_.assume_value().front();
            MONAD_ASSERT(buffer.size() > 0);
            // Did the Receiver forget to set lifetime_managed_internally?
            MONAD_DEBUG_ASSERT(io_state->lifetime_is_managed_internally());
            node = deserialize_node_from_buffer(
                (unsigned char *)buffer.data() + buffer_off,
                buffer.size() - buffer_off);
        }
        else {
            static_assert(false);
        }
        return node;
    }
}

MONAD_MPT_NAMESPACE_END
