#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

static constexpr unsigned RECORD_SIZE = 16;
static constexpr unsigned SLOTS = 3600 * 4;
static constexpr size_t BLOCK_START_OFF =
    round_up_4k(RECORD_SIZE * (1 + SLOTS));

static constexpr uint8_t SIZE_OF_CHILD_COUNT = 1, SIZE_OF_PATH_LEN = 1,
                         SIZE_OF_DATA_LEN = 1, SIZE_OF_NODE_REF = 32,
                         SIZE_OF_SUBNODE_BITMASK = 2, SIZE_OF_FILE_OFFSET = 8,
                         CACHE_LEVELS = 5;
static constexpr unsigned MAX_DISK_NODE_SIZE = 1536;

MONAD_TRIE_NAMESPACE_END
