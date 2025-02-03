#pragma once

// The code in this file is intended to be removed once the compiler is properly
// integrated into the client; it's a minimal dummy unsafe implementation of the
// signal-safe assertion mechanism in the client.

#ifdef __cplusplus
extern "C"
{
#endif

void __attribute__((noreturn)) monad_compiler_assertion_failed(
    char const *expr, char const *function, char const *file, long line);

#ifdef MONAD_LIKELY
    #define MONAD_COMPILER_LIKELY(x) MONAD_LIKELY(x)
#else
    #define MONAD_COMPILER_LIKELY(x) __builtin_expect(!!(x), 1)
#endif

#ifdef MONAD_UNLIKELY
    #define MONAD_COMPILER_UNLIKELY(x) MONAD_UNLIKELY(x)
#else
    #define MONAD_COMPILER_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

#ifdef MONAD_ASSERT
    #define MONAD_COMPILER_ASSERT(expr) MONAD_ASSERT(expr)
#else
    #define MONAD_COMPILER_ASSERT(expr)                                        \
        if (MONAD_COMPILER_LIKELY(expr)) { /* likeliest */                     \
        }                                                                      \
        else {                                                                 \
            monad_compiler_assertion_failed(                                   \
                #expr, __extension__ __PRETTY_FUNCTION__, __FILE__, __LINE__); \
        }
#endif

#ifdef MONAD_DEBUG_ASSERT
    #define MONAD_COMPILER_DEBUG_ASSERT(expr) MONAD_DEBUG_ASSERT(expr)
#else
    #ifdef NDEBUG
        #define MONAD_COMPILER_DEBUG_ASSERT(x)                                 \
            do {                                                               \
                (void)sizeof(x);                                               \
            }                                                                  \
            while (0)
    #else
        #define MONAD_COMPILER_DEBUG_ASSERT(x) MONAD_COMPILER_ASSERT(x)
    #endif
#endif

#ifdef __cplusplus
}
#endif
