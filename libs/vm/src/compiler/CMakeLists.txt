add_library(monad-compiler
    lib.cpp
    uint256.cpp
    ir/bytecode.cpp
    ir/basic_blocks.cpp
    ir/instruction.cpp
    ir/local_stacks.cpp
    ir/poly_typed/strongly_connected_components.cpp
    ir/poly_typed/infer_state.cpp
    ir/poly_typed/kind.cpp
    ir/poly_typed/subst_map.cpp
    ir/poly_typed/unify.cpp
    ir/poly_typed/infer.cpp
    ir/poly_typed.cpp
    ir/simple_llvm.cpp
)

monad_compile_options(monad-compiler)
monad_link_llvm(monad-compiler)

set_property(TARGET monad-compiler
    PROPERTY POSITION_INDEPENDENT_CODE ON
)

target_include_directories(monad-compiler
    PUBLIC include
)

target_link_libraries(
    monad-compiler
    PUBLIC intx::intx
)
