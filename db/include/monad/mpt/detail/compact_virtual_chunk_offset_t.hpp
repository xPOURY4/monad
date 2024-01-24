#pragma once

#include <monad/mpt/util.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

namespace detail
{
    //! Low resolution offset type that truncates the last 16 bits of
    //! chunk_offset_t, for which we save space for `Node` without losing too
    //! much granularity in compaction offset.
    class compact_virtual_chunk_offset_t
    {
        static constexpr unsigned most_significant_bits = sizeof(uint32_t) * 8;
        static constexpr unsigned bits_to_truncate = 48 - most_significant_bits;
        uint32_t v_{0};

    public:
        constexpr compact_virtual_chunk_offset_t(uint32_t v)
            : v_{v}
        {
        }

        constexpr compact_virtual_chunk_offset_t(
            virtual_chunk_offset_t const offset)
            : v_{static_cast<uint32_t>(offset.raw() >> bits_to_truncate)}
        {
        }

        constexpr uint32_t get_count() const
        {
            // most significant 20 bits
            static constexpr unsigned count_bits = 20;
            return v_ >> (most_significant_bits - count_bits);
        }

        constexpr operator uint32_t() const noexcept
        {
            return v_;
        }

        constexpr compact_virtual_chunk_offset_t
        operator-(compact_virtual_chunk_offset_t const &o) const noexcept
        {
            return compact_virtual_chunk_offset_t(v_ - o.v_);
        }

        constexpr compact_virtual_chunk_offset_t &
        operator+=(compact_virtual_chunk_offset_t const &o)
        {
            v_ += o.v_;
            return *this;
        }
    };

    static_assert(sizeof(compact_virtual_chunk_offset_t) == 4);
    static_assert(alignof(compact_virtual_chunk_offset_t) == 4);
}

MONAD_MPT_NAMESPACE_END
