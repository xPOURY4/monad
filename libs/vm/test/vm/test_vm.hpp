#pragma once

#include "test_state.hpp"

#include <monad/vm/compiler/ir/x86.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/vm.hpp>

#include <evmc/evmc.hpp>

#include <evmone/baseline.hpp>
#include <evmone/vm.hpp>

#include <unordered_map>

class BlockchainTestVM : public evmc_vm
{
public:
    enum class Implementation
    {
        Compiler,
        Interpreter,
        Evmone,
    };

    template <typename V>
    using CodeMap = std::unordered_map<
        evmc::bytes32, V, monad::vm::utils::Hash32Hash,
        monad::vm::utils::Bytes32Equal>;

    BlockchainTestVM(
        Implementation impl,
        monad::vm::compiler::native::EmitterHook post_instruction_emit_hook =
            nullptr);

    evmc::Result execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);

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

    void
    precompile_contracts(evmc_revision rev, evmone::state::State const &state);

private:
    Implementation impl_;
    evmone::VM evmone_vm_;
    monad::vm::VM monad_vm_;
    char const *debug_dir_;
    monad::vm::CompilerConfig base_config;
    CodeMap<evmone::baseline::CodeAnalysis> code_analyses_;
    CodeMap<monad::vm::SharedIntercode> intercodes_;

    evmone::baseline::CodeAnalysis const &get_code_analysis(
        evmc::bytes32 const &code_hash, uint8_t const *code, size_t code_size);

    monad::vm::SharedIntercode const &get_intercode(
        evmc::bytes32 const &code_hash, uint8_t const *code, size_t code_size);

    std::pair<
        monad::vm::SharedIntercode const &,
        monad::vm::SharedNativecode const> const
    get_intercode_nativecode(
        evmc_revision const rev, evmc::bytes32 const &code_hash,
        uint8_t const *code, size_t code_size);

    evmc::Result execute_evmone(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);

    evmc::Result execute_compiler(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);

    evmc::Result execute_interpreter(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size);
};
