#include <monad/compiler/ir/basic_blocks.h>
#include <monad/compiler/types.h>

#include <algorithm>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace monad::compiler::basic_blocks
{
    /*
     * Block
     */

    bool Block::is_valid() const
    {
        return is_fallthrough_terminator(terminator) ==
               (fallthrough_dest != INVALID_BLOCK_ID);
    }

    bool operator==(Block const &a, Block const &b)
    {
        return std::tie(a.instrs, a.terminator, a.fallthrough_dest, a.offset) ==
               std::tie(b.instrs, b.terminator, b.fallthrough_dest, b.offset);
    }

    /*
     * IR
     */

    bool BasicBlocksIR::is_valid() const
    {
        auto all_blocks_valid =
            std::all_of(blocks_.begin(), blocks_.end(), [](auto const &b) {
                return b.is_valid();
            });

        auto all_dests_valid = std::all_of(
            jump_dests_.begin(), jump_dests_.end(), [this](auto const &entry) {
                auto [offset, block_id] = entry;
                return block_id < blocks_.size();
            });

        return all_blocks_valid && all_dests_valid;
    }

    /*
     * IR: Private construction methods
     */

    void BasicBlocksIR::add_block(byte_offset offset)
    {
        blocks_.push_back(Block{.offset = offset});
        blocks_.back().instrs.reserve(16);
    }

    void BasicBlocksIR::add_terminator(Terminator t)
    {
        blocks_.back().instrs.shrink_to_fit();
        blocks_.back().terminator = t;
    }

    void BasicBlocksIR::add_fallthrough_terminator(Terminator t)
    {
        add_terminator(t);
        blocks_.back().fallthrough_dest = curr_block_id() + 1;
    }
}
