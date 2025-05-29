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
    struct uint256_t;
}

extern "C" void monad_vm_runtime_mul(
    monad::vm::utils::uint256_t *, monad::vm::utils::uint256_t const *,
    monad::vm::utils::uint256_t const *) noexcept;

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

        [[gnu::always_inline]] friend inline constexpr bool
        constexpr_operator_bool(uint256_t const &x) noexcept
        {
            return x.words_[0] | x.words_[1] | x.words_[2] | x.words_[3];
        }

        [[gnu::always_inline]]
        friend inline bool avx2_operator_bool(uint256_t const &x) noexcept
        {
            auto mask = _mm256_setr_epi64x(-1, -1, -1, -1);
            return !_mm256_testz_si256(x.to_avx(), mask);
        }

        [[gnu::always_inline]]
        inline constexpr explicit operator bool() const noexcept
        {
            if consteval {
                return constexpr_operator_bool(*this);
            }
            else {
                return avx2_operator_bool(*this);
            }
        }

        template <typename Int>
        [[gnu::always_inline]]
        inline constexpr explicit operator Int() const noexcept
            requires(
                std::is_integral_v<Int> && sizeof(Int) <= sizeof(word_type))
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
        inline uint8_t *as_bytes() noexcept
        {
            return reinterpret_cast<uint8_t *>(&words_);
        }

        [[gnu::always_inline]]
        inline uint8_t const *as_bytes() const noexcept
        {
            return reinterpret_cast<uint8_t const *>(&words_);
        }

#define INHERIT_INTX_BINOP(return_ty, op_name)                                 \
    [[gnu::always_inline]] friend inline constexpr return_ty operator op_name( \
        uint256_t const &x, uint256_t const &y) noexcept                       \
    {                                                                          \
        return return_ty(x.to_intx() op_name y.to_intx());                     \
    }

        INHERIT_INTX_BINOP(uint256_t, /);
        INHERIT_INTX_BINOP(uint256_t, %);
