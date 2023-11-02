#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/nibble.h>
#include <monad/mpt/config.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

MONAD_MPT_NAMESPACE_BEGIN

class NibblesView;
class Node;

class Nibbles
{
private:
    friend class NibblesView;
    using size_type = uint8_t; // max length support is 255 nibbles
    std::unique_ptr<unsigned char[]> data_;
    bool begin_nibble_{false};
    size_type end_nibble_{0};

public:
    constexpr Nibbles() = default;

    Nibbles(unsigned const end_nibble)
        : data_(std::make_unique<unsigned char[]>((end_nibble + 1) / 2))
        , begin_nibble_(false)
        , end_nibble_(static_cast<size_type>(end_nibble))
    {
        MONAD_DEBUG_ASSERT(end_nibble <= std::numeric_limits<size_type>::max());
    }

    Nibbles &operator=(NibblesView const &other);

    constexpr unsigned size() const noexcept
    {
        return ((size_type)begin_nibble_ == end_nibble_)
                   ? 0
                   : ((end_nibble_ + 1) / 2);
    }

    constexpr void set(unsigned const i, unsigned char const value)
    {
        MONAD_DEBUG_ASSERT(value <= 0xF);
        MONAD_DEBUG_ASSERT(
            i < static_cast<unsigned>(
                    end_nibble_ - static_cast<size_type>(begin_nibble_)));
        ::set_nibble(data_.get(), begin_nibble_ + i, value);
    }
};
static_assert(sizeof(Nibbles) == 16);
static_assert(alignof(Nibbles) == 8);

class NibblesView
{
private:
    friend class Nibbles;
    using size_type = Nibbles::size_type;
    unsigned char const *data_{nullptr};
    bool begin_nibble_{false};
    size_type end_nibble_{0};

public:
    constexpr NibblesView() = default;
    constexpr NibblesView(NibblesView const &) = default;
    NibblesView &operator=(NibblesView const &) = default;

    constexpr explicit NibblesView(
        unsigned const begin_nibble, unsigned const end_nibble,
        unsigned char const *const data) noexcept
        : data_(
              (begin_nibble == end_nibble) ? nullptr
                                           : (data + begin_nibble / 2))
        , begin_nibble_(data_ == nullptr ? false : (begin_nibble & 1))
        , end_nibble_(
              data_ == nullptr ? 0
                               : static_cast<size_type>(
                                     end_nibble - begin_nibble + begin_nibble_))
    {
        MONAD_DEBUG_ASSERT(
            begin_nibble <= end_nibble &&
            end_nibble <= std::numeric_limits<size_type>::max());
    }

    // constructor from byte_string_view
    constexpr NibblesView(byte_string_view const &s) noexcept
        : NibblesView(false, static_cast<uint8_t>(2 * s.size()), s.data())
    {
        MONAD_DEBUG_ASSERT(
            (s.size() * 2) <= std::numeric_limits<size_type>::max());
    }

    // constructor from byte_string
    constexpr NibblesView(byte_string const &s) noexcept
        : NibblesView(byte_string_view{s})
    {
    }

    // construct from Nibbles
    constexpr NibblesView(Nibbles const &n) noexcept
        : NibblesView{n.begin_nibble_, n.end_nibble_, n.data_.get()}
    {
    }

    constexpr size_type nibble_size() const
    {
        return end_nibble_ - static_cast<size_type>(begin_nibble_);
    }

    // size of data in bytes
    constexpr unsigned size() const
    {
        return (static_cast<size_type>(begin_nibble_) == end_nibble_)
                   ? 0
                   : ((end_nibble_ + 1) / 2);
    }

    constexpr NibblesView suffix(size_type pos) const
    {
        return NibblesView{
            static_cast<unsigned>(begin_nibble_ + pos), end_nibble_, data_};
    }

    constexpr bool operator==(NibblesView const &other) const
    {
        if (this == &other) {
            return true;
        }

        if (nibble_size() != other.nibble_size()) {
            return false;
        }

        if (nibble_size()) {
            MONAD_DEBUG_ASSERT(data_ && other.data_);
            for (auto i = 0u; i < nibble_size(); ++i) {
                if (get(i) != other.get(i)) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] unsigned char get(unsigned const i) const
    {
        MONAD_ASSERT(i < nibble_size());
        return get_nibble(data_, begin_nibble_ + i);
    }

    friend void serialize_to_node(NibblesView const, Node &);
};
static_assert(sizeof(NibblesView) == 16);
static_assert(alignof(NibblesView) == 8);
static_assert(std::is_trivially_copyable_v<NibblesView> == true);

inline Nibbles &Nibbles::operator=(NibblesView const &n)
{
    begin_nibble_ = n.begin_nibble_;
    end_nibble_ = n.end_nibble_;
    data_.reset();
    if (begin_nibble_ != end_nibble_) {
        data_ = std::make_unique<unsigned char[]>(n.size());
        std::memcpy(data_.get(), n.data_, n.size());
    }
    return *this;
}

inline Nibbles concat3(
    NibblesView const prefix, unsigned char const nibble,
    NibblesView const suffix)
{
    Nibbles res{prefix.nibble_size() + 1u + suffix.nibble_size()};
    for (auto i = 0u; i < prefix.nibble_size(); ++i) {
        res.set(i, prefix.get(i));
    }
    res.set(prefix.nibble_size(), nibble);
    for (auto i = 0u; i < suffix.nibble_size(); ++i) {
        res.set(i + prefix.nibble_size() + 1u, suffix.get(i));
    }
    return res;
}

inline Nibbles concat2(unsigned char const nibble, NibblesView const suffix)
{
    Nibbles res{1u + suffix.nibble_size()};
    res.set(0u, nibble);
    for (auto i = 0u; i < suffix.nibble_size(); ++i) {
        res.set(i + 1u, suffix.get(i));
    }
    return res;
}

MONAD_MPT_NAMESPACE_END
