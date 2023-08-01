#pragma once

#include <monad/config.hpp>

#include "../../../third_party/ankerl/robin_hood.h"
#include "../../../third_party/ankerl/unordered_dense.h"

#include <unordered_map>
#include <unordered_set>

MONAD_NAMESPACE_BEGIN

/* std::unordered_map is very slow by modern hash table standards (see
  https://martin.ankerl.com/2022/08/27/hashmap-bench-01/#random-insert--access-uint64_t),
  so use a much better third party map implementation.

  I've chosen ankerl's hash maps, even though he's almost certainly biased in
  his benchmarking. I think his hash maps won't be especially slower than
  alternatives, and he makes single include file self contained implementations
  which makes them very easy to integrate. They don't splosh outside their
  namespaces, and don't interfere with other code.

  His hash maps are virtually unusable in the debugger, so if `NDEBUG` is not
  defined, everything turns into `std::unordered_map`.
*/

namespace detail
{
    class unordered_flat_map_disabled;
    class unordered_flat_set_disabled;
} // namespace detail

/*! \brief A much faster node-based drop in alternative to `std::unordered_map`,
usually at least 2x faster.

- Like `std::unordered_map`, references are stable to modification.
- Like `std::unordered_map`, iterators are NOT stable to modification.
- Much slower than `unordered_flat_map` or `unordered_dense_map`, so use
those instead if you can.

Be aware:

- The current implementation does not expose its allocation for customisation.
- The current implementation terminates the process on memory exhaustion.
- The current implementation predates the C++ 17 map extraction extensions,
and does not implement those.
*/
#ifdef NDEBUG
template <
    class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_node_map =
    robin_hood::unordered_node_map<Key, T, Hash, Compare>;
template <
    class Key, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_node_set = robin_hood::unordered_node_set<Key, Hash, Compare>;
#else
template <
    class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_node_map = std::unordered_map<Key, T, Hash, Compare>;
template <
    class Key, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_node_set = std::unordered_set<Key, Hash, Compare>;
#endif

/*! \brief A much faster inline-storage-based alternative to
`std::unordered_map`, usually around 5x faster.

- This is NOT drop in compatible with `std::unordered_map` as references are not
stable to modification.
- State of the art insertion and lookup speed but at the cost of removal speed.
If you need fast removals, use `unordered_flat_map` instead.
- Supports PMR custom allocators and the C++ 17 map extraction extensions.
- Metaprogramming disables implementation with a useful compiler diagnostic if
value type's size exceeds 64 bytes, as you probably should use a node map
instead for such large values.

Be aware:

- Maximum item count is 2^32-1.
*/
#ifdef NDEBUG
template <
    class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<Key, T>>>
using unordered_dense_map = std::conditional_t<
    sizeof(std::pair<Key, T>) <= 64,
    ankerl::unordered_dense::segmented_map<Key, T, Hash, Compare, Allocator>,
    detail::unordered_flat_map_disabled>;
template <
    class Key, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>, class Allocator = std::allocator<Key>>
using unordered_dense_set = std::conditional_t<
    sizeof(Key) <= 64,
    ankerl::unordered_dense::segmented_set<Key, Hash, Compare, Allocator>,
    detail::unordered_flat_set_disabled>;
#else
template <
    class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>,
    class Allocator = std::allocator<std::pair<const Key, T>>>
using unordered_dense_map = std::conditional_t<
    sizeof(std::pair<Key, T>) <= 64,
    std::unordered_map<Key, T, Hash, Compare, Allocator>,
    detail::unordered_flat_map_disabled>;
template <
    class Key, class Hash = ankerl::unordered_dense::hash<Key>,
    class Compare = std::equal_to<Key>, class Allocator = std::allocator<Key>>
using unordered_dense_set = std::conditional_t<
    sizeof(Key) <= 64, std::unordered_set<Key, Hash, Compare, Allocator>,
    detail::unordered_flat_set_disabled>;
#endif

/*! \brief A much faster inline-storage-based alternative to
`std::unordered_map`, usually around 4x faster.

- This is NOT drop in compatible with `std::unordered_map` as references are not
stable to modification.
- Unless you need fast removals, `unordered_dense_map` will be faster.
- Metaprogramming disables implementation with a useful compiler diagnostic if
value type's size exceeds 64 bytes, as you probably should use a node map
instead for such large values.

Be aware:

- The current implementation requires the value type to be nothrow movable.
- The current implementation does not expose its allocation for customisation.
- The current implementation terminates the process on memory exhaustion.
- The current implementation predates the C++ 17 map extraction extensions,
and does not implement those.
*/
#ifdef NDEBUG
template <
    class Key, class T, class Hash = robin_hood::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_flat_map = std::conditional_t<
    sizeof(robin_hood::pair<Key, T>) <= 64 &&
        std::is_nothrow_move_constructible<robin_hood::pair<Key, T>>::value &&
        std::is_nothrow_move_assignable<robin_hood::pair<Key, T>>::value,
    robin_hood::unordered_flat_map<Key, T, Hash, Compare>,
    detail::unordered_flat_map_disabled>;
template <
    class Key, class Hash = robin_hood::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_flat_set = std::conditional_t<
    sizeof(Key) <= 64 && std::is_nothrow_move_constructible<Key>::value &&
        std::is_nothrow_move_assignable<Key>::value,
    robin_hood::unordered_flat_set<Key, Hash, Compare>,
    detail::unordered_flat_set_disabled>;
#else
template <
    class Key, class T, class Hash = robin_hood::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_flat_map = std::conditional_t<
    sizeof(robin_hood::pair<Key, T>) <= 64 &&
        std::is_nothrow_move_constructible<robin_hood::pair<Key, T>>::value &&
        std::is_nothrow_move_assignable<robin_hood::pair<Key, T>>::value,
    std::unordered_map<Key, T, Hash, Compare>,
    detail::unordered_flat_map_disabled>;
template <
    class Key, class Hash = robin_hood::hash<Key>,
    class Compare = std::equal_to<Key>>
using unordered_flat_set = std::conditional_t<
    sizeof(Key) <= 64 && std::is_nothrow_move_constructible<Key>::value &&
        std::is_nothrow_move_assignable<Key>::value,
    std::unordered_set<Key, Hash, Compare>,
    detail::unordered_flat_set_disabled>;
#endif

MONAD_NAMESPACE_END
