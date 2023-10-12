#pragma once

#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/upward_tnode.hpp>
#include <monad/mpt/util.hpp>

#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>

#include <cstdint>
#include <stack>

MONAD_MPT_NAMESPACE_BEGIN

static_assert(
    MONAD_ASYNC_NAMESPACE::AsyncIO::READ_BUFFER_SIZE >=
        round_up_align<DISK_PAGE_BITS>(
            uint16_t(MAX_DISK_NODE_SIZE + DISK_PAGE_SIZE)),
    "AsyncIO::READ_BUFFER_SIZE needs to be raised");
template <class T>
using result = MONAD_ASYNC_NAMESPACE::result<T>;
using MONAD_ASYNC_NAMESPACE::errc;
using MONAD_ASYNC_NAMESPACE::failure;
using MONAD_ASYNC_NAMESPACE::posix_code;
using MONAD_ASYNC_NAMESPACE::success;

struct Compute;
class Node;

struct async_write_node_result
{
    file_offset_t offset_written_to;
    unsigned bytes_appended;
    MONAD_ASYNC_NAMESPACE::erased_connected_operation *io_state;
};
struct write_operation_io_receiver
{
    // Node *parent{nullptr};
    void set_value(
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
        result<std::span<const std::byte>> res)
    {
        MONAD_ASSERT(res);
        // TODO: when adding upsert_sender
        // if (parent->current_process_updates_sender_ != nullptr) {
        //     parent->current_process_updates_sender_
        //         ->notify_write_operation_completed_(rawstate);
        // }
    }
    void reset() {}
};

using node_writer_unique_ptr_type =
    MONAD_ASYNC_NAMESPACE::AsyncIO::connected_operation_unique_ptr_type<
        MONAD_ASYNC_NAMESPACE::write_single_buffer_sender,
        write_operation_io_receiver>;

struct UpdateAux
{
    Compute &comp;
    MONAD_ASYNC_NAMESPACE::AsyncIO *io{nullptr};
    node_writer_unique_ptr_type node_writer{};
    /* Note on list dimension: when using nested update list with level-based
     * caching in trie, need to specify which dimension in the nested list to
     * apply cache rule on. We keep all nodes before that dimension during batch
     * upsert, apply cache rule starting that dimension, and always evict nodes
     * when larger than that dimension */
    unsigned const list_dim_to_apply_cache{1};
    unsigned current_list_dim{0};

    UpdateAux(
        Compute &comp_, MONAD_ASYNC_NAMESPACE::AsyncIO *io_ = nullptr,
        unsigned const list_dim_to_apply_cache_ = 1, file_offset_t root_off = 0)
        : comp(comp_)
        , node_writer(node_writer_unique_ptr_type{})
        , list_dim_to_apply_cache(list_dim_to_apply_cache_)
        , current_list_dim{0}
    {
        if (io_) {
            set_io(io_, root_off);
        }
    }

    void set_io(MONAD_ASYNC_NAMESPACE::AsyncIO *io_, file_offset_t root_off = 0)
    {
        io = io_;
        node_writer =
            io ? io->make_connected(
                     MONAD_ASYNC_NAMESPACE::write_single_buffer_sender{
                         round_up_align<DISK_PAGE_BITS>(root_off),
                         {(const std::byte *)nullptr,
                          MONAD_ASYNC_NAMESPACE::AsyncIO::WRITE_BUFFER_SIZE}},
                     write_operation_io_receiver{})
               : node_writer_unique_ptr_type{};
    }

    constexpr bool is_in_memory() const noexcept
    {
        return io == nullptr;
    }

    constexpr bool is_on_disk() const noexcept
    {
        return io != nullptr;
    }
};
static_assert(sizeof(UpdateAux) == 32);
static_assert(alignof(UpdateAux) == 8);

async_write_node_result async_write_node(
    MONAD_ASYNC_NAMESPACE::AsyncIO &io,
    node_writer_unique_ptr_type &node_writer, Node *node);
// invoke at the end of each block upsert
async_write_node_result
write_new_root_node(UpdateAux &update_aux, tnode_unique_ptr &root_tnode);

// batch upsert, updates can be nested
node_ptr upsert(UpdateAux &update_aux, Node *const old, UpdateList &&updates);

