#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>

#include <monad/io/ring.hpp>
#include <monad/trie/config.hpp>
#include <monad/trie/merge.hpp>
#include <monad/trie/node_helper.hpp>
#include <monad/trie/tnode.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

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

    // static_assert(sizeof(write_uring_data_t) == 16);
    // static_assert(alignof(write_uring_data_t) == 8);

    monad::io::Ring &uring_;
    cpool_29_t *cpool_;
    std::function<void(void *, AsyncIO &)> readcb_;

    unsigned char *write_buffer_;
    size_t buffer_idx_;
    size_t block_off_;

    // IO records
    IORecord records_;

    static const int WRITE_BUFFER_SIZE = 64 * 1024;
    static const int ALIGNMENT = 512;
    static const int READ_BUFFER_SIZE = 2048;

    unsigned char *get_avail_buffer(size_t size)
    {
        return (unsigned char *)aligned_alloc(ALIGNMENT, size);
    }

    // handle the last buffer to write
    void flush_last_buffer()
    {
        if (buffer_idx_ > 1) {
            // TODO: can continue write to the same buffer even for a new block
            // update bookkeeping block on the version and root off
            async_write_request(write_buffer_, block_off_);
            block_off_ += WRITE_BUFFER_SIZE;
        }
        else {
            free(write_buffer_);
        }
        write_buffer_ = nullptr;
    }

    void write_callback(write_uring_data_t *data) { free(data->buffer); }

public:
    AsyncIO(
        monad::io::Ring &ring, off_t block_off, cpool_29_t *cpool,
        std::function<void(void *, AsyncIO &)> readcb)
        : uring_(ring)
        , cpool_(cpool)
        , readcb_(readcb)
        , write_buffer_(nullptr)
        , buffer_idx_(0)
        , block_off_(block_off)
    {
    }

    ~AsyncIO() { clearup(); }

    void clearup();

    // use it in the beginning of each block
    void prepare_data_block()
    {
        write_buffer_ = get_avail_buffer(WRITE_BUFFER_SIZE);
        *write_buffer_ = BLOCK_TYPE_DATA;
        buffer_idx_ = 1;
    }

    // use it at the end of each block
    void flush()
    {
        records_.nreads_ = 0;
        flush_last_buffer();
    }

    [[nodiscard]] [[gnu::always_inline]] IORecord &get_records()
    {
        return records_;
    }

    void submit_request(
        unsigned char *const buffer, unsigned int nbytes,
        unsigned long long offset, void *const uring_data, bool is_write);

    [[nodiscard]] int64_t async_write_node(merkle_node_t *node);

    void async_write_request(unsigned char *buffer, int64_t const offset);

    template <typename TReadData>
    void async_read_request(TReadData *uring_data)
    {
        // get io_uring sqe, if no available entry, wait on poll() to reap some
        while (records_.inflight_ >= uring_.get_sq_entries()) {
            poll_uring();
        }

        unsigned char *rd_buffer = get_avail_buffer(READ_BUFFER_SIZE);

        uring_data->buffer = rd_buffer;

        submit_request(
            rd_buffer, READ_BUFFER_SIZE, uring_data->offset, uring_data, false);
        ++records_.inflight_;
        ++records_.inflight_rd_;
        ++records_.nreads_;
    }

    // [[gnu::always_inline]] void print_inflight_info() {}

    void poll_uring()
    {
        struct io_uring_cqe *cqe;
        int ret =
            io_uring_wait_cqe(const_cast<io_uring *>(&uring_.get_ring()), &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe fail: %s\n", strerror(-ret));
            fflush(stderr);
            exit(1);
        }
        if (cqe->res < 0) {
            /* The system call invoked asynchonously failed */
            fprintf(stderr, "async syscall failed: %s\n", strerror(-cqe->res));
            // TODO: resubmit the request
            exit(1);
        }
        // MONAD_TRIE_ASSERT(!ret);
        // MONAD_TRIE_ASSERT(!cqe->res);

        void *data = io_uring_cqe_get_data(cqe);
        MONAD_TRIE_ASSERT(data);
        io_uring_cqe_seen(const_cast<io_uring *>(&uring_.get_ring()), cqe);
        --records_.inflight_;

        if (((write_uring_data_t *)data)->rw_flag ==
            uring_data_type_t::IS_WRITE) {
            write_callback((write_uring_data_t *)data);
        }
        else {
            --records_.inflight_rd_;
            readcb_(data, *this);
        }
    }
};

MONAD_TRIE_NAMESPACE_END
