#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace monad::vm::interpreter
{
    class Intercode
    {
    public:
        using JumpdestMap = std::vector<bool>;

        Intercode(std::span<std::uint8_t const> const);

        Intercode(std::uint8_t const *code, std::size_t code_size)
            : Intercode{std::span<std::uint8_t const>{code, code_size}}
        {
        }

        std::uint8_t const *code() const noexcept;
        std::size_t code_size() const noexcept;

        bool is_jumpdest(std::size_t const) const noexcept;

    private:
        std::unique_ptr<std::uint8_t[]> padded_code_;
        std::size_t code_size_;
        JumpdestMap jumpdest_map_;
    };
}
