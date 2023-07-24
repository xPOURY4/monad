#pragma once

#include <monad/mpt/update.hpp>
#include <monad/trie/trie.hpp>

#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <gtest/gtest.h>
#include <string>
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
        : ring(monad::io::Ring(2, 0))
        , rwbuf(monad::io::Buffers(
              ring, 2, 2, AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
              AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE))
        , trie([&, this] {
            auto index = std::make_shared<index_t>(use_anonymous_inode_tag{});
            return MerkleTrie(
                IS_ACCOUNT,
                index->get_start_offset(),
                nullptr,
                std::make_shared<AsyncIO>(
                    use_anonymous_inode_tag{}, ring, rwbuf),
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
        trie.get_io().flush();
    }

    void process_updates(UpdateList &updates, uint64_t block_id = 0)
    {
        trie.process_updates(updates, block_id);
        trie.get_io().flush();
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

    MerkleTrie trie{IS_ACCOUNT, 0};

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
