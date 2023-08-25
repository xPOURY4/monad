#pragma once

#include <monad/async/io.hpp>

#include <chrono>
#include <cstddef>
#include <span>

MONAD_ASYNC_NAMESPACE_BEGIN

/*! \class read_single_buffer_sender
\brief A Sender which (possibly partially) fills a single buffer of bytes read
from an offset in a file.
*/
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
        io_state->executor()->submit_read_request(_buffer, _offset, io_state);
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
static_assert(sender<read_single_buffer_sender>);

/*! \class write_single_buffer_sender
\brief A Sender which (possibly partially) writes a single buffer of bytes into
an offset in a file.
*/
class write_single_buffer_sender
{
public:
    using buffer_type = std::span<const std::byte>;
    using const_buffer_type = std::span<const std::byte>;
    using result_type = result<const_buffer_type>;

private:
    file_offset_t _offset;
    buffer_type _buffer;
    std::byte *_append;

public:
    explicit constexpr write_single_buffer_sender(
        file_offset_t offset, buffer_type buffer)
        : _offset(offset)
        , _buffer(buffer)
        , _append(const_cast<std::byte *>(buffer.data()))
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
        _append = const_cast<std::byte *>(buffer.data());
    }
    result<void> operator()(erased_connected_operation *io_state) noexcept
    {
        // TODO: Handle failures to submit, because one can temporarily
        // fail to submit if the ring is full.
        _buffer = {_buffer.data(), _append};
        io_state->executor()->submit_write_request(_buffer, _offset, io_state);
        return success();
    }
    result_type completed(
        erased_connected_operation *,
        result<size_t> bytes_transferred) const noexcept
    {
        if (!bytes_transferred) {
            fprintf(
                stderr,
                "ERROR: Write of %zu bytes to offset %llu failed with error "
                "'%s'\n",
                buffer().size(),
                offset(),
                bytes_transferred.assume_error().message().c_str());
        }
        BOOST_OUTCOME_TRY(auto &&count, std::move(bytes_transferred));
        return {_buffer.data(), count};
    }
    constexpr size_t written_buffer_bytes() const noexcept
    {
        return _append - _buffer.data();
    }
    constexpr size_t remaining_buffer_bytes() const noexcept
    {
        return _buffer.data() + _buffer.size() - _append;
    }
    constexpr std::byte *advance_buffer_append(size_t bytes) noexcept
    {
        if (bytes > remaining_buffer_bytes()) {
            return nullptr;
        }
        auto *ret = _append;
        _append += bytes;
        return ret;
    }
};
static_assert(sizeof(write_single_buffer_sender) == 32);
static_assert(alignof(write_single_buffer_sender) == 8);
static_assert(sender<write_single_buffer_sender>);

/*! \class timed_delay_sender
\brief A Sender which completes after a delay. The delay can be measured by
system clock or by monotonic clock. The delay can be absolute or relative to
now.
*/
class timed_delay_sender
{
    AsyncIO::timed_invocation_state _state;

    template <class Rep, class Period>
    static __kernel_timespec
    _to_timespec(std::chrono::duration<Rep, Period> rel)
    {
        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(rel).count();
        return __kernel_timespec{ns / 1000000000, ns % 1000000000};
    }
    static __kernel_timespec
    _to_timespec(std::chrono::steady_clock::time_point dle)
    {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            dle.time_since_epoch())
                            .count();
        return __kernel_timespec{ns / 1000000000, ns % 1000000000};
    }
    static __kernel_timespec
    _to_timespec(std::chrono::system_clock::time_point dle)
    {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            dle.time_since_epoch())
                            .count();
        return __kernel_timespec{ns / 1000000000, ns % 1000000000};
    }

public:
    using result_type = result<void>;

public:
    //! Complete after the specified delay from now. WARNING: Uses a monotonic
    //! clock NOT invariant to sleep!
    template <class Rep, class Period>
    explicit constexpr timed_delay_sender(
        std::chrono::duration<Rep, Period> rel)
        : _state{
              .ts = _to_timespec(rel),
              .timespec_is_absolute = false,
              .timespec_is_utc_clock = false}
    {
    }
    //! Complete when this future point in time passes (monotonic clock
    //! invariant to system sleep)
    explicit constexpr timed_delay_sender(
        std::chrono::steady_clock::time_point dle)
        : _state{
              .ts = _to_timespec(dle),
              .timespec_is_absolute = true,
              .timespec_is_utc_clock = false}
    {
    }
    //! Complete when this future point in time passes (UTC date time clock)
    explicit constexpr timed_delay_sender(
        std::chrono::system_clock::time_point dle)
        : _state{
              .ts = _to_timespec(dle),
              .timespec_is_absolute = true,
              .timespec_is_utc_clock = true}
    {
    }
    template <class Rep, class Period>
    void reset(std::chrono::duration<Rep, Period> rel)
    {
        _state.ts = _to_timespec(rel);
    }
    void reset(std::chrono::steady_clock::time_point dle)
    {
        _state.ts = _to_timespec(dle);
    }
    void reset(std::chrono::system_clock::time_point dle)
    {
        _state.ts = _to_timespec(dle);
    }
    result<void> operator()(erased_connected_operation *io_state) noexcept
    {
        io_state->executor()->submit_timed_invocation_request(
            &_state, io_state);
        return success();
    }
    result_type
    completed(erased_connected_operation *, result<void> res) noexcept
    {
        // Ignore ETIME failures, which simply mean the timer fired
        if (!res &&
            res.assume_error() ==
                errc::stream_timeout /* This is a stupid name for ETIME */) {
            return success();
        }
        return res;
    }
};
static_assert(sizeof(timed_delay_sender) == 24);
static_assert(alignof(timed_delay_sender) == 8);
static_assert(sender<timed_delay_sender>);

MONAD_ASYNC_NAMESPACE_END
