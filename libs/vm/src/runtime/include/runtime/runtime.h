#pragma once

#include <evmc/evmc.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct monad_runtime_interface
{
    struct evmc_result result;
    struct evmc_host_interface const *host;
    struct evmc_host_context *context;
    enum evmc_revision revision;
    struct evmc_message const *message;
};

void monad_evm_runtime_stop(struct monad_runtime_interface *host, void *args);

#ifdef __cplusplus
}
#endif
