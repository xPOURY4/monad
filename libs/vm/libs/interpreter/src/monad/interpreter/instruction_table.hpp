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
    consteval InstrTable make_instruction_table()
    {
        constexpr auto since = [](evmc_revision first, InstrEval impl) {
            return (Rev >= first) ? impl : error;
        };

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
            exp<Rev>, // 0x0A,
            signextend, // 0x0B,
            error, //
            error, //
            error, //
            error, //

            lt, // 0x10,
            gt, // 0x11,
            slt, // 0x12,
            sgt, // 0x13,
            eq, // 0x14,
            iszero, // 0x15,
            and_, // 0x16,
            or_, // 0x17,
            xor_, // 0x18,
            not_, // 0x19,
            byte, // 0x1A,
            since(EVMC_CONSTANTINOPLE, shl), // 0x1B,
            since(EVMC_CONSTANTINOPLE, shr), // 0x1C,
            since(EVMC_CONSTANTINOPLE, sar), // 0x1D,
            error, //
            error, //

            error, // 0x20,
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

            error, // 0x30,
            error, // 0x31,
            error, // 0x32,
            error, // 0x33,
            error, // 0x34,
            calldataload, // 0x35,
            error, // 0x36,
            error, // 0x37,
            error, // 0x38,
            error, // 0x39,
            error, // 0x3A,
            error, // 0x3B,
            error, // 0x3C,
            error, //
            error, //
            error, //

            error, // 0x40,
            error, // 0x41,
            error, // 0x42,
            error, // 0x43,
            error, // 0x44,
            error, // 0x45,
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

            pop, // 0x50,
            error, // 0x51,
            error, // 0x52,
            error, // 0x53,
            sload<Rev>, // 0x54,
            sstore<Rev>, // 0x55,
            jump, // 0x56,
            jumpi, // 0x57,
            error, // 0x58,
            error, // 0x59,
            error, // 0x5A,
            jumpdest, // 0x5B,
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

            dup<1>, // 0x80,
            dup<2>, // 0x81,
            dup<3>, // 0x82,
            dup<4>, // 0x83,
            dup<5>, // 0x84,
            dup<6>, // 0x85,
            dup<7>, // 0x86,
            dup<8>, // 0x87,
            dup<9>, // 0x88,
            dup<10>, // 0x89,
            dup<11>, // 0x8A,
            dup<12>, // 0x8B,
            dup<13>, // 0x8C,
            dup<14>, // 0x8D,
            dup<15>, // 0x8E,
            dup<16>, // 0x8F,

            swap<1>, // 0x90,
            swap<2>, // 0x91,
            swap<3>, // 0x92,
            swap<4>, // 0x93,
            swap<5>, // 0x94,
            swap<6>, // 0x95,
            swap<7>, // 0x96,
            swap<8>, // 0x97,
            swap<9>, // 0x98,
            swap<10>, // 0x99,
            swap<11>, // 0x9A,
            swap<12>, // 0x9B,
            swap<13>, // 0x9C,
            swap<14>, // 0x9D,
            swap<15>, // 0x9E,
            swap<16>, // 0x9F,

            error, // 0xA0,
            error, // 0xA1,
            error, // 0xA2,
            error, // 0xA3,
            error, // 0xA4,
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

            error, // 0xF0,
            call<Rev>, // 0xF1,
            error, // 0xF2,
            return_, // 0xF3,
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
            error // 0xFF,
        };
    }

    template <evmc_revision Rev>
    constexpr InstrTable instruction_table = make_instruction_table<Rev>();
}
