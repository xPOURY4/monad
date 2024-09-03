#include <vm/vm.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/DynamicLibrary.h>

#define BIND_SYMBOL(name)                                                      \
    do {                                                                       \
        llvm::sys::DynamicLibrary::AddSymbol(                                  \
            #name, reinterpret_cast<void *>(&name));                           \
    }                                                                          \
    while (false);

namespace monad::vm
{
    void bind_runtime()
    {
        BIND_SYMBOL(monad_evm_gas_left);
        BIND_SYMBOL(monad_evm_runtime_sstore);
        BIND_SYMBOL(monad_evm_runtime_stop);
    }
}
