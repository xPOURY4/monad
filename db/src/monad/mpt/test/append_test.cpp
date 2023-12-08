#include "test_fixtures_gtest.hpp"

#include <monad/async/config.hpp>
#include <monad/async/util.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>

#include <iostream>

using namespace MONAD_ASYNC_NAMESPACE;
using namespace MONAD_MPT_NAMESPACE;
using namespace monad::literals;

template <typename TFixture>
struct AppendTest : public TFixture
{
};

using AppendTestFastListOnly = monad::test::FillDBWithChunksGTest<2, false>;
using AppendTestSlowAndFastList = monad::test::FillDBWithChunksGTest<2, true>;

using AppendTestTypes =
    ::testing::Types<AppendTestFastListOnly, AppendTestSlowAndFastList>;

TYPED_TEST_SUITE(AppendTest, AppendTestTypes);

TYPED_TEST(AppendTest, works)
{
    auto const root_off = this->state()->aux.get_root_offset();
    auto const latest_slow_off =
        this->state()->aux.get_start_of_wip_slow_offset();
    auto const root_hash_before = this->state()->root_hash();
    auto const rand_state = this->state()->rand;

    this->state()->ensure_total_chunks(3);
    auto const root_hash_after1 = this->state()->root_hash();

    std::cout << "\nBefore rewind:";
    this->state()->print(std::cout);

    // Rewind
    this->state()->aux.advance_offsets_to(root_off, latest_slow_off);
    Node *const root =
        read_node_blocking(this->state()->io.storage_pool(), root_off);
    this->state()->root.reset(root);
    chunk_offset_t const fast_offset = round_up_align<DISK_PAGE_BITS>(
        root_off.add_to_offset(root->get_disk_size()));
    // destroy contents after fast_offset.id chunk, and reset
    // node_writer's offset.
    this->state()->aux.rewind_to_match_offset(fast_offset);

    std::cout << "\nAfter rewind:";
    this->state()->print(std::cout);

    // Has the root hash returned to what it should be?
    EXPECT_EQ(this->state()->root_hash(), root_hash_before);

    this->state()->rand = rand_state;
    this->state()->ensure_total_chunks(3);
    auto const root_hash_after2 = this->state()->root_hash();

    // Has the root hash returned to what it should be?
    EXPECT_EQ(root_hash_after1, root_hash_after2);

    std::cout << "\nAfter append after rewind:";
    this->state()->print(std::cout);
}
