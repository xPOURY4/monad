#include <compiler/ir/x86.h>

#include <asmjit/core/codeholder.h>
#include <asmjit/x86/x86assembler.h>

#include <evmc/evmc.h>

#include <cstdint>
#include <span>

using namespace asmjit;
using namespace monad::compiler::local_stacks;

namespace monad::compiler::native
{
    void compile(
        CodeHolder &into, std::span<uint8_t const> contract, evmc_revision rev)
    {
        (void)contract;
        (void)rev;

        auto a = x86::Assembler(&into);

        a.ret();
    }
}
