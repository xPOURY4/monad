#include "test_fixtures_gtest.hpp"

#include <monad/async/config.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/trie.hpp>

#include <monad/test/gtest_signal_stacktrace_printer.hpp> // NOLINT

#include <cstddef>
#include <cstdint>

using namespace MONAD_MPT_NAMESPACE;
using namespace MONAD_ASYNC_NAMESPACE;
using namespace monad::literals;

static constexpr size_t CHUNKS_TO_FILL = 3;

struct NodeWriterTest
    : public monad::test::FillDBWithChunksGTest<CHUNKS_TO_FILL>
{
};

TEST_F(NodeWriterTest, replace_node_writer_close_to_chunk_boundary)
{
    auto fast_chunk_ids = state()->fast_list_ids();
    EXPECT_EQ(fast_chunk_ids.size(), CHUNKS_TO_FILL);

    // rewind to close to the end of second to last fast chunk in use
    auto const rewind_chunk_id = fast_chunk_ids[1].first;
    auto chunk =
        state()->io.storage_pool().chunk(storage_pool::seq, rewind_chunk_id);
    auto const fast_offset_rewind_to = chunk_offset_t{
        rewind_chunk_id, chunk->size() - 200}; // 200 bytes remained

    // reset fast offset to close to the end of last chunk
    state()->aux.advance_offsets_to(
        state()->aux.get_root_offset(),
        fast_offset_rewind_to,
        state()->aux.get_start_of_wip_slow_offset());
    this->state()->aux.rewind_to_match_offsets();

    EXPECT_EQ(
        state()->aux.node_writer_fast->sender().offset(),
        fast_offset_rewind_to);
    uint32_t const curr_node_writer_chunk_id =
        state()->aux.node_writer_fast->sender().offset().id;
    EXPECT_EQ(
        state()
            ->aux.db_metadata()
            ->at(curr_node_writer_chunk_id)
            ->insertion_count(),
        1);

    size_t const bytes_yet_to_be_appended_to_existing = 100;
    size_t const bytes_to_write_to_new_writer = 800;
    // After writing the 100 bytes, current chunk has no enough space to write
    // rest of the 800 bytes, thus will start off at the beginning of a new
    // chunk
    auto new_node_writer = replace_node_writer(
        state()->aux,
        state()->aux.node_writer_fast,
        bytes_yet_to_be_appended_to_existing,
        bytes_to_write_to_new_writer);
    EXPECT_EQ(new_node_writer->sender().offset().offset, 0);

    uint32_t const new_node_writer_chunk_id =
        new_node_writer->sender().offset().id;
    // new node writer offset should start at the next chunk
    EXPECT_TRUE(new_node_writer_chunk_id != curr_node_writer_chunk_id);
    EXPECT_EQ(
        (uint32_t)state()
            ->aux.db_metadata()
            ->at(new_node_writer_chunk_id)
            ->insertion_count(),
        (uint32_t)state()
                ->aux.db_metadata()
                ->at(curr_node_writer_chunk_id)
                ->insertion_count() +
            1);
}
