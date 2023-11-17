#pragma once

#include <monad/mpt/config.hpp>

#include <monad/async/detail/start_lifetime_as_polyfill.hpp>

#include "unsigned_20.hpp"

MONAD_MPT_NAMESPACE_BEGIN
class UpdateAux;

namespace detail
{
#ifndef __clang__
    constexpr bool bitfield_layout_check()
    {
        /* Make sure reserved0_ definitely lives at offset +3
         */
        constexpr struct
        {
            uint32_t chunk_info_count : 20;
            uint32_t unused0_ : 4;
            uint32_t reserved0_ : 8;
        } v{.reserved0_ = 1};
        struct type
        {
            uint8_t x[sizeof(v)];
        };
        constexpr type ret = std::bit_cast<type>(v);
        return ret.x[3];
    }
#endif

    // For the memory map of the first conventional chunk
    struct db_metadata
    {
        friend class MONAD_MPT_NAMESPACE::UpdateAux;

        char magic[4]; // "MND0"
        uint32_t chunk_info_count : 20; // items in chunk_info below
        uint32_t unused0_ : 4; // next item MUST be on a byte boundary
        uint32_t reserved_for_is_dirty_ : 8; // for is_dirty below
        // DO NOT INSERT ANYTHING IN HERE
        uint64_t capacity_in_free_list; // used to detect when free space is
                                        // running low
        chunk_offset_t root_offset;

        // used to know if the metadata was being
        // updated when the process suddenly exited
        std::atomic<uint8_t> &is_dirty() noexcept
        {
            static_assert(sizeof(std::atomic<uint8_t>) == sizeof(uint8_t));
#ifndef __clang__
            static_assert(bitfield_layout_check());
#endif
            return *start_lifetime_as<std::atomic<uint8_t>>(
                (std::byte *)&capacity_in_free_list - 1);
        }

        struct id_pair
        {
            uint32_t begin, end;
        } free_list, fast_list, slow_list;
        struct chunk_info_t
        {
            static constexpr uint32_t INVALID_CHUNK_ID = 0xfffff;
            uint64_t prev_chunk_id : 20; // same bits as from chunk_offset_t
            uint64_t in_fast_list : 1;
            uint64_t in_slow_list : 1;
            uint64_t insertion_count0_ : 10; // align next to 8 bit boundary to
                                             // aid codegen
            uint64_t next_chunk_id : 20; // same bits as from chunk_offset_t
            uint64_t unused0_ : 2;
            uint64_t insertion_count1_ : 10;

            uint32_t index(db_metadata const *parent) const noexcept
            {
                auto ret = uint32_t(this - parent->chunk_info);
                assert(ret < parent->chunk_info_count);
                return ret;
            }
            unsigned_20 insertion_count() const noexcept
            {
                return uint32_t(insertion_count1_ << 10) |
                       uint32_t(insertion_count0_);
            }
            chunk_info_t const *prev(db_metadata const *parent) const noexcept
            {
                if (prev_chunk_id == INVALID_CHUNK_ID) {
                    return nullptr;
                }
                assert(prev_chunk_id < parent->chunk_info_count);
                return &parent->chunk_info[prev_chunk_id];
            }
            chunk_info_t const *next(db_metadata const *parent) const noexcept
            {
                if (next_chunk_id == INVALID_CHUNK_ID) {
                    return nullptr;
                }
                assert(next_chunk_id < parent->chunk_info_count);
                return &parent->chunk_info[next_chunk_id];
            }
        };
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wc99-extensions"
#elif defined __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif
        chunk_info_t chunk_info[];
#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined __GNUC__
    #pragma GCC diagnostic pop
#endif
        static_assert(sizeof(chunk_info_t) == 8);
        static_assert(std::is_trivially_copyable_v<chunk_info_t>);

        auto hold_dirty() noexcept
        {
            static_assert(sizeof(std::atomic<uint8_t>) == sizeof(uint8_t));
            struct holder_t
            {
                db_metadata *parent;

                explicit holder_t(db_metadata *p)
                    : parent(p)
                {
                    parent->is_dirty().store(1, std::memory_order_release);
                }
                holder_t(holder_t const &) = delete;
                holder_t(holder_t &&o) noexcept
                    : parent(o.parent)
                {
                    o.parent = nullptr;
                }
                ~holder_t()
                {
                    if (parent != nullptr) {
                        parent->is_dirty().store(0, std::memory_order_release);
                    }
                }
            };
            return holder_t(this);
        }
        chunk_info_t const *at(uint32_t idx) const noexcept
        {
            assert(idx < chunk_info_count);
            return &chunk_info[idx];
        }
        chunk_info_t const &operator[](uint32_t idx) const noexcept
        {
            assert(idx < chunk_info_count);
            return chunk_info[idx];
        }
        chunk_info_t const *free_list_begin() const noexcept
        {
            if (free_list.begin == UINT32_MAX) {
                return nullptr;
            }
            assert(free_list.begin < chunk_info_count);
            return &chunk_info[free_list.begin];
        }
        chunk_info_t const *free_list_end() const noexcept
        {
            if (free_list.end == UINT32_MAX) {
                return nullptr;
            }
            assert(free_list.end < chunk_info_count);
            return &chunk_info[free_list.end];
        }
        chunk_info_t const *fast_list_begin() const noexcept
        {
            if (fast_list.begin == UINT32_MAX) {
                return nullptr;
            }
            assert(fast_list.begin < chunk_info_count);
            return &chunk_info[fast_list.begin];
        }
        chunk_info_t const *fast_list_end() const noexcept
        {
            if (fast_list.end == UINT32_MAX) {
                return nullptr;
            }
            assert(fast_list.end < chunk_info_count);
            return &chunk_info[fast_list.end];
        }
        chunk_info_t const *slow_list_begin() const noexcept
        {
            if (slow_list.begin == UINT32_MAX) {
                return nullptr;
            }
            assert(slow_list.begin < chunk_info_count);
            return &chunk_info[slow_list.begin];
        }
        chunk_info_t const *slow_list_end() const noexcept
        {
            if (slow_list.end == UINT32_MAX) {
                return nullptr;
            }
            assert(slow_list.end < chunk_info_count);
            return &chunk_info[slow_list.end];
        }

