#include <monad/mem/spinlock.h>

bool try_lock_disas(struct spin_lock *const lk)
{
    return try_lock(lk);
}

void lock_disas(struct spin_lock *const lk)
{
    lock(lk);
}

void unlock_disas(struct spin_lock *const lk)
{
    unlock(lk);
}
