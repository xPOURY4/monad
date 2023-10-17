#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/util.hpp>

#include <boost/fiber/future.hpp>

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

void find_recursive(
    AsyncIO &io, ::boost::fibers::promise<find_result_type> &promise,
    Node *node, NibblesView key,
    std::optional<unsigned> opt_node_pi = std::nullopt);

struct find_receiver
{
    AsyncIO *io;
    ::boost::fibers::promise<find_result_type> &promise;
    Node *parent;
    NibblesView next_key;
    file_offset_t rd_offset;
    unsigned bytes_to_read;
    uint16_t buffer_off;
    unsigned char const branch_j;

    find_receiver(
        AsyncIO &_io, ::boost::fibers::promise<find_result_type> &_promise,
        Node *_parent, unsigned char const _branch, NibblesView const _next_key)
        : io(&_io)
        , promise(_promise)
        , parent(_parent)
        , next_key(_next_key)
        , branch_j(parent->to_j(_branch))
    {
        file_offset_t offset = parent->fnext_j(branch_j);
        file_offset_t node_offset = offset & MASK_TO_CLEAR_HIGH_FOUR_BITS;
        auto const num_pages_to_load_node =
            offset >> 62; // top 2 bits are for no_pages
        assert(num_pages_to_load_node <= 3);
        bytes_to_read = num_pages_to_load_node << DISK_PAGE_BITS;
        rd_offset = round_down_align<DISK_PAGE_BITS>(node_offset);
        buffer_off = uint16_t(node_offset - rd_offset);
    }

    void set_value(
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
        result<std::span<const std::byte>> _buffer)
    {
        MONAD_ASSERT(_buffer);
        std::span<const std::byte> buffer = std::move(_buffer).assume_value();
        Node *node = deserialize_node_from_buffer(
                         (unsigned char *)buffer.data() + buffer_off)
                         .release();
        parent->set_next_j(branch_j, node);
        find_recursive(*io, promise, node, next_key);
        return;
    }
};

void find_recursive(
    AsyncIO &io, ::boost::fibers::promise<find_result_type> &promise,
    Node *node, NibblesView key, std::optional<unsigned> opt_node_pi)
{
    MONAD_ASSERT(node != nullptr);
    unsigned pi = 0, node_pi = opt_node_pi.has_value()
                                   ? opt_node_pi.value()
                                   : node->bitpacked.path_nibble_index_start;
    for (; node_pi < node->path_nibble_index_end; ++node_pi, ++pi) {
        if (pi >= key.nibble_size()) {
            promise.set_value(
                {nullptr, find_result::key_ends_ealier_than_node_failure});
            return;
        }
        if (key[pi] != get_nibble(node->path_data(), node_pi)) {
            promise.set_value({nullptr, find_result::key_mismatch_failure});
            return;
        }
    }
    if (pi == key.nibble_size()) {
        promise.set_value({node, find_result::success});
        return;
    }
    MONAD_ASSERT(pi < key.nibble_size());
    if (unsigned char branch = key[pi]; node->mask & (1u << branch)) {
        NibblesView next_key = key.suffix(pi + 1);
        if (node->next(branch) == nullptr) {
            find_receiver receiver(io, promise, node, branch, next_key);
            read_update_sender sender(receiver);
            auto iostate =
                io.make_connected(std::move(sender), std::move(receiver));
            iostate->initiate();
            iostate.release();
            return;
        }
        auto next_node = node->next(branch);
        find_recursive(io, promise, next_node, next_key);
    }
    else {
        promise.set_value({nullptr, find_result::branch_not_exist_failure});
    }
}

void find_notify_fiber_future(
    AsyncIO &io, ::boost::fibers::promise<find_result_type> &promise,
    Node *const node, byte_string_view key, std::optional<unsigned> opt_node_pi)
{
    // default is to find from start node pi
    if (!node) {
        promise.set_value({nullptr, find_result::root_node_is_null_failure});
        return;
    }
    find_recursive(io, promise, node, key, opt_node_pi);
}

MONAD_MPT_NAMESPACE_END