#undef INHERIT_INTX_BINOP

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator+(uint256_t const &lhs, uint256_t const &rhs) noexcept
        {
            static_assert(sizeof(unsigned long long) == sizeof(uint64_t));
            unsigned long long carry = 0;
            uint256_t result;
            result[0] = __builtin_addcll(lhs[0], rhs[0], carry, &carry);
            result[1] = __builtin_addcll(lhs[1], rhs[1], carry, &carry);
            result[2] = __builtin_addcll(lhs[2], rhs[2], carry, &carry);
            result[3] = __builtin_addcll(lhs[3], rhs[3], carry, &carry);
            return result;
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator-(uint256_t const &lhs, uint256_t const &rhs) noexcept
        {
            static_assert(sizeof(unsigned long long) == sizeof(uint64_t));
            unsigned long long borrow = 0;
            uint256_t result;
            result[0] = __builtin_subcll(lhs[0], rhs[0], borrow, &borrow);
            result[1] = __builtin_subcll(lhs[1], rhs[1], borrow, &borrow);
            result[2] = __builtin_subcll(lhs[2], rhs[2], borrow, &borrow);
            result[3] = __builtin_subcll(lhs[3], rhs[3], borrow, &borrow);
            return result;
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator*(uint256_t const &lhs, uint256_t const &rhs) noexcept
        {
            uint256_t result;
            monad_vm_runtime_mul(&result, &lhs, &rhs);
            return result;
        }

        [[gnu::always_inline]]
        inline constexpr uint256_t &operator*=(uint256_t const &rhs) noexcept
        {
            monad_vm_runtime_mul(this, this, &rhs);
            return *this;
        }

        [[gnu::always_inline]]
        friend inline constexpr bool
        operator<(uint256_t const &lhs, uint256_t const &rhs) noexcept
        {
            static_assert(sizeof(unsigned long long) == sizeof(uint64_t));
            unsigned long long borrow = 0;
            uint256_t result;
            result[0] = __builtin_subcll(lhs[0], rhs[0], borrow, &borrow);
            result[1] = __builtin_subcll(lhs[1], rhs[1], borrow, &borrow);
            result[2] = __builtin_subcll(lhs[2], rhs[2], borrow, &borrow);
            result[3] = __builtin_subcll(lhs[3], rhs[3], borrow, &borrow);
            return borrow;
        }

        [[gnu::always_inline]]
        friend inline constexpr bool
        operator<=(uint256_t const &lhs, uint256_t const &rhs) noexcept
        {
            return !(lhs > rhs);
        }

        [[gnu::always_inline]]
        friend inline constexpr bool
        operator>(uint256_t const &lhs, uint256_t const &rhs) noexcept
        {
            return rhs < lhs;
        }

        [[gnu::always_inline]]
        friend inline constexpr bool
        operator>=(uint256_t const &lhs, uint256_t const &rhs) noexcept
        {
            return rhs <= lhs;
        }

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
        constexpr_operator_eq(uint256_t const &x, uint256_t const &y)
        {
            return (x[0] ^ y[0]) | (x[1] ^ y[1]) | (x[2] ^ y[2]) |
                   (x[3] ^ y[3]);
        }

        [[gnu::always_inline]] friend inline bool
        avx2_operator_eq(uint256_t const &x, uint256_t const &y)
        {
            auto xor_bits = x.to_avx() ^ y.to_avx();
            return _mm256_testz_si256(xor_bits, xor_bits);
        }

        [[gnu::always_inline]] friend inline constexpr bool
        operator==(uint256_t const &x, uint256_t const &y)
        {
            if consteval {
                return constexpr_operator_eq(x, y);
            }
            else {
                return avx2_operator_eq(x, y);
            }
        }

        [[gnu::always_inline]] inline constexpr uint256_t operator-() const
        {
            return 0 - *this;
        }

        [[gnu::always_inline]] inline constexpr uint256_t operator~() const
        {
            return uint256_t(~this->to_avx());
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        constexpr_operator_shl(uint256_t const &x, uint64_t shift)
        {
            if (shift >= 256) [[unlikely]] {
                return 0;
            }
            shift &= 255;
            if (shift < 128) {
                if (shift < 64) [[unlikely]] {
                    return uint256_t{
                        x[0] << shift,
                        (x[1] << shift) | ((x[0] >> 1) >> (63 - shift)),
                        (x[2] << shift) | ((x[1] >> 1) >> (63 - shift)),
                        (x[3] << shift) | ((x[2] >> 1) >> (63 - shift))};
                }
                else {
                    shift &= 63;
                    return uint256_t{
                        0,
                        x[0] << shift,
                        (x[1] << shift) | ((x[0] >> 1) >> (63 - shift)),
                        (x[2] << shift) | ((x[1] >> 1) >> (63 - shift))};
                }
            }
            else {
                shift &= 127;
                if (shift < 64) {
                    return uint256_t{
                        0,
                        0,
                        x[0] << shift,
                        (x[1] << shift) | ((x[0] >> 1) >> (63 - shift))};
                }
                else {
                    shift &= 63;
                    return uint256_t{0, 0, 0, x[0] << shift};
                }
            }
        }

        [[gnu::always_inline]]
        friend inline uint256_t
        avx2_operator_shl(uint256_t const &x0, uint64_t shift)
        {
            static int32_t const table[34] = {
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  1,  2,  3,  4,  5,  6,  7,
                0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1};

            uint64_t table_ix = 8 - std::min(shift >> 5, uint64_t{8});

            __m256i r;
            std::memcpy(&r, &table[table_ix], 32);
            __m256i l;
            std::memcpy(&l, &(table + 1)[table_ix], 32);
            __m256i r_mask;
            std::memcpy(&r_mask, &(table + 17)[table_ix], 32);
            __m256i l_mask;
            std::memcpy(&l_mask, &(table + 18)[table_ix], 32);

            __m256i x = std::bit_cast<__m256i>(x0);
            auto l_perm = _mm256_permutevar8x32_epi32(x, l) & l_mask;
            auto r_perm = _mm256_permutevar8x32_epi32(x, r) & r_mask;

            uint32_t bit_shift = static_cast<uint32_t>(shift & 31);
            auto l_shifted =
                _mm256_sll_epi32(l_perm, _mm_set1_epi64x(bit_shift));
            auto r_shifted =
                _mm256_srl_epi32(r_perm, _mm_set1_epi64x(32 - bit_shift));

            return std::bit_cast<uint256_t>(l_shifted | r_shifted);
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator<<(uint256_t const &x, uint64_t shift) noexcept
        {
            if consteval {
                return constexpr_operator_shl(x, shift);
            }
            else {
                return avx2_operator_shl(x, shift);
            }
        }

        [[gnu::always_inline]]
        inline constexpr uint256_t &operator<<=(uint256_t const &shift) noexcept
        {
            return *this = *this << shift;
        }

        [[gnu::always_inline]] friend inline constexpr uint256_t
        operator<<(uint256_t const &x, uint256_t const &shift) noexcept
        {
            if (shift[1] | shift[2] | shift[3]) [[unlikely]] {
                return 0;
            }
            return x << shift[0];
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        constexpr_operator_shr(uint256_t const &x, uint64_t shift)
        {
            if (shift >= 256) [[unlikely]] {
                return 0;
            }
            shift &= 255;
            if (shift < 128) {
                if (shift < 64) [[unlikely]] {
                    return uint256_t{
                        (x[0] >> shift) | ((x[1] << 1) << (63 - shift)),
                        (x[1] >> shift) | ((x[2] << 1) << (63 - shift)),
                        (x[2] >> shift) | ((x[3] << 1) << (63 - shift)),
                        x[3] >> shift,
                    };
                }
                else {
                    shift &= 63;
                    return uint256_t{
                        (x[1] >> shift) | ((x[2] << 1) << (63 - shift)),
                        (x[2] >> shift) | ((x[3] << 1) << (63 - shift)),
                        x[3] >> shift,
                        0};
                }
            }
            else {
                shift &= 127;
                if (shift < 64) {
                    return uint256_t{
                        (x[2] >> shift) | ((x[3] << 1) << (63 - shift)),
                        x[3] >> shift,
                        0,
                        0};
                }
                else {
                    shift &= 63;
                    return uint256_t{x[3] >> shift, 0, 0, 0};
                }
            }
        }

        [[gnu::always_inline]]
        friend inline uint256_t
        avx2_operator_shr(uint256_t const &x0, uint64_t shift)
        {
            static int32_t const table[34] = {
                0,  1,  2,  3,  4,  5,  6,  7,  0, 0, 0, 0, 0, 0, 0, 0, 0,
                -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0};

            uint64_t table_ix = std::min(shift >> 5, uint64_t{8});

            __m256i x = std::bit_cast<__m256i>(x0);

            __m256i rs;
            std::memcpy(&rs, &table[table_ix], 32);
            __m256i ls;
            std::memcpy(&ls, &(table + 1)[table_ix], 32);

            __m256i r_mask;
            std::memcpy(&r_mask, &(table + 17)[table_ix], 32);
            __m256i l_mask;
            std::memcpy(&l_mask, &(table + 18)[table_ix], 32);

            auto r_perm = _mm256_permutevar8x32_epi32(x, rs) & r_mask;
            auto l_perm = _mm256_permutevar8x32_epi32(x, ls) & l_mask;

            uint32_t bit_shift = static_cast<uint32_t>(shift & 31);
            auto r_shifted =
                _mm256_srl_epi32(r_perm, _mm_set1_epi64x(bit_shift));
            auto l_shifted =
                _mm256_sll_epi32(l_perm, _mm_set1_epi64x(32 - bit_shift));

            return std::bit_cast<uint256_t>(r_shifted | l_shifted);
        }

        [[gnu::always_inline]]
        friend inline constexpr uint256_t
        operator>>(uint256_t const &x, uint64_t shift) noexcept
        {
            if consteval {
                return constexpr_operator_shr(x, shift);
            }
            else {
                return avx2_operator_shr(x, shift);
            }
        }

        [[gnu::always_inline]] friend inline constexpr uint256_t
        operator>>(uint256_t const &x, uint256_t const &shift) noexcept
        {
            if (shift[1] | shift[2] | shift[3]) [[unlikely]] {
                return 0;
            }
            return x >> shift[0];
        }

        [[gnu::always_inline]]
        inline constexpr uint256_t &operator>>=(uint256_t const &shift) noexcept
        {
            return *this = *this >> shift;
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
        inline DstT store_be() const noexcept
        {
            return ::intx::be::store<DstT>(this->to_intx());
        }

        [[gnu::always_inline]]
        inline void store_be(uint8_t *dest) const noexcept
        {
            std::uint64_t ts[4] = {
                std::byteswap((*this)[3]),
                std::byteswap((*this)[2]),
                std::byteswap((*this)[1]),
                std::byteswap((*this)[0])};
            std::memcpy(dest, ts, 32);
        }

        [[gnu::always_inline]]
        inline void store_le(uint8_t *dest) const noexcept
        {
            std::memcpy(dest, &this->words_, 32);
        }

        [[gnu::always_inline]]
        inline std::string to_string(int base = 10) const
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

    [[gnu::always_inline]] inline constexpr uint32_t
    count_significant_words(uint256_t const &x) noexcept
    {
        for (size_t i = uint256_t::num_words; i > 0; --i) {
            if (x[i - 1] != 0) {
                return static_cast<uint32_t>(i);
            }
        }
        return 0;
    }

    [[gnu::always_inline]]
    inline constexpr uint32_t
    count_significant_bytes(uint256_t const &x) noexcept
    {
        auto significant_words = count_significant_words(x);
        if (significant_words == 0) {
            return 0;
        }
        else {
            auto leading_word_leading_zeros = static_cast<uint32_t>(
                std::countl_zero(x[significant_words - 1]));
            return leading_word_leading_zeros + (significant_words - 1) * 8;
        }
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
        auto sign_x = x[3] >> 63;
        auto sign_y = y[3] >> 63;
        if (sign_x == sign_y) {
            return x < y;
        }
        else {
            return sign_x;
        }
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
        static_assert(sizeof(unsigned long long) == sizeof(uint64_t));
        unsigned long long carry_out;
        auto value = __builtin_addcll(x, y, carry, &carry_out);
        return result_with_carry{
            .value = value, .carry = static_cast<bool>(carry_out)};
    }

    [[gnu::always_inline]] inline constexpr uint256_t
    exp(uint256_t base, uint256_t const &exponent) noexcept
    {
        uint256_t result{1};
        if (base == 2) {
            return result << exponent;
        }

        size_t sig_words = count_significant_words(exponent.to_intx());
        for (size_t w = 0; w < sig_words; w++) {
            uint64_t word_exp = exponent[w];
            int32_t significant_bits =
                w + 1 == sig_words ? 64 - std::countl_zero(word_exp) : 64;
            while (significant_bits) {
                if (word_exp & 1) {
                    result *= base;
                }
                base *= base;
                word_exp >>= 1;
                significant_bits -= 1;
            }
        }
        return result;
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

    inline constexpr size_t countl_zero(uint256_t const &x)
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
