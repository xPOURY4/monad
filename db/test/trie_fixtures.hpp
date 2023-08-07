#pragma once

#include <monad/mpt/update.hpp>
#include <monad/trie/trie.hpp>

#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <gtest/gtest.h>
#include <vector>

using namespace monad::trie;
using namespace monad::mpt;

template <bool IS_ACCOUNT>
struct on_disk_trie_fixture_t : public testing::Test
{
protected:
    monad::io::Ring ring;
    monad::io::Buffers rwbuf;

public:
    MerkleTrie trie;

    constexpr bool is_account()
    {
        return IS_ACCOUNT;
    };

    on_disk_trie_fixture_t()
        : ring(monad::io::Ring(128, 0))
        , rwbuf(monad::io::Buffers(ring, 8, 8))
        , trie([&] {
            std::filesystem::path p = "unittest.db";
            auto index = std::make_shared<index_t>(p);
            file_offset_t block_off = index->get_start_offset();
            return MerkleTrie(
                IS_ACCOUNT,
                nullptr,
                std::make_shared<AsyncIO>(
                    p, ring, rwbuf, block_off, &update_callback),
                index,
                5);
        }())
    {
    }

    void process_updates(std::vector<Update> &update_vec, uint64_t block_id = 0)
    {
        UpdateList updates;
        for (auto it = update_vec.begin(); it != update_vec.end(); ++it) {
            updates.push_front(*it);
        }
        trie.process_updates(updates, block_id);
        trie.flush_last_buffer();
    }

    void process_updates(UpdateList &updates, uint64_t block_id = 0)
    {
        trie.process_updates(updates, block_id);
        trie.flush_last_buffer();
    }

    monad::byte_string root_hash()
    {
        monad::byte_string ret(32, 0);
        trie.root_hash(ret.data());
        return ret;
    }
};

template <bool IS_ACCOUNT>
struct in_memory_trie_fixture_t : public testing::Test
{

    MerkleTrie trie{IS_ACCOUNT};

    constexpr bool is_account()
    {
        return IS_ACCOUNT;
    };

    void process_updates(std::vector<Update> &update_vec)
    {
        UpdateList updates;
        for (auto it = update_vec.begin(); it != update_vec.end(); ++it) {
            updates.push_front(*it);
        }
        trie.process_updates(updates);
    }

    void process_updates(UpdateList &updates)
    {
        trie.process_updates(updates);
    }

    monad::byte_string root_hash()
    {
        monad::byte_string ret(32, 0);
        trie.root_hash(ret.data());
        return ret;
    }
};
