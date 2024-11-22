#include <compiler/ir/basic_blocks.h>
#include <compiler/types.h>

#include <algorithm>
#include <cassert>
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
}
