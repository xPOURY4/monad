#include "gtest/gtest.h"

#include <monad/core/small_prng.hpp>

#include <monad/mpt/compute.hpp>
#include <monad/mpt/trie.hpp>

#include <vector>

namespace
{
    using namespace MONAD_ASYNC_NAMESPACE;
    using namespace MONAD_MPT_NAMESPACE;
    using namespace monad::literals;

    static constexpr size_t CHUNKS_TO_FILL = 8;

    struct CompactionTest : public testing::Test
    {
        struct state_t
        {
            storage_pool pool{
                use_anonymous_inode_tag{},
                AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};
            monad::io::Ring ring{2, 0};
            monad::io::Buffers rwbuf{
                ring,
                2,
                4,
                AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
                AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};
            AsyncIO io{pool, ring, rwbuf};
            MerkleCompute comp;
            node_ptr root;
            UpdateAux update_aux{comp, &io, /*list_dim_to_apply_cache*/ 0};
            monad::small_prng rand;
            std::vector<std::pair<monad::byte_string, size_t>> keys;

            state_t()
            {
                size_t current_chunk_idx = 0;
                auto lastchunk = pool.activate_chunk(pool.seq, CHUNKS_TO_FILL),
                     current_chunk =
                         pool.activate_chunk(pool.seq, current_chunk_idx);
                std::vector<Update> updates;
                updates.reserve(1000);
                while (lastchunk->size() == 0) {
                    UpdateList update_ls;
                    updates.clear();
                    for (size_t n = 0; n < 1000; n++) {
                        {
                            monad::byte_string key(
                                0x1234567812345678123456781234567812345678123456781234567812345678_hex);
                            for (size_t n = 0; n < key.size(); n += 4) {
                                *(uint32_t *)(key.data() + n) = rand();
                            }
                            keys.emplace_back(
                                std::move(key), current_chunk_idx);
                        }
                        updates.push_back(
                            make_update(keys.back().first, keys.back().first));
                        update_ls.push_front(updates.back());
                    }
                    root = upsert(update_aux, root.get(), std::move(update_ls));
                    if (current_chunk->size() == current_chunk->capacity()) {
                        current_chunk =
                            pool.activate_chunk(pool.seq, ++current_chunk_idx);
                    }
                }
                auto v = pool.devices().front().capacity();
                std::cout << "After suite set up before testing:"
                          << "\n   Storage pool capacity = " << v.first
                          << " consumed = " << v.second
                          << " chunks = " << pool.chunks(pool.seq) << std::endl;
                for (size_t n = 0; n <= CHUNKS_TO_FILL; n++) {
                    auto chunk = pool.activate_chunk(pool.seq, n);
                    std::cout << "\n      Chunk " << n
                              << " has capacity = " << chunk->capacity()
                              << " consumed = " << chunk->size();
                }
                std::cout << std::endl;
            }
        };
        static state_t *&state()
        {
            static state_t *v;
            return v;
        }
        static void SetUpTestSuite()
        {
            state() = new state_t;
        }
        static void TearDownTestSuite()
        {
            delete state();
        }
    };

    TEST_F(CompactionTest, first_chunk_is_compacted)
    {
        std::vector<Update> updates;
        for (auto &i : state()->keys) {
            if (i.second > 0) {
                break;
            }
            updates.push_back(make_update(i.first, std::nullopt));
        }
        std::cout << "Erasing the first " << updates.size()
                  << " inserted keys, which should enable the whole of the "
                     "first block to be compacted away."
                  << std::endl;
        UpdateList update_ls;
        for (auto &i : updates) {
            update_ls.push_front(i);
        }
        state()->root = upsert(
            state()->update_aux, state()->root.get(), std::move(update_ls));
        std::cout << "\nBefore compaction:";
        auto v = state()->pool.devices().front().capacity();
        std::cout << "\n   Storage pool capacity = " << v.first
                  << " consumed = " << v.second
                  << " chunks = " << state()->pool.chunks(state()->pool.seq)
                  << std::endl;
        for (size_t n = 0; n <= CHUNKS_TO_FILL; n++) {
            auto chunk = state()->pool.activate_chunk(state()->pool.seq, n);
            std::cout << "\n      Chunk " << n
                      << " has capacity = " << chunk->capacity()
                      << " consumed = " << chunk->size();
        }
        // TODO DO COMPACTION
        // TODO CHECK POOL'S FIRST CHUNK WAS DEFINITELY RELEASED
    }
}
