#pragma once

#include <evmc/evmc.h>

struct monad_runtime_interface
{
    struct evmc_result result;
    struct evmc_host_interface const *host;
    struct evmc_host_context *context;
    enum evmc_revision revision;
    struct evmc_message const *message;
};
