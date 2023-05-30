#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

static const unsigned RECORD_SIZE = 16;
static const unsigned SLOTS = 3600 * 4;
static const size_t BLOCK_START_OFF =
    high_4k_aligned(RECORD_SIZE * (1 + SLOTS));

static const uint8_t SIZE_OF_CHILD_COUNT = 1, SIZE_OF_PATH_LEN = 1,
                     SIZE_OF_TRIE_DATA = 32, SIZE_OF_SUBNODE_BITMASK = 2,
                     SIZE_OF_FILE_OFFSET = 8, BLOCK_TYPE_DATA = 0,
                     BLOCK_TYPE_META = 1, CACHE_LEVELS = 5;
static const unsigned MAX_DISK_NODE_SIZE = 1536;

MONAD_TRIE_NAMESPACE_END
