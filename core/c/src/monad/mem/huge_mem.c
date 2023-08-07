#include <monad/mem/huge_mem.h>

#include <monad/core/assert.h>

#include <linux/mman.h>
#include <sys/mman.h>

static size_t round_up(size_t size, unsigned const bits)
{
    size_t const mask = (1UL << bits) - 1;
    _Bool const rem = size & mask; /* TODO vscode bool */
    size >>= bits;
    size += rem;
    size <<= bits;
    return size;
}

/**
 * TODO
 * - mbind (same numa node)
 * - mlock (no paging)
 */
void huge_mem_alloc(huge_mem_t *const mem, size_t const size)
{
    MONAD_ASSERT(mem->size == 0);
    MONAD_ASSERT(mem->data == NULL);
    MONAD_ASSERT(size > 0);
    mem->size = round_up(size, MAP_HUGE_2MB >> MAP_HUGE_SHIFT);
    mem->data = mmap(
        NULL,
        mem->size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB,
        -1,
        0);
    MONAD_ASSERT(mem->data != MAP_FAILED);
}

void huge_mem_free(huge_mem_t *const mem)
{
    MONAD_ASSERT(mem->size);
    MONAD_ASSERT(mem->data);
    MONAD_ASSERT(!munmap(mem->data, mem->size));
    mem->size = 0;
    mem->data = NULL;
}
