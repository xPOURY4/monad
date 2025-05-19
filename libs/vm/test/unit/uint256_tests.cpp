#include <cstddef>
#include <intx/intx.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <gtest/gtest.h>
#include <sys/types.h>

using namespace monad::vm::utils;

TEST(uint256, signextend)
{
    uint256_t i;
    uint256_t x;

    i = 0;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), 0);

    i = 1;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), ~uint256_t{0xffff} | x);

    i = 2;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), ~uint256_t{0xffffff} | x);

    i = 3;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), x);

    i = 30;
    x = uint256_t{0x0080} << 240;
    ASSERT_EQ(signextend(i, x), uint256_t{0xff80} << 240);

    i = 30;
    x = uint256_t{0x0070} << 240;
    ASSERT_EQ(signextend(i, x), uint256_t{0x0070} << 240);

    i = 31;
    x = uint256_t{0xf0} << 248;
    ASSERT_EQ(signextend(i, x), uint256_t{0xf0} << 248);
}

TEST(uint256, byte)
{
    uint256_t i;
    uint256_t x;

    i = 31;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0);

    i = 30;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0x80);

    i = 29;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0xff);

    i = 28;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0);

    i = 1;
    x = uint256_t{0x0080} << 240;
    ASSERT_EQ(byte(i, x), 0x80);

    i = 0;
    x = uint256_t{0x0080} << 240;
    ASSERT_EQ(byte(i, x), 0);

    i = 0;
    x = uint256_t{0xf0} << 248;
    ASSERT_EQ(byte(i, x), 0xf0);

    i = 32;
    x = uint256_t{0xff} << 248;
    ASSERT_EQ(byte(i, x), 0);
}

TEST(uint256, sar)
{
    uint256_t i;
    uint256_t x;

    i = 0;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), x);

    i = 1;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), uint256_t{0xc0} << 248);

    i = 1;
    x = uint256_t{0x70} << 248;
    ASSERT_EQ(sar(i, x), uint256_t{0x38} << 248);

    i = 255;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), ~uint256_t{0});

    i = 254;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), ~uint256_t{0} - 1);

    i = 254;
    x = uint256_t{0x40} << 248;
    ASSERT_EQ(sar(i, x), 1);

    i = 255;
    x = uint256_t{0x7f} << 248;
    ASSERT_EQ(sar(i, x), 0);
}

template <size_t N>
void test_bit_width()
{
    ASSERT_EQ(bit_width(pow2(N)), N + 1);
    if constexpr (N > 0) {
        test_bit_width<N - 1>();
    }
}

TEST(uint256, bit_width)
{
    test_bit_width<255>();
}

::intx::uint256 from_words(std::array<uint64_t, 4> words)
{
    return ::intx::uint256{words[0], words[1], words[2], words[3]};
}

TEST(uint256, intx_iso)
{
    uint64_t ONES = ~uint64_t(0);
    std::array<uint64_t, 4> inputs[]{
        {0, 0, 0, 0},
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
        {ONES, ONES, ONES, ONES},
        {ONES, 0, 0, 0},
        {0, ONES, 0, 0},
        {0, 0, ONES, 0},
        {0, 0, 0, ONES},
        {0x12345678, 0x9abcdef0, 0x87654321, 0x0fedcba9}};

    for (auto input : inputs) {
        auto x = uint256_t{input};
        auto intx = from_words(input);
        ASSERT_EQ(x.to_intx(), intx);
        ASSERT_EQ(x, uint256_t(intx));
    }
}

TEST(uint256, avx_iso)
{
    uint64_t ones = ~uint64_t(0);
    std::vector<uint256_t> inputs{
        {0, 0, 0, 0},
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
        {ones, ones, ones, ones},
        {ones, 0, 0, 0},
        {0, ones, 0, 0},
        {0, 0, ones, 0},
        {0, 0, 0, ones},
        {0x12345678, 0x9abcdef0, 0x87654321, 0x0fedcba9}};

    for (auto input : inputs) {
        ASSERT_EQ(input, uint256_t(input.to_avx()));
    }
}

