#include "gtest/gtest.h"

#include "monad/trie/node_helper.hpp"

#if !defined(__clang__)
    #pragma GCC diagnostic ignored "-Warray-bounds"
#endif

namespace
{

    TEST(NodeHelper, max_disk_node_size)
    {
        static constexpr size_t CHILDREN = 16;
        using namespace MONAD_TRIE_NAMESPACE;
        auto data256 = std::make_unique<unsigned char[]>(255);
        memset(data256.get(), 0xff, 255);
        struct node_storage_t
        {
            merkle_node_t node;
            merkle_child_info_t children[CHILDREN];
        } storage;
        memset(&storage, 0, sizeof(storage));
        storage.node.nsubnodes = CHILDREN;
        storage.node.mask = 0xffff;
        for (size_t n = 0; n < CHILDREN; n++) {
            storage.children[n].data = data256.get();
            storage.children[n].set_data_len(110);
            storage.children[n].set_path_len(33);
        }
        EXPECT_EQ(get_disk_node_size(&storage.node), MAX_DISK_NODE_SIZE);
    }

}