// TODO: implement a recursive find, prepare for fiber wrapped find()

// TODO: have find() backed by disk too
inline Node *find(Node *node, byte_string_view key, unsigned node_pi)
{
    if (!node) {
        return nullptr;
    }
    unsigned pi = 0;
    unsigned const key_nibble_size = 2 * key.size();
    while (pi < key_nibble_size) {
        unsigned char nibble = get_nibble(key.data(), pi);
        if (node->path_nibble_index_end == node_pi) {
            if (!(node->mask & (1u << nibble))) {
                return nullptr;
            }
            // go to node's matched child
            node = node->next(nibble);
            node_pi = node->bitpacked.path_nibble_index_start;
            ++pi;
            continue;
        }
        if (nibble != get_nibble(node->path_data(), node_pi)) {
            return nullptr;
        }
        // nibble is matched
        ++pi;
        ++node_pi;
    }
    if (node_pi != node->path_nibble_index_end) {
        // prefix key exists but no leaf ends at `key`
        return nullptr;
    }
    return node;
}

inline Node *find(Node *node, byte_string_view key)
{
    if (!node) {
        return nullptr;
    }
    return find(node, key, node->bitpacked.path_nibble_index_start);
}

inline int find_and_dealloc(Node *node, byte_string_view key)
{
    std::stack<std::pair<Node *, unsigned char>> node_stack{};

    unsigned pi = 0, node_pi = node->bitpacked.path_nibble_index_start;
    unsigned const key_nibble_size = 2 * key.size();
    while (pi < key_nibble_size) {
        unsigned char nibble = get_nibble(key.data(), pi);
        if (node->path_nibble_index_end == node_pi) {
            if (!(node->mask & (1u << nibble))) {
                return -1;
            }
            // go to node's matched child
            node_stack.push({node, nibble});
            node = node->next(nibble);
            node_pi = node->bitpacked.path_nibble_index_start;
            ++pi; // branch nibble
            continue;
        }
        if (nibble != get_nibble(node->path_data(), node_pi)) {
            return -1;
        }
        // nibble is matched
        ++pi;
        ++node_pi;
    }
    if (node_pi != node->path_nibble_index_end) {
        // prefix key exists but no leaf ends at `key`
        return -1;
    }

    // deallocate upward
    while (!node_stack.empty()) {
        auto [parent, branch] = node_stack.top();
        node_stack.pop();
        unsigned num_mem_child = 0;
        for (unsigned j = 0; j < parent->n(); ++j) {
            if (parent->next_j(j)) {
                ++num_mem_child;
            }
        }
        parent->next_ptr(branch);
        if (num_mem_child > 1) {
            break;
        }
    }
    return 0;
}

/* Copy a leaf node under prefix `src` to prefix `dest`. Invoked before
 * committing block updates to triedb. By copy we mean everything other than the
 * relpath. When copying children over, also remove the original's child
 * pointers to avoid dup referencing.
 * For on-disk trie specifically, once done with the copy, deallocate nodes from
 * memory under prefix `src` from bottom up using find_and_dealloc()
 */
inline node_ptr copy_node(
    node_ptr root, byte_string_view const src, byte_string_view const dest,
    bool on_disk = false)
{
    Node *src_leaf = find(root.get(), src);

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
                        src_leaf, /*unlink children to avoid dup reference*/
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
                            ret->set_next_j(j++, node->next_j(old_j++));
                        }
                    }
                    return ret;
                }();
                break;
            }
            // go to next node's matching branch
            parent = node;
            branch_i = nibble;
            node = node->next(nibble);
            node_pi = node->bitpacked.path_nibble_index_start;
            ++pi;
            continue;
        }
        if (auto node_nibble = get_nibble(node->path_data(), node_pi);
            nibble != node_nibble) {
            // split node's path: turn node to a branch node having two branches
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
        parent->next_ptr(branch_i); // deallocate child = node
        parent->set_next(branch_i, new_node);
    }
    else {
        assert(node == root.get());
        root = node_ptr{new_node}; // deallocate node
    }
    // in memory version doesn't dealloc any nodes
    if (on_disk) {
        find_and_dealloc(root.get(), src);
    }
    return root;
}

MONAD_MPT_NAMESPACE_END