TEST(uint256, constructors)
{
    uint256_t x;
    ::intx::uint256 intx;

    x = uint256_t();
    intx = 0;
    ASSERT_EQ(x.to_intx(), intx);

    x = 1;
    intx = 1;
    ASSERT_EQ(x.to_intx(), intx);

    x = 0xabcd;
    intx = 0xabcd;
    ASSERT_EQ(x.to_intx(), intx);

    x = {0xabcd, 0x1234};
    intx = {0xabcd, 0x1234};
    ASSERT_EQ(x.to_intx(), intx);

    x = {0xabcd, 0x1234, 0xdcba};
    intx = {0xabcd, 0x1234, 0xdcba};
    ASSERT_EQ(x.to_intx(), intx);

    x = {0xabcd, 0x1234, 0xdcba, 0x4321};
    intx = {0xabcd, 0x1234, 0xdcba, 0x4321};
    ASSERT_EQ(x.to_intx(), intx);

    x = -1;
    intx = -1;
    ASSERT_EQ(x.to_intx(), intx);

    x = {0xabcd, -0x1234, 0xdcba, -0x4321};
    intx = {0xabcd, -0x1234, 0xdcba, -0x4321};
    ASSERT_EQ(x.to_intx(), intx);
}

TEST(uint256, literals)
{
    uint256_t x;

    x = 0_u256;
    ASSERT_EQ(x, uint256_t(0, 0, 0, 0));

    x = 1_u256;
    ASSERT_EQ(x, uint256_t(1, 0, 0, 0));

    x = 0xff_u256;
    ASSERT_EQ(x, uint256_t(0xff, 0, 0, 0));

    x = 0x4a4b4c4d414243443a3b3c3d313233342a2b2c2d212223241a1b1c1d11121314_u256;
    ASSERT_EQ(
        x,
        uint256_t(
            0x1a1b1c1d11121314,
            0x2a2b2c2d21222324,
            0x3a3b3c3d31323334,
            0x4a4b4c4d41424344));
}

TEST(uint256, index)
{
    uint256_t x = {1, 2, 3, 4};

    ASSERT_EQ(x[0], 1);
    ASSERT_EQ(x[1], 2);
    ASSERT_EQ(x[2], 3);
    ASSERT_EQ(x[3], 4);
}

TEST(uint256, int_cast)
{
    uint256_t x = {0xabcd, 0xdef0, 0x1234, 0x5678};
    ASSERT_EQ(static_cast<uint64_t>(x), 0xabcd);
    ASSERT_EQ(static_cast<int64_t>(x), 0xabcd);
    ASSERT_EQ(static_cast<uint32_t>(x), 0xabcd);
    ASSERT_EQ(static_cast<int32_t>(x), 0xabcd);

    x = {-0xabcd, 0xdef0, 0x1234, 0x5678};
    ASSERT_EQ(static_cast<uint64_t>(x), -0xabcd);
    ASSERT_EQ(static_cast<int64_t>(x), -0xabcd);
    ASSERT_EQ(static_cast<uint32_t>(x), -0xabcd);
    ASSERT_EQ(static_cast<int32_t>(x), -0xabcd);

    x = {0x1234aabbccdd4321, 0xdef0, 0x1234, 0x5678};
    ASSERT_EQ(static_cast<uint64_t>(x), 0x1234aabbccdd4321);
    ASSERT_EQ(static_cast<int64_t>(x), 0x1234aabbccdd4321);
    ASSERT_EQ(static_cast<uint32_t>(x), 0xccdd4321);
    ASSERT_EQ(static_cast<int32_t>(x), 0xccdd4321);
    ASSERT_EQ(static_cast<uint16_t>(x), 0x4321);
    ASSERT_EQ(static_cast<int16_t>(x), 0x4321);
    ASSERT_EQ(static_cast<uint8_t>(x), 0x21);
    ASSERT_EQ(static_cast<int8_t>(x), 0x21);
}

