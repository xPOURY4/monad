#include <monad/vm/evm/opcodes.hpp>
#include <monad/vm/interpreter/intercode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

using namespace monad::vm::compiler;

namespace monad::vm::interpreter
{
    Intercode::Intercode(std::span<std::uint8_t const> const code)
        : padded_code_(pad(code))
        , code_size_(code.size())
        , jumpdest_map_(find_jumpdests(code))
    {
    }

    std::uint8_t const *Intercode::code() const noexcept
    {
        return padded_code_.get() + start_padding_size;
    }

    std::size_t Intercode::code_size() const noexcept
    {
        return code_size_;
    }

    bool Intercode::is_jumpdest(std::size_t const pc) const noexcept
    {
        return pc < code_size_ && jumpdest_map_[pc];
    }

    std::unique_ptr<std::uint8_t[]>
    Intercode::pad(std::span<std::uint8_t const> const code)
    {
        auto buffer = std::make_unique_for_overwrite<std::uint8_t[]>(
            start_padding_size + code.size() + end_padding_size);

        std::fill_n(&buffer[0], start_padding_size, 0);
        std::copy(code.begin(), code.end(), &buffer[start_padding_size]);
        std::fill_n(
            &buffer[code.size() + start_padding_size], end_padding_size, 0);

        return buffer;
    }

    auto Intercode::find_jumpdests(std::span<std::uint8_t const> const code)
        -> JumpdestMap
    {
        auto jumpdests = JumpdestMap(code.size(), false);

        for (auto i = 0u; i < code.size(); ++i) {
            auto const op = code[i];

            if (op == EvmOpCode::JUMPDEST) {
                jumpdests[i] = true;
            }

            if (is_push_opcode(op)) {
                i += get_push_opcode_index(op);
            }
        }

        return jumpdests;
    }
}
