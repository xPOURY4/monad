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
            StateMachineWithBlockNo sm{1};
            UpdateAux aux{&io}; // trie section starts from account
            monad::small_prng rand;
            std::vector<std::pair<monad::byte_string, size_t>> keys;

            state_t()
            {
                std::vector<Update> updates;
                updates.reserve(1000);
                for (;;) {
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
                                std::move(key), aux.get_root_offset().id);
                        }
                        updates.push_back(
                            make_update(keys.back().first, keys.back().first));
                        update_ls.push_front(updates.back());
                    }
                    root = upsert(aux, sm, root.get(), std::move(update_ls));
                    size_t count = 0;
                    for (auto *ci = aux.db_metadata()->fast_list_begin();
                         ci != nullptr;
                         count++, ci = ci->next(aux.db_metadata())) {
                    }
                    if (count >= CHUNKS_TO_FILL) {
                        break;
                    }
                }
                std::cout << "After suite set up before testing:";
                print(std::cout);
            }

            std::ostream &print(std::ostream &s) const
            {
                auto v = pool.devices().front().capacity();
                std::cout << "\n   Storage pool capacity = " << v.first
                          << " consumed = " << v.second
                          << " chunks = " << pool.chunks(pool.seq);
                auto const diff =
                    (int64_t(aux.get_lower_bound_free_space()) -
                     int64_t(v.first - v.second));
                std::cout << "\n   DB thinks there is a lower bound of "
                          << aux.get_lower_bound_free_space()
                          << " bytes free whereas the syscall thinks there is "
                          << (v.first - v.second)
                          << " bytes free, which is a difference of " << diff
                          << ".\n";
                for (auto *ci = aux.db_metadata()->fast_list_begin();
                     ci != nullptr;
                     ci = ci->next(aux.db_metadata())) {
                    auto idx = ci->index(aux.db_metadata());
                    auto chunk = pool.chunk(pool.seq, idx);
                    std::cout << "\n      Chunk " << idx
                              << " has capacity = " << chunk->capacity()
                              << " consumed = " << chunk->size();
                }
                std::cout << std::endl;
                EXPECT_LE(-diff, AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE * 2);
                return s;
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
            updates.push_back(make_update(i.first, UpdateList{}));
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
            state()->aux,
            state()->sm,
            state()->root.get(),
            std::move(update_ls));
        std::cout << "\nBefore compaction:";
        state()->print(std::cout);
        // TODO DO COMPACTION
        // TODO CHECK POOL'S FIRST CHUNK WAS DEFINITELY RELEASED
    }
}
