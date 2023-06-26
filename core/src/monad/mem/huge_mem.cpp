#include <monad/mem/huge_mem.hpp>

#include <monad/core/assert.h>
#include <monad/core/running_on_ci.hpp>

#include <linux/mman.h>
#include <sys/mman.h>

#include <cassert>
#include <concepts>
#include <cstddef> // for std::byte
#include <cstdint> // for uintptr_t
#include <span>

MONAD_NAMESPACE_BEGIN

namespace
{
    template <std::unsigned_integral T>
    T round_up(T size, unsigned const bits)
    {
        T const mask = (T(1) << bits) - 1;
        bool const rem = size & mask;
        size >>= bits;
        size += rem;
        size <<= bits;
        return size;
    }
}

HugeMem::HugeMem(size_t const size)
    : size_{[size] {
        MONAD_ASSERT(size > 0);
        return round_up(size, MAP_HUGE_2MB >> MAP_HUGE_SHIFT);
    }()}
    , data_{[this] {
        auto *data = (std::byte *)mmap(
            nullptr,
            size_,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB,
            -1,
            0);
        /* CI if it has a supply of huge pages at all they will
        be very limited, so we need a fallback which synthesises
        huge pages on demand.
        */
        if (data == MAP_FAILED && running_on_ci()) {
            const auto toreserve =
                round_up(size_ << 1U, MAP_HUGE_2MB >> MAP_HUGE_SHIFT);
            // Reserve address space twice larger than the requested amount
            std::span<std::byte> reservation(
                (std::byte *)mmap(
                    nullptr,
                    toreserve,
                    PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                    -1,
                    0),
                toreserve);
            if (reservation.data() != MAP_FAILED) {
                data = (std::byte *)round_up(
                    uintptr_t(reservation.data()),
                    MAP_HUGE_2MB >> MAP_HUGE_SHIFT);
                std::span<std::byte> front(
                    reservation.data(), data - reservation.data());
                std::span<std::byte> back(
                    data + size_,
                    reservation.data() + reservation.size() - (data + size_));
                assert(front.data() + front.size() == data);
                assert(
                    back.data() + back.size() ==
                    reservation.data() + reservation.size());
                assert((((uintptr_t)data) & (MAP_HUGE_2MB - 1)) == 0);
                // Free the regions before and after to leave a
                // reservation aligned to the huge page size
                if (front.size() > 0) {
                    (void)munmap(front.data(), front.size());
                }
                if (back.size() > 0) {
                    (void)munmap(back.data(), back.size());
                }
                // Commit the reservation, this can fail on strict
                // memory accounting configured systems
                auto *committed = (std::byte *)mmap(
                    data,
                    size_,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                    -1,
                    0);
                if (committed == MAP_FAILED) {
                    // Free the reservation
                    (void)munmap(data, size_);
                    data = (std::byte *)MAP_FAILED;
                }
                else {
                    // For memory of huge page size
                    // alignment and granularity this is almost
                    // certainly already a huge page. But let's
                    // flag it as such anyway.
                    assert(committed == data);
                    data = committed;
                    (void)madvise(data, size_, MADV_HUGEPAGE);
                }
            }
            MONAD_ASSERT(data != MAP_FAILED);
        }
        return reinterpret_cast<unsigned char *>(data);
    }()}
{
    /**
     * TODO
     * - mbind (same numa node)
     */
    if (!running_on_ci()) {
        MONAD_ASSERT(!mlock(data_, size_));
    }
}

HugeMem::~HugeMem()
{
    if (!running_on_ci()) {
        MONAD_ASSERT(!munlock(data_, size_));
    }
    MONAD_ASSERT(!munmap(data_, size_));
}

MONAD_NAMESPACE_END
