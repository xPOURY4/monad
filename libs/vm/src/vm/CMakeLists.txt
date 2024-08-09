add_library(monad-compiler-vm SHARED vm.cpp)

target_link_libraries(monad-compiler-vm
    PRIVATE
        evmc::evmc
        compiler
)