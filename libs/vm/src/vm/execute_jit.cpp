#include <vm/execute_jit.h>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace monad::vm
{
    std::unique_ptr<llvm::ExecutionEngine>
    create_engine(std::unique_ptr<llvm::Module> mod)
    {
        auto target_opts = llvm::TargetOptions{};
        auto err = std::string{};

        auto *engine_ptr = llvm::EngineBuilder(std::move(mod))
                               .setErrorStr(&err)
                               .setEngineKind(llvm::EngineKind::JIT)
                               .setTargetOptions(target_opts)
                               .create();

        if (!engine_ptr) {
            throw std::runtime_error(err);
        }

        return std::unique_ptr<llvm::ExecutionEngine>(engine_ptr);
    }
}
