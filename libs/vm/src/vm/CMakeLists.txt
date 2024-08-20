add_library(monad-compiler-vm SHARED
    vm.cpp
    execute_jit.cpp
    testing.cpp
)

monad_compile_options(monad-compiler-vm)
monad_link_llvm(monad-compiler-vm)

target_link_libraries(monad-compiler-vm
    PUBLIC
        intx::intx
        evmc::evmc
    PRIVATE
        utils
        compiler
        runtime
)

target_include_directories(monad-compiler-vm
    PUBLIC include
)