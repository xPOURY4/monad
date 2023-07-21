#pragma once

#include <monad/trie/util.hpp>

#include <monad/io/buffer_pool.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <monad/trie/allocators.hpp>
#include <monad/trie/node_helper.hpp>

#include <fcntl.h>

#include <cstddef>
#include <filesystem>
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
    unsigned inflight{0};
    unsigned inflight_rd{0};
    unsigned nreads{0};
};

class AsyncIO final
{
    constexpr static unsigned READ = 0, WRITE = 1;
    // TODO: using user_data_t = variant<update_data_t, write_data_t>
    struct write_uring_data_t
    {
        uring_data_type_t rw_flag;
        char pad[7];
        unsigned char *buffer;

        using allocator_type =
            boost_unordered_pool_allocator<write_uring_data_t>;
        static allocator_type &pool()
        {
            static allocator_type v;
            return v;
        }
        using unique_ptr_type = std::unique_ptr<
            write_uring_data_t, unique_ptr_allocator_deleter<
                                    allocator_type, &write_uring_data_t::pool>>;
        static unique_ptr_type make(write_uring_data_t v)
        {
            return allocate_unique<allocator_type, &write_uring_data_t::pool>(
                v);
        }
    };

    static_assert(sizeof(write_uring_data_t) == 16);
    static_assert(alignof(write_uring_data_t) == 8);

    int fds_[2];
    monad::io::Ring &uring_;
    monad::io::Buffers &rwbuf_;
    monad::io::BufferPool rd_pool_;
    monad::io::BufferPool wr_pool_;
    std::function<void(void *)> readcb_;

    unsigned char *write_buffer_;
    size_t buffer_idx_;
    file_offset_t block_off_;

    // IO records
    IORecord records_;

    void submit_request(
        unsigned char *const buffer, unsigned int nbytes, file_offset_t offset,
        void *uring_data, bool is_write);

    void submit_write_request(
        unsigned char *buffer, file_offset_t const offset, unsigned write_size);

    void poll_uring();

public:
    AsyncIO(
        std::filesystem::path &p, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf, file_offset_t block_off,
        std::function<void(void *)> readcb)
        : uring_(ring)
        , rwbuf_(rwbuf)
        , rd_pool_(monad::io::BufferPool(rwbuf, true))
        , wr_pool_(monad::io::BufferPool(rwbuf, false))
        , readcb_(readcb)
        , write_buffer_(wr_pool_.alloc())
        , buffer_idx_(0)
        , block_off_(round_up_align<DISK_PAGE_BITS>(block_off))
    {
        MONAD_ASSERT(write_buffer_);

        // append only file descriptor
        fds_[WRITE] = open(p.c_str(), O_CREAT | O_WRONLY | O_DIRECT, 0600);
        MONAD_ASSERT(fds_[WRITE] != -1);
        // read only file descriptor
        fds_[READ] = open(p.c_str(), O_RDONLY | O_DIRECT);
        MONAD_ASSERT(fds_[READ] != -1);

        // register files
        MONAD_ASSERT(!io_uring_register_files(
            const_cast<io_uring *>(&uring_.get_ring()), fds_, 2));
    }

    ~AsyncIO()
    {
        // handle the last buffer to write
        if (buffer_idx_ > 1) {
            submit_write_request(
                write_buffer_, block_off_, rwbuf_.get_write_size());
        }
        else {
            wr_pool_.release(write_buffer_);
        }
        // poll the last buffer submitted for write
        while (records_.inflight) {
            poll_uring();
        }
        MONAD_ASSERT(!records_.inflight);

        MONAD_ASSERT(!io_uring_unregister_files(
            const_cast<io_uring *>(&uring_.get_ring())));

        close(fds_[READ]);
        close(fds_[WRITE]);
    }

    void release_read_buffer(unsigned char *const buffer)
    {
        rd_pool_.release(buffer);
    };

    struct async_write_node_result
    {
        file_offset_t offset_written_to;
        unsigned bytes_appended;
    };
    async_write_node_result async_write_node(merkle_node_t const *const node);

    void flush_last_buffer()
    {
        // Write the last pending buffer for current block.
        // mainly useful for unit test purposes for now, where updates are not
        // enough to fill single buffer. So there's gap where node is
        // deallocated but not yet reaching disk for read.
        // Always cache the latest version(s) in memory will resolve this
        // problem nicely.
        if (buffer_idx_ > 1) {
            unsigned write_size = round_up_align<DISK_PAGE_BITS>(buffer_idx_);
            submit_write_request(write_buffer_, block_off_, write_size);
            write_buffer_ = wr_pool_.alloc();
            MONAD_ASSERT(write_buffer_);
            block_off_ += write_size;
            buffer_idx_ = 0;

            poll_uring();
            MONAD_ASSERT(records_.inflight == 0);
        }
    }
    // invoke at the end of each block
    async_write_node_result flush(merkle_node_t *root)
    {
        while (records_.inflight) {
            poll_uring();
        }
        // only write root to disk if trie is not empty
        // root write is pending, will submit or poll in next round
        auto root_off = root->valid_mask
                            ? async_write_node(root)
                            : async_write_node_result{INVALID_OFFSET, 0};

        MONAD_ASSERT(records_.inflight <= 1);

        records_.nreads = 0;
        return root_off;
    }

    template <unique_ptr TReadData>
    void async_read_request(TReadData uring_data)
    {
        // get io_uring sqe, if no available entry, wait on poll() to reap some
        while (records_.inflight >= uring_.get_sq_entries()) {
            poll_uring();
        }

        unsigned char *rd_buffer = rd_pool_.alloc();
        MONAD_ASSERT(rd_buffer);

        uring_data->buffer = rd_buffer;

        // We release the ownership of uring_data to io_uring. We reclaim
        // ownership after we reap the i/o completion.
        auto *uring_data_ = uring_data.release();
        submit_request(
            rd_buffer,
            uring_data_->bytes_to_read,
            uring_data_->offset,
            uring_data_,
            false);
        ++records_.inflight;
        ++records_.inflight_rd;
        ++records_.nreads;
    }

    constexpr int get_rd_fd() noexcept
    {
        return fds_[READ];
    }
};

static_assert(sizeof(AsyncIO) == 112);
static_assert(alignof(AsyncIO) == 8);

MONAD_TRIE_NAMESPACE_END
