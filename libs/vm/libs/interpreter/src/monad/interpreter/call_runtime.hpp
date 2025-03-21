#pragma once

#include <monad/interpreter/state.hpp>
#include <monad/runtime/detail.hpp>
#include <monad/runtime/types.hpp>

namespace monad::interpreter
{
    template <typename... FnArgs>
    [[gnu::always_inline]]
    inline std::int64_t call_runtime(
        void (*f)(FnArgs...), runtime::Context &ctx, State &state,
        std::int64_t gas_remaining)
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
                return std::tuple{(state.stack_top - Is)...};
            }(std::make_index_sequence<stack_arg_count>());

        auto const with_result_args = [&] {
            if constexpr (use_result) {
                if constexpr (stack_arg_count == 0) {
                    return std::tuple(state.stack_top + 1);
                }
                else {
                    return std::tuple_cat(
                        std::tuple(std::get<stack_arg_count - 1>(stack_args)),
                        stack_args);
                }
            }
            else {
                return stack_args;
            }
        }();

        auto const with_context_args = [&] {
            if constexpr (use_context) {
                return std::tuple_cat(std::tuple(&ctx), with_result_args);
            }
            else {
                return with_result_args;
            }
        }();

        auto const all_args = [&] {
            if constexpr (use_base_gas) {
                return std::tuple_cat(
                    with_context_args, std::tuple(std::int64_t{0}));
            }
            else {
                return with_context_args;
            }
        }();

        ctx.gas_remaining = gas_remaining;
        std::apply(f, all_args);

        static_assert(
            stack_arg_count <= std::numeric_limits<std::ptrdiff_t>::max());
        constexpr std::ptrdiff_t stack_adjustment =
            static_cast<std::ptrdiff_t>(stack_arg_count) - (use_result ? 1 : 0);
        state.stack_top -= stack_adjustment;

        return ctx.gas_remaining;
    }
}
