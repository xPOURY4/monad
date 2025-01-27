#include <vm/evmone/baseline_execute.h>
#include <vm/evmone/code_analysis.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <evmone/baseline.hpp>
#include <evmone/evmone.h>
#include <evmone/execution_state.hpp>
#include <evmone/vm.hpp>

#include <memory>

namespace monad
{
    struct VMDeleter
    {
        static void operator()(evmc_vm *vm)
        {
            vm->destroy(vm);
        }
    };

    evmc::Result baseline_execute(
        evmc_message const &msg, evmc_revision const rev,
        evmc::Host *const host, CodeAnalysis const &code_analysis)
    {
        std::unique_ptr<evmc_vm, VMDeleter> const vm{evmc_create_evmone()};

        if (code_analysis.executable_code().empty()) {
            return evmc::Result{EVMC_SUCCESS, msg.gas};
        }

        auto const result = evmone::baseline::execute(
            *static_cast<evmone::VM *>(vm.get()),
            host->get_interface(),
            host->to_context(),
            rev,
            msg,
            code_analysis);

        return evmc::Result{result};
    }
}
