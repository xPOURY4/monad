#pragma once

#include <monad/mpt/update.hpp>
#include <monad/trie/trie.hpp>

#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace monad::trie;
using namespace monad::mpt;

std::string make_unique_filename()
{
    using boost::lexical_cast;
    using boost::uuids::random_generator;
    return lexical_cast<std::string>((random_generator())()) + ".db";
}

template <bool IS_ACCOUNT>
struct on_disk_trie_fixture_t : public testing::Test
{
protected:
    std::filesystem::path dbpath;
    monad::io::Ring ring;
    monad::io::Buffers rwbuf;

public:
    MerkleTrie trie;

    constexpr bool is_account()
    {
        return IS_ACCOUNT;
    };

    on_disk_trie_fixture_t()
        : dbpath(make_unique_filename())
        , ring(monad::io::Ring(2, 0))
        , rwbuf(monad::io::Buffers(ring, 2, 2))
        , trie([&, this] {
            auto index = std::make_shared<index_t>(dbpath);
            file_offset_t block_off = index->get_start_offset();
            return MerkleTrie(
                IS_ACCOUNT,
                nullptr,
                std::make_shared<AsyncIO>(
                    dbpath, ring, rwbuf, block_off, &update_callback),
                index,
                5);
        }())
    {
        unlink(dbpath.c_str());
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
