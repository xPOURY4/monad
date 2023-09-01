#pragma once

#include <cstdint>

#include <linux/types.h> // for __u64

#define MONAD_ASYNC_NAMESPACE_BEGIN                                            \
    namespace monad                                                            \
    {                                                                          \
        namespace async                                                        \
        {

#define MONAD_ASYNC_NAMESPACE_END                                              \
    }                                                                          \
    }

#define MONAD_ASYNC_NAMESPACE ::monad::async

MONAD_ASYNC_NAMESPACE_BEGIN

//! The same type io_uring uses for offsets into files during i/o
using file_offset_t = __u64;

//! Tag type for tests to ask for anonymous inodes
struct use_anonymous_inode_tag
{
};

//! The invalid file offset
static constexpr file_offset_t INVALID_OFFSET = file_offset_t(-1);

//! The CPU page size and bits to assume
static constexpr uint16_t CPU_PAGE_BITS = 12;
static constexpr uint16_t CPU_PAGE_SIZE = (1U << CPU_PAGE_BITS);

//! The storage i/o page size and bits to assume
static constexpr uint16_t DISK_PAGE_BITS = 9;
static constexpr uint16_t DISK_PAGE_SIZE = (1U << DISK_PAGE_BITS);

//! The DMA friendly page size and bits
static constexpr uint16_t DMA_PAGE_BITS = 6;
static constexpr uint16_t DMA_PAGE_SIZE = (1U << DMA_PAGE_BITS);

MONAD_ASYNC_NAMESPACE_END
