#include "compiler/ir/poly_typed/kind.h"

#include <gtest/gtest.h>
#include <vector>

using namespace monad::compiler;
using namespace monad::compiler::poly_typed;

TEST(alpha_equal, test_1)
{
    ASSERT_TRUE(alpha_equal(word, word));
    ASSERT_FALSE(alpha_equal(word, any));
    ASSERT_FALSE(alpha_equal(word, cont(cont_kind({}))));
}

TEST(alpha_equal, test_2)
{
    auto mk = [](VarName s) { return cont_kind({word}, s); };
    ContKind const left = mk(0);
    ASSERT_TRUE(alpha_equal(left, left));
    ASSERT_TRUE(alpha_equal(left, mk(1)));
    ASSERT_FALSE(alpha_equal(left, cont_kind({word})));
    ASSERT_FALSE(alpha_equal(left, cont_kind({word, word}, 0)));
    ASSERT_FALSE(alpha_equal(left, cont_kind({any}, 0)));
}

TEST(alpha_equal, test_3)
{
    auto mk = [](VarName s1, VarName s2, VarName v1, VarName v2, VarName l1) {
        return cont(cont_kind(
            {kind_var(v1),
             cont(cont_kind({}, s2)),
             kind_var(v1),
             kind_var(v2),
             literal_var(l1, cont_kind({kind_var(v1)}, s1))},
            s1));
    };
    Kind const left = mk(0, 1, 0, 1, 0);
    ASSERT_TRUE(alpha_equal(left, left));
    ASSERT_TRUE(alpha_equal(left, mk(10, 20, 30, 40, 0)));
    ASSERT_FALSE(alpha_equal(left, mk(10, 20, 30, 40, 1)));
    ASSERT_FALSE(alpha_equal(left, mk(10, 10, 30, 40, 0)));
    ASSERT_FALSE(alpha_equal(left, mk(10, 20, 30, 30, 0)));
}

TEST(alpha_equal, test_4)
{
    ASSERT_TRUE(alpha_equal(
        cont_kind({kind_var(0), kind_var(1)}, 0),
        cont_kind({kind_var(1), kind_var(0)}, 0)));
    ASSERT_TRUE(alpha_equal(
        cont_kind({kind_var(0), kind_var(1)}, 0),
        cont_kind({kind_var(1), kind_var(0)}, 1)));
    ASSERT_TRUE(alpha_equal(
        cont_kind({kind_var(0), kind_var(1)}, 0),
        cont_kind({kind_var(0), kind_var(1)}, 0)));
    ASSERT_TRUE(alpha_equal(
        cont_kind({kind_var(0), kind_var(1)}, 0),
        cont_kind({kind_var(0), kind_var(1)}, 1)));
    ASSERT_FALSE(alpha_equal(
        cont_kind({kind_var(0), kind_var(0)}, 0),
        cont_kind({kind_var(1), kind_var(0)}, 0)));
    ASSERT_FALSE(alpha_equal(
        cont_kind({kind_var(0), kind_var(0)}, 0),
        cont_kind({kind_var(1), kind_var(0)}, 1)));
    ASSERT_FALSE(alpha_equal(
        cont_kind({kind_var(0), kind_var(0)}, 1),
        cont_kind({kind_var(1), kind_var(0)}, 1)));
}
