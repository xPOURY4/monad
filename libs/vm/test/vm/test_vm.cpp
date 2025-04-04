#include "test_vm.hpp"

#include <monad/vm/compiler/ir/x86/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/interpreter/execute.hpp>
#include <monad/vm/vm.hpp>

#include <evmone/evmone.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <unordered_map>

using namespace monad;
using namespace monad::vm::compiler;

using namespace evmc::literals;

namespace fs = std::filesystem;

namespace
{
    constexpr auto SYSTEM_ADDRESS =
        0xfffffffffffffffffffffffffffffffffffffffe_address;

    void destroy(evmc_vm *vm)
    {
        delete reinterpret_cast<BlockchainTestVM *>(vm);
    }

    evmc_result execute(
        evmc_vm *vm, evmc_host_interface const *host,
        evmc_host_context *context, evmc_revision rev, evmc_message const *msg,
        uint8_t const *code, size_t code_size)
    {
        return reinterpret_cast<BlockchainTestVM *>(vm)->execute(
            host, context, rev, msg, code, code_size);
    }

    evmc_capabilities_flagset get_capabilities(evmc_vm *vm)
    {
        return reinterpret_cast<BlockchainTestVM *>(vm)->get_capabilities();
    }

    BlockchainTestVM::Implementation
    impl_from_env(BlockchainTestVM::Implementation const impl) noexcept
    {
        static auto *const evmone_vm_only_env =
            std::getenv("MONAD_COMPILER_EVMONE_ONLY");
        static bool const evmone_vm_only =
            evmone_vm_only_env && std::strcmp(evmone_vm_only_env, "1") == 0;
        if (evmone_vm_only) {
            return BlockchainTestVM::Implementation::Evmone;
        }
        return impl;
    }

    bool is_compiler_runtime_debug_trace_enabled()
    {
        static auto *const debug_trace_env =
            std::getenv("MONAD_COMPILER_DEBUG_TRACE");
        static bool const debug_trace =
            debug_trace_env && std::strcmp(debug_trace_env, "1") == 0;
        return debug_trace;
    }
}

size_t CompiledContractHash::operator()(CompiledContractId const &p)
{
    static_assert(sizeof(evmc::bytes32) == 32);

    auto [d, x] = p;
    uint64_t x0;
    std::memcpy(&x0, x.bytes, 8);
    uint64_t x1;
    std::memcpy(&x1, x.bytes + 8, 8);
    uint64_t x2;
    std::memcpy(&x2, x.bytes + 16, 8);
    uint64_t x3;
    std::memcpy(&x3, x.bytes + 24, 8);
    return static_cast<uint64_t>(d) ^ x0 ^ x1 ^ x2 ^ x3;
}

bool CompiledContractEqual::operator()(
    CompiledContractId const &p, CompiledContractId const &q)
{
    auto [d, x] = p;
    auto [e, y] = q;
    return d == e && std::memcmp(x.bytes, y.bytes, sizeof(x.bytes)) == 0;
}

BlockchainTestVM::BlockchainTestVM(
    Implementation impl, native::EmitterHook post_hook)
    : evmc_vm{EVMC_ABI_VERSION, "monad-compiler-blockchain-test-vm", "0.0.0", ::destroy, ::execute, ::get_capabilities, nullptr}
    , impl_{impl_from_env(impl)}
    , evmone_vm_{evmc_create_evmone()}
    , debug_dir_{std::getenv("MONAD_COMPILER_ASM_DIR")}
    , base_config{
          .runtime_debug_trace = is_compiler_runtime_debug_trace_enabled(),
          .post_instruction_emit_hook = post_hook}
{
    MONAD_VM_ASSERT(!debug_dir_ || fs::is_directory(debug_dir_));
}

evmc_result BlockchainTestVM::execute(
    evmc_host_interface const *host, evmc_host_context *context,
    evmc_revision rev, evmc_message const *msg, uint8_t const *code,
    size_t code_size)
{
    if (impl_ == Implementation::Evmone || msg->kind == EVMC_CREATE ||
        msg->kind == EVMC_CREATE2 || msg->sender == SYSTEM_ADDRESS) {
        auto *p = evmone_vm_.get_raw_pointer();
        return p->execute(p, host, context, rev, msg, code, code_size);
    }
    else if (impl_ == Implementation::Compiler) {
        return execute_compiler(host, context, rev, msg, code, code_size);
    }
    else {
        MONAD_VM_ASSERT(impl_ == Implementation::Interpreter);
        return execute_interpreter(host, context, rev, msg, code, code_size);
    }
}

evmc_result BlockchainTestVM::execute_compiler(
    evmc_host_interface const *host, evmc_host_context *context,
    evmc_revision rev, evmc_message const *msg, uint8_t const *code,
    size_t code_size)
{
    auto code_hash = host->get_code_hash(context, &msg->code_address);

    if (auto it = compiled_contracts_.find({rev, code_hash});
        it != compiled_contracts_.end()) {
        return monad_vm_.execute(
            it->second, host, context, msg, code, code_size);
    }

    std::optional<native::entrypoint_t> f;
    if (debug_dir_) {
        std::ostringstream file(std::ostringstream::ate);
        file.str(debug_dir_);
        file << '/';
        for (auto b : msg->code_address.bytes) {
            file << std::format("{:02x}", (int)b);
        }
        native::CompilerConfig config{base_config};
        auto asm_log_path = file.str();
        config.asm_log_path = asm_log_path.c_str();
        f = monad_vm_.compile(rev, code, code_size, config);
    }
    else {
        f = monad_vm_.compile(rev, code, code_size, base_config);
    }

    if (!f) {
        return evmc_result{
            .status_code = EVMC_INTERNAL_ERROR,
            .gas_left = 0,
            .gas_refund = 0,
            .output_data = nullptr,
            .output_size = 0,
            .release = nullptr,
            .create_address = {},
            .padding = {},
        };
    }

    compiled_contracts_.insert({{rev, code_hash}, *f});
    return monad_vm_.execute(*f, host, context, msg, code, code_size);
}

evmc_result BlockchainTestVM::execute_interpreter(
    evmc_host_interface const *host, evmc_host_context *context,
    evmc_revision rev, evmc_message const *msg, uint8_t const *code,
    size_t code_size)
{
    return monad::vm::interpreter::execute(
        host, context, rev, msg, {code, code_size});
}

evmc_capabilities_flagset BlockchainTestVM::get_capabilities() const
{
    return EVMC_CAPABILITY_EVM1;
}
