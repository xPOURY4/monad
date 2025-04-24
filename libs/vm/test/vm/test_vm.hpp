#pragma once

#include <monad/vm/compiler/ir/x86.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/vm.hpp>

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
    CompiledContractId, monad::vm::SharedNativecode, CompiledContractHash,
    CompiledContractEqual>;

class BlockchainTestVM : public evmc_vm
{
public:
    enum class Implementation
    {
        Compiler,
        Interpreter,
        Evmone,
    };

    BlockchainTestVM(
        Implementation impl,
        monad::vm::compiler::native::EmitterHook post_instruction_emit_hook =
            nullptr);

    evmc_result execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);

    evmc_capabilities_flagset get_capabilities() const;

    static constexpr std::string_view
    impl_name(BlockchainTestVM::Implementation const impl) noexcept
    {
        switch (impl) {
        case Implementation::Interpreter:
            return "interpreter";
        case Implementation::Compiler:
            return "compiler";
        case Implementation::Evmone:
            return "evmone";
        }

        std::unreachable();
    };

private:
    Implementation impl_;
    evmc::VM evmone_vm_;
    monad::vm::VM monad_vm_;
    CompiledContractsMap compiled_contracts_;
    char const *debug_dir_;
    monad::vm::compiler::native::CompilerConfig base_config;

    evmc_result execute_compiler(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);

    evmc_result execute_interpreter(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);
};
