#include <monad/mem/mem_map.hpp>

#include <monad/core/assert.h>

#include <unistd.h>

#include <linux/mman.h>
#include <sys/mman.h>

#include <bit>

MONAD_NAMESPACE_BEGIN

namespace
{
    size_t round_up(size_t size, unsigned const bits)
    {
        size_t const mask = (1UL << bits) - 1;
        bool const rem = size & mask;
        size >>= bits;
        size += rem;
        size <<= bits;
        return size;
    }

    size_t getpagesize()
    {
        static size_t const pagesize = [] {
            long const sc_pagesize = sysconf(_SC_PAGESIZE);
            MONAD_ASSERT(sc_pagesize > 0);
            return static_cast<size_t>(sc_pagesize);
        }();
        return pagesize;
    }
}

MemMap::MemMap(size_t const size, size_t pagesize)
    : size_{[size, &pagesize] {
        MONAD_ASSERT(size > 0);
        if (!pagesize) {
            pagesize = getpagesize();
        }
        MONAD_ASSERT(std::has_single_bit(pagesize));
        return round_up(size, pagesize);
    }()}
    , data_{[this, pagesize] {
        int huge_flags = 0;
        if (pagesize > getpagesize()) {
            size_t const pagebits = std::countr_zero(pagesize);
            huge_flags |= MAP_HUGETLB;
            huge_flags |= pagebits << MAP_HUGE_SHIFT;
        }
        void *const data = mmap(
            nullptr,
            size_,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | huge_flags,
            -1,
            0);
        MONAD_ASSERT(data != MAP_FAILED);
        return static_cast<unsigned char *>(data);
    }()}
{
    /**
     * TODO
     * - mbind (same numa node)
     * - mlock (no paging)
     */
}

MemMap::~MemMap()
{
    MONAD_ASSERT(!munmap(data_, size_));
}

MONAD_NAMESPACE_END
