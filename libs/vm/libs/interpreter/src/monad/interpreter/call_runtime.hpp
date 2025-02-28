#pragma once

#include <monad/interpreter/state.hpp>
#include <monad/runtime/detail.hpp>
#include <monad/runtime/types.hpp>

namespace monad::interpreter
{
    template <typename... FnArgs>
    [[gnu::always_inline]]
    inline void
    call_runtime(void (*f)(FnArgs...), runtime::Context &ctx, State &state)
    {
        using namespace monad::runtime;

        constexpr auto use_context = detail::uses_context_v<FnArgs...>;
        constexpr auto use_result = detail::uses_result_v<FnArgs...>;
        constexpr auto use_base_gas = detail::uses_remaining_gas_v<FnArgs...>;

        constexpr auto stack_arg_count =
            sizeof...(FnArgs) -
            std::ranges::count(
                std::array{use_context, use_result, use_base_gas}, true);

        auto const stack_args =
            [&state]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::array{(state.stack_top - Is)...};
            }(std::make_index_sequence<stack_arg_count>());

        auto const word_args = [&] {
            if constexpr (use_context && use_result) {
                return std::tuple_cat(
                    std::tuple(&ctx, stack_args[stack_arg_count - 1]),
                    stack_args);
            }
            else if constexpr (use_context) {
                return std::tuple_cat(std::tuple(&ctx), stack_args);
            }
            else if constexpr (use_result) {
                return std::tuple_cat(
                    std::tuple(stack_args[stack_arg_count - 1]), stack_args);
            }
            else {
                return stack_args;
            }
        }();

        auto const all_args = [&] {
            if constexpr (use_base_gas) {
                return std::tuple_cat(word_args, std::tuple(std::int64_t{0}));
            }
            else {
                return word_args;
            }
        }();

        std::apply(f, all_args);

        constexpr auto stack_adjustment =
            stack_arg_count - (use_result ? 1 : 0);
        state.stack_top -= stack_adjustment;
    }
}
