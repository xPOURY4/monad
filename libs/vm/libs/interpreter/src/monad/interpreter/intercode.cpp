#include <monad/evm/opcodes.hpp>
#include <monad/interpreter/intercode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

using namespace monad::compiler;

namespace monad::interpreter
{
    namespace
    {
        std::unique_ptr<std::uint8_t[]>
        pad(std::span<std::uint8_t const> const code)
        {
            // 32 for a truncated PUSH32, 1 for a STOP so that we don't have to
            // worry about going off the end.
            constexpr std::size_t padding_size = 32 + 1;

            auto buffer = std::make_unique_for_overwrite<std::uint8_t[]>(
                code.size() + padding_size);

            std::copy(code.begin(), code.end(), &buffer[0]);
            std::fill_n(&buffer[code.size()], padding_size, 0);

            return buffer;
        }

        Intercode::JumpdestMap
        find_jumpdests(std::span<std::uint8_t const> const code)
        {
            auto jumpdests = Intercode::JumpdestMap(code.size(), false);

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

    Intercode::Intercode(std::span<std::uint8_t const> const code)
        : padded_code_(pad(code))
        , code_size_(code.size())
        , jumpdest_map_(find_jumpdests(code))
    {
    }

    std::uint8_t const *Intercode::code() const noexcept
    {
        return padded_code_.get();
    }

    std::size_t Intercode::code_size() const noexcept
    {
        return code_size_;
    }

    bool Intercode::is_jumpdest(std::size_t const pc) const noexcept
    {
        return pc < code_size_ && jumpdest_map_[pc];
    }
}
