#include "gtest/gtest.h"

#include <monad/core/hex_literal.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/node.hpp>

using namespace monad::mpt;
using namespace monad::literals;

struct DummyCompute final : Compute
{
    // hash length = 1
    virtual unsigned compute_len(std::span<ChildData> hashes) override
    {
        unsigned len = 0;
        for (unsigned i = 0; i < hashes.size(); ++i) {
            len += hashes[i].len;
        }
        return len >= 32 ? 32 : len;
    }

    virtual unsigned compute_branch(unsigned char *, Node *) override
    {
        return 0;
    }

    virtual unsigned
    compute(unsigned char *buffer, Node *, unsigned = -1) override
    {
        buffer[0] = 0xa;
        return 1;
    }
};

auto const data = 0x12345678_hex;
auto const path = 0xabcdabcdabcdabcd_hex;

TEST(NodeTest, leaf)
{
    NibblesView relpath{1, 10, path.data()};
    node_ptr node = create_leaf(data, relpath);

    EXPECT_EQ(node->mask, 0);
    EXPECT_EQ(node->leaf_view(), data);
    EXPECT_EQ(node->path_nibble_view(), relpath);
    EXPECT_EQ(node->node_mem_size(), 17);
}

TEST(NodeTest, leaf_single_branch)
{
    DummyCompute comp{};
    NibblesView relpath{12, 16, path.data()};
    node_ptr child = create_leaf(data, relpath);

    ChildData hashes[1];
    hashes[0].len = 1;
    hashes[0].data[0] = 0xa;
    hashes[0].branch = 0xc;
    node_ptr nexts[1] = {std::move(child)};
    NibblesView relpath2{1, 10, path.data()};
    uint16_t const mask = 1u << 0xc;
    node_ptr node =
        create_node(comp, mask, mask, hashes, nexts, relpath2, data);

    EXPECT_EQ(node->leaf_view(), data);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->hash_len, 0);
    EXPECT_EQ(node->node_mem_size(), 28);
}

TEST(NodeTest, leaf_multiple_branches)
{
    DummyCompute comp{};
    NibblesView relpath{12, 16, path.data()};
    node_ptr child1 = create_leaf(data, relpath);
    node_ptr child2 = create_leaf(data, relpath);

    ChildData hash;
    hash.len = 1;
    hash.data[0] = 0xa;
    ChildData hashes[2] = {hash, hash};
    hashes[0].branch = 0xa;
    hashes[1].branch = 0xc;
    node_ptr nexts[2] = {std::move(child1), std::move(child2)};
    NibblesView relpath2{1, 10, path.data()};
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    node_ptr node =
        create_node(comp, mask, mask, hashes, nexts, relpath2, data);

    EXPECT_EQ(node->leaf_view(), data);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->hash_len, 2);
    EXPECT_EQ(node->node_mem_size(), 41);
}

TEST(NodeTest, branch_node)
{
    DummyCompute comp{};
    NibblesView relpath{12, 16, path.data()};
    node_ptr child1 = create_leaf(data, relpath);
    node_ptr child2 = create_leaf(data, relpath);

    ChildData hash;
    hash.len = 1;
    hash.data[0] = 0xa;
    ChildData hashes[2] = {hash, hash};
    hashes[0].branch = 0xa;
    hashes[1].branch = 0xc;
    node_ptr nexts[2] = {std::move(child1), std::move(child2)};
    NibblesView relpath2{1, 1, path.data()}; // relpath2 is empty
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    node_ptr node = create_node(comp, mask, mask, hashes, nexts, relpath2);

    EXPECT_EQ(node->leaf_len, 0);
    EXPECT_EQ(node->hash_len, 0);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->node_mem_size(), 30);
}

TEST(NodeTest, extension_node)
{
    DummyCompute comp{};
    NibblesView relpath{12, 16, path.data()};
    node_ptr child1 = create_leaf(data, relpath);
    node_ptr child2 = create_leaf(data, relpath);

    ChildData hash;
    hash.len = 1;
    hash.data[0] = 0xa;
    ChildData hashes[2] = {hash, hash};
    hashes[0].branch = 0xa;
    hashes[1].branch = 0xc;
    node_ptr nexts[2] = {std::move(child1), std::move(child2)};
    NibblesView relpath2{1, 10, path.data()};
    uint16_t const mask = (1u << 0xa) | (1u << 0xc);
    node_ptr node = create_node(comp, mask, mask, hashes, nexts, relpath2);

    EXPECT_EQ(node->leaf_len, 0);
    EXPECT_EQ(node->path_nibble_view(), relpath2);
    EXPECT_EQ(node->hash_len, 2);
    EXPECT_EQ(node->node_mem_size(), 37);
}
