#pragma once

#include <bit>
#include <format>
#include <immintrin.h>
#include <intx/intx.hpp>
#include <limits>

#ifndef __AVX2__
    #error "Target architecture must support AVX2"
#endif

namespace monad::vm::utils
{
    struct uint256_t
    {
        using word_type = uint64_t;
        static constexpr auto word_num_bits = sizeof(word_type) * 8;
        static constexpr auto num_bits = 256;
        static constexpr auto num_bytes = num_bits / 8;
        static constexpr auto num_words = num_bits / word_num_bits;

    private:
        std::array<uint64_t, num_words> words_{0, 0, 0, 0};

    public:
        template <typename... T>
        [[gnu::always_inline]] constexpr explicit(false)
            uint256_t(T... v) noexcept
            requires std::conjunction_v<std::is_convertible<T, uint64_t>...>
            : words_{static_cast<uint64_t>(v)...}
        {
        }

        [[gnu::always_inline]] constexpr explicit(true)
            uint256_t(std::array<uint64_t, 4> const &x) noexcept
            : words_{x}
        {
        }

        template <typename... T>
        [[gnu::always_inline]]
        constexpr explicit(true) uint256_t(::intx::uint256 const &x) noexcept
            : words_{x[0], x[1], x[2], x[3]}
        {
        }

        [[gnu::always_inline]]
        inline constexpr ::intx::uint256 to_intx() const noexcept
        {
            return ::intx::uint256{words_[0], words_[1], words_[2], words_[3]};
        }

        [[gnu::always_inline]]
        constexpr explicit(true) uint256_t(__m256i x) noexcept
            : words_{std::bit_cast<std::array<uint64_t, num_words>>(x)}
        {
        }

        [[gnu::always_inline]]
        inline constexpr __m256i to_avx() const noexcept
        {
            return std::bit_cast<__m256i>(words_);
        }

        template <typename Int>
        [[gnu::always_inline]]
        inline constexpr explicit operator Int() const noexcept
            requires std::is_integral_v<Int>
        {
            return static_cast<Int>(words_[0]);
        }

        [[gnu::always_inline]]
        inline constexpr uint64_t &operator[](size_t i) noexcept
        {
            return words_[i];
        }

        [[gnu::always_inline]]
        inline constexpr uint64_t const &operator[](size_t i) const noexcept
        {
            return words_[i];
        }

        [[gnu::always_inline]]
        inline constexpr uint8_t *as_bytes() noexcept
        {
            return reinterpret_cast<uint8_t *>(&words_);
        }

        [[gnu::always_inline]]
        inline constexpr uint8_t const *as_bytes() const noexcept
        {
            return reinterpret_cast<uint8_t const *>(&words_);
        }

#define INHERIT_INTX_BINOP(return_ty, op_name)                                 \
    [[gnu::always_inline]] friend inline constexpr return_ty operator op_name( \
        uint256_t const &x, uint256_t const &y) noexcept                       \
    {                                                                          \
        return return_ty(x.to_intx() op_name y.to_intx());                     \
    }

        INHERIT_INTX_BINOP(uint256_t, +);
        INHERIT_INTX_BINOP(uint256_t, -);
        INHERIT_INTX_BINOP(uint256_t, *);

        INHERIT_INTX_BINOP(uint256_t, /);
        INHERIT_INTX_BINOP(uint256_t, %);
        INHERIT_INTX_BINOP(uint256_t, <<);
        INHERIT_INTX_BINOP(uint256_t, >>);

        INHERIT_INTX_BINOP(bool, <);
        INHERIT_INTX_BINOP(bool, <=);
        INHERIT_INTX_BINOP(bool, >);
        INHERIT_INTX_BINOP(bool, >=);

#undef INHERIT_INTX_BINOP

#define INHERIT_AVX_BINOP(return_ty, op_name)                                  \
    [[gnu::always_inline]] friend inline constexpr return_ty operator op_name( \
        uint256_t const &x, uint256_t const &y)                                \
    {                                                                          \
        return return_ty(x.to_avx() op_name y.to_avx());                       \
    }
        INHERIT_AVX_BINOP(uint256_t, &);
        INHERIT_AVX_BINOP(uint256_t, |);
        INHERIT_AVX_BINOP(uint256_t, ^);
#undef INHERIT_AVX_BINOP

        [[gnu::always_inline]] friend inline constexpr bool
        operator==(uint256_t const &x, uint256_t const &y)
        {
            if (std::is_constant_evaluated()) {
                return x.to_intx() == y.to_intx();
            }
            else {
                auto xor_bits = x.to_avx() ^ y.to_avx();
                return _mm256_testz_si256(xor_bits, xor_bits);
            }
        }

        [[gnu::always_inline]] inline constexpr uint256_t operator-() const
        {
            return uint256_t(-this->to_intx());
        }

