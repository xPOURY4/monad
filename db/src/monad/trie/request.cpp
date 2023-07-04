#include <monad/core/nibble.h>
#include <monad/trie/request.hpp>

#include <cassert>

MONAD_TRIE_NAMESPACE_BEGIN

Request::unique_ptr_type Request::split_into_subqueues(
    unique_ptr_type self, SubRequestInfo *subinfo, bool const not_root)
{
    if (is_leaf() && not_root) {
        pi = 64;
        return self;
    }
    monad::mpt::UpdateList tmp_queues[16];
    while (!pending.empty()) { // pop
        monad::mpt::Update &req = pending.front();
        pending.pop_front();
        uint8_t branch = get_nibble(req.key, pi);
        if (tmp_queues[branch].empty()) {
            subinfo->mask |= 1u << branch;
        }
        tmp_queues[branch].push_front(req);
    }
    size_t nsubnodes = size_t(std::popcount(subinfo->mask));
    if (nsubnodes == 1 && not_root) {
        int only_child = std::countr_zero(subinfo->mask);
        pending = std::move(tmp_queues[only_child]);
        ++pi;
        subinfo->mask = 0;
        return self;
    }
    else { // if is root, or if more than one subinfo branch
        subinfo->subqueues = owning_span<Request::unique_ptr_type>(nsubnodes);
        subinfo->path_len = pi;
        for (uint16_t i = 0, child_idx = 0, bit = 1; i < 16; ++i, bit <<= 1) {
            if ((subinfo->mask & bit) != 0) {
                assert(child_idx < subinfo->subqueues.size());
                subinfo->subqueues[child_idx++] =
                    Request::make(std::move(tmp_queues[i]), pi + 1);
            }
        }
        self.reset(); // destroys *this
        return {};
    }
}

MONAD_TRIE_NAMESPACE_END