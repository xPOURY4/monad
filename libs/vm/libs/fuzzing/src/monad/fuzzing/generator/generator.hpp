#pragma once

#include <monad/fuzzing/generator/choice.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/uint256.hpp>

#include <array>
#include <limits>
#include <random>
#include <variant>

namespace monad::fuzzing
{
    struct ValidAddress
    {
    };

    struct ValidJumpDest
    {
    };

    struct Constant
    {
        utils::uint256_t value;
    };

    template <typename Engine>
    Constant meaningful_constant(Engine &gen)
    {
        constexpr auto values = std::array<utils::uint256_t, 4>{
            0,
            1,
            std::numeric_limits<utils::uint256_t>::max() - 1,
            std::numeric_limits<utils::uint256_t>::max(),
        };

        auto dist =
            std::uniform_int_distribution<std::size_t>(0, values.size() - 1);

        return Constant{values[dist(gen)]};
    }

    template <typename Engine>
    Constant power_of_two_constant(Engine &gen)
    {
        auto dist = std::uniform_int_distribution(0, 254);
        return Constant{
            intx::exp(utils::uint256_t(2), utils::uint256_t(dist(gen)))};
    }

    template <typename Engine>
    Constant random_constant(Engine &gen)
    {
        auto dist =
            std::uniform_int_distribution<utils::uint256_t::word_type>();

        return Constant{
            utils::uint256_t{dist(gen), dist(gen), dist(gen), dist(gen)}};
    }

    using Push = std::variant<ValidAddress, ValidJumpDest, Constant>;

    template <typename Engine>
    Push generate_push(Engine &eng)
    {
        return uniform_choice<Push>(
            eng,
            [](auto &g) { return random_constant(g); },
            Choice(0.25, [](auto &) { return ValidJumpDest{}; }),
            Choice(0.25, [](auto &) { return ValidAddress{}; }),
            Choice(0.20, [](auto &g) { return meaningful_constant(g); }),
            Choice(0.20, [](auto &g) { return power_of_two_constant(g); }));
    }
}
