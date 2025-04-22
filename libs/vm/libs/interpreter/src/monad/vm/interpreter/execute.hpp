#pragma once

#include <monad/vm/runtime/allocator.hpp>
#include <monad/vm/runtime/types.hpp>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <span>

namespace monad::vm::interpreter
{
    evmc_result execute(
        monad::vm::runtime::EvmStackAllocator &allocator,
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg,
        std::span<uint8_t const> code);
}
