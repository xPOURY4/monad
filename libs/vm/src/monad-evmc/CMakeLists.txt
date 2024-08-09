add_executable(monad-evmc main.cpp)

monad_compile_options(monad-evmc)
monad_link_llvm(monad-evmc)

target_link_libraries(monad-evmc
    PRIVATE
        compiler
        CLI11::CLI11
)