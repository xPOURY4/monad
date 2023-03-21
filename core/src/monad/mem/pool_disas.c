#include <monad/mem/pool.h>

void pool_init_disas(pool_t *const p, unsigned char *const mem)
{
    pool_init(p, mem);
}

unsigned char *pool_ptr_disas(pool_t *const p, uint32_t const i)
{
    return pool_ptr(p, i);
}

uint32_t pool_reserve_disas(pool_t *const p, uint32_t const n)
{
    return pool_reserve(p, n);
}

void pool_advance_disas(pool_t *const p, uint32_t const n)
{
    pool_advance(p, n);
}

bool pool_valid_disas(pool_t *const p, uint32_t const i)
{
    return pool_valid(p, i);
}
