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

#define BITS 21
#define SIZE (1 << BITS)
#define MASK (SIZE - 1)

typedef struct
{
    unsigned char *mem;
    uint32_t next;
} pool_t;

static_assert(sizeof(pool_t) == 16, "");
static_assert(alignof(pool_t) == 8, "");

static inline void pool_init(pool_t *const p, unsigned char *const mem)
{
    p->mem = mem;
    p->next = 0;
}

static inline unsigned char *pool_ptr(pool_t const *const p, uint32_t const i)
{
    return p->mem + (i & MASK);
}

static inline uint32_t pool_reserve(pool_t *const p, uint32_t const n)
{
    uint32_t const m_next = p->next & MASK;
    if (MONAD_UNLIKELY(m_next + n > SIZE)) {
        p->next += SIZE - m_next;
    }
    return p->next;
}

static inline void pool_advance(pool_t *const p, uint32_t const n)
{
    p->next += n;
}

/**
 * valid objects are in [next - SIZE, next)
 * invalid objects are in [next, next - SIZE)
 */
static inline bool pool_valid(pool_t const *const p, uint32_t const i)
{
    bool valid = false;
    if (MONAD_UNLIKELY(SIZE > p->next)) {
        if (i < p->next || i >= p->next - SIZE) {
            valid = true;
        }
    }
    else if (MONAD_LIKELY(i >= p->next - SIZE && i < p->next)) {
        valid = true;
    }
    return valid;
}