        [[gnu::always_inline]] inline constexpr uint256_t operator~() const
        {
            return uint256_t(~this->to_avx());
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator<<(uint256_t const &x, uint64_t shift) noexcept
        {
            return uint256_t(x.to_intx() << shift);
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator<<(uint256_t const &x, std::integral auto shift) noexcept
        {
            return uint256_t(x.to_intx() << shift);
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator>>(uint256_t const &x, uint64_t shift) noexcept
        {
            return uint256_t(x.to_intx() >> shift);
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator>>(uint256_t const &x, std::integral auto shift) noexcept
        {
            return uint256_t(x.to_intx() >> shift);
        }

        [[gnu::always_inline]]
        inline constexpr uint256_t &operator>>=(uint256_t const &shift) noexcept
        {
            return *this = *this >> shift;
        }

        [[gnu::always_inline]]
        inline constexpr uint256_t &operator<<=(uint256_t const &shift) noexcept
        {
            return *this = *this << shift;
        }

        [[gnu::always_inline]]
        static inline constexpr uint256_t
        load_be(uint8_t const (&bytes)[num_bytes]) noexcept
        {
            return uint256_t(::intx::be::load<::intx::uint256>(bytes));
        }

        [[gnu::always_inline]]
        static inline constexpr uint256_t
        load_le(uint8_t const (&bytes)[num_bytes]) noexcept
        {
            return uint256_t(::intx::le::load<::intx::uint256>(bytes));
        }

        [[gnu::always_inline]]
        static inline constexpr uint256_t
        load_be_unsafe(uint8_t const *bytes) noexcept
        {
            return uint256_t(::intx::be::unsafe::load<::intx::uint256>(bytes));
        }

        [[gnu::always_inline]] static inline constexpr uint256_t
        load_le_unsafe(uint8_t const *bytes) noexcept
        {
            return uint256_t(::intx::le::unsafe::load<::intx::uint256>(bytes));
        }

        template <typename DstT>
        [[gnu::always_inline]]
        inline constexpr DstT store_be() const noexcept
        {
            return ::intx::be::store<DstT>(this->to_intx());
        }

        [[gnu::always_inline]]
        inline constexpr void store_be(uint8_t *dest) const noexcept
        {
            std::uint64_t ts[4] = {
                std::byteswap((*this)[3]),
                std::byteswap((*this)[2]),
                std::byteswap((*this)[1]),
                std::byteswap((*this)[0])};
            std::memcpy(dest, ts, 32);
        }

        [[gnu::always_inline]]
        inline constexpr void store_le(uint8_t *dest) const noexcept
        {
            std::memcpy(dest, &this->words_, 32);
        }

        [[gnu::always_inline]]
        inline constexpr std::string to_string(int base = 10) const
        {
            return ::intx::to_string(this->to_intx(), base);
        }

        [[gnu::always_inline]] static inline constexpr uint256_t
        from_string(std::string const &s);
    };

    static_assert(
        alignof(uint256_t) == alignof(::intx::uint256),
        "Alignment of uint256_t is incompatible with intx");
    static_assert(
        sizeof(uint256_t) == sizeof(::intx::uint256),
        "Size of uint256_t is incompatible with intx");

    uint256_t signextend(uint256_t const &byte_index, uint256_t const &x);
    uint256_t byte(uint256_t const &byte_index, uint256_t const &x);
    uint256_t sar(uint256_t const &shift_index, uint256_t const &x);
    uint256_t countr_zero(uint256_t const &x);

    constexpr size_t popcount(uint256_t const &x)
    {
        return static_cast<size_t>(
            std::popcount(x[0]) + std::popcount(x[1]) + std::popcount(x[2]) +
            std::popcount(x[3]));
    }

    [[gnu::always_inline]]
    inline constexpr uint32_t
    count_significant_bytes(uint256_t const &x) noexcept
    {
        return ::intx::count_significant_bytes(x.to_intx());
    }

    struct div_result
    {
        uint256_t quot;
        uint256_t rem;
    };

    [[gnu::always_inline]]
    inline constexpr div_result
    sdivrem(uint256_t const &x, uint256_t const &y) noexcept
    {
        auto result = ::intx::sdivrem(x.to_intx(), y.to_intx());
        return {uint256_t(result.quot), uint256_t(result.rem)};
    }

    [[gnu::always_inline]]
    inline constexpr bool slt(uint256_t const &x, uint256_t const &y) noexcept
    {
        return ::intx::slt(x.to_intx(), y.to_intx());
    }

    [[gnu::always_inline]]
    inline constexpr uint256_t addmod(
        uint256_t const &x, uint256_t const &y, uint256_t const &mod) noexcept
    {
        return uint256_t(
            ::intx::addmod(x.to_intx(), y.to_intx(), mod.to_intx()));
    }

    [[gnu::always_inline]]
    inline constexpr uint256_t mulmod(
        uint256_t const &x, uint256_t const &y, uint256_t const &mod) noexcept
    {
        return uint256_t(
            ::intx::mulmod(x.to_intx(), y.to_intx(), mod.to_intx()));
    }

    struct result_with_carry
    {
        uint64_t value;
        bool carry;
    };

    [[gnu::always_inline]]
    inline constexpr result_with_carry
    addc(uint64_t x, uint64_t y, bool carry = false) noexcept
    {
        auto result = ::intx::addc(x, y, carry);
        return {result.value, result.carry};
    }

    [[gnu::always_inline]] inline constexpr uint256_t
    exp(uint256_t const &base, uint256_t const &exponent) noexcept
    {
        return uint256_t(::intx::exp(base.to_intx(), exponent.to_intx()));
    }

    consteval uint256_t operator""_u256(char const *s)
    {
        return uint256_t(::intx::from_string<intx::uint256>(s));
    }

    /**
     * Parse a range of raw bytes with length `n` into a 256-bit big-endian word
     * value.
     *
     * If there are fewer than `n` bytes remaining in the source data (that is,
     * `remaining < n`), then treat the input as if it had been padded to the
     * right with zero bytes.
     */
    uint256_t
    from_bytes(std::size_t n, std::size_t remaining, uint8_t const *src);

    /**
     * Parse a range of raw bytes with length `n` into a 256-bit big-endian word
     * value.
     *
     * There must be at least `n` bytes readable from `src`; if there are not,
     * use the safe overload that allows for the number of bytes remaining to be
     * specified.
     */
    uint256_t from_bytes(std::size_t n, uint8_t const *src);

    inline size_t countl_zero(uint256_t const &x)
    {
        size_t cnt = 0;
        for (size_t i = 0; i < 4; i++) {
            cnt += static_cast<size_t>(std::countl_zero(x[3 - i]));
            if (cnt != ((i + 1U) * 64U)) {
                return cnt;
            }
        }
        return cnt;
    }

    consteval uint256_t pow2(size_t n)
    {
        return uint256_t{1} << n;
    }
}

namespace std
{
    template <>
    struct numeric_limits<monad::vm::utils::uint256_t>
    {
        using type = monad::vm::utils::uint256_t;

        static constexpr bool is_specialized = true;
        static constexpr bool is_integer = true;
        static constexpr bool is_signed = false;
        static constexpr bool is_exact = true;
        static constexpr bool has_infinity = false;
        static constexpr bool has_quiet_NaN = false;
        static constexpr bool has_signaling_NaN = false;
        static constexpr float_denorm_style has_denorm = denorm_absent;
        static constexpr bool has_denorm_loss = false;
        static constexpr float_round_style round_style = round_toward_zero;
        static constexpr bool is_iec559 = false;
        static constexpr bool is_bounded = true;
        static constexpr bool is_modulo = true;
        static constexpr int digits = CHAR_BIT * sizeof(type);
        static constexpr int digits10 = int(0.3010299956639812 * digits);
        static constexpr int max_digits10 = 0;
        static constexpr int radix = 2;
        static constexpr int min_exponent = 0;
        static constexpr int min_exponent10 = 0;
        static constexpr int max_exponent = 0;
        static constexpr int max_exponent10 = 0;
        static constexpr bool traps = std::numeric_limits<unsigned>::traps;
        static constexpr bool tinyness_before = false;

        static constexpr type min() noexcept
        {
            return 0;
        }

        static constexpr type lowest() noexcept
        {
            return min();
        }

        static constexpr type max() noexcept
        {
            return ~type{0};
        }

        static constexpr type epsilon() noexcept
        {
            return 0;
        }

        static constexpr type round_error() noexcept
        {
            return 0;
        }

        static constexpr type infinity() noexcept
        {
            return 0;
        }

        static constexpr type quiet_NaN() noexcept
        {
            return 0;
        }

        static constexpr type signaling_NaN() noexcept
        {
            return 0;
        }

        static constexpr type denorm_min() noexcept
        {
            return 0;
        }
    };
}

namespace monad::vm::utils
{
    inline size_t bit_width(uint256_t const &x)
    {
        return static_cast<size_t>(std::numeric_limits<uint256_t>::digits) -
               countl_zero(x);
    }

    [[gnu::always_inline]]
    inline constexpr uint256_t uint256_t::from_string(std::string const &s)
    {
        return ::intx::from_string<uint256_t>(s);
    }
}

template <>
struct std::formatter<monad::vm::utils::uint256_t>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto
    format(monad::vm::utils::uint256_t const &v, std::format_context &ctx) const
    {
        return std::format_to(
            ctx.out(), "0x{}", intx::to_string(v.to_intx(), 16));
    }
};
