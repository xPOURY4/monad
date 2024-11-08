add_library(monad-compiler-vm SHARED
    vm.cpp
)

monad_compile_options(monad-compiler-vm)

target_link_libraries(monad-compiler-vm
    PUBLIC
        intx::intx
        evmc::evmc
        runtime
    PRIVATE
        utils
        monad-compiler
)

target_include_directories(monad-compiler-vm
    PUBLIC include
)