#pragma once

#include <monad/vm/core/assert.h>
#include <monad/vm/evm/opcodes.hpp>
#include <monad/vm/utils/cases.hpp>
#include <monad/vm/utils/evm-as/builder.hpp>
#include <monad/vm/utils/evm-as/instruction.hpp>
#include <monad/vm/utils/evm-as/resolver.hpp>
#include <monad/vm/utils/evm-as/utils.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <evmc/evmc.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace monad::vm::utils::evm_as
{
    namespace mc = monad::vm::compiler;

    //
    // Generic bytecode compiler
    //
    template <evmc_revision Rev>
    void compile(
        EvmBuilder<Rev> const &eb, std::invocable<uint8_t const> auto emit_byte)
    {
        auto const label_offsets = resolve_labels<Rev>(eb);
        for (auto const &ins : eb) {
            std::array<uint8_t, sizeof(uint256_t)> imm_bytes{};
            if (Instruction::is_comment(ins)) {
                continue;
            }
            std::visit(
                Cases{
                    [&](PlainI const &plain) -> void {
                        emit_byte(plain.opcode);
                    },
                    [&](PushI const &push) -> void {
                        emit_byte(push.opcode);
                        // SAFETY: The buffer `imm_bytes` is
                        // large enough to hold an uint256_t.
                        push.imm.store_be(imm_bytes.data());
                        auto const n = push.n();
                        for (size_t i = 0; i < n; i++) {
                            emit_byte(imm_bytes[32 - n + i]);
                        }
                    },
                    [&](PushLabelI const &push) -> void {
                        auto const it = label_offsets.find(push.label);
                        if (it == label_offsets.end()) {
                            // Undefined label
                            emit_byte(0xFE);
                            return;
                        }

                        size_t offset = it->second;
                        size_t n = offset == 0 ? offset : byte_width(offset);
                        emit_byte(
                            mc::EvmOpCode::PUSH0 + static_cast<uint8_t>(n));
                        // Note: assumes we are executing on a
                        // little endian machine.
                        for (size_t i = 0; i < n; i++) {
                            emit_byte((offset >> ((n - i - 1) * 8)) & 0xFF);
                        }
                    },
                    [&](JumpdestI const &) -> void {
                        emit_byte(mc::EvmOpCode::JUMPDEST);
                    },
                    [&](InvalidI const &) -> void { emit_byte(0xFE); },
                    [&](auto const &) -> void { MONAD_VM_ASSERT(false); }},
                ins);
        }
    }

    // Assembles and emits the corresponding byte code of the provided
    // builder object to the provided `bytecode` vector.
    template <evmc_revision Rev>
    inline void
    compile(EvmBuilder<Rev> const &eb, std::vector<uint8_t> &bytecode)
    {
        bytecode.reserve(eb.size() /* optimistic estimate */);
        compile<Rev>(
            eb, [&](uint8_t const byte) -> void { bytecode.push_back(byte); });
    }

    // Assembles and emits the corresponding byte code of the provided
    // builder object to the provided `os` out stream.
    template <evmc_revision Rev>
    inline void compile(EvmBuilder<Rev> const &eb, std::ostream &os)
    {
        compile<Rev>(eb, [&](uint8_t const byte) -> void {
            os << static_cast<std::ostream::char_type>(byte);
        });
    }

    // Assembles and emits the corresponding byte code of the provided
    // builder object in the returned string.
    template <evmc_revision Rev>
    inline std::string compile(EvmBuilder<Rev> const &eb)
    {
        std::stringstream ss{};
        compile<Rev>(eb, ss);
        return ss.str();
    }

    //
    // Mnemonic compiler
    //
    template <evmc_revision Rev>
    inline void mcompile(EvmBuilder<Rev> const &eb, std::ostream &os)
    {
        for (auto const &ins : eb) {
            std::visit(
                Cases{
                    [&](PlainI const &plain) -> void {
                        auto const info = mc::opcode_table<Rev>[plain.opcode];
                        os << info.name << std::endl;
                    },
                    [&](PushI const &push) -> void {
                        auto const info = mc::opcode_table<Rev>[push.opcode];
                        std::string imm_str = push.imm.to_string(16);
                        std::transform(
                            imm_str.begin(),
                            imm_str.end(),
                            imm_str.begin(),
                            ::toupper);
                        os << info.name << " 0x" << imm_str << std::endl;
                    },
                    [&](PushLabelI const &push) -> void {
                        os << "PUSH " << push.label << std::endl;
                    },
                    [&](JumpdestI const &jumpdest) -> void {
                        os << "JUMPDEST " << jumpdest.label << std::endl;
                    },
                    [&](InvalidI const &) -> void {
                        os << "INVALID" << std::endl;
                    },
                    [&](CommentI const &comment) -> void {
                        if (comment.msg.empty()) {
                            os << "//" << std::endl;
                            return;
                        }
                        std::stringstream ss(comment.msg);
                        std::string msg;
                        while (std::getline(ss, msg, '\n')) {
                            os << "// " << msg << std::endl;
                        }
                    }},
                ins);
        }
    }

    // Returns a mnemonic representation of the provided builder object
    // as a string; convenient for testing.
    template <evmc_revision Rev>
    inline std::string mcompile(EvmBuilder<Rev> const &eb)
    {
        std::stringstream ss{};
        mcompile(eb, ss);
        return ss.str();
    }
}
