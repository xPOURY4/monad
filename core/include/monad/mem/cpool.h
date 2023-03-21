#pragma once

#include <monad/core/likely.h>

#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * memory pool where old allocations expire as new allocations are made
 * implemented via a circular queue
 */

#define CPOOL_DEFINE(BITS)                                                     \
                                                                               \
    typedef struct                                                             \
    {                                                                          \
        unsigned char *mem;                                                    \
        uint32_t next;                                                         \
    } cpool_##BITS##_t;                                                        \
                                                                               \
    static_assert(sizeof(cpool_##BITS##_t) == 16, "");                         \
    static_assert(alignof(cpool_##BITS##_t) == 8, "");                         \
                                                                               \
    static inline void cpool_init##BITS(                                       \
        cpool_##BITS##_t *const p, unsigned char *const mem)                   \
    {                                                                          \
        p->mem = mem;                                                          \
        p->next = 0;                                                           \
    }                                                                          \
                                                                               \
    static inline unsigned char *cpool_ptr##BITS(                              \
        cpool_##BITS##_t const *const p, uint32_t const i)                     \
    {                                                                          \
        return p->mem + (i & ((1 << BITS) - 1));                               \
    }                                                                          \
                                                                               \
    static inline uint32_t cpool_reserve##BITS(                                \
        cpool_##BITS##_t *const p, uint32_t const n)                           \
    {                                                                          \
        uint32_t const m_next = p->next & ((1 << BITS) - 1);                   \
        if (MONAD_UNLIKELY(m_next + n > (1 << BITS))) {                        \
            p->next += (1 << BITS) - m_next;                                   \
        }                                                                      \
        return p->next;                                                        \
    }                                                                          \
                                                                               \
    static inline void cpool_advance##BITS(                                    \
        cpool_##BITS##_t *const p, uint32_t const n)                           \
    {                                                                          \
        p->next += n;                                                          \
    }                                                                          \
                                                                               \
    /**                                                                        \
     * valid objects are in [next - SIZE, next)                                \
     * invalid objects are in [next, next - SIZE)                              \
     */                                                                        \
    static inline bool cpool_valid##BITS(                                      \
        cpool_##BITS##_t const *const p, uint32_t const i)                     \
    {                                                                          \
        bool valid = false;                                                    \
        if (MONAD_UNLIKELY((1 << BITS) > p->next)) {                           \
            if (i < p->next || i >= p->next - (1 << BITS)) {                   \
                valid = true;                                                  \
            }                                                                  \
        }                                                                      \
        else if (MONAD_LIKELY(i >= p->next - (1 << BITS) && i < p->next)) {    \
            valid = true;                                                      \
        }                                                                      \
        return valid;                                                          \
    }
