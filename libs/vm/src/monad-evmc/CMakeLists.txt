add_executable(monad-evmc main.cpp)

monad_compile_options(monad-evmc)

target_link_libraries(monad-evmc
    PRIVATE
        monad-compiler
        utils
        CLI11::CLI11
)