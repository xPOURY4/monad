add_library(monad-compiler-vm STATIC)

target_sources(monad-compiler-vm PRIVATE
    "src/monad/vm/vm.cpp"
    "src/monad/vm/vm.hpp"
    # evmone wrappers
    "src/monad/vm/evmone/baseline_execute.cpp"
    "src/monad/vm/evmone/baseline_execute.hpp"
    "src/monad/vm/evmone/code_analysis.hpp"
)

monad_compile_options(monad-compiler-vm)

target_include_directories(monad-compiler-vm
    PUBLIC src/
)

target_link_libraries(monad-compiler-vm
    PUBLIC intx::intx
    PUBLIC evmc::evmc
    PUBLIC asmjit::asmjit
    PUBLIC monad-vm::monad-vm-runtime
    PUBLIC monad-vm::monad-vm-utils
    PUBLIC monad-compiler
)