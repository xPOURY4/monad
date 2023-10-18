#pragma once

#include <monad/core/nibble.h>
#include <monad/mpt/config.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/util.hpp>

#include <cstdint>
#include <optional>

MONAD_MPT_NAMESPACE_BEGIN
struct Requests
{
    uint16_t mask{0};
    uint8_t prefix_len{0};
    UpdateList sublists[16];
    std::optional<Update> opt_leaf{std::nullopt};

    Requests() = default;

    UpdateList const &operator[](size_t i) const & noexcept
    {
        assert(i < 16);
        return sublists[i];
    }

    UpdateList &&operator[](size_t i) && noexcept
    {
        assert(i < 16);
        return std::move(sublists[i]);
    }

    constexpr unsigned get_first_branch() const noexcept
    {
        return std::countr_zero(mask);
    }

    constexpr UpdateList &&first_and_only_list() && noexcept
    {
        MONAD_DEBUG_ASSERT(bitmask_count(mask) == 1);
        return std::move(sublists[get_first_branch()]);
    }

    constexpr unsigned const char *get_first_path() const noexcept
    {
        return sublists[get_first_branch()].front().key.data();
    }

    //! return the number of sublists it splits into, equals #distinct_nibbles
    //! at prefix index i.
    //! if single update, pi != key.size() * 2, put to one of sublists, n = 1
    //! if single update, pi == key.size() * 2, set opt_leaf, n = 0
    //! if multiple updates, pi = one of key size, set opt_leaf, split the rest
    //! to sublists, n >= 1
    unsigned split_into_sublists(UpdateList &&updates, unsigned const pi)
    {
        assert(updates.size());
        mask = 0;
        unsigned n = 0;
        while (!updates.empty()) {
            Update &req = updates.front();
            updates.pop_front();
            if (pi == req.key.size() * 2) {
                opt_leaf = req;
                continue;
            }
            uint8_t branch = get_nibble(req.key.data(), pi);
            if (sublists[branch].empty()) {
                mask |= 1u << branch;
                ++n;
            }
            sublists[branch].push_front(req);
        }
        prefix_len = pi;
        return n;
    }
};

MONAD_MPT_NAMESPACE_END