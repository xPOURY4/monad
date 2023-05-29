#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>

#include <monad/trie/config.hpp>

#include <monad/io/buffer_pool.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <monad/trie/node_helper.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

enum class uring_data_type_t : unsigned char
{
    IS_WRITE = 0,
    IS_READ
};

struct IORecord
{
    unsigned inflight_;
    unsigned inflight_rd_;
    unsigned nreads_;

    IORecord()
        : inflight_(0)
        , inflight_rd_(0)
        , nreads_(0)
    {
    }
};

class AsyncIO final
{

    struct write_uring_data_t
    {
        uring_data_type_t rw_flag;
        char pad[7];
        unsigned char *buffer;
    };

    static_assert(sizeof(write_uring_data_t) == 16);
    static_assert(alignof(write_uring_data_t) == 8);

    monad::io::Ring &uring_;
    monad::io::Buffers &rwbuf_;
    monad::io::BufferPool rd_pool_;
    monad::io::BufferPool wr_pool_;
    cpool_29_t *cpool_;
    std::function<void(void *, AsyncIO &)> readcb_;

    unsigned char *write_buffer_;
    size_t buffer_idx_;
    size_t block_off_;

    // IO records
    IORecord records_;

    [[gnu::always_inline]] void submit_request(
        unsigned char *const buffer, unsigned int nbytes,
        unsigned long long offset, void *const uring_data, bool is_write);

    void submit_write_request(unsigned char *buffer, int64_t const offset);

    void poll_uring();

public:
    AsyncIO(
        monad::io::Ring &ring, monad::io::Buffers &rwbuf, off_t block_off,
        cpool_29_t *cpool, std::function<void(void *, AsyncIO &)> readcb)
        : uring_(ring)
        , rwbuf_(rwbuf)
        , rd_pool_(monad::io::BufferPool(rwbuf, true))
        , wr_pool_(monad::io::BufferPool(rwbuf, false))
        , cpool_(cpool)
        , readcb_(readcb)
        , write_buffer_(wr_pool_.alloc())
        , block_off_(block_off)
    {
        *write_buffer_ = BLOCK_TYPE_DATA;
        buffer_idx_ = 1;
    }

    // TODO: unregister uring file
    ~AsyncIO()
    {
        // handle the last buffer to write
        if (buffer_idx_ > 1) {
            submit_write_request(write_buffer_, block_off_);
            block_off_ += rwbuf_.get_write_size();
            while (records_.inflight_) {
                poll_uring();
            }
        }
        else {
            wr_pool_.release(write_buffer_);
        }
        MONAD_TRIE_ASSERT(!records_.inflight_);
        MONAD_TRIE_ASSERT(
            rd_pool_.get_avail_count() == rwbuf_.get_read_count());
        MONAD_TRIE_ASSERT(
            wr_pool_.get_avail_count() == rwbuf_.get_write_count());
    }

    [[gnu::always_inline]] void release_read_buffer(unsigned char *const buffer)
    {
        rd_pool_.release(buffer);
    };

    void uring_register_files(int const *fds, unsigned nr_files)
    {
        MONAD_TRIE_ASSERT(!io_uring_register_files(
            const_cast<io_uring *>(&uring_.get_ring()), fds, nr_files));
    }

    // invoke at the end of each block
    void flush(merkle_node_t *root)
    {
        while (records_.inflight_) {
            poll_uring();
        }
        // TODO: root_off = async_write(). update bookkeeping record
        async_write_node(root);
        while (records_.inflight_) {
            poll_uring();
        }
        MONAD_TRIE_ASSERT(!records_.inflight_);
        MONAD_TRIE_ASSERT(
            rd_pool_.get_avail_count() == rwbuf_.get_read_count());
        records_.nreads_ = 0;
    }

    int64_t async_write_node(merkle_node_t *node);

    template <typename TReadData>
    void async_read_request(TReadData *uring_data)
    {
        // get io_uring sqe, if no available entry, wait on poll() to reap some
        while (records_.inflight_ >= uring_.get_sq_entries()) {
            poll_uring();
        }

        unsigned char *rd_buffer = rd_pool_.alloc();

        uring_data->buffer = rd_buffer;

        submit_request(
            rd_buffer,
            rwbuf_.get_read_size(),
            uring_data->offset,
            uring_data,
            false);
        ++records_.inflight_;
        ++records_.inflight_rd_;
        ++records_.nreads_;
    }
};

static_assert(sizeof(AsyncIO) == 128);
static_assert(alignof(AsyncIO) == 8);

MONAD_TRIE_NAMESPACE_END
