# Testing VM

add_library(blockchain-test-vm STATIC)
target_sources(blockchain-test-vm
    PRIVATE
        test_vm.hpp
        test_vm.cpp
)
target_link_libraries(blockchain-test-vm
    PUBLIC
        evmc::evmc
        monad-compiler
        monad-compiler-vm
        monad-vm::monad-vm-interpreter
)
target_include_directories(blockchain-test-vm
    PUBLIC .
)
monad_compile_options(blockchain-test-vm)
