#pragma once

#include <monad/mem/cpool.h>
CPOOL_DEFINE(29);

#define MONAD_TRIE_NAMESPACE_BEGIN                                             \
    namespace monad                                                            \
    {                                                                          \
        namespace trie                                                         \
        {

#define MONAD_TRIE_NAMESPACE_END                                               \
    }                                                                          \
    }
