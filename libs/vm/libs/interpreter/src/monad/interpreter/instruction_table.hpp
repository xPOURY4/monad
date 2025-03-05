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
            return (Rev >= first) ? impl : invalid;
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
            invalid, //
            invalid, //
            invalid, //
            invalid, //

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
            invalid, //
            invalid, //

            sha3, // 0x20,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            address, // 0x30,
            balance<Rev>, // 0x31,
            origin, // 0x32,
            caller, // 0x33,
            callvalue, // 0x34,
            calldataload, // 0x35,
            calldatasize, // 0x36,
            calldatacopy, // 0x37,
            codesize, // 0x38,
            codecopy, // 0x39,
            gasprice, // 0x3A,
            extcodesize<Rev>, // 0x3B,
            extcodecopy<Rev>, // 0x3C,
            since(EVMC_BYZANTIUM, returndatasize), // 0x3D,
            since(EVMC_BYZANTIUM, returndatacopy), // 0x3E,
            since(EVMC_CONSTANTINOPLE, extcodehash<Rev>), // 0x3F,

            blockhash, // 0x40,
            coinbase, // 0x41,
            timestamp, // 0x42,
            number, // 0x43,
            prevrandao, // 0x44,
            gaslimit, // 0x45,
            since(EVMC_ISTANBUL, chainid), // 0x46,
            since(EVMC_ISTANBUL, selfbalance), // 0x47,
            since(EVMC_LONDON, basefee), // 0x48,
            since(EVMC_CANCUN, blobhash), // 0x49,
            since(EVMC_CANCUN, blobbasefee), // 0x4A,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            pop, // 0x50,
            mload, // 0x51,
            mstore, // 0x52,
            mstore8, // 0x53,
            sload<Rev>, // 0x54,
            sstore<Rev>, // 0x55,
            jump, // 0x56,
            jumpi, // 0x57,
            pc, // 0x58,
            msize, // 0x59,
            gas, // 0x5A,
            jumpdest, // 0x5B,
            since(EVMC_CANCUN, tload), // 0x5C,
            since(EVMC_CANCUN, tstore), // 0x5D,
            since(EVMC_CANCUN, mcopy), // 0x5E,
            since(EVMC_SHANGHAI, push<0>), // 0x5F,

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

            log<0>, // 0xA0,
            log<1>, // 0xA1,
            log<2>, // 0xA2,
            log<3>, // 0xA3,
            log<4>, // 0xA4,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            create<Rev>, // 0xF0,
            call<Rev>, // 0xF1,
            callcode<Rev>, // 0xF2,
            return_, // 0xF3,
            since(EVMC_HOMESTEAD, delegatecall<Rev>), // 0xF4,
            since(EVMC_CONSTANTINOPLE, create2<Rev>), // 0xF5,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            since(EVMC_BYZANTIUM, staticcall<Rev>), // 0xFA,
            invalid, //
            invalid, //
            since(EVMC_BYZANTIUM, revert), // 0xFD,
            invalid, // 0xFE,
            selfdestruct<Rev>, // 0xFF,
        };
    }

    template <evmc_revision Rev>
    constexpr InstrTable instruction_table = make_instruction_table<Rev>();
}
