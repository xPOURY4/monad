#include "gtest/gtest.h"

#include "monad/trie/node_helper.hpp"

namespace
{

    TEST(NodeHelper, max_disk_node_size)
    {
        static constexpr size_t CHILDREN = 16;
        using namespace MONAD_TRIE_NAMESPACE;
        auto node = merkle_node_t::make_with_children(CHILDREN);
        node->mask = 0xffff;
        for (size_t n = 0; n < CHILDREN; n++) {
            node->children()[n].data = MONAD_NAMESPACE::allocators::
                make_resizeable_unique_for_overwrite<unsigned char[]>(255);
            node->children()[n].set_data_len(110);
            node->children()[n].set_path_len(33);
            node->children()[n].set_noderef_len(32);
        }
        EXPECT_EQ(get_disk_node_size(node.get()), MAX_DISK_NODE_SIZE);
    }

}
