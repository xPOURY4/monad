#pragma once

#include <monad/async/io.hpp>

#include <chrono>
#include <cstddef>
#include <span>

MONAD_ASYNC_NAMESPACE_BEGIN

/*! \class filled_read_buffer
\brief A span denoting how much of a `AsyncIO::read_buffer_ptr` has been filled,
also holding lifetime to the i/o buffer.
*/
template <class T>
class filled_read_buffer : public std::span<T>
{
    using base_ = std::span<T>;
    AsyncIO::read_buffer_ptr buffer_;

public:
    constexpr filled_read_buffer() {}

    filled_read_buffer(filled_read_buffer const &) = delete;
    filled_read_buffer(filled_read_buffer &&) = default;
    filled_read_buffer &operator=(filled_read_buffer const &) = delete;
    filled_read_buffer &operator=(filled_read_buffer &&) = default;

    constexpr explicit filled_read_buffer(size_t bytes_to_read)
        : base_((T *)nullptr, bytes_to_read)
    {
    }

    //! True if read buffer has been allocated
    explicit operator bool() const noexcept
    {
        return !!buffer_;
    }

    //! Allocates the i/o buffer
    void set_read_buffer(AsyncIO::read_buffer_ptr b) noexcept
    {
        buffer_ = std::move(b);
        auto *self = static_cast<base_ *>(this);
        *self = {buffer_.get(), self->size()};
    }

    //! Sets the span length
    void set_bytes_transferred(size_t bytes) noexcept
    {
        auto *span = static_cast<base_ *>(this);
        *span = span->subspan(0, bytes);
    }

    //! Reset the filled read buffer, releasing its i/o buffer
    void reset()
    {
        this->~filled_read_buffer();
        new (this) filled_read_buffer;
    }
};

/*! \class read_single_buffer_sender
\brief A Sender which (possibly partially) fills a single buffer of bytes
read from an offset in a file.
*/
class read_single_buffer_sender
{
public:
    using buffer_type = filled_read_buffer<std::byte>;
    using const_buffer_type = filled_read_buffer<std::byte const>;
    using result_type = result<std::reference_wrapper<buffer_type>>;

    static constexpr operation_type my_operation_type = operation_type::read;

private:
    chunk_offset_t offset_;
    buffer_type buffer_;

public:
    constexpr read_single_buffer_sender(
        chunk_offset_t offset, size_t bytes_to_read)
        : offset_(offset)
        , buffer_(bytes_to_read)
    {
    }

    constexpr read_single_buffer_sender(
        chunk_offset_t offset, buffer_type buffer)
        : offset_(offset)
        , buffer_(std::move(buffer))
    {
    }

    constexpr chunk_offset_t offset() const noexcept
    {
        return offset_;
    }

    constexpr buffer_type const &buffer() const & noexcept
    {
        return buffer_;
    }

    constexpr buffer_type buffer() && noexcept
    {
        return std::move(buffer_);
    }

    void reset(chunk_offset_t offset, size_t bytes_to_read)
    {
        offset_ = offset;
        buffer_ = buffer_type(bytes_to_read);
    }

    void reset(chunk_offset_t offset, buffer_type buffer)
    {
        offset_ = offset;
        buffer_ = std::move(buffer);
    }

    result<void> operator()(erased_connected_operation *io_state) noexcept
    {
        if (!buffer_) {
            buffer_.set_read_buffer(
                io_state->executor()->get_read_buffer(buffer_.size()));
        }
        if (io_state->executor()->submit_read_request(
                buffer_, offset_, io_state)) {
            // It completed early
            return make_status_code(
                sender_errc::initiation_immediately_completed, buffer_.size());
        }
        return success();
    }

    result_type completed(
        erased_connected_operation *, result<size_t> bytes_transferred) noexcept
    {
        BOOST_OUTCOME_TRY(auto &&count, std::move(bytes_transferred));
        buffer_.set_bytes_transferred(count);
        return success(std::ref(buffer_));
    }
};

static_assert(sizeof(read_single_buffer_sender) == 40);
static_assert(alignof(read_single_buffer_sender) == 8);
static_assert(sender<read_single_buffer_sender>);

/*! \class filled_write_buffer
\brief Currently a wrapper of `std::span<T>` for consistency with
`filled_read_buffer`.
*/
template <class T>
class filled_write_buffer : public std::span<T>
{
    using base_ = std::span<T>;

public:
    constexpr filled_write_buffer() {}

    filled_write_buffer(filled_write_buffer const &) = delete;
    filled_write_buffer(filled_write_buffer &&) = default;
    filled_write_buffer &operator=(filled_write_buffer const &) = delete;
    filled_write_buffer &operator=(filled_write_buffer &&) = default;

    constexpr explicit filled_write_buffer(size_t bytes_to_write)
        : base_((T *)nullptr, bytes_to_write)
    {
    }

    constexpr explicit filled_write_buffer(std::span<T> buffer)
        : base_(buffer)
    {
    }

    constexpr filled_write_buffer(T *data, size_t len)
        : base_(data, len)
    {
    }

    //! True if write buffer has been allocated
    constexpr explicit operator bool() const noexcept
    {
        return true;
    }

    //! Sets the span length
    void set_bytes_transferred(size_t bytes) noexcept
    {
        auto *span = static_cast<base_ *>(this);
        *span = span->subspan(0, bytes);
    }

    //! Reset the filled write buffer
    void reset()
    {
        this->~filled_write_buffer();
        new (this) filled_write_buffer;
    }
};

/*! \class write_single_buffer_sender
\brief A Sender which (possibly partially) writes a single buffer of bytes
into an offset in a file.
*/
class write_single_buffer_sender
{
public:
    using buffer_type = filled_write_buffer<std::byte const>;
    using const_buffer_type = filled_write_buffer<std::byte const>;
    using result_type = result<std::reference_wrapper<buffer_type>>;

    static constexpr operation_type my_operation_type = operation_type::write;

private:
    chunk_offset_t offset_;
    buffer_type buffer_;
    std::byte *append_;

public:
    constexpr write_single_buffer_sender(
        chunk_offset_t offset, size_t bytes_to_write)
        : offset_(offset)
        , buffer_(bytes_to_write)
        , append_(const_cast<std::byte *>(buffer_.data()))
    {
    }

    constexpr write_single_buffer_sender(
        chunk_offset_t offset, buffer_type buffer)
        : offset_(offset)
        , buffer_(std::move(buffer))
        , append_(const_cast<std::byte *>(buffer.data()))
    {
    }

    constexpr chunk_offset_t offset() const noexcept
    {
        return offset_;
    }

    constexpr buffer_type const &buffer() const & noexcept
    {
        return buffer_;
    }

    constexpr buffer_type buffer() && noexcept
    {
        return std::move(buffer_);
    }

    void reset(chunk_offset_t offset, size_t bytes_to_write)
    {
        offset_ = offset;
        buffer_ = buffer_type(bytes_to_write);
        append_ = const_cast<std::byte *>(buffer_.data());
    }

    void reset(chunk_offset_t offset, buffer_type buffer)
    {
        offset_ = offset;
        buffer_ = std::move(buffer);
        append_ = const_cast<std::byte *>(buffer_.data());
    }

    result<void> operator()(erased_connected_operation *io_state) noexcept
    {
        buffer_.set_bytes_transferred(size_t(append_ - buffer_.data()));
        io_state->executor()->submit_write_request(buffer_, offset_, io_state);
        return success();
    }

    result_type completed(
        erased_connected_operation *, result<size_t> bytes_transferred) noexcept
    {
        if (!bytes_transferred) {
            fprintf(
                stderr,
                "ERROR: Write of %zu bytes to chunk %u offset %llu failed "
                "with "
                "error "
                "'%s'\n",
                buffer().size(),
                offset().id,
                file_offset_t(offset().offset),
                bytes_transferred.assume_error().message().c_str());
        }
        BOOST_OUTCOME_TRY(auto &&count, std::move(bytes_transferred));
        buffer_.set_bytes_transferred(count);
        return std::ref(buffer_);
    }

    constexpr size_t written_buffer_bytes() const noexcept
    {
        MONAD_DEBUG_ASSERT(buffer_.data() <= append_);
        return static_cast<size_t>(append_ - buffer_.data());
    }

    constexpr size_t remaining_buffer_bytes() const noexcept
    {
        auto const *end = buffer_.data() + buffer_.size();
        MONAD_DEBUG_ASSERT(end >= append_);
        return static_cast<size_t>(end - append_);
    }

    constexpr std::byte *advance_buffer_append(size_t bytes) noexcept
    {
        if (bytes > remaining_buffer_bytes()) {
            return nullptr;
        }
        auto *ret = append_;
        append_ += bytes;
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

```
Benchmarking timed_delay_sender with a non-zero timeout ...
   Did 1.45344e+06 completions per second
Benchmarking timed_delay_sender with a zero timeout ...
   Did 4.76564e+06 completions per second
```
*/
class timed_delay_sender
{
    AsyncIO::timed_invocation_state state_;

    template <class Rep, class Period>
    static __kernel_timespec
    to_timespec_(std::chrono::duration<Rep, Period> rel)
    {
        auto const ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(rel).count();
        return __kernel_timespec{ns / 1000000000, ns % 1000000000};
    }

    static __kernel_timespec
    to_timespec_(std::chrono::steady_clock::time_point dle)
    {
        auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            dle.time_since_epoch())
                            .count();
        return __kernel_timespec{ns / 1000000000, ns % 1000000000};
    }

    static __kernel_timespec
    to_timespec_(std::chrono::system_clock::time_point dle)
    {
        auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            dle.time_since_epoch())
                            .count();
        return __kernel_timespec{ns / 1000000000, ns % 1000000000};
    }

public:
    using result_type = result<void>;

    static constexpr operation_type my_operation_type = operation_type::timeout;

public:
    //! Complete after the specified delay from now. WARNING: Uses a
    //! monotonic clock NOT invariant to sleep!
    template <class Rep, class Period>
    explicit constexpr timed_delay_sender(
        std::chrono::duration<Rep, Period> rel)
        : state_{
              .ts = to_timespec_(rel),
              .timespec_is_absolute = false,
              .timespec_is_utc_clock = false}
    {
    }

    //! Complete when this future point in time passes (monotonic clock
    //! invariant to system sleep)
    explicit constexpr timed_delay_sender(
        std::chrono::steady_clock::time_point dle)
        : state_{
              .ts = to_timespec_(dle),
              .timespec_is_absolute = true,
              .timespec_is_utc_clock = false}
    {
    }

    //! Complete when this future point in time passes (UTC date time clock)
    explicit constexpr timed_delay_sender(
        std::chrono::system_clock::time_point dle)
        : state_{
              .ts = to_timespec_(dle),
              .timespec_is_absolute = true,
              .timespec_is_utc_clock = true}
    {
    }

    template <class Rep, class Period>
    void reset(std::chrono::duration<Rep, Period> rel)
    {
        state_.ts = to_timespec_(rel);
    }

    void reset(std::chrono::steady_clock::time_point dle)
    {
        state_.ts = to_timespec_(dle);
    }

    void reset(std::chrono::system_clock::time_point dle)
    {
        state_.ts = to_timespec_(dle);
    }

    result<void> operator()(erased_connected_operation *io_state) noexcept
    {
        io_state->executor()->submit_timed_invocation_request(
            &state_, io_state);
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

/*! \class threadsafe_sender
\brief A Sender which completes on the kernel thread executing an `AsyncIO`
instance, but which can be initiated thread safely from any other kernel
thread.

```
Benchmarking threadsafe_sender ...
   Did 1.5978e+06 completions per second
```
*/
class threadsafe_sender
{
public:
    using result_type = result<void>;

    static constexpr operation_type my_operation_type =
        operation_type::threadsafeop;

public:
    threadsafe_sender() = default;

    void reset() {}

    result<void> operator()(erased_connected_operation *io_state) noexcept
    {
        io_state->executor()->submit_threadsafe_invocation_request(io_state);
        return success();
    }
};

static_assert(sizeof(threadsafe_sender) == 1);
static_assert(alignof(threadsafe_sender) == 1);
static_assert(sender<threadsafe_sender>);

MONAD_ASYNC_NAMESPACE_END
