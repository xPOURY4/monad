#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

#include <monad/io/buffer_pool.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <monad/trie/node_helper.hpp>

#include <cstddef>
#include <cstdlib>
#include <functional>

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
    uint64_t block_off_;

    // IO records
    IORecord records_;

    [[gnu::always_inline]] void submit_request(
        unsigned char *const buffer, unsigned int nbytes,
        unsigned long long offset, void *const uring_data, bool is_write);

    void submit_write_request(unsigned char *buffer, int64_t const offset);

    void poll_uring();

public:
    AsyncIO(
        monad::io::Ring &ring, monad::io::Buffers &rwbuf, uint64_t block_off,
        cpool_29_t *cpool, std::function<void(void *, AsyncIO &)> readcb)
        : uring_(ring)
        , rwbuf_(rwbuf)
        , rd_pool_(monad::io::BufferPool(rwbuf, true))
        , wr_pool_(monad::io::BufferPool(rwbuf, false))
        , cpool_(cpool)
        , readcb_(readcb)
        , write_buffer_(wr_pool_.alloc())
        , block_off_(high_4k_aligned(block_off))
    {
        MONAD_ASSERT(write_buffer_);
        *write_buffer_ = BLOCK_TYPE_DATA;
        buffer_idx_ = 1;
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
        while (records_.inflight_) {
            poll_uring();
        }
        MONAD_ASSERT(!records_.inflight_);
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

    [[nodiscard]] int64_t async_write_node(merkle_node_t *node);

    // invoke at the end of each block
    [[nodiscard]] int64_t flush(merkle_node_t *root)
    {
        while (records_.inflight_) {
            poll_uring();
        }
        int64_t root_off = async_write_node(root);
        // pending root write, will submit or poll in next round

        records_.nreads_ = 0;
        return root_off;
    }

    template <typename TReadData>
    void async_read_request(TReadData *uring_data)
    {
        // get io_uring sqe, if no available entry, wait on poll() to reap some
        while (records_.inflight_ >= uring_.get_sq_entries()) {
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
        ++records_.inflight_;
        ++records_.inflight_rd_;
        ++records_.nreads_;
    }
};

static_assert(sizeof(AsyncIO) == 112);
static_assert(alignof(AsyncIO) == 8);

MONAD_TRIE_NAMESPACE_END
