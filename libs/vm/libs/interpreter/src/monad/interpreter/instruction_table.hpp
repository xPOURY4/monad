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
    using InstrEval = std::int64_t (*)(
        runtime::Context &, State &, utils::uint256_t const *, std::int64_t);
    using InstrTable = std::array<InstrEval, 256>;

    template <evmc_revision Rev>
    consteval InstrTable make_instruction_table()
    {
        constexpr auto since = [](evmc_revision first, InstrEval impl) {
            return (Rev >= first) ? impl : invalid;
        };

        return {
            stop, // 0x00
            add<Rev>, // 0x01
            mul<Rev>, // 0x02
            sub<Rev>, // 0x03
            udiv<Rev>, // 0x04,
            sdiv<Rev>, // 0x05,
            umod<Rev>, // 0x06,
            smod<Rev>, // 0x07,
            addmod<Rev>, // 0x08,
            mulmod<Rev>, // 0x09,
            exp<Rev>, // 0x0A,
            signextend<Rev>, // 0x0B,
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            lt<Rev>, // 0x10,
            gt<Rev>, // 0x11,
            slt<Rev>, // 0x12,
            sgt<Rev>, // 0x13,
            eq<Rev>, // 0x14,
            iszero<Rev>, // 0x15,
            and_<Rev>, // 0x16,
            or_<Rev>, // 0x17,
            xor_<Rev>, // 0x18,
            not_<Rev>, // 0x19,
            byte<Rev>, // 0x1A,
            since(EVMC_CONSTANTINOPLE, shl<Rev>), // 0x1B,
            since(EVMC_CONSTANTINOPLE, shr<Rev>), // 0x1C,
            since(EVMC_CONSTANTINOPLE, sar<Rev>), // 0x1D,
            invalid, //
            invalid, //

            sha3<Rev>, // 0x20,
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

            address<Rev>, // 0x30,
            balance<Rev>, // 0x31,
            origin<Rev>, // 0x32,
            caller<Rev>, // 0x33,
            callvalue<Rev>, // 0x34,
            calldataload<Rev>, // 0x35,
            calldatasize<Rev>, // 0x36,
            calldatacopy<Rev>, // 0x37,
            codesize<Rev>, // 0x38,
            codecopy<Rev>, // 0x39,
            gasprice<Rev>, // 0x3A,
            extcodesize<Rev>, // 0x3B,
            extcodecopy<Rev>, // 0x3C,
            since(EVMC_BYZANTIUM, returndatasize<Rev>), // 0x3D,
            since(EVMC_BYZANTIUM, returndatacopy<Rev>), // 0x3E,
            since(EVMC_CONSTANTINOPLE, extcodehash<Rev>), // 0x3F,

            blockhash<Rev>, // 0x40,
            coinbase<Rev>, // 0x41,
            timestamp<Rev>, // 0x42,
            number<Rev>, // 0x43,
            prevrandao<Rev>, // 0x44,
            gaslimit<Rev>, // 0x45,
            since(EVMC_ISTANBUL, chainid<Rev>), // 0x46,
            since(EVMC_ISTANBUL, selfbalance<Rev>), // 0x47,
            since(EVMC_LONDON, basefee<Rev>), // 0x48,
            since(EVMC_CANCUN, blobhash<Rev>), // 0x49,
            since(EVMC_CANCUN, blobbasefee<Rev>), // 0x4A,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            pop<Rev>, // 0x50,
            mload<Rev>, // 0x51,
            mstore<Rev>, // 0x52,
            mstore8<Rev>, // 0x53,
            sload<Rev>, // 0x54,
            sstore<Rev>, // 0x55,
            jump<Rev>, // 0x56,
            jumpi<Rev>, // 0x57,
            pc<Rev>, // 0x58,
            msize<Rev>, // 0x59,
            gas<Rev>, // 0x5A,
            jumpdest<Rev>, // 0x5B,
            since(EVMC_CANCUN, tload<Rev>), // 0x5C,
            since(EVMC_CANCUN, tstore<Rev>), // 0x5D,
            since(EVMC_CANCUN, mcopy<Rev>), // 0x5E,
            since(EVMC_SHANGHAI, push<0, Rev>), // 0x5F,

            push<1, Rev>, // 0x60,
            push<2, Rev>, // 0x61,
            push<3, Rev>, // 0x62,
            push<4, Rev>, // 0x63,
            push<5, Rev>, // 0x64,
            push<6, Rev>, // 0x65,
            push<7, Rev>, // 0x66,
            push<8, Rev>, // 0x67,
            push<9, Rev>, // 0x68,
            push<10, Rev>, // 0x69,
            push<11, Rev>, // 0x6A,
            push<12, Rev>, // 0x6B,
            push<13, Rev>, // 0x6C,
            push<14, Rev>, // 0x6D,
            push<15, Rev>, // 0x6E,
            push<16, Rev>, // 0x6F,

            push<17, Rev>, // 0x70,
            push<18, Rev>, // 0x71,
            push<19, Rev>, // 0x72,
            push<20, Rev>, // 0x73,
            push<21, Rev>, // 0x74,
            push<22, Rev>, // 0x75,
            push<23, Rev>, // 0x76,
            push<24, Rev>, // 0x77,
            push<25, Rev>, // 0x78,
            push<26, Rev>, // 0x79,
            push<27, Rev>, // 0x7A,
            push<28, Rev>, // 0x7B,
            push<29, Rev>, // 0x7C,
            push<30, Rev>, // 0x7D,
            push<31, Rev>, // 0x7E,
            push<32, Rev>, // 0x7F,

            dup<1, Rev>, // 0x80,
            dup<2, Rev>, // 0x81,
            dup<3, Rev>, // 0x82,
            dup<4, Rev>, // 0x83,
            dup<5, Rev>, // 0x84,
            dup<6, Rev>, // 0x85,
            dup<7, Rev>, // 0x86,
            dup<8, Rev>, // 0x87,
            dup<9, Rev>, // 0x88,
            dup<10, Rev>, // 0x89,
            dup<11, Rev>, // 0x8A,
            dup<12, Rev>, // 0x8B,
            dup<13, Rev>, // 0x8C,
            dup<14, Rev>, // 0x8D,
            dup<15, Rev>, // 0x8E,
            dup<16, Rev>, // 0x8F,

            swap<1, Rev>, // 0x90,
            swap<2, Rev>, // 0x91,
            swap<3, Rev>, // 0x92,
            swap<4, Rev>, // 0x93,
            swap<5, Rev>, // 0x94,
            swap<6, Rev>, // 0x95,
            swap<7, Rev>, // 0x96,
            swap<8, Rev>, // 0x97,
            swap<9, Rev>, // 0x98,
            swap<10, Rev>, // 0x99,
            swap<11, Rev>, // 0x9A,
            swap<12, Rev>, // 0x9B,
            swap<13, Rev>, // 0x9C,
            swap<14, Rev>, // 0x9D,
            swap<15, Rev>, // 0x9E,
            swap<16, Rev>, // 0x9F,

            log<0, Rev>, // 0xA0,
            log<1, Rev>, // 0xA1,
            log<2, Rev>, // 0xA2,
            log<3, Rev>, // 0xA3,
            log<4, Rev>, // 0xA4,
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
            return_<Rev>, // 0xF3,
            since(EVMC_HOMESTEAD, delegatecall<Rev>), // 0xF4,
            since(EVMC_CONSTANTINOPLE, create2<Rev>), // 0xF5,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            since(EVMC_BYZANTIUM, staticcall<Rev>), // 0xFA,
            invalid, //
            invalid, //
            since(EVMC_BYZANTIUM, revert<Rev>), // 0xFD,
            invalid, // 0xFE,
            selfdestruct<Rev>, // 0xFF,
        };
    }

    template <evmc_revision Rev>
    constexpr InstrTable instruction_table = make_instruction_table<Rev>();
}
