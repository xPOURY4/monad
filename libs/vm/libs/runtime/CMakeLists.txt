enable_language(ASM)

add_library(runtime STATIC)

target_sources(runtime PRIVATE
    "src/monad/runtime/bin.h"
    "src/monad/runtime/call.h"
    "src/monad/runtime/create.h"
    "src/monad/runtime/data.h"
    "src/monad/runtime/detail.h"
    "src/monad/runtime/environment.h"
    "src/monad/runtime/exit.cpp"
    "src/monad/runtime/exit.S"
    "src/monad/runtime/keccak.h"
    "src/monad/runtime/log.h"
    "src/monad/runtime/math.h"
    "src/monad/runtime/memory.h"
    "src/monad/runtime/runtime.h"
    "src/monad/runtime/selfdestruct.h"
    "src/monad/runtime/storage_costs.h"
    "src/monad/runtime/storage.h"
    "src/monad/runtime/transmute.h"
    "src/monad/runtime/types.h"
)

monad_compile_options(runtime)

target_include_directories(runtime
    PUBLIC src/
)

target_link_libraries(runtime
    PUBLIC evmc::evmc
    PUBLIC ethash::keccak
    PUBLIC utils
)