    private:
        chunk_info_t *at_(uint32_t idx) noexcept
        {
            assert(idx < chunk_info_count);
            return &chunk_info[idx];
        }
        void append_(id_pair &list, chunk_info_t *i) noexcept
        {
            auto g = hold_dirty();
            i->in_fast_list = (&list == &fast_list);
            i->in_slow_list = (&list == &slow_list);
            i->insertion_count0_ = i->insertion_count1_ = 0;
            i->next_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
            if (list.end == UINT32_MAX) {
                assert(list.begin == UINT32_MAX);
                i->prev_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
                list.begin = list.end = i->index(this);
                return;
            }
            assert((list.end & ~0xfffffU) == 0);
            i->prev_chunk_id = list.end & 0xfffffU;
            auto *tail = at_(list.end);
            auto const insertion_count = tail->insertion_count() + 1;
            assert(tail->next_chunk_id == chunk_info_t::INVALID_CHUNK_ID);
            i->insertion_count0_ = uint32_t(insertion_count) & 0x3ff;
            i->insertion_count1_ = uint32_t(insertion_count >> 10) & 0x3ff;
            list.end = tail->next_chunk_id = i->index(this) & 0xfffffU;
        }
        void prepend_(id_pair &list, chunk_info_t *i) noexcept
        {
            auto g = hold_dirty();
            i->in_fast_list = (&list == &fast_list);
            i->in_slow_list = (&list == &slow_list);
            i->insertion_count0_ = i->insertion_count1_ = 0;
            i->prev_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
            if (list.begin == UINT32_MAX) {
                assert(list.end == UINT32_MAX);
                i->next_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
                list.begin = list.end = i->index(this);
                return;
            }
            assert((list.begin & ~0xfffffU) == 0);
            i->next_chunk_id = list.begin & 0xfffffU;
            auto *head = at_(list.begin);
            auto const insertion_count = head->insertion_count() - 1;
            assert(head->prev_chunk_id == chunk_info_t::INVALID_CHUNK_ID);
            i->insertion_count0_ = uint32_t(insertion_count) & 0x3ff;
            i->insertion_count1_ = uint32_t(insertion_count >> 10) & 0x3ff;
            list.begin = head->prev_chunk_id = i->index(this) & 0xfffff;
        }
        void remove_(chunk_info_t *i) noexcept
        {
            auto get_list = [&]() -> id_pair & {
                if (i->in_fast_list) {
                    return fast_list;
                }
                if (i->in_slow_list) {
                    return slow_list;
                }
                return free_list;
            };
            auto g = hold_dirty();
            if (i->prev_chunk_id == chunk_info_t::INVALID_CHUNK_ID &&
                i->next_chunk_id == chunk_info_t::INVALID_CHUNK_ID) {
                id_pair &list = get_list();
                assert(list.begin == i->index(this));
                assert(list.end == i->index(this));
                list.begin = list.end = UINT32_MAX;
#ifndef NDEBUG
                i->in_fast_list = i->in_slow_list = false;
#endif
                return;
            }
            if (i->prev_chunk_id == chunk_info_t::INVALID_CHUNK_ID) {
                id_pair &list = get_list();
                assert(list.begin == i->index(this));
                auto *next = at_(i->next_chunk_id);
                next->prev_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
                list.begin = next->index(this);
#ifndef NDEBUG
                i->in_fast_list = i->in_slow_list = false;
                i->next_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
#endif
                return;
            }
            if (i->next_chunk_id == chunk_info_t::INVALID_CHUNK_ID) {
                id_pair &list = get_list();
                assert(list.end == i->index(this));
                auto *prev = at_(i->prev_chunk_id);
                prev->next_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
                list.end = prev->index(this);
#ifndef NDEBUG
                i->in_fast_list = i->in_slow_list = false;
                i->prev_chunk_id = chunk_info_t::INVALID_CHUNK_ID;
#endif
                return;
            }
            MONAD_ASSERT(
                "remove_() has had mid-list removals explicitly disabled to "
                "prevent insertion count becoming inaccurate" == nullptr);
            auto *prev = at_(i->prev_chunk_id);
            auto *next = at_(i->next_chunk_id);
            prev->next_chunk_id = next->index(this) & 0xfffffU;
            next->prev_chunk_id = prev->index(this) & 0xfffffU;
#ifndef NDEBUG
            i->in_fast_list = i->in_slow_list = false;
            i->prev_chunk_id = i->next_chunk_id =
                chunk_info_t::INVALID_CHUNK_ID;
#endif
        }
        void free_capacity_add_(uint64_t bytes) noexcept
        {
            auto g = hold_dirty();
            capacity_in_free_list += bytes;
        }
        void free_capacity_sub_(uint64_t bytes) noexcept
        {
            auto g = hold_dirty();
            capacity_in_free_list -= bytes;
        }
    };
    static_assert(std::is_trivially_copyable_v<db_metadata>);
}

MONAD_MPT_NAMESPACE_END
