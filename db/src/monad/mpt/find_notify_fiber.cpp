#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/util.hpp>

#include <boost/fiber/future.hpp>

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

void find_recursive(
    AsyncIO &, inflight_map_t &, ::boost::fibers::promise<find_result_type> &,
    Node *, NibblesView key,
    std::optional<unsigned> opt_node_pi = std::nullopt);

struct find_receiver
{
    AsyncIO *io;
    inflight_map_t &inflights;
    Node *parent;
    chunk_offset_t rd_offset; // required for sender
    unsigned bytes_to_read; // required for sender too
    uint16_t buffer_off;
    unsigned const branch_j;

    find_receiver(
        AsyncIO &_io, inflight_map_t &inflights_, Node *const parent_,
        unsigned char const branch_)
        : io(&_io)
        , inflights(inflights_)
        , parent(parent_)
        , rd_offset(0, 0)
        , branch_j(parent->to_j(branch_))
    {
        chunk_offset_t offset = parent->fnext_j(branch_j);
        auto const num_pages_to_load_node =
            offset.spare; // top 2 bits are for no_pages
        assert(num_pages_to_load_node <= 3);
        bytes_to_read =
            static_cast<unsigned>(num_pages_to_load_node << DISK_PAGE_BITS);
        rd_offset = offset;
        auto const new_offset = round_down_align<DISK_PAGE_BITS>(offset.offset);
        MONAD_DEBUG_ASSERT(new_offset <= chunk_offset_t::max_offset);
        rd_offset.offset = new_offset & chunk_offset_t::max_offset;
        buffer_off = uint16_t(offset.offset - rd_offset.offset);
    }

    //! notify a list of requests pending on this node
    void set_value(
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
        result<std::span<const std::byte>> buffer_)
    {
        MONAD_ASSERT(buffer_);
        std::span<const std::byte> buffer = std::move(buffer_).assume_value();
        MONAD_ASSERT(parent->next_j(branch_j) == nullptr);
        Node *node = deserialize_node_from_buffer(
                         (unsigned char *)buffer.data() + buffer_off)
                         .release();
        parent->set_next_j(branch_j, node);
        auto const offset = parent->fnext_j(branch_j);
        auto &pendings = inflights.at(offset);
        for (auto &[key, promise] : pendings) {
            MONAD_DEBUG_ASSERT(promise != nullptr);
            find_recursive(*io, inflights, *promise, node, key);
        }
        inflights.erase(offset);
        return;
    }
};

// Use a hashtable for inflight requests, it maps a file offset to a list of
// requests. If a read request exists in the hash table, simply append to an
// existing inflight read, Otherwise, send a read request and put itself on the
// map
void find_recursive(
    AsyncIO &io, inflight_map_t &inflights,
    ::boost::fibers::promise<find_result_type> &promise, Node *node,
    NibblesView const key, std::optional<unsigned> opt_node_pi)
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
        if (key.get(pi) != get_nibble(node->path_data(), node_pi)) {
            promise.set_value({nullptr, find_result::key_mismatch_failure});
            return;
        }
    }
    if (pi == key.nibble_size()) {
        promise.set_value({node, find_result::success});
        return;
    }
    MONAD_ASSERT(pi < key.nibble_size());
    if (unsigned char const branch = key.get(pi); node->mask & (1u << branch)) {
        MONAD_DEBUG_ASSERT(pi < std::numeric_limits<unsigned char>::max());
        auto const next_key = key.substr(static_cast<unsigned char>(pi) + 1u);
        if (node->next(branch) != nullptr) {
            find_recursive(
                io, inflights, promise, node->next(branch), next_key);
            return;
        }
        chunk_offset_t const offset = node->fnext(branch);
        if (auto lt = inflights.find(offset); lt != inflights.end()) {
            lt->second.push_back({next_key, &promise});
            return;
        }
        inflights.emplace(
            offset, std::list<detail::pending_request_t>{{next_key, &promise}});
        find_receiver receiver(io, inflights, node, branch);
        read_update_sender sender(receiver);
        auto iostate =
            io.make_connected(std::move(sender), std::move(receiver));
        iostate->initiate();
        iostate.release();
    }
    else {
        promise.set_value({nullptr, find_result::branch_not_exist_failure});
    }
}

void find_notify_fiber_future(
    AsyncIO &io, inflight_map_t &inflights, find_request_t const req)
{
    // default is to find from start node pi
    if (!req.root) {
        req.promise->set_value(
            {nullptr, find_result::root_node_is_null_failure});
        return;
    }
    find_recursive(io, inflights, *req.promise, req.root, req.key, req.node_pi);
}

MONAD_MPT_NAMESPACE_END
