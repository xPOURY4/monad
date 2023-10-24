#pragma once

#include <monad/mpt/config.hpp>

#include <monad/core/byte_string.hpp>

#include <boost/intrusive/slist.hpp>

#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

using Data = byte_string_view;

static_assert(sizeof(std::optional<Data>) == 24);
static_assert(alignof(std::optional<Data>) == 8);

struct UpdateBase
{
    byte_string_view key{};
    std::optional<Data> opt{std::nullopt};
    void *next{nullptr};
    bool incarnation{false};
};

static_assert(sizeof(UpdateBase) == 56);
static_assert(alignof(UpdateBase) == 8);

using UpdateMemberHook = boost::intrusive::slist_member_hook<
    boost::intrusive::link_mode<boost::intrusive::normal_link>>;

struct Update : UpdateBase
{
    UpdateMemberHook hook_;

    constexpr bool is_deletion() const noexcept
    {
        return !opt.has_value() && !next;
    }
};

static_assert(sizeof(Update) == 64);
static_assert(alignof(Update) == 8);

using UpdateList = boost::intrusive::slist<
    Update,
    boost::intrusive::member_hook<Update, UpdateMemberHook, &Update::hook_>,
    boost::intrusive::constant_time_size<true>>;

static_assert(sizeof(UpdateList) == 16);
static_assert(alignof(UpdateList) == 8);

// An update can mean
// 1. underlying trie updates: when opt is empty, next is set
// 2. curr trie leaf update: when opt contains a value, next = nullptr
// 3. leaf erase: when opt is empty, next = nullptr
inline Update make_update(
    monad::byte_string_view const key, monad::byte_string_view const value,
    bool incarnation = false, UpdateList *next = nullptr) noexcept
{
    return Update{
        {key, std::optional<Data>{value}, (void *)next, incarnation},
        UpdateMemberHook{}};
}

// When updates in the nested list but not in this key value pair itself
inline Update
make_update(monad::byte_string_view const key, UpdateList *next) noexcept
{
    return Update{
        {key, std::nullopt, (void *)next, /*incarnation*/ false},
        UpdateMemberHook{}};
}

inline Update make_erase(monad::byte_string_view const key) noexcept
{
    return Update{{key, std::nullopt}, UpdateMemberHook{}};
}

MONAD_MPT_NAMESPACE_END
