#include <monad/trie/offset_ptr.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

namespace disas
{
    struct OffsetPtrTest
    {
    };
}

template class offset_ptr_t<disas::OffsetPtrTest>;

template class offset_ptr_t<disas::OffsetPtrTest const>;

MONAD_TRIE_NAMESPACE_END
