#include <vm/execute_jit.h>

#include <llvm/ExecutionEngine/MCJIT.h>

#include <expected>
#include <string>

namespace monad::vm
{
    std::expected<std::unique_ptr<llvm::ExecutionEngine>, std::string>
    create_engine(std::unique_ptr<llvm::Module> mod)
    {
        auto target_opts = llvm::TargetOptions{};
        auto err = std::string{};

        auto engine_ptr = llvm::EngineBuilder(std::move(mod))
                              .setErrorStr(&err)
                              .setEngineKind(llvm::EngineKind::JIT)
                              .setTargetOptions(target_opts)
                              .create();

        if (!engine_ptr) {
            return std::unexpected(err);
        }

        return std::unique_ptr<llvm::ExecutionEngine>(engine_ptr);
    }
}
