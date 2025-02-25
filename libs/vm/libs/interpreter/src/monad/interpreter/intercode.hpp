#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace monad::interpreter
{
    class Intercode
    {
    public:
        using JumpdestMap = std::vector<bool>;

        Intercode(std::span<std::uint8_t const> const);

        std::uint8_t const *code() const noexcept;
        std::size_t code_size() const noexcept;

        bool is_jumpdest(std::size_t const) const noexcept;

    private:
        std::unique_ptr<std::uint8_t[]> padded_code_;
        std::size_t code_size_;
        JumpdestMap jumpdest_map_;
    };
}
