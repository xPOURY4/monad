#pragma once

#include <monad/core/tl_tid.h>

#include <stdatomic.h>

struct spin_lock
{
    _Atomic(int) lock_ = 0; // 0 when unlocked, thread_id when locked
};

void lock(spin_lock *const lock)
{
    for (;;) {
        while (atomic_load_explicit(&lock->lock_, memory_order_relaxed)) {
            __builtin_ia32_pause();
        }
        int old_val = 0;
        if (atomic_compare_exchange_weak_explicit(
                &lock->lock_,
                &old_val,
                get_tl_tid(),
                memory_order_acquire,
                memory_order_acquire)) {
            break;
        }
    }
}

void unlock(spin_lock *const lock)
{
    atomic_store_explicit(&lock->lock_, 0, memory_order_release);
}

bool try_lock(spin_lock *const lock)
{
    int old_val = 0;
    if (atomic_compare_exchange_weak_explicit(
            &lock->lock_,
            &old_val,
            get_tl_tid(),
            memory_order_acquire,
            memory_order_acquire)) {
        return true;
    }
    return false;
}
