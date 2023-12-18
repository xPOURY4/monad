#include "gtest/gtest.h"

#include <monad/core/hex_literal.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/test/gtest_signal_stacktrace_printer.hpp> // NOLINT

#include <cstdint>
#include <span>

using namespace monad::mpt;
using namespace monad::literals;

struct DummyCompute final : Compute
{
    // hash length = 1
    virtual unsigned
    compute_len(std::span<ChildData> const children, uint16_t const) override
    {
        unsigned len = 0;
        for (auto const &i : children) {
            len += i.len;
        }
        return len >= 32 ? 32 : len;
    }

    virtual unsigned compute_branch(unsigned char *, Node *) override
    {
        return 0;
    }

    virtual unsigned compute(unsigned char *buffer, Node *) override
    {
        buffer[0] = 0xa;
        return 1;
    }
};

auto const value = 0x12345678_hex;
auto const path = 0xabcdabcdabcdabcd_hex;

TEST(NodeTest, leaf)
{
    NibblesView const path1{1, 10, path.data()};
    Node::UniquePtr node{make_node(0, {}, path1, value, {})};

    EXPECT_EQ(node->mask, 0);
    EXPECT_EQ(node->value(), value);
    EXPECT_EQ(node->path_nibble_view(), path1);
    EXPECT_EQ(node->get_mem_size(), 21);
    EXPECT_EQ(node->get_disk_size(), 21);
}

TEST(NodeTest, leaf_single_branch)
{
    DummyCompute comp{};
    NibblesView const path1{12, 16, path.data()};
    Node *child = make_node(0, {}, path1, value, {}).release();

    ChildData children[1];
    children[0].len = 1;
    children[0].data[0] = 0xa;
    children[0].branch = 0xc;
    children[0].ptr = child;
    NibblesView const path2{1, 10, path.data()};
    uint16_t const mask = 1u << 0xc;
    Node::UniquePtr node{create_node(comp, mask, children, path2, value)};

    EXPECT_EQ(node->value(), value);
    EXPECT_EQ(node->path_nibble_view(), path2);
    EXPECT_EQ(node->data_len, 1);
    EXPECT_EQ(node->get_mem_size(), 49);
    EXPECT_EQ(node->get_disk_size(), 41);
}

TEST(NodeTest, leaf_multiple_branches)
{
    DummyCompute comp{};
    NibblesView const path1{12, 16, path.data()};
    Node *child1 = make_node(0, {}, path1, value, {}).release();
    Node *child2 = make_node(0, {}, path1, value, {}).release();

    ChildData child;
    child.len = 1;
    child.data[0] = 0xa;
    ChildData children[2] = {child, child};
    children[0].branch = 0xa;
    children[1].branch = 0xc;
    children[0].ptr = child1;
    children[1].ptr = child2;
    NibblesView const path2{1, 10, path.data()};
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    Node::UniquePtr node{create_node(comp, mask, children, path2, value)};

    EXPECT_EQ(node->value(), value);
    EXPECT_EQ(node->path_nibble_view(), path2);
    EXPECT_EQ(node->data_len, 2);
    EXPECT_EQ(node->get_mem_size(), 77);
    EXPECT_EQ(node->get_disk_size(), 61);
}

TEST(NodeTest, branch_node)
{
    DummyCompute comp{};
    NibblesView const path1{12, 16, path.data()};
    Node *child1 = make_node(0, {}, path1, value, {}).release();
    Node *child2 = make_node(0, {}, path1, value, {}).release();

    ChildData child;
    child.len = 1;
    child.data[0] = 0xa;
    ChildData children[2] = {child, child};
    children[0].branch = 0xa;
    children[1].branch = 0xc;
    children[0].ptr = child1;
    children[1].ptr = child2;
    NibblesView const path2{1, 1, path.data()}; // path2 is empty
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    Node::UniquePtr node{create_node(comp, mask, children, path2)};

    EXPECT_EQ(node->value_len, 0);
    EXPECT_EQ(node->data_len, 0);
    EXPECT_EQ(node->path_nibble_view(), path2);
    EXPECT_EQ(node->get_mem_size(), 66);
    EXPECT_EQ(node->get_disk_size(), 50);
}

TEST(NodeTest, extension_node)
{
    DummyCompute comp{};
    NibblesView const path1{12, 16, path.data()};
    Node *child1 = make_node(0, {}, path1, value, {}).release();
    Node *child2 = make_node(0, {}, path1, value, {}).release();

    ChildData child;
    child.len = 1;
    child.data[0] = 0xa;
    ChildData children[2] = {child, child};
    children[0].branch = 0xa;
    children[1].branch = 0xc;
    children[0].ptr = child1;
    children[1].ptr = child2;
    NibblesView const path2{1, 10, path.data()};
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    Node::UniquePtr node{create_node(comp, mask, children, path2)};

    EXPECT_EQ(node->value_len, 0);
    EXPECT_EQ(node->path_nibble_view(), path2);
    EXPECT_EQ(node->data_len, 0);
    EXPECT_EQ(node->get_mem_size(), 71);
    EXPECT_EQ(node->get_disk_size(), 55);
}
