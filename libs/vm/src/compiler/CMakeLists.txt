add_library(compiler
    lib.cpp
    ir/bytecode.cpp
    ir/basic_blocks.cpp
    ir/local_stacks.cpp
)

monad_compile_options(compiler)
monad_link_llvm(compiler)

set_property(TARGET compiler
    PROPERTY POSITION_INDEPENDENT_CODE ON
)

target_include_directories(compiler
    PUBLIC include
)

target_link_libraries(
    compiler
    PUBLIC intx::intx
)
