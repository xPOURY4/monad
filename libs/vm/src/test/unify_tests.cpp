#include "poly_typed/kind.h"
#include "poly_typed/subst_map.h"
#include "poly_typed/unify.h"

#include <gtest/gtest.h>
#include <vector>

using namespace monad::compiler;
using namespace monad::compiler::poly_typed;

TEST(unify, test_1)
{
    SubstMap su{};
    Kind const k1 = kind_var(0);
    Kind const k2 = word;
    ASSERT_TRUE(unify(su, k1, k2));
    ASSERT_EQ(su.subst(k1).value(), word);
    ASSERT_EQ(su.subst(k2).value(), word);
}

TEST(unify, test_2)
{
    SubstMap su{};
    ContKind const c1 =
        cont_kind({kind_var(0), literal_var(0, cont_kind({kind_var(1)}))}, 0);
    ContKind const c2 = cont_kind(
        {kind_var(4),
         literal_var(5, cont_kind({kind_var(2), kind_var(3)})),
         any},
        1);

    ASSERT_TRUE(unify(su, c1, c2));

    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind(
            {kind_var(0), literal_var(0, cont_kind({kind_var(1)})), any}, 0)));

    ContKind const c3 = cont_kind(
        {kind_var(0), literal_var(1, cont_kind({kind_var(1)})), any}, 0);
    ASSERT_FALSE(unify(su, c1, c3));
    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind(
            {kind_var(0), literal_var(0, cont_kind({kind_var(1)})), any}, 0)));

    ContKind const c4 = cont_kind(
        {kind_var(0),
         literal_var(0, cont_kind({kind_var(1)})),
         any,
         kind_var(0)},
        2);
    ASSERT_TRUE(unify(su, c1, c4));
    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind(
            {kind_var(0),
             literal_var(0, cont_kind({kind_var(1)})),
             any,
             kind_var(0)},
            0)));

    ContKind const c5 = cont_kind(
        {kind_var(4), literal_var(2, cont_kind({kind_var(1), any}))}, 3);
    ASSERT_TRUE(unify(su, c1, c5));
    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind({kind_var(0), word, any, kind_var(0)}, 0)));
}

TEST(unify, test_3)
{
    SubstMap su{};
    ContKind const c1 = cont_kind(
        {cont(cont_kind({cont(cont_kind({}, 1))}, 0)),
         cont(cont_kind({kind_var(0)}, 1))},
        0);
    ContKind const c2 = cont_kind(
        {cont(cont_kind({cont(cont_kind({}, 2))}, 3)),
         cont(cont_kind({kind_var(0)}, 2)),
         cont(cont_kind({}, 4))},
        4);
    ASSERT_TRUE(unify(su, c1, c2));
    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind(
            {cont(cont_kind(
                 {cont(cont_kind({}, 1)), cont(cont_kind({}, 4))}, 4)),
             cont(cont_kind({kind_var(0)}, 1)),
             cont(cont_kind({}, 4))},
            4)));

    ContKind const c3 =
        cont_kind({cont(cont_kind({cont(cont_kind({word}))}, 5))}, 6);
    ASSERT_TRUE(unify(su, c1, c3));
    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind(
            {cont(cont_kind({cont(cont_kind({})), cont(cont_kind({}, 4))}, 4)),
             cont(cont_kind({kind_var(0)})),
             cont(cont_kind({}, 4))},
            4)));

    ContKind const c4 =
        cont_kind({cont(cont_kind({cont(cont_kind({any}))}, 5))}, 6);
    ASSERT_FALSE(unify(su, c1, c4));
    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind(
            {cont(cont_kind({cont(cont_kind({})), cont(cont_kind({}, 4))}, 4)),
             cont(cont_kind({kind_var(0)})),
             cont(cont_kind({}, 4))},
            4)));

    ContKind const c5 =
        cont_kind({cont(cont_kind({cont(cont_kind({word}))}, 5))}, 6);
    ASSERT_TRUE(unify(su, c1, c5));
    ASSERT_TRUE(alpha_equal(
        su.subst(c1).value(),
        cont_kind(
            {cont(cont_kind({cont(cont_kind({})), cont(cont_kind({}, 4))}, 4)),
             cont(cont_kind({kind_var(0)})),
             cont(cont_kind({}, 4))},
            4)));
}
