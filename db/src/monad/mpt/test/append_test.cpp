#include "test_fixtures_gtest.hpp"

#include <monad/async/config.hpp>
#include <monad/async/util.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>

#include <iostream>

using namespace MONAD_ASYNC_NAMESPACE;
using namespace MONAD_MPT_NAMESPACE;
using namespace monad::literals;

struct AppendTest : public monad::test::FillDBWithChunksGTest<2>
{
};

TEST_F(AppendTest, works)
{
    auto const root_off = state()->aux.get_root_offset();
    auto const root_hash_before = state()->root_hash();
    auto const rand_state = state()->rand;

    state()->ensure_total_chunks(3);
    auto const root_hash_after1 = state()->root_hash();

    std::cout << "\nBefore rewind:";
    state()->print(std::cout);

    // Rewind
    Node *const root = read_node_blocking(state()->io.storage_pool(), root_off);
    state()->root.reset(root);
    chunk_offset_t const fast_offset = round_up_align<DISK_PAGE_BITS>(
        root_off.add_to_offset(root->get_disk_size()));
    // destroy contents after fast_offset.id chunk, and reset
    // node_writer's offset.
    state()->aux.rewind_to_match_offset(fast_offset);

    std::cout << "\nAfter rewind:";
    state()->print(std::cout);

    // Has the root hash returned to what it should be?
    EXPECT_EQ(state()->root_hash(), root_hash_before);

    state()->rand = rand_state;
    state()->ensure_total_chunks(3);
    auto const root_hash_after2 = state()->root_hash();

    // Has the root hash returned to what it should be?
    EXPECT_EQ(root_hash_after1, root_hash_after2);

    std::cout << "\nAfter append after rewind:";
    state()->print(std::cout);
}
