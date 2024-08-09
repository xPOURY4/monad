add_library(compiler lib.cpp)

monad_compile_options(compiler)
monad_link_llvm(compiler)

target_include_directories(compiler
    PUBLIC include
)