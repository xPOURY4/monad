#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>

MONAD_MPT_NAMESPACE_BEGIN

find_result _find_and_dealloc(Node *node, byte_string_view key)
{
    std::stack<std::pair<Node *, unsigned char>> node_stack{};

    unsigned pi = 0, node_pi = node->bitpacked.path_nibble_index_start;
    unsigned const key_nibble_size = 2 * key.size();
    while (pi < key_nibble_size) {
        unsigned char nibble = get_nibble(key.data(), pi);
        if (node->path_nibble_index_end == node_pi) {
            if (!(node->mask & (1u << nibble))) {
                return find_result::branch_not_exist_failure;
            }
            // go to node's matched child
            node_stack.push({node, nibble});
            node = node->next(nibble);
            MONAD_ASSERT(node); // nodes indexed by `key` is already in memory
            node_pi = node->bitpacked.path_nibble_index_start;
            ++pi;
            continue;
        }
        if (nibble != get_nibble(node->path_data(), node_pi)) {
            return find_result::key_mismatch_failure;
        }
        // nibble is matched
        ++pi;
        ++node_pi;
    }
    if (node_pi != node->path_nibble_index_end) {
        // prefix key exists but no leaf ends at `key`
        return find_result::key_ends_ealier_than_node_failure;
    }

    // deallocate node from the stack until it's not the single child in memory
    while (!node_stack.empty()) {
        auto [parent, branch] = node_stack.top();
        node_stack.pop();
        unsigned num_mem_child = 0;
        for (unsigned j = 0; j < parent->n(); ++j) {
            if (parent->next_j(j)) {
                ++num_mem_child;
            }
        }
        parent->next_ptr(branch).reset();
        if (num_mem_child > 1) {
            break;
        }
    }
    return find_result::success;
}

node_ptr copy_node(
    UpdateAux &update_aux, node_ptr root, byte_string_view const src,
    byte_string_view const dest)
{
    int const fd = update_aux.is_on_disk() ? update_aux.io->get_rd_fd() : -1;
    auto [src_leaf, res] = find_blocking(fd, root.get(), src);
    MONAD_ASSERT(res == find_result::success);

    Node *parent = nullptr, *node = root.get(), *new_node = nullptr;
    unsigned pi = 0, node_pi = root->bitpacked.path_nibble_index_start;
    unsigned const dest_nibble_size = 2 * dest.size();
    unsigned char branch_i = INVALID_BRANCH;

    // Disconnect src_leaf's children
    // Insert `dest` to trie, the node created will need to have the same
    // children as node at `src`
    while (pi < dest_nibble_size) {
        unsigned char nibble = get_nibble(dest.data(), pi);
        if (node->path_nibble_index_end == node_pi) {
            if (!(node->mask & (1u << nibble))) {
                // add a branch = nibble to new_node
                new_node = [&]() -> Node * {
                    Node *leaf = update_node_diff_path_leaf(
                        src_leaf, /* unlink children to avoid dup reference */
                        NibblesView{pi + 1, dest_nibble_size, dest.data()},
                        src_leaf->leaf_view());
                    // create a node, with no leaf data
                    uint16_t const mask = node->mask | (1u << nibble);
                    Node *ret =
                        create_node_nodata(mask, node->path_nibble_view());
                    for (unsigned i = 0, j = 0, old_j = 0, bit = 1;
                         j < ret->n();
                         ++i, bit <<= 1) {
                        if (i == nibble) {
                            ret->set_next_j(j++, leaf);
                        }
                        else if (mask & bit) {
                            // Assume child has no data for now
                            if (update_aux.is_on_disk()) {
                                ret->fnext_j(j) = node->fnext_j(old_j);
                            }
                            // also clear node's child mem ptr
                            ret->set_next_j(
                                j++, node->next_j_ptr(old_j++).release());
                        }
                    }
                    return ret;
                }();
                break;
            }
            // go to node's matched child
            parent = node;
            branch_i = nibble;
            node = node->next(nibble);
            node_pi = node->bitpacked.path_nibble_index_start;
            ++pi;
            continue;
        }
        if (auto node_nibble = get_nibble(node->path_data(), node_pi);
            nibble != node_nibble) {
            // split node's path: turn node to a branch node with two children
            new_node = [&]() -> Node * {
                Node *leaf = update_node_diff_path_leaf(
                    src_leaf, /* unlink its children to avoid dup reference */
                    NibblesView{pi + 1, dest_nibble_size, dest.data()},
                    src_leaf->leaf_view());
                Node *node_latter_half = update_node_diff_path_leaf(
                    node,
                    NibblesView{
                        node_pi + 1,
                        node->path_nibble_index_end,
                        node->path_data()},
                    node->leaf_view());
                uint16_t const mask = (1u << nibble) | (1u << node_nibble);
                Node *ret = create_node_nodata(
                    mask,
                    NibblesView{
                        node->bitpacked.path_nibble_index_start,
                        node_pi,
                        node->path_data()});
                bool const leaf_first = nibble < node_nibble;
                assert(leaf);
                assert(node_latter_half);
                ret->set_next_j(leaf_first ? 0 : 1, leaf);
                ret->set_next_j(leaf_first ? 1 : 0, node_latter_half);
                if (update_aux.is_on_disk()) {
                    // Request a write for the modified node, not necessarily
                    // land on disk immediately, but it's queued until the
                    // buffer is full or a root node is written in the next
                    // batch update.
                    auto off = async_write_node(
                                   *update_aux.io,
                                   update_aux.node_writer,
                                   node_latter_half)
                                   .offset_written_to;
                    off |=
                        ((file_offset_t)num_pages(
                             off, node_latter_half->get_disk_size())
                         << 62);
                    ret->fnext_j(leaf_first ? 1 : 0) = off;
                }
                return ret;
            }();
            break;
        }
        // nibble is matched
        ++pi;
        ++node_pi;
    }
    // deallocate previous child at branch i
    if (parent) {
        parent->next_ptr(branch_i).reset(); // deallocate child (= node)
        parent->set_next(branch_i, new_node);
    }
    else {
        assert(node == root.get());
        root = node_ptr{new_node}; // deallocate root (= node)
    }
    // in memory version doesn't dealloc any nodes
    if (update_aux.is_on_disk()) {
        MONAD_ASSERT(
            _find_and_dealloc(root.get(), src) == find_result::success);
    }
    return root;
}

MONAD_MPT_NAMESPACE_END