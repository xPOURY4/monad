#pragma once

#include <cstdlib>

#include <sys/mman.h>

#include <monad/trie/config.hpp>
#include <monad/trie/constants.hpp>
#include <monad/trie/util.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

struct block_trie_info
{
    uint64_t vid;
    uint64_t root_off;
};

static_assert(sizeof(block_trie_info) == 16);
static_assert(alignof(block_trie_info) == 8);

// use mmap()
class Index
{
    int fd_;
    unsigned block_start_off_;
    unsigned char *header_block_;
    unsigned char *mmap_block_;

    unsigned char *_memmap(off_t offset)
    {
        return reinterpret_cast<unsigned char *>(mmap(
            nullptr,
            PAGE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd_,
            offset));
    }

    [[nodiscard]] [[gnu::always_inline]] constexpr off_t
    _get_record_off(uint64_t vid)
    {
        return RECORD_SIZE + (vid % SLOTS) * RECORD_SIZE;
    }

public:
    Index(int fd)
        : fd_(fd)
        , block_start_off_(0)
        , header_block_(_memmap(0))
        , mmap_block_(nullptr)
    {
    }

    ~Index()
    {
        if (mmap_block_) {
            munmap(mmap_block_, PAGE_SIZE);
        }
        munmap(header_block_, PAGE_SIZE);
    }

    [[nodiscard]] [[gnu::always_inline]] constexpr size_t get_start_offset()
    {
        return BLOCK_START_OFF;
    }

    // call only once right after intialization if append
    [[nodiscard]] block_trie_info *get_trie_info(uint64_t vid)
    {
        // aligned blocking read from fd
        off_t record_off = _get_record_off(vid);
        if (mmap_block_) {
            block_start_off_ = low_4k_aligned(record_off);
            if (mmap_block_) {
                munmap(mmap_block_, PAGE_SIZE);
            }
            mmap_block_ = _memmap(block_start_off_);
            return reinterpret_cast<block_trie_info *>(
                mmap_block_ + (record_off - block_start_off_));
        }
        return reinterpret_cast<block_trie_info *>(header_block_ + record_off);
    }

    void write_record(uint64_t vid, uint64_t root_off)
    {
        off_t record_off = _get_record_off(vid);
        if (record_off >= block_start_off_ + PAGE_SIZE) {
            // renew mmap_block_
            if (mmap_block_) {
                munmap(mmap_block_, PAGE_SIZE);
            }
            block_start_off_ += PAGE_SIZE;
            mmap_block_ = _memmap(block_start_off_);
        }
        // update vid record slot
        unsigned char *tmp_block = mmap_block_ ? mmap_block_ : header_block_;
        *reinterpret_cast<block_trie_info *>(
            tmp_block + record_off - block_start_off_) =
            block_trie_info{vid, root_off};
        // update header vid
        *reinterpret_cast<uint64_t *>(header_block_) = vid;
    }
};

static_assert(sizeof(Index) == 24);
static_assert(alignof(Index) == 8);

MONAD_TRIE_NAMESPACE_END