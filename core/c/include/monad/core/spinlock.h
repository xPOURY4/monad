#pragma once

#include <monad/core/tl_tid.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>

static_assert(ATOMIC_INT_LOCK_FREE == 2);

typedef atomic_int spin_lock_t;

static inline bool try_lock(spin_lock_t *const lock)
{
    int expected = 0;
    int const desired = get_tl_tid();
    return atomic_compare_exchange_weak_explicit(
        lock, &expected, desired, memory_order_acquire, memory_order_relaxed);
}

static inline void lock(spin_lock_t *const lock)
{
    int const desired = get_tl_tid();
    for (;;) {
        while (atomic_load_explicit(lock, memory_order_relaxed)) {
            __builtin_ia32_pause();
        }
        int expected = 0;
        if (atomic_compare_exchange_weak_explicit(
                lock,
                &expected,
                desired,
                memory_order_acquire,
                memory_order_relaxed)) {
            break;
        }
    }
}

static inline void unlock(spin_lock_t *const lock)
{
    atomic_store_explicit(lock, 0, memory_order_release);
}
