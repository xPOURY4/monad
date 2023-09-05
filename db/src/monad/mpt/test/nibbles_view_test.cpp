#include "gtest/gtest.h"

#include <monad/core/hex_literal.hpp>
#include <monad/mpt/nibbles_view.hpp>

using namespace monad::mpt;
using namespace monad::literals;

TEST(NibblesViewTest, nibbles)
{
    auto path =
        0x1234567812345678123456781234567812345678123456781234567812345678_hex;

    NibblesView a{12, 12, path.data()};
    EXPECT_EQ(a.size(), 0);
    EXPECT_EQ(a.data, nullptr);

    NibblesView b{12, 16, path.data()}, c{20, 24, path.data()};
    EXPECT_EQ(b.si, false);
    EXPECT_EQ(b.ei, 4);
    EXPECT_EQ(b, c);

    NibblesView d{15, 18, path.data()};
    EXPECT_EQ(d.si, true);
    EXPECT_EQ(d.ei, 4);
}