#include <monad/vm/interpreter/types.hpp>
#include <monad/vm/runtime/types.hpp>

#include <evmc/evmc.h>

#include <cstdint>

namespace monad::vm::interpreter
{
    // Arithmetic
    template <evmc_revision Rev>
    OpcodeResult
    add(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    mul(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    sub(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult udiv(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult sdiv(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult umod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult smod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult addmod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult mulmod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    exp(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult signextend(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Boolean
    template <evmc_revision Rev>
    OpcodeResult
    lt(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    gt(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    slt(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    sgt(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    eq(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult iszero(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Bitwise
    template <evmc_revision Rev>
    OpcodeResult and_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    or_(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult xor_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult not_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult byte(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    shl(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    shr(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    sar(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Data
    template <evmc_revision Rev>
    OpcodeResult sha3(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult address(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult balance(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult origin(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult caller(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult callvalue(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult calldataload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult calldatasize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult calldatacopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult codesize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult codecopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult gasprice(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult extcodesize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult extcodecopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult returndatasize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult returndatacopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult extcodehash(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult blockhash(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult coinbase(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult timestamp(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult number(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult prevrandao(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult gaslimit(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult chainid(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult selfbalance(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult basefee(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult blobhash(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult blobbasefee(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Memory & Storage
    template <evmc_revision Rev>
    OpcodeResult mload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult mstore(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult mstore8(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult mcopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult sstore(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult sload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult tstore(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult tload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Execution Intercode
    template <evmc_revision Rev>
    OpcodeResult
    pc(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult msize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    gas(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Stack
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 32)
    OpcodeResult push(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult
    pop(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    OpcodeResult
    dup(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    OpcodeResult swap(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult jump(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult jumpi(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult jumpdest(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Logging
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 4)
    OpcodeResult
    log(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Call & Create
    template <evmc_revision Rev>
    OpcodeResult create(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult call(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult callcode(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult delegatecall(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult create2(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult staticcall(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // VM Control
    template <evmc_revision Rev>
    OpcodeResult return_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult revert(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    OpcodeResult selfdestruct(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    inline OpcodeResult stop(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    inline OpcodeResult invalid(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);
}
