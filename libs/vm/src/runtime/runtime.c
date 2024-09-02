#include <runtime/runtime.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int64_t monad_evm_gas_left = LLONG_MAX;

void monad_evm_runtime_stop(struct monad_runtime_interface *host, void *)
{
    host->result.status_code = EVMC_SUCCESS;
    host->result.gas_left = monad_evm_gas_left;
}

void monad_evm_runtime_set_gas(int64_t value)
{
    monad_evm_gas_left = value;
}
