add_library(compiler
    lib.cpp
    uint256.cpp
    ir/bytecode.cpp
    ir/basic_blocks.cpp
    ir/local_stacks.cpp
    ir/poly_typed/strongly_connected_components.cpp
    ir/poly_typed/infer_state.cpp
    ir/poly_typed/kind.cpp
    ir/poly_typed/subst_map.cpp
    ir/poly_typed/unify.cpp
    ir/poly_typed.cpp
    ir/simple_llvm.cpp
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