constexpr uint256_t test_inputs[] = {
    {0, 0, 0, 0},
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1},
    {~0, 0, 0, 0},
    {0, ~0, 0, 0},
    {0, 0, ~0, 0},
    {0, 0, 0, ~0},
    {~0, ~0, ~0, ~0},
    {~0, ~0, ~0, 0x07ffffffffffffff},
    {0x1234, 0, 0, 0},
    {0, 0x1234, 0, 0},
    {0, 0, 0x1234, 0},
    {0, 0, 0, 0x1234},
    {0x1234, 0xabcd, 0xbcda, 0x4321},
    {
        0xabcda1b2c3d41234,
        0x12341a2b3c4dabcd,
        0xdcbad4c3b2a14321,
        0x43214d3c2b1abcda,
    },
    {
        0x43214d3c2b1abcda,
        0xabcda1b2c3d41234,
        0x12341a2b3c4dabcd,
        0xdcbad4c3b2a14321,
    },
    {
        0xdcbad4c3b2a14321,
        0x43214d3c2b1abcda,
        0xabcda1b2c3d41234,
        0x12341a2b3c4dabcd,
    },
    {
        0x12341a2b3c4dabcd,
        0xdcbad4c3b2a14321,
        0x43214d3c2b1abcda,
        0xabcda1b2c3d41234,
    },
};

TEST(uint256, arithmetic)
{
    for (auto const &x : test_inputs) {
        for (auto const &y : test_inputs) {
            ASSERT_EQ(x + y, uint256_t(x.to_intx() + y.to_intx()));
            ASSERT_EQ(x - y, uint256_t(x.to_intx() - y.to_intx()));
            ASSERT_EQ(x * y, uint256_t(x.to_intx() * y.to_intx()));
            ASSERT_EQ(
                exp(x, y), uint256_t(::intx::exp(x.to_intx(), y.to_intx())));

            if (y != 0) {
                ASSERT_EQ(x / y, uint256_t(x.to_intx() / y.to_intx()));
                ASSERT_EQ(x % y, uint256_t(x.to_intx() % y.to_intx()));

                auto sdivrem_result = sdivrem(x, y);
                auto sdivrem_result_intx =
                    ::intx::sdivrem(x.to_intx(), y.to_intx());
                ASSERT_EQ(
                    sdivrem_result.quot, uint256_t(sdivrem_result_intx.quot));
                ASSERT_EQ(
                    sdivrem_result.rem, uint256_t(sdivrem_result_intx.rem));
            }

            for (auto const &z : test_inputs) {
                if (z == 0) {
                    continue;
                }
                ASSERT_EQ(
                    addmod(x, y, z),
                    uint256_t(
                        ::intx::addmod(x.to_intx(), y.to_intx(), z.to_intx())));
                ASSERT_EQ(
                    mulmod(x, y, z),
                    uint256_t(
                        ::intx::mulmod(x.to_intx(), y.to_intx(), z.to_intx())));
            }
        }
        ASSERT_EQ(-x, uint256_t(-(x.to_intx())));
    }
}

TEST(uint256, predicates)
{
    for (auto const &x : test_inputs) {
        for (auto const &y : test_inputs) {
            ASSERT_EQ(x == y, x.to_intx() == y.to_intx());
            ASSERT_EQ(x < y, x.to_intx() < y.to_intx());
            ASSERT_EQ(x <= y, x.to_intx() <= y.to_intx());
            ASSERT_EQ(x > y, x.to_intx() > y.to_intx());
            ASSERT_EQ(x >= y, x.to_intx() >= y.to_intx());
            ASSERT_EQ(slt(x, y), ::intx::slt(x.to_intx(), y.to_intx()));
        }
    }
}

TEST(uint256, bitwise)
{
    for (auto const &x : test_inputs) {
        for (auto const &y : test_inputs) {
            ASSERT_EQ(x | y, uint256_t(x.to_intx() | y.to_intx()));
            ASSERT_EQ(x & y, uint256_t(x.to_intx() & y.to_intx()));
            ASSERT_EQ(x ^ y, uint256_t(x.to_intx() ^ y.to_intx()));
        }
        ASSERT_EQ(~x, uint256_t(~(x.to_intx())));
    }
}

TEST(uint256, shifts)
{
    for (auto const &x : test_inputs) {
        for (auto const &y : test_inputs) {
            ASSERT_EQ(x << y, uint256_t(x.to_intx() << y.to_intx()));
            ASSERT_EQ(x >> y, uint256_t(x.to_intx() >> y.to_intx()));
        }
        for (uint64_t shift = 0; shift <= 256; shift++) {
            ASSERT_EQ(x << shift, uint256_t(x.to_intx() << shift));
            ASSERT_EQ(x >> shift, uint256_t(x.to_intx() >> shift));
        }
    }
}

TEST(uint256, load_store)
{
    for (auto x : test_inputs) {
        auto le_bytes = std::bit_cast<uint8_t(*)[32]>(x.as_bytes());
        ASSERT_EQ(x, uint256_t::load_le_unsafe(x.as_bytes()));
        ASSERT_EQ(x, uint256_t::load_le(*le_bytes));

        uint8_t le_stored[32];
        x.store_le(le_stored);
        ASSERT_EQ(0, std::memcmp(le_bytes, le_stored, 32));
        ASSERT_EQ(x, uint256_t::load_le(le_stored));

        auto x_be = uint256_t{
            std::byteswap(x[3]),
            std::byteswap(x[2]),
            std::byteswap(x[1]),
            std::byteswap(x[0])};
        auto be_bytes = std::bit_cast<uint8_t(*)[32]>(x_be.as_bytes());
        ASSERT_EQ(x, uint256_t::load_be_unsafe(x_be.as_bytes()));
        ASSERT_EQ(x, uint256_t::load_be(*be_bytes));

        uint8_t be_stored[32];
        x.store_be(be_stored);
        ASSERT_EQ(0, std::memcmp(be_bytes, be_stored, 32));
        ASSERT_EQ(x, uint256_t::load_be(be_stored));
    }
}

TEST(uint256, string_conversion)
{
    for (auto x : test_inputs) {
        ASSERT_EQ(x, uint256_t::from_string(x.to_string()));
        ASSERT_EQ(x, uint256_t::from_string("0x" + x.to_string(16)));
    }

    std::tuple<uint256_t, std::string, std::string> const test_cases[] = {
        {0_u256, "0", "0"},
        {1_u256, "1", "1"},
        {10_u256, "10", "a"},
        {0xff_u256, "255", "ff"},
        {0xd6835e065763db1bca70cd12f26ebc651c18c2c94b09b7db8b1220bf20e9c14d_u256,
         "970270554974245014818020843390850589381796664120294801326746575421176"
         "12175693",
         "d6835e065763db1bca70cd12f26ebc651c18c2c94b09b7db8b1220bf20e9c14d"},
        {0xffeab2a2c43647e865829e7450e3797caf94def32b9d0f98b22176ee483d3035_u256,
         "115754451500915698797016776063775039799476313935046177147294877365978"
         "332475445",
         "ffeab2a2c43647e865829e7450e3797caf94def32b9d0f98b22176ee483d3035"}};

    for (auto const &[x, dec_str, hex_str] : test_cases) {
        ASSERT_EQ(x.to_string(), dec_str);
        ASSERT_EQ(x.to_string(16), hex_str);

        ASSERT_EQ(uint256_t::from_string(dec_str), x);
        ASSERT_EQ(uint256_t::from_string("0x" + hex_str), x);
    }

    auto hex_digit_in_dec =
        "ffeab2a2c43647e865829e7450e3797caf94def32b9d0f98b22176ee483d3035";
    ASSERT_THROW(
        uint256_t::from_string(hex_digit_in_dec), std::invalid_argument);

    auto too_big =
        "0xffeab2a2c43647e865829e7450e3797caf94def32b9d0f98b22176ee483d30350";
    ASSERT_THROW(uint256_t::from_string(too_big), std::out_of_range);
}
