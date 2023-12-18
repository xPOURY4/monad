#include <monad/core/spinlock.h>

bool try_lock_disas(spin_lock_t *const lk)
{
    return try_lock(lk);
}

void lock_disas(spin_lock_t *const lk)
{
    lock(lk);
}

void unlock_disas(spin_lock_t *const lk)
{
    unlock(lk);
}
