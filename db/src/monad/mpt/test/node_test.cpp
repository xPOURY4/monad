#include "gtest/gtest.h"

#include <monad/mpt/nibbles_view.hpp>
#include <monad/core/hex_literal.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/node.hpp>

#include <cstdint>
#include <span>

using namespace monad::mpt;
using namespace monad::literals;

struct DummyCompute final : Compute
{
    // hash length = 1
    virtual unsigned compute_len(std::span<ChildData> const children) override
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

auto const data = 0x12345678_hex;
auto const path = 0xabcdabcdabcdabcd_hex;

TEST(NodeTest, leaf)
{
    NibblesView const relpath{1, 10, path.data()};
    node_ptr node{create_leaf(data, relpath)};

    EXPECT_EQ(node->mask, 0);
    EXPECT_EQ(node->value(), data);
    EXPECT_EQ(node->path_nibble_view(), relpath);
    EXPECT_EQ(node->get_mem_size(), 17);
    EXPECT_EQ(node->get_disk_size(), 17);
}

TEST(NodeTest, leaf_single_branch)
{
    DummyCompute comp{};
    NibblesView const relpath{12, 16, path.data()};
    Node *child = create_leaf(data, relpath);

    ChildData children[1];
    children[0].len = 1;
    children[0].data[0] = 0xa;
    children[0].branch = 0xc;
    children[0].ptr = child;
    NibblesView const relpath2{1, 10, path.data()};
    uint16_t const mask = 1u << 0xc;
    node_ptr node{create_node(comp, mask, children, relpath2, data)};

    EXPECT_EQ(node->value(), data);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->data_len, 1);
    EXPECT_EQ(node->get_mem_size(), 41);
    EXPECT_EQ(node->get_disk_size(), 33);
}

TEST(NodeTest, leaf_multiple_branches)
{
    DummyCompute comp{};
    NibblesView const relpath{12, 16, path.data()};
    Node *child1 = create_leaf(data, relpath);
    Node *child2 = create_leaf(data, relpath);

    ChildData child;
    child.len = 1;
    child.data[0] = 0xa;
    ChildData children[2] = {child, child};
    children[0].branch = 0xa;
    children[1].branch = 0xc;
    children[0].ptr = child1;
    children[1].ptr = child2;
    NibblesView const relpath2{1, 10, path.data()};
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    node_ptr node{create_node(comp, mask, children, relpath2, data)};

    EXPECT_EQ(node->value(), data);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->data_len, 2);
    EXPECT_EQ(node->get_mem_size(), 65);
    EXPECT_EQ(node->get_disk_size(), 49);
}

TEST(NodeTest, branch_node)
{
    DummyCompute comp{};
    NibblesView const relpath{12, 16, path.data()};
    Node *child1 = create_leaf(data, relpath);
    Node *child2 = create_leaf(data, relpath);

    ChildData child;
    child.len = 1;
    child.data[0] = 0xa;
    ChildData children[2] = {child, child};
    children[0].branch = 0xa;
    children[1].branch = 0xc;
    children[0].ptr = child1;
    children[1].ptr = child2;
    NibblesView const relpath2{1, 1, path.data()}; // relpath2 is empty
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    node_ptr node{create_node(comp, mask, children, relpath2)};

    EXPECT_EQ(node->value_len, 0);
    EXPECT_EQ(node->data_len, 0);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->get_mem_size(), 54);
    EXPECT_EQ(node->get_disk_size(), 38);
}

TEST(NodeTest, extension_node)
{
    DummyCompute comp{};
    NibblesView const relpath{12, 16, path.data()};
    Node *child1 = create_leaf(data, relpath);
    Node *child2 = create_leaf(data, relpath);

    ChildData child;
    child.len = 1;
    child.data[0] = 0xa;
    ChildData children[2] = {child, child};
    children[0].branch = 0xa;
    children[1].branch = 0xc;
    children[0].ptr = child1;
    children[1].ptr = child2;
    NibblesView const relpath2{1, 10, path.data()};
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    node_ptr node{create_node(comp, mask, children, relpath2)};

    EXPECT_EQ(node->value_len, 0);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->data_len, 0);
    EXPECT_EQ(node->get_mem_size(), 59);
    EXPECT_EQ(node->get_disk_size(), 43);
}
