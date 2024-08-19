add_library(compiler
    lib.cpp
    ir/bytecode.cpp
    ir/instruction.cpp
    ir/registers.cpp
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
