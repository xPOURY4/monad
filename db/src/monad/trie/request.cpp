#include <monad/core/nibble.h>
#include <monad/trie/request.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

Request *Request::split_into_subqueues(
    SubRequestInfo *const subinfo, bool const not_root)
{
    if (is_leaf() && not_root) {
        pi = 64;
        return this;
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
    int nsubnodes = std::popcount(subinfo->mask);
    if (nsubnodes == 1 && not_root) {
        int only_child = std::countr_zero(subinfo->mask);
        pending = std::move(tmp_queues[only_child]);
        ++pi;
        subinfo->mask = 0;
        return this;
    }
    else { // if is root, or if more than one subinfo branch
        subinfo->subqueues = std::make_unique<Request *[]>(nsubnodes);
        subinfo->path_len = pi;
        for (int i = 0, child_idx = 0; i < 16; ++i) {
            if (subinfo->mask & 1u << i) {
                subinfo->subqueues[child_idx++] =
                    pool.construct(tmp_queues[i], pi + 1);
            }
        }
        pool.destroy(this);
        return nullptr;
    }
}

MONAD_TRIE_NAMESPACE_END