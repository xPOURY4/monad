#pragma once

#include <monad/mpt/update.hpp>
#include <monad/trie/trie.hpp>

#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <gtest/gtest.h>
#include <vector>

using namespace monad::trie;
using namespace monad::mpt;

struct on_disk_trie_fixture_t : public testing::Test
{
protected:
    monad::io::Ring ring;
    monad::io::Buffers rwbuf;

public:
    MerkleTrie trie;
    on_disk_trie_fixture_t()
        : ring(monad::io::Ring(128, 15))
        , rwbuf(monad::io::Buffers(ring, 128, 128))
        , trie([&] {
            std::filesystem::path p = "unittest.db";
            return MerkleTrie(
                nullptr,
                std::make_shared<AsyncIO>(p, ring, rwbuf, 0, &update_callback));
        }())
    {
    }

    void process_updates(std::vector<Update> &update_vec)
    {
        UpdateList updates;
        for (auto it = update_vec.begin(); it != update_vec.end(); ++it) {
            updates.push_front(*it);
        }
        trie.process_updates(updates);
        trie.flush_last_buffer();
    }

    void process_updates(UpdateList &updates)
    {
        trie.process_updates(updates);
        trie.flush_last_buffer();
    }

    monad::byte_string root_hash()
    {
        monad::byte_string ret(32, 0);
        trie.root_hash(ret.data());
        return ret;
    }
};

struct in_memory_trie_fixture_t : public testing::Test
{

    MerkleTrie trie{};

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
