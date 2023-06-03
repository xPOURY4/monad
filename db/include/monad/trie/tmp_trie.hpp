#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/globals.hpp>

#include <monad/trie/data.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

enum class tmp_node_type_t : unsigned char
{
    UNKNOWN = 0,
    BRANCH,
    LEAF
};

struct tmp_branch_node_t
{
    tmp_node_type_t type;

    unsigned char path_len; // number of nibbles
    unsigned char path[32];

    char pad[6];

    uint32_t next[16];
    uint16_t subnode_bitmask;
    uint8_t nsubnodes;
};

static_assert(sizeof(tmp_branch_node_t) == 108);
static_assert(alignof(tmp_branch_node_t) == 4);

struct tmp_leaf_node_t
{
    tmp_node_type_t type;

    unsigned char path_len;
    unsigned char path[32];
    bool tombstone;

    char pad[5];

    trie_data_t data; // will change to a pointer, no copy
};

static_assert(sizeof(tmp_leaf_node_t) == 72);
static_assert(alignof(tmp_leaf_node_t) == 8);

/* inline helper functions */

class TmpTrie final
{
    uint32_t const root_i_;
    tmp_branch_node_t *const root_;

    uint32_t get_new_branch(unsigned char const *path, unsigned char path_len);
    uint32_t get_new_leaf(
        unsigned char const *path, unsigned char path_len,
        trie_data_t const *data, bool tombstone);

public:
    TmpTrie()
        : root_i_{get_new_branch(nullptr, 0)}
        , root_{(tmp_branch_node_t *)cpool_ptr29(tmppool_, root_i_)}
    {
    }

    void upsert(
        unsigned char const *path, uint8_t path_len, trie_data_t const *,
        bool erase);

    [[gnu::always_inline]] constexpr tmp_branch_node_t *get_root()
    {
        return root_;
    }

    [[gnu::always_inline]] static inline tmp_branch_node_t *
    get_node(uint32_t const i)
    {
        return (tmp_branch_node_t *)cpool_ptr29(tmppool_, i);
    }
};

static_assert(sizeof(TmpTrie) == 16);
static_assert(alignof(TmpTrie) == 8);

MONAD_TRIE_NAMESPACE_END