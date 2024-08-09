add_library(compiler lib.cpp)

monad_compile_options(compiler)
target_include_directories(compiler PUBLIC include)