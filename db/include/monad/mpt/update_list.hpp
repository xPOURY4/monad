#pragma once

#include <monad/mpt/config.hpp>
#include <monad/mpt/update.hpp>

#include <boost/intrusive/slist.hpp>

MONAD_MPT_NAMESPACE_BEGIN

using UpdateMemberHook = boost::intrusive::slist_member_hook<
    boost::intrusive::link_mode<boost::intrusive::normal_link>>;

struct UpdateNode : Update
{
    UpdateMemberHook hook_;
};

static_assert(sizeof(UpdateNode) == 56);
static_assert(alignof(UpdateNode) == 8);

using UpdateList = boost::intrusive::slist<
    UpdateNode,
    boost::intrusive::member_hook<
        UpdateNode, UpdateMemberHook, &UpdateNode::hook_>,
    boost::intrusive::constant_time_size<false>>;

static_assert(sizeof(UpdateList) == 8);
static_assert(alignof(UpdateList) == 8);

MONAD_MPT_NAMESPACE_END
