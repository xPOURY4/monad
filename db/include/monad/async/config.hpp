#pragma once

#include <cassert>
#include <compare>
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

#include <monad/core/assert.h>
#include <monad/core/hash.hpp>

MONAD_ASYNC_NAMESPACE_BEGIN

//! The same type io_uring uses for offsets into files during i/o
using file_offset_t = __u64;

//! An identifier of data within a `storage_pool`
struct chunk_offset_t
{
    file_offset_t offset : 28; //!< Offset into the chunk, max is 256Mb
    file_offset_t id : 20; //!< Id of the chunk, max is 1 million, therefore
                           //!< maximum addressable storage is 256Tb
    file_offset_t spare : 16;

    static constexpr file_offset_t max_offset = (1ULL << 28) - 1;
    static constexpr file_offset_t max_id = (1U << 20) - 1;
    static constexpr file_offset_t max_spare = (1ULL << 16) - 1;

    static constexpr chunk_offset_t invalid_value() noexcept
    {
        return {max_id, max_offset};
    }

    constexpr chunk_offset_t(
        uint32_t id_, file_offset_t offset_, file_offset_t spare_ = 0xffff)
        : offset(offset_ & max_offset)
        , id(id_ & max_id)
        , spare{spare_ & max_spare}
    {
        MONAD_DEBUG_ASSERT(id_ <= max_id);
        MONAD_DEBUG_ASSERT(offset_ <= max_offset);
        MONAD_DEBUG_ASSERT(spare_ <= max_spare);
    }

    constexpr bool operator==(chunk_offset_t const &o) const noexcept
    {
        return offset == o.offset && id == o.id;
    }

    constexpr auto operator<=>(chunk_offset_t const &o) const noexcept
    {
        if (offset == o.offset && id == o.id) {
            return std::weak_ordering::equivalent;
        }
        if (id < o.id || (id == o.id && offset < o.offset)) {
            return std::weak_ordering::less;
        }
        return std::weak_ordering::greater;
    }

    constexpr chunk_offset_t add_to_offset(file_offset_t offset_) const noexcept
    {
        chunk_offset_t ret(*this);
        offset_ += ret.offset;
        MONAD_DEBUG_ASSERT(offset_ <= max_offset);
        ret.offset = offset_ & max_offset;
        return ret;
    }

    constexpr file_offset_t raw() const noexcept
    {
        union _
        {
            file_offset_t ret;
            chunk_offset_t self;

            constexpr _()
                : ret{}
            {
            }
        } u;

        u.self = *this;
        u.self.spare =
            0; // must be flattened, otherwise can't go into the rbtree key
        return u.ret;
    }
};

static_assert(sizeof(chunk_offset_t) == 8);
static_assert(alignof(chunk_offset_t) == 8);
static_assert(std::is_trivially_copyable_v<chunk_offset_t>);

struct chunk_offset_t_hasher
{
    constexpr size_t operator()(chunk_offset_t v) const noexcept
    {
        v.spare = 0xffff;
        return fnv1a_hash<chunk_offset_t>()(v);
    }
};

//! Tag type for tests to ask for anonymous inodes
struct use_anonymous_inode_tag
{
};

//! The invalid file offset
static constexpr chunk_offset_t INVALID_OFFSET =
    chunk_offset_t::invalid_value();

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
