add_library(monad-compiler-vm SHARED
    vm.cpp
    evmone/baseline_execute.cpp
)

monad_compile_options(monad-compiler-vm)

target_link_libraries(monad-compiler-vm
    PUBLIC
        intx::intx
        evmc::evmc
        runtime
        asmjit::asmjit
        utils
        monad-compiler
)

target_include_directories(monad-compiler-vm
    PUBLIC include
)