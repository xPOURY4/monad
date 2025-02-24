#pragma once

#include <monad/utils/assert.h>

#include <optional>
#include <random>

namespace monad::fuzzing
{
    namespace detail
    {
        template <typename Tuple, typename Func>
        void for_each_tuple(Tuple &&t, Func &&f)
        {
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (std::forward<Func>(f)(std::get<Is>(std::forward<Tuple>(t))),
                 ...);
            }(std::make_index_sequence<std::tuple_size_v<Tuple>>());
        }
    }

    template <typename Action>
    struct Choice
    {
        double probability;
        Action action;

        Choice(double p, Action a)
            : probability(p)
            , action(std::move(a))
        {
        }
    };

    template <
        typename Result, typename Engine, typename Default, typename... Choices>
    auto discrete_choice(Engine &eng, Default &&d, Choices &&...choices)
    {
        auto result = std::optional<Result>{};
        auto cumulative = 0.0;

        auto dist = std::uniform_real_distribution<double>(0.0, 1.0);
        auto const cutoff = dist(eng);

        detail::for_each_tuple(
            std::forward_as_tuple(
                choices..., Choice(1.0, std::forward<Default>(d))),
            [&](auto &&choice) {
                using Choice = decltype(choice);

                cumulative += std::forward<Choice>(choice).probability;
                if (!result && cumulative >= cutoff) {
                    result = Result{std::forward<Choice>(choice).action(eng)};
                }
            });

        MONAD_COMPILER_DEBUG_ASSERT(result.has_value());
        return *result;
    }

    template <typename Engine, typename Action>
    void
    with_probability(Engine &eng, double const probability, Action &&action)
    {
        auto dist = std::uniform_real_distribution<double>(0.0, 1.0);
        auto const cutoff = dist(eng);

        if (probability >= cutoff) {
            std::forward<Action>(action)(eng);
        }
    }

    template <typename Engine, typename Container>
    Container::value_type const &
    uniform_sample(Engine &eng, Container const &in)
    {
        MONAD_COMPILER_DEBUG_ASSERT(!in.empty());
        auto dist =
            std::uniform_int_distribution<std::size_t>(0, in.size() - 1);
        return in[dist(eng)];
    }
}
