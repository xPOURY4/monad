
#include "test_fixtures_base.hpp"

#include <gtest/gtest.h>

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/traverse.hpp>
#include <monad/mpt/trie.hpp>

#include <initializer_list>

using namespace monad::mpt;
using namespace monad::literals;
using namespace monad::test;

namespace monad::test
{
    Nibbles make_nibbles(std::initializer_list<uint8_t> nibbles)
    {
        Nibbles ret{nibbles.size()};
        for (auto it = nibbles.begin(); it < nibbles.end(); ++it) {
            MONAD_ASSERT(*it <= 0xf);
            ret.set(
                static_cast<unsigned>(std::distance(nibbles.begin(), it)), *it);
        }
        return ret;
    }

};

TEST(TraverseTest, simple)
{
    StateMachineAlwaysEmpty sm{};
    UpdateAux aux{};
    auto const root = upsert_updates(
        aux,
        sm,
        {},
        make_update(0x12345678_hex, 0xcafebabe_hex),
        make_update(0x12346678_hex, 0xdeadbeef_hex),
        make_update(0x12445678_hex, 0xdeadbabe_hex));

    /*
            12
          /    \
         34      445678
        / \
     5678  6678
    */
    struct SimpleTraverse : public TraverseMachine
    {
        size_t index = 0;
        size_t num_up = 0;

        virtual void down(Node const &node) override
        {
            if (index == 0) {
                EXPECT_EQ(node.number_of_children(), 2);
                EXPECT_EQ(node.mask, 0b11000);
                EXPECT_FALSE(node.has_value());
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(node.path_nibble_view(), NibblesView(0x12_hex));
            }
            else if (index == 1) {
                EXPECT_EQ(node.number_of_children(), 2);
                EXPECT_EQ(node.mask, 0b1100000);
                EXPECT_FALSE(node.has_value());
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(node.path_nibble_view(), make_nibbles({0x4}));
            }
            else if (index == 2) {
                EXPECT_EQ(node.number_of_children(), 0);
                EXPECT_EQ(node.mask, 0);
                EXPECT_TRUE(node.has_value());
                EXPECT_EQ(node.value(), 0xcafebabe_hex);
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(
                    node.path_nibble_view(), make_nibbles({0x6, 0x7, 0x8}));
            }
            else if (index == 3) {
                EXPECT_EQ(node.number_of_children(), 0);
                EXPECT_EQ(node.mask, 0);
                EXPECT_TRUE(node.has_value());
                EXPECT_EQ(node.value(), 0xdeadbeef_hex);
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(
                    node.path_nibble_view(), make_nibbles({0x6, 0x7, 0x8}));
            }
            else if (index == 4) {
                EXPECT_EQ(node.number_of_children(), 0);
                EXPECT_EQ(node.mask, 0);
                EXPECT_TRUE(node.has_value());
                EXPECT_EQ(node.value(), 0xdeadbabe_hex);
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(
                    node.path_nibble_view(),
                    make_nibbles({0x4, 0x5, 0x6, 0x7, 0x8}));
            }
            else {
                FAIL();
            }

            ++index;
        }

        virtual void up(Node const &) override
        {
            ++num_up;
        }
    } traverse;

    preorder_traverse(*root, traverse);
    EXPECT_EQ(traverse.index, 5);
    EXPECT_EQ(traverse.num_up, 5);
}
