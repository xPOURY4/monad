#pragma once

#include <runtime/types.h>
#include <utils/uint256.h>

#include <gtest/gtest.h>

#include <evmc/evmc.hpp>
#include <evmc/mocked_host.hpp>

#include <limits>
#include <optional>

using namespace evmc::literals;

namespace detail
{
    template <std::size_t N, typename... Ts>
    using nth_t = std::tuple_element_t<N, std::tuple<Ts...>>;

    template <typename... Ts>
    using last_t = nth_t<sizeof...(Ts) - 1, Ts...>;

    template <typename T>
    struct is_const_pointer : std::false_type
    {
    };

    template <typename T>
    struct is_const_pointer<T const *> : std::true_type
    {
    };

    template <typename T>
    constexpr auto is_const_pointer_v = is_const_pointer<T>::value;

    template <typename... Args>
    struct uses_result
    {
        static_assert(
            sizeof...(Args) >= 2,
            "All runtime functions take at least two arguments");

        static constexpr auto value = !is_const_pointer_v<nth_t<1, Args...>>;
    };

    template <typename... Args>
    constexpr auto uses_result_v = uses_result<Args...>::value;

    template <typename... Args>
    struct uses_base_gas
    {
        static constexpr auto value =
            std::is_same_v<last_t<Args...>, std::int64_t>;
    };

    template <typename... Args>
    constexpr auto uses_base_gas_v = uses_base_gas<Args...>::value;
}

namespace monad::compiler::test
{
    class RuntimeTest : public testing::Test
    {
    protected:
        evmc::MockedHost host_;

        runtime::Context ctx_ = {
            .host = &host_.get_interface(),
            .context = host_.to_context(),
            .gas_remaining = std::numeric_limits<std::int64_t>::max(),
            .gas_refund = 0,
            .env =
                {
                    .evmc_flags = 0,
                    .depth = 0,
                    .recipient = evmc::address{},
                    .sender = evmc::address{},
                    .value = {},
                    .create2_salt = {},
                    .input_data = {},
                    .code = {},
                    .return_data = {},
                },
        };

        /**
         * This function performs some slightly gnarly metaprogramming to make
         * it easier for us to write unit tests for the runtime library.
         *
         * The runtime library functions are designed to take pointer arguments
         * so that the compiler can directly call them from the code generator.
         * However, this makes it irritating to unit test them, as we need to
         * pass the pointers ourselves by hand.
         *
         * This function performs a generic version of what we'd have to do by
         * hand; it takes a pack of arguments that can be converted to uint256_t
         * objects, and creates an array of the corresponding uint256_t objects
         * on the stack, which can then be passed to the runtime.
         */
        template <typename... FnArgs, typename... Args>
        auto call(void (*f)(FnArgs...), Args &&...args)
            -> std::conditional_t<
                detail::uses_result_v<FnArgs...>, utils::uint256_t, void>
        {
            constexpr auto use_result = detail::uses_result_v<FnArgs...>;
            constexpr auto use_base_gas = detail::uses_base_gas_v<FnArgs...>;

            auto result = utils::uint256_t{};

            auto uint_args =
                std::array{utils::uint256_t(std::forward<Args>(args))...};

            auto arg_ptrs =
                std::array<utils::uint256_t const *, uint_args.size()>{};
            for (auto i = 0u; i < uint_args.size(); ++i) {
                arg_ptrs[i] = std::addressof(uint_args[i]);
            }

            auto word_args = [&] {
                if constexpr (use_result) {
                    return std::tuple_cat(
                        std::tuple(&ctx_, std::addressof(result)), arg_ptrs);
                }
                else {
                    return std::tuple_cat(std::tuple(&ctx_), arg_ptrs);
                }
            }();

            auto all_args = [&] {
                if constexpr (use_base_gas) {
                    return std::tuple_cat(
                        word_args, std::tuple(std::int64_t{0}));
                }
                else {
                    return word_args;
                }
            }();

            std::apply(f, all_args);

            if constexpr (use_result) {
                return result;
            }
            else {
                return;
            }
        }
    };
}
