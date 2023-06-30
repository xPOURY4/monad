#pragma once

#include <monad/mpt/config.hpp>

#include <monad/core/byte_string.hpp>

#include <boost/intrusive/slist.hpp>

#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

struct Data
{
    byte_string_view val;
    byte_string_view aux;
};

static_assert(sizeof(Data) == 32);
static_assert(alignof(Data) == 8);

static_assert(sizeof(std::optional<Data>) == 40);
static_assert(alignof(std::optional<Data>) == 8);

struct UpdateBase
{
    unsigned char *key;
    std::optional<Data> opt;
};

static_assert(sizeof(UpdateBase) == 48);
static_assert(alignof(UpdateBase) == 8);

inline constexpr bool is_deletion(UpdateBase const &u) noexcept
{
    return !u.opt.has_value();
}

using UpdateMemberHook = boost::intrusive::slist_member_hook<
    boost::intrusive::link_mode<boost::intrusive::normal_link>>;

struct Update : UpdateBase
{
    UpdateMemberHook hook_;
};

static_assert(sizeof(Update) == 56);
static_assert(alignof(Update) == 8);

using UpdateList = boost::intrusive::slist<
    Update,
    boost::intrusive::member_hook<Update, UpdateMemberHook, &Update::hook_>,
    boost::intrusive::constant_time_size<false>>;

static_assert(sizeof(UpdateList) == 8);
static_assert(alignof(UpdateList) == 8);

MONAD_MPT_NAMESPACE_END
