#include <monad/vm/interpreter/types.hpp>
#include <monad/vm/runtime/types.hpp>

#include <evmc/evmc.h>

#include <cstdint>

namespace monad::vm::interpreter
{
    // Arithmetic
    template <evmc_revision Rev>
    void
    add(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    mul(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    sub(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void udiv(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void sdiv(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void umod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void smod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void addmod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void mulmod(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    exp(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void signextend(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Boolean
    template <evmc_revision Rev>
    void
    lt(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    gt(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    slt(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    sgt(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    eq(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void iszero(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Bitwise
    template <evmc_revision Rev>
    void and_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    or_(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void xor_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void not_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void byte(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    shl(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    shr(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    sar(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Data
    template <evmc_revision Rev>
    void sha3(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void address(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void balance(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void origin(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void caller(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void callvalue(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void calldataload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void calldatasize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void calldatacopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void codesize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void codecopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void gasprice(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void extcodesize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void extcodecopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void returndatasize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void returndatacopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void extcodehash(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void blockhash(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void coinbase(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void timestamp(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void number(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void prevrandao(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void gaslimit(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void chainid(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void selfbalance(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void basefee(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void blobhash(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void blobbasefee(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Memory & Storage
    template <evmc_revision Rev>
    void mload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void mstore(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void mstore8(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void mcopy(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void sstore(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void sload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void tstore(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void tload(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Execution Intercode
    template <evmc_revision Rev>
    void
    pc(runtime::Context &, Intercode const &, utils::uint256_t const *,
       utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void msize(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    gas(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Stack
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 32)
    void push(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void
    pop(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    void
    dup(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    void swap(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void jump(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void jumpi(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void jumpdest(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Logging
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 4)
    void
    log(runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // Call & Create
    template <evmc_revision Rev>
    void create(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void call(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void callcode(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void delegatecall(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void create2(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void staticcall(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    // VM Control
    template <evmc_revision Rev>
    void return_(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void revert(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    template <evmc_revision Rev>
    void selfdestruct(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    inline void stop(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    inline void invalid(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);
}
