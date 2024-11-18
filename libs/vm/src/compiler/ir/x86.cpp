#include "asmjit/core/api-config.h"
#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/bytecode.h"
#include "compiler/ir/local_stacks.h"
#include <compiler/ir/x86.h>

#include <asmjit/core/codeholder.h>
#include <asmjit/x86/x86assembler.h>

#include <evmc/evmc.h>

#include <cstdint>
#include <span>

static_assert(ASMJIT_ARCH_X86 == 64);

using namespace monad::compiler;
using namespace monad::compiler::local_stacks;
using namespace monad::compiler::native;

namespace
{
    template <evmc_revision rev>
    void
    compile_local_stacks(asmjit::x86::Assembler &as, LocalStacksIR const &ir)
    {
        (void)ir;
        as.ret();
    }

    template <evmc_revision rev>
    void compile_contract(
        asmjit::x86::Assembler &as, std::span<uint8_t const> contract)
    {
        // TODO - Need to change opcode table to depend on revision.
        auto ir = LocalStacksIR(
            basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(contract)));
        compile_local_stacks<rev>(as, ir);
    }
}

namespace monad::compiler::native
{
    void compile(
        asmjit::CodeHolder &into, std::span<uint8_t const> contract,
        evmc_revision rev)
    {
        auto as = asmjit::x86::Assembler(&into);
        (void)rev;
        // TODO - branch on revision here?
        return ::compile_contract<EVMC_CANCUN>(as, contract);
    }
}
