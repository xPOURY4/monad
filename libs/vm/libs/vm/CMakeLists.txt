add_library(monad-vm STATIC)

target_sources(monad-vm PRIVATE
    "src/monad/vm/vm.cpp"
    "src/monad/vm/vm.hpp"
    # evmone wrappers
    "src/monad/vm/evmone/baseline_execute.cpp"
    "src/monad/vm/evmone/baseline_execute.hpp"
    "src/monad/vm/evmone/code_analysis.hpp"
)

monad_compile_options(monad-vm)

target_include_directories(monad-vm
    PUBLIC src/
)

target_link_libraries(monad-vm
    PUBLIC intx::intx
    PUBLIC evmc::evmc
    PUBLIC asmjit::asmjit
    PUBLIC monad-vm::monad-vm-compiler
    PRIVATE monad-vm::monad-vm-core
    PRIVATE monad-vm::monad-vm-interpreter
    PRIVATE monad-vm::monad-vm-runtime
    PRIVATE monad-vm::monad-vm-utils
)

add_library(monad-vm::monad-vm ALIAS monad-vm)