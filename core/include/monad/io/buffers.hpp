#pragma once

#include <monad/io/config.hpp>

#include <monad/core/assert.h>
#include <monad/mem/huge_mem.hpp>

#include <cstddef>
#include <cstdint>

MONAD_IO_NAMESPACE_BEGIN

class Ring;

class Buffers final
{
    Ring &ring_;
    size_t const read_bits_;
    size_t const write_bits_;
    HugeMem const read_buf_;
    HugeMem const write_buf_;
    size_t const read_count_;
    size_t const write_count_;

public:
    Buffers(
        Ring &, size_t read_count, size_t write_count,
        size_t read_size = 1UL << 12, size_t write_size = 1UL << 16);

    ~Buffers();

    [[gnu::always_inline]] size_t get_read_count() const
    {
        return read_count_;
    }

    [[gnu::always_inline]] size_t get_write_count() const
    {
        return write_count_;
    }

    [[gnu::always_inline]] size_t get_read_size() const
    {
        return 1UL << read_bits_;
    }

    [[gnu::always_inline]] size_t get_write_size() const
    {
        return 1UL << write_bits_;
    }

    [[gnu::always_inline]] static constexpr uint16_t get_read_index()
    {
        return 0;
    }

    [[gnu::always_inline]] static constexpr uint16_t get_write_index()
    {
        return 1;
    }

    [[gnu::always_inline]] unsigned char *get_read_buffer(size_t const i) const
    {
        MONAD_DEBUG_ASSERT(i < read_count_);
        unsigned char *const ret = read_buf_.get_data() + (i << read_bits_);
        MONAD_DEBUG_ASSERT(((void)ret[0], true));
        return ret;
    }

    [[gnu::always_inline]] unsigned char *get_write_buffer(size_t const i) const
    {
        MONAD_DEBUG_ASSERT(i < write_count_);
        unsigned char *const ret = write_buf_.get_data() + (i << write_bits_);
        MONAD_DEBUG_ASSERT(((void)ret[0], true));
        return ret;
    }
};

MONAD_IO_NAMESPACE_END
