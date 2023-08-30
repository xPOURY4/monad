#pragma once

#include <monad/core/likely.h>

void __attribute__((noreturn)) monad_assertion_failed(
    char const *expr, char const *function, char const *file, long line);

#define MONAD_ASSERT(expr)                                                     \
    if (MONAD_LIKELY(expr)) { /* likeliest */                                  \
    }                                                                          \
    else {                                                                     \
        monad_assertion_failed(                                                \
            #expr, __PRETTY_FUNCTION__, __FILE__, __LINE__);                   \
    }

#ifdef NDEBUG
    #define MONAD_DEBUG_ASSERT(x)                                              \
        do {                                                                   \
            (void)sizeof(x);                                                   \
        }                                                                      \
        while (0)
#else
    #define MONAD_DEBUG_ASSERT(x) MONAD_ASSERT(x)
#endif
