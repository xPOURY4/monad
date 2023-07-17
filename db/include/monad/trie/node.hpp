#pragma once

#include <monad/core/assert.h>
#include <monad/trie/util.hpp>

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <type_traits>

MONAD_TRIE_NAMESPACE_BEGIN

struct merkle_node_t;

struct merkle_child_info_t
{
    typedef uint8_t data_len_t;
    typedef uint8_t path_len_t;
    typedef file_offset_t fnext_t;
    typedef std::array<unsigned char, 32> noderef_t;

    noderef_t noderef;
    merkle_node_t *next;
    unsigned char *data;

    // Bitfields do NOT allow these to span byte boundaries, it can be slow!
    struct bitpacked_storage_t
    {
        uint64_t node_len_disk_pages0 : 1;
        uint64_t fnext_div_two : 47;
        uint64_t data_len : 8; // in bytes, max possible is 256
        uint64_t path_len : 7; // in nibbles, max possible is 64
        uint64_t node_len_disk_pages1 : 1;
    } bitpacked;
    static_assert(sizeof(bitpacked) == 8);
    static_assert(
        std::endian::native == std::endian::little,
        "C bitfields stored to disk have the endian of their machine, big "
        "endian would need a bit swapping loader implementation");

    unsigned char path[32]; // TODO: change to var length

    constexpr file_offset_t fnext() const noexcept
    {
        return bitpacked.fnext_div_two << 1;
    }
    void set_fnext(file_offset_t v) noexcept
    {
        MONAD_ASSERT(v < (file_offset_t(1) << 48));
        assert((v & 1) == 0);
        bitpacked.fnext_div_two = v >> 1;
    }
    constexpr data_len_t data_len() const noexcept
    {
        return bitpacked.data_len;
    }
    constexpr void set_data_len(data_len_t v) noexcept
    {
        bitpacked.data_len = v;
    }
    constexpr unsigned node_len_upper_bound() const noexcept
    {
        /* Size histogram from monad_merge_trie_test:

        512: 14505275
        1024: 22447875
        1536: 821542
        2048: 10
        2560: 0
        3072: 0  (MAX_DISK_NODE_SIZE currently = 2674)

        Therefore:
           0 = 1 * DISK_PAGE_SIZE (512)
           1 = 2 * DISK_PAGE_SIZE (1024)
           2 = 3 * DISK_PAGE_SIZE (1536)
           3 = 6 * DISK_PAGE_SIZE (3072)
        */
        unsigned node_len_disk_pages = (bitpacked.node_len_disk_pages1 << 1) |
                                       bitpacked.node_len_disk_pages0;
        switch (node_len_disk_pages) {
        case 0:
            return 1U << DISK_PAGE_BITS;
        case 1:
            return 2U << DISK_PAGE_BITS;
        case 2:
            return 3U << DISK_PAGE_BITS;
        default:
            return round_up_align<DISK_PAGE_BITS>(MAX_DISK_NODE_SIZE);
        }
    }
    constexpr void set_node_len_upper_bound(size_t bytes) noexcept
    {
        assert(bytes > 0);
        assert(bytes <= MAX_DISK_NODE_SIZE);
        auto pages = (bytes + DISK_PAGE_SIZE - 1) >> DISK_PAGE_BITS;
        assert(pages > 0);
        pages -= 1;
        if (pages < 3) {
            bitpacked.node_len_disk_pages0 = (pages & 1) != 0;
            bitpacked.node_len_disk_pages1 = (pages & 2) != 0;
        }
        else {
            bitpacked.node_len_disk_pages0 = 1;
            bitpacked.node_len_disk_pages1 = 1;
        }
    }
    constexpr path_len_t path_len() const noexcept
    {
        return bitpacked.path_len;
    }
    constexpr void set_path_len(path_len_t v) noexcept
    {
        assert(v < (1U << 7));
        bitpacked.path_len = v;
    }
};

static_assert(sizeof(merkle_child_info_t) == 88);
static_assert(alignof(merkle_child_info_t) == 8);
static_assert(std::is_trivially_copyable_v<merkle_child_info_t>);

struct merkle_node_t
{
    typedef uint16_t mask_t;
    typedef uint8_t path_len_t;

    mask_t mask;
    mask_t valid_mask;
    mask_t tomb_arr_mask;
    uint8_t nsubnodes;
    path_len_t path_len;

    merkle_child_info_t children[0];
};

static_assert(sizeof(merkle_node_t) == 8);
static_assert(alignof(merkle_node_t) == 8);
static_assert(std::is_trivially_copyable_v<merkle_node_t>);

inline uint16_t merkle_child_mask(merkle_node_t const *const node) noexcept
{
    return node->mask;
}

inline bool
merkle_child_test(merkle_node_t const *const node, unsigned const i) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return mask & (1u << i);
}

inline bool merkle_child_all(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return mask == UINT16_MAX;
}

inline bool merkle_child_any(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return mask;
}

inline bool merkle_child_none(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return !mask;
}

inline unsigned merkle_child_count(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return std::popcount(mask);
}

inline unsigned
merkle_child_index(merkle_node_t const *const node, unsigned const i) noexcept
{
    return child_index(node->mask, i);
}

inline unsigned
merkle_child_count_tomb(merkle_node_t const *const node) noexcept
{
    return node->nsubnodes - std::popcount(node->valid_mask);
}

inline unsigned
merkle_child_count_valid(merkle_node_t const *const node) noexcept
{
    return std::popcount(node->valid_mask);
}

inline unsigned char
partial_path_len(merkle_node_t const *const parent, unsigned const i) noexcept
{
    return parent->children[i].path_len() - parent->path_len - 1;
}

MONAD_TRIE_NAMESPACE_END
