#pragma once

#include <monad/evm/opcodes.hpp>
#include <monad/interpreter/instructions.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/types.hpp>

#include <evmc/evmc.h>

#include <array>
#include <cstdint>

namespace monad::interpreter
{
    using InstrEval = void (*)(runtime::Context &, State &);
    using InstrTable = std::array<InstrEval, 256>;

    template <evmc_revision Rev>
    consteval InstrTable make_instruction_table() = delete;

    template <evmc_revision Rev>
    constexpr InstrTable instruction_table = make_instruction_table<Rev>();

    template <>
    consteval InstrTable make_instruction_table<EVMC_FRONTIER>()
    {
        return {
            stop, // 0x00
            add, // 0x01
            mul, // 0x02
            sub, // 0x03
            udiv, // 0x04,
            sdiv, // 0x05,
            umod, // 0x06,
            smod, // 0x07,
            addmod, // 0x08,
            mulmod, // 0x09,
            exp<EVMC_FRONTIER>, // 0x0A,
            stop, // 0x0B,
            error, //
            error, //
            error, //
            error, //

            stop, // 0x10,
            stop, // 0x11,
            stop, // 0x12,
            stop, // 0x13,
            stop, // 0x14,
            stop, // 0x15,
            stop, // 0x16,
            stop, // 0x17,
            stop, // 0x18,
            stop, // 0x19,
            stop, // 0x1A,
            error, //
            error, //
            error, //
            error, //
            error, //

            stop, // 0x20,
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //

            stop, // 0x30,
            stop, // 0x31,
            stop, // 0x32,
            stop, // 0x33,
            stop, // 0x34,
            calldataload, // 0x35,
            stop, // 0x36,
            stop, // 0x37,
            stop, // 0x38,
            stop, // 0x39,
            stop, // 0x3A,
            stop, // 0x3B,
            stop, // 0x3C,
            error, //
            error, //
            error, //

            stop, // 0x40,
            stop, // 0x41,
            stop, // 0x42,
            stop, // 0x43,
            stop, // 0x44,
            stop, // 0x45,
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //

            stop, // 0x50,
            stop, // 0x51,
            stop, // 0x52,
            stop, // 0x53,
            stop, // 0x54,
            sstore<EVMC_FRONTIER>, // 0x55,
            stop, // 0x56,
            stop, // 0x57,
            stop, // 0x58,
            stop, // 0x59,
            stop, // 0x5A,
            stop, // 0x5B,
            error, //
            error, //
            error, //
            error, //

            push<1>, // 0x60,
            push<2>, // 0x61,
            push<3>, // 0x62,
            push<4>, // 0x63,
            push<5>, // 0x64,
            push<6>, // 0x65,
            push<7>, // 0x66,
            push<8>, // 0x67,
            push<9>, // 0x68,
            push<10>, // 0x69,
            push<11>, // 0x6A,
            push<12>, // 0x6B,
            push<13>, // 0x6C,
            push<14>, // 0x6D,
            push<15>, // 0x6E,
            push<16>, // 0x6F,

            push<17>, // 0x70,
            push<18>, // 0x71,
            push<19>, // 0x72,
            push<20>, // 0x73,
            push<21>, // 0x74,
            push<22>, // 0x75,
            push<23>, // 0x76,
            push<24>, // 0x77,
            push<25>, // 0x78,
            push<26>, // 0x79,
            push<27>, // 0x7A,
            push<28>, // 0x7B,
            push<29>, // 0x7C,
            push<30>, // 0x7D,
            push<31>, // 0x7E,
            push<32>, // 0x7F,

            stop, // 0x80,
            stop, // 0x81,
            stop, // 0x82,
            stop, // 0x83,
            stop, // 0x84,
            stop, // 0x85,
            stop, // 0x86,
            stop, // 0x87,
            stop, // 0x88,
            stop, // 0x89,
            stop, // 0x8A,
            stop, // 0x8B,
            stop, // 0x8C,
            stop, // 0x8D,
            stop, // 0x8E,
            stop, // 0x8F,

            stop, // 0x90,
            stop, // 0x91,
            stop, // 0x92,
            stop, // 0x93,
            stop, // 0x94,
            stop, // 0x95,
            stop, // 0x96,
            stop, // 0x97,
            stop, // 0x98,
            stop, // 0x99,
            stop, // 0x9A,
            stop, // 0x9B,
            stop, // 0x9C,
            stop, // 0x9D,
            stop, // 0x9E,
            stop, // 0x9F,

            stop, // 0xA0,
            stop, // 0xA1,
            stop, // 0xA2,
            stop, // 0xA3,
            stop, // 0xA4,
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //

            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //

            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //

            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //

            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //

            stop, // 0xF0,
            call<EVMC_FRONTIER>, // 0xF1,
            stop, // 0xF2,
            stop, // 0xF3,
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            error, //
            stop // 0xFF,
        };
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_HOMESTEAD>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_HOMESTEAD)>;
        table[EXP] = exp<EVMC_HOMESTEAD>;
        table[SSTORE] = sstore<EVMC_HOMESTEAD>;
        table[CALL] = call<EVMC_HOMESTEAD>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_TANGERINE_WHISTLE>()
    {
        using namespace monad::compiler;

        auto table =
            instruction_table<previous_evm_revision(EVMC_TANGERINE_WHISTLE)>;
        table[EXP] = exp<EVMC_TANGERINE_WHISTLE>;
        table[SSTORE] = sstore<EVMC_TANGERINE_WHISTLE>;
        table[CALL] = call<EVMC_TANGERINE_WHISTLE>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_SPURIOUS_DRAGON>()
    {
        using namespace monad::compiler;

        auto table =
            instruction_table<previous_evm_revision(EVMC_SPURIOUS_DRAGON)>;
        table[EXP] = exp<EVMC_SPURIOUS_DRAGON>;
        table[SSTORE] = sstore<EVMC_SPURIOUS_DRAGON>;
        table[CALL] = call<EVMC_SPURIOUS_DRAGON>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_BYZANTIUM>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_BYZANTIUM)>;
        table[EXP] = exp<EVMC_BYZANTIUM>;
        table[SSTORE] = sstore<EVMC_BYZANTIUM>;
        table[CALL] = call<EVMC_BYZANTIUM>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_CONSTANTINOPLE>()
    {
        using namespace monad::compiler;

        auto table =
            instruction_table<previous_evm_revision(EVMC_CONSTANTINOPLE)>;
        table[EXP] = exp<EVMC_CONSTANTINOPLE>;
        table[SSTORE] = sstore<EVMC_CONSTANTINOPLE>;
        table[CALL] = call<EVMC_CONSTANTINOPLE>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_PETERSBURG>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_PETERSBURG)>;
        table[EXP] = exp<EVMC_PETERSBURG>;
        table[SSTORE] = sstore<EVMC_PETERSBURG>;
        table[CALL] = call<EVMC_PETERSBURG>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_ISTANBUL>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_ISTANBUL)>;
        table[EXP] = exp<EVMC_ISTANBUL>;
        table[SSTORE] = sstore<EVMC_ISTANBUL>;
        table[CALL] = call<EVMC_ISTANBUL>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_BERLIN>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_BERLIN)>;
        table[EXP] = exp<EVMC_BERLIN>;
        table[SSTORE] = sstore<EVMC_BERLIN>;
        table[CALL] = call<EVMC_BERLIN>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_LONDON>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_LONDON)>;
        table[EXP] = exp<EVMC_LONDON>;
        table[SSTORE] = sstore<EVMC_LONDON>;
        table[CALL] = call<EVMC_LONDON>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_PARIS>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_PARIS)>;
        table[EXP] = exp<EVMC_PARIS>;
        table[SSTORE] = sstore<EVMC_PARIS>;
        table[CALL] = call<EVMC_PARIS>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_SHANGHAI>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_SHANGHAI)>;
        table[EXP] = exp<EVMC_SHANGHAI>;
        table[SSTORE] = sstore<EVMC_SHANGHAI>;
        table[CALL] = call<EVMC_SHANGHAI>;

        return table;
    }

    template <>
    consteval InstrTable make_instruction_table<EVMC_CANCUN>()
    {
        using namespace monad::compiler;

        auto table = instruction_table<previous_evm_revision(EVMC_CANCUN)>;
        table[EXP] = exp<EVMC_CANCUN>;
        table[SSTORE] = sstore<EVMC_CANCUN>;
        table[CALL] = call<EVMC_CANCUN>;

        return table;
    }
}
