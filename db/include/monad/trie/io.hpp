#pragma once

#include <monad/trie/util.hpp>

#include <monad/io/buffer_pool.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <monad/trie/node_helper.hpp>

#include <boost/pool/object_pool.hpp>
#include <cstddef>
#include <functional>

MONAD_TRIE_NAMESPACE_BEGIN

enum class uring_data_type_t : unsigned char
{
    IS_WRITE = 0,
    IS_READ
};

// helper struct that records IO stats
struct IORecord
{
    unsigned inflight;
    unsigned inflight_rd;
    unsigned nreads;

    IORecord()
        : inflight(0)
        , inflight_rd(0)
        , nreads(0)
    {
    }
};

class AsyncIO final
{
    // TODO: using user_data_t = variant<update_data_t, write_data_t>
    struct write_uring_data_t
    {
        uring_data_type_t rw_flag;
        char pad[7];
        unsigned char *buffer;
        static inline boost::object_pool<write_uring_data_t> pool{};
    };

    static_assert(sizeof(write_uring_data_t) == 16);
    static_assert(alignof(write_uring_data_t) == 8);

    monad::io::Ring &uring_;
    monad::io::Buffers &rwbuf_;
    monad::io::BufferPool rd_pool_;
    monad::io::BufferPool wr_pool_;
    std::function<void(void *, AsyncIO &)> readcb_;

    unsigned char *write_buffer_;
    size_t buffer_idx_;
    uint64_t block_off_;

    // IO records
    IORecord records_;

    void submit_request(
        unsigned char *const buffer, unsigned int nbytes,
        unsigned long long offset, void *const uring_data, bool is_write);

    void submit_write_request(unsigned char *buffer, int64_t const offset);

    void poll_uring();

public:
    AsyncIO(
        monad::io::Ring &ring, monad::io::Buffers &rwbuf, uint64_t block_off,
        std::function<void(void *, AsyncIO &)> readcb)
        : uring_(ring)
        , rwbuf_(rwbuf)
        , rd_pool_(monad::io::BufferPool(rwbuf, true))
        , wr_pool_(monad::io::BufferPool(rwbuf, false))
        , readcb_(readcb)
        , write_buffer_(wr_pool_.alloc())
        , buffer_idx_(0)
        , block_off_(round_up_4k(block_off))
    {
        MONAD_ASSERT(write_buffer_);
    }

    // TODO: unregister uring file
    ~AsyncIO()
    {
        // handle the last buffer to write
        if (buffer_idx_ > 1) {
            submit_write_request(write_buffer_, block_off_);
        }
        else {
            wr_pool_.release(write_buffer_);
        }
        while (records_.inflight) {
            poll_uring();
        }
        MONAD_ASSERT(!records_.inflight);
        MONAD_ASSERT(!io_uring_unregister_files(
            const_cast<io_uring *>(&uring_.get_ring())));
    }

    [[gnu::always_inline]] void release_read_buffer(unsigned char *const buffer)
    {
        rd_pool_.release(buffer);
    };

    void uring_register_files(int const *fds, unsigned nr_files)
    {
        MONAD_ASSERT(!io_uring_register_files(
            const_cast<io_uring *>(&uring_.get_ring()), fds, nr_files));
    }

    int64_t async_write_node(merkle_node_t *node);

    // invoke at the end of each block
    int64_t flush(merkle_node_t *root)
    {
        while (records_.inflight) {
            poll_uring();
        }
        int64_t root_off = async_write_node(root);
        // pending root write, will submit or poll in next round

        records_.nreads = 0;
        return root_off;
    }

    template <typename TReadData>
    void async_read_request(TReadData *uring_data)
    {
        // get io_uring sqe, if no available entry, wait on poll() to reap some
        while (records_.inflight >= uring_.get_sq_entries()) {
            poll_uring();
        }

        bool across_blocks =
            uring_data->buffer_off + MAX_DISK_NODE_SIZE > PAGE_SIZE;
        unsigned read_size = across_blocks ? 2 * PAGE_SIZE : PAGE_SIZE;

        unsigned char *rd_buffer = rd_pool_.alloc();
        MONAD_ASSERT(rd_buffer);

        uring_data->buffer = rd_buffer;

        submit_request(
            rd_buffer, read_size, uring_data->offset, uring_data, false);
        ++records_.inflight;
        ++records_.inflight_rd;
        ++records_.nreads;
    }
};

static_assert(sizeof(AsyncIO) == 104);
static_assert(alignof(AsyncIO) == 8);

MONAD_TRIE_NAMESPACE_END
