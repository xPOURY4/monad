#pragma once

#include <evmc/evmc.h>

#ifdef __cplusplus
extern "C"
{
#endif

extern int64_t monad_evm_gas_left;

struct monad_runtime_interface
{
    struct evmc_result result;
    struct evmc_host_interface const *host;
    struct evmc_host_context *context;
    enum evmc_revision revision;
    struct evmc_message const *message;
};

void monad_evm_runtime_sstore(
    struct monad_runtime_interface *host, uint8_t *key_bytes,
    uint8_t *val_bytes);
void monad_evm_runtime_stop(struct monad_runtime_interface *host);
void monad_evm_runtime_set_gas(int64_t value);

#ifdef __cplusplus
}
#endif
