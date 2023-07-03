#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

static constexpr unsigned RECORD_SIZE = 16;
static constexpr unsigned SLOTS = 3600 * 4;
static constexpr size_t BLOCK_START_OFF =
    round_up_4k(RECORD_SIZE * (1 + SLOTS));

static const uint8_t SIZE_OF_CHILD_COUNT = 1, SIZE_OF_PATH_LEN = 1,
                     SIZE_OF_DATA_LEN = 1, SIZE_OF_NODE_REF = 32,
                     SIZE_OF_SUBNODE_BITMASK = 2, SIZE_OF_FILE_OFFSET = 8;
static const unsigned MAX_DISK_NODE_SIZE = 1536;

static auto empty_trie_hash = byte_string(
    {0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6, 0xff, 0x83, 0x45,
     0xe6, 0x92, 0xc0, 0xf8, 0x6e, 0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c,
     0xad, 0xc0, 0x01, 0x62, 0x2f, 0xb5, 0xe3, 0x63, 0xb4, 0x21});

MONAD_TRIE_NAMESPACE_END
