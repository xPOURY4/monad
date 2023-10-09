#pragma once

#include <monad/mpt/update.hpp>

namespace monad::test
{

    // compare number of nibbles first. if equal, compare lexicographically
    [[nodiscard]] inline int
    path_compare(byte_string_view s1, byte_string_view s2)
    {
        assert(!s1.empty());
        assert(!s2.empty());

        auto const s1_size = static_cast<uint8_t>(s1[0]);
        auto const s2_size = static_cast<uint8_t>(s2[0]);

        auto rc = std::memcmp(&s1_size, &s2_size, 1);
        if (rc != 0) {
            return rc;
        }

        bool const odd = s1_size % 2;
        assert(s1.size() == (1u + s1_size / 2u + odd));
        assert(s2.size() == (1u + s1_size / 2u + odd));
        rc = std::memcmp(s1.data(), s2.data(), s1.size() - odd);
        if (rc != 0 || !odd) {
            return rc;
        }

        uint8_t const b1 = s1[s1.size() - 1] & 0xF0;
        uint8_t const b2 = s2[s2.size() - 1] & 0xF0;

        return std::memcmp(&b1, &b2, 1);
    }

    struct InMemoryPathComparator
    {
        [[nodiscard]] inline bool
        operator()(byte_string_view element, byte_string_view value) const
        {
            return path_compare(element, value) < 0;
        }
    };

    struct InMemoryPrefixPathComparator
    {
        [[nodiscard]] inline bool
        operator()(byte_string_view element, byte_string_view value) const
        {
            assert(element.size() > 20);
            assert(value.size() > 20);

            auto const rc = std::memcmp(element.data(), value.data(), 20);
            if (rc != 0) {
                return rc < 0;
            }

            element.remove_prefix(20);
            value.remove_prefix(20);

            return path_compare(element, value) < 0;
        }
    };

}
