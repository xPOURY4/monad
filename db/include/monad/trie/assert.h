#pragma once

#include <monad/trie/likely.h>

#ifdef __cplusplus
extern "C"
{
#endif

void monad_trie_assertion_failed(
    char const *expr, char const *function, char const *file, long line);

#ifdef __cplusplus
}
#endif

#define MONAD_TRIE_ASSERT(expr)                                                \
    (MONAD_TRIE_LIKELY(!!(expr))                                               \
         ? ((void)0)                                                           \
         : monad_trie_assertion_failed(                                        \
               #expr, __PRETTY_FUNCTION__, __FILE__, __LINE__))
