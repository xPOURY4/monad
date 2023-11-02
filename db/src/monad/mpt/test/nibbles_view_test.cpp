#include "gtest/gtest.h"

#include <monad/core/hex_literal.hpp>
#include <monad/mpt/nibbles_view.hpp>

using namespace monad::mpt;
using namespace monad::literals;

TEST(NibblesViewTest, nibbles_view)
{
    auto path =
        0x1234567812345678123456781234567812345678123456781234567812345678_hex;

    NibblesView a{12, 12, path.data()};
    EXPECT_EQ(a.size(), 0);
    EXPECT_EQ(a.data_, nullptr);

    NibblesView b{12, 16, path.data()}, c{20, 24, path.data()};
    EXPECT_EQ(b.begin_nibble_, false);
    EXPECT_EQ(b.end_nibble_, 4);
    EXPECT_EQ(b, c);

    NibblesView d{15, 18, path.data()};
    EXPECT_EQ(d.begin_nibble_, true);
    EXPECT_EQ(d.end_nibble_, 4);
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
