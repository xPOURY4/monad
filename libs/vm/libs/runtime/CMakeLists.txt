enable_language(ASM)

add_library(runtime STATIC)

target_sources(runtime PRIVATE
    "src/monad/runtime/bin.hpp"
    "src/monad/runtime/call.hpp"
    "src/monad/runtime/create.hpp"
    "src/monad/runtime/data.hpp"
    "src/monad/runtime/detail.hpp"
    "src/monad/runtime/environment.hpp"
    "src/monad/runtime/exit.S"
    "src/monad/runtime/exit.cpp"
    "src/monad/runtime/math.S"
    "src/monad/runtime/keccak.hpp"
    "src/monad/runtime/log.hpp"
    "src/monad/runtime/math.hpp"
    "src/monad/runtime/memory.hpp"
    "src/monad/runtime/runtime.hpp"
    "src/monad/runtime/selfdestruct.hpp"
    "src/monad/runtime/storage.hpp"
    "src/monad/runtime/storage_costs.hpp"
    "src/monad/runtime/transmute.hpp"
    "src/monad/runtime/types.hpp"
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