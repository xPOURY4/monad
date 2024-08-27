#include <runtime/runtime.h>

void monad_evm_runtime_stop(struct monad_runtime_interface *host, void *)
{
    host->result.status_code = EVMC_SUCCESS;
}
