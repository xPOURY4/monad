#include <compiler/ir/basic_blocks.h>
#include <compiler/types.h>
#include <utils/assert.h>

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

    std::vector<Block> const &BasicBlocksIR::blocks() const
    {
        return blocks_;
    }

    std::vector<Block> &BasicBlocksIR::blocks()
    {
        return blocks_;
    }

    Block const &BasicBlocksIR::block(block_id id) const
    {
        return blocks_.at(id);
    }

    std::unordered_map<byte_offset, block_id> const &
    BasicBlocksIR::jump_dests() const
    {
        return jump_dests_;
    }

    std::unordered_map<byte_offset, block_id> &BasicBlocksIR::jump_dests()
    {
        return jump_dests_;
    }

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

    block_id BasicBlocksIR::curr_block_id() const
    {
        return blocks_.size() - 1;
    }

    byte_offset BasicBlocksIR::curr_block_offset() const
    {
        return blocks_.back().offset;
    }

    void BasicBlocksIR::add_jump_dest()
    {
        MONAD_COMPILER_DEBUG_ASSERT(blocks_.back().instrs.empty());
        jump_dests_.emplace(curr_block_offset(), curr_block_id());
    }

    void BasicBlocksIR::add_block(byte_offset offset)
    {
        blocks_.push_back(Block{.offset = offset});
    }

    void BasicBlocksIR::add_terminator(Terminator t)
    {
        blocks_.back().terminator = t;
    }

    void BasicBlocksIR::add_fallthrough_terminator(Terminator t)
    {
        blocks_.back().terminator = t;
        blocks_.back().fallthrough_dest = curr_block_id() + 1;
    }
}
