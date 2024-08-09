add_library(compiler lib.cpp lexer.cpp)

monad_compile_options(compiler)
monad_link_llvm(compiler)

set_property(TARGET compiler
    PROPERTY POSITION_INDEPENDENT_CODE ON
)

target_include_directories(compiler
    PUBLIC include
)
