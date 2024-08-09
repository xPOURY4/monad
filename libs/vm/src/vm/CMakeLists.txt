add_library(monad-compiler-vm SHARED vm.cpp)

monad_compile_options(monad-compiler-vm)
target_link_libraries(monad-compiler-vm
    PRIVATE
        evmc::evmc
        compiler
)