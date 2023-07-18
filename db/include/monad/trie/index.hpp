#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

#include <monad/core/assert.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>

#include <fcntl.h>
#include <sys/mman.h>

MONAD_TRIE_NAMESPACE_BEGIN

struct block_trie_info_t
{
    uint64_t vid;
    file_offset_t root_off;
};

static_assert(sizeof(block_trie_info_t) == 16);
static_assert(alignof(block_trie_info_t) == 8);

template <
    unsigned SLOTS = 3600 * 4, unsigned CPU_PAGE_BITS_TO_USE = CPU_PAGE_BITS>
class Index
{
    int fd_;
    unsigned block_start_off_;
    unsigned char *header_block_;
    unsigned char *mmap_block_;

    constexpr static size_t CPU_PAGE_SIZE = (1UL << CPU_PAGE_BITS_TO_USE);

    unsigned char *_memmap(file_offset_t offset)
    {
        // Trap unintentional use of high bit offsets
        MONAD_ASSERT(offset <= (file_offset_t(1) << 48));
        void *buffer = mmap(
            nullptr,
            CPU_PAGE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd_,
            off_t(offset));
        MONAD_ASSERT(buffer != MAP_FAILED);

        return reinterpret_cast<unsigned char *>(buffer);
    }

    constexpr unsigned _get_record_off(uint64_t vid) noexcept
    {
        return sizeof(block_trie_info_t) +
               (vid % SLOTS) * sizeof(block_trie_info_t);
    }

public:
    Index(std::filesystem::path &p)
        : fd_([&] {
            int flag = O_CREAT | O_RDWR;
            int fd = open(p.c_str(), flag, 0777);
            MONAD_ASSERT(fd != -1);
            return fd;
        }())
        , block_start_off_(0)
        , header_block_(_memmap(0))
        , mmap_block_(nullptr)
    {
        // resize file to max index chunk to avoid BusError
        // skip block device
        if (!std::filesystem::is_block_file(p)) {
            [&](size_t const new_file_size) {
                if (std::filesystem::file_size(p) < new_file_size) {
                    std::filesystem::resize_file(p, new_file_size);
                }
            }(get_start_offset());
        }
    }

    ~Index()
    {
        if (mmap_block_) {
            MONAD_ASSERT(!munmap(mmap_block_, CPU_PAGE_SIZE));
        }
        MONAD_ASSERT(!munmap(header_block_, CPU_PAGE_SIZE));

        close(fd_);
        fd_ = -1;
    }

    constexpr int get_rw_fd() noexcept
    {
        return fd_;
    }

    constexpr unsigned get_num_slots() noexcept
    {
        return SLOTS;
    }

    constexpr size_t get_start_offset()
    {
        // default cpu page aligned start block
        return round_up_align<CPU_PAGE_BITS_TO_USE>(
            sizeof(block_trie_info_t) * SLOTS);
    }

    std::optional<file_offset_t> get_history_root_off(uint64_t vid)
    {
        unsigned record_off = _get_record_off(vid);
        // 512 aligned blocking read from fd
        unsigned offset = round_down_align<DISK_PAGE_BITS>(record_off);
        auto buffer = std::make_unique<unsigned char[]>(1UL << 9);
        MONAD_ASSERT(pread(fd_, buffer.get(), 1UL << 9, offset) != -1);

        auto info = reinterpret_cast<block_trie_info_t *>(
            buffer.get() + record_off - offset);
        if (info->vid != vid) {
            return {};
        }
        return info->root_off;
    }

    void write_record(uint64_t vid, uint64_t root_off)
    {
        unsigned record_off = _get_record_off(vid);

        auto new_block_start_off =
            round_down_align<CPU_PAGE_BITS_TO_USE>(record_off);
        if (new_block_start_off != block_start_off_) {
            // map mmap_block_ to a new one
            if (mmap_block_) {
                MONAD_ASSERT(block_start_off_);
                MONAD_ASSERT(!munmap(mmap_block_, PAGE_SIZE));
                mmap_block_ = nullptr;
            }
            block_start_off_ = new_block_start_off;
            if (block_start_off_) {
                mmap_block_ = _memmap(block_start_off_);
            }
        }
        // update vid record slot
        unsigned char *write_block = mmap_block_ ? mmap_block_ : header_block_;
        *reinterpret_cast<block_trie_info_t *>(
            write_block + record_off - block_start_off_) =
            block_trie_info_t{vid, root_off};

        // update header vid
        *reinterpret_cast<uint64_t *>(header_block_) = vid;
        // tell the compiler and CPU to not reorder stores to the mapped file
        // after that function returns. kernel will handle dirty page write
        std::atomic_thread_fence(std::memory_order_release);
    }
};

using index_t = Index<>;

static_assert(sizeof(index_t) == 24);
static_assert(alignof(index_t) == 8);

MONAD_TRIE_NAMESPACE_END