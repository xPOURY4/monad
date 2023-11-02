#include "gtest/gtest.h"

#include <monad/core/hex_literal.hpp>
#include <monad/mpt/nibbles_view.hpp>

using namespace monad::mpt;
using namespace monad::literals;

TEST(NibblesViewTest, nibbles_view)
{
    auto const path =
        0x1234567812345678123456781234567812345678123456781234567812345678_hex;

    NibblesView const a{12, 12, path.data()};
    EXPECT_EQ(a.size(), 0);
    EXPECT_EQ(a, NibblesView{});

    NibblesView const b{12, 16, path.data()}, c{20, 24, path.data()};
    EXPECT_EQ(b, c);

    NibblesView const d{15, 18, path.data()};
    auto const expected_bytes = 0x8120_hex;
    auto const expected = NibblesView{0, 3, expected_bytes.data()};
    EXPECT_EQ(d, expected);
}

TEST(NibblesTest, concat_nibbles)
{
    auto path =
        0x1234567812345678123456781234567812345678123456781234567812345678_hex;

    Nibbles a =
        concat2(get_nibble(path.data(), 0), NibblesView{1, 12, path.data()});
    EXPECT_EQ(a, (NibblesView{0, 12, path.data()}));

    Nibbles b = concat3(
        NibblesView{12, 16, path.data()},
        get_nibble(path.data(), 16),
        NibblesView{17, 20, path.data()});
    EXPECT_EQ(b, (NibblesView{12, 20, path.data()}));
}

TEST(NibblesTest, concat) {}
