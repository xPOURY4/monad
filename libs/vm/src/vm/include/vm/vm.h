#pragma once

#include <runtime/runtime.h>

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <string>

extern "C" evmc_vm *evmc_create_monad_compiler_vm();
