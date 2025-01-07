#pragma once

#include <compiler/ir/x86.h>
#include <utils/assert.h>
#include <vm/vm.h>

#include <evmc/evmc.hpp>

#include <evmone/evmone.h>

#include <unordered_map>
#include <utility>

using CompiledContractId = std::pair<evmc_revision, evmc::bytes32>;

struct CompiledContractHash
{
    static size_t operator()(CompiledContractId const &p);
};

struct CompiledContractEqual
{
    static bool
    operator()(CompiledContractId const &p, CompiledContractId const &q);
};

using CompiledContractsMap = std::unordered_map<
    CompiledContractId, monad::compiler::native::entrypoint_t,
    CompiledContractHash, CompiledContractEqual>;

class BlockchainTestVM : public evmc_vm
{
public:
    BlockchainTestVM();

    evmc_result execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);

    evmc_capabilities_flagset get_capabilities() const;

private:
    evmc::VM evmone_vm_;
    monad::compiler::VM monad_vm_;
    CompiledContractsMap compiled_contracts_;
    char const *debug_dir_;
    bool only_evmone_;
};
