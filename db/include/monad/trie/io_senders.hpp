#pragma once

#include <monad/trie/io.hpp>

#include <cstddef>
#include <span>

MONAD_TRIE_NAMESPACE_BEGIN

class read_single_buffer_sender
{
public:
    using buffer_type = std::span<std::byte>;
    using const_buffer_type = std::span<const std::byte>;
    using result_type = result<const_buffer_type>;

private:
    file_offset_t _offset;
    buffer_type _buffer;

public:
    constexpr read_single_buffer_sender(
        file_offset_t offset, buffer_type buffer)
        : _offset(offset)
        , _buffer(buffer)
    {
    }
    constexpr file_offset_t offset() const noexcept
    {
        return _offset;
    }
    constexpr buffer_type buffer() const noexcept
    {
        return _buffer;
    }
    void reset(file_offset_t offset, buffer_type buffer)
    {
        _offset = offset;
        _buffer = buffer;
    }
    result<void> operator()(erased_connected_operation *io_state) noexcept
    {
        // TODO: Handle failures to submit, because one can temporarily
        // fail to submit if the ring is full.
        io_state->executor().submit_read_request(_buffer, _offset, io_state);
        return success();
    }
    result_type completed(
        erased_connected_operation *,
        result<size_t> bytes_transferred) const noexcept
    {
        BOOST_OUTCOME_TRY(auto &&count, std::move(bytes_transferred));
        return {_buffer.data(), count};
    }
};
static_assert(sizeof(read_single_buffer_sender) == 24);
static_assert(alignof(read_single_buffer_sender) == 8);

MONAD_TRIE_NAMESPACE_END
