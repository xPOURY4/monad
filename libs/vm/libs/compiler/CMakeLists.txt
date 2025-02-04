add_library(monad-compiler STATIC)

target_sources(monad-compiler PRIVATE
    "src/monad/compiler/evm_opcodes.h"
    "src/monad/compiler/transactional_unordered_map.h"
    "src/monad/compiler/types.h"
    # IR
    "src/monad/compiler/ir/basic_blocks.cpp"
    "src/monad/compiler/ir/basic_blocks.h"
    "src/monad/compiler/ir/local_stacks.cpp"
    "src/monad/compiler/ir/local_stacks.h"
    # polymorphic types
    "src/monad/compiler/ir/poly_typed.h"
    "src/monad/compiler/ir/poly_typed.cpp"
    "src/monad/compiler/ir/poly_typed/block.h"
    "src/monad/compiler/ir/poly_typed/exceptions.h"
    "src/monad/compiler/ir/poly_typed/infer.cpp"
    "src/monad/compiler/ir/poly_typed/infer.h"
    "src/monad/compiler/ir/poly_typed/infer_state.cpp"
    "src/monad/compiler/ir/poly_typed/infer_state.cpp"
    "src/monad/compiler/ir/poly_typed/kind.cpp"
    "src/monad/compiler/ir/poly_typed/kind.h"
    "src/monad/compiler/ir/poly_typed/strongly_connected_components.cpp"
    "src/monad/compiler/ir/poly_typed/strongly_connected_components.h"
    "src/monad/compiler/ir/poly_typed/subst_map.cpp"
    "src/monad/compiler/ir/poly_typed/subst_map.h"
    "src/monad/compiler/ir/poly_typed/unify.cpp"
    "src/monad/compiler/ir/poly_typed/unify.h"
    # x86 backend
    "src/monad/compiler/ir/x86.h"
    "src/monad/compiler/ir/x86.cpp"
    "src/monad/compiler/ir/x86/emitter.cpp"
    "src/monad/compiler/ir/x86/emitter.h"
    "src/monad/compiler/ir/x86/virtual_stack.cpp"
    "src/monad/compiler/ir/x86/virtual_stack.h"
)

monad_compile_options(monad-compiler)

target_include_directories(monad-compiler
    PUBLIC src/
)

target_link_libraries(monad-compiler
    PUBLIC intx::intx
    PUBLIC asmjit::asmjit
    PUBLIC evmc::evmc
    PUBLIC evmone
    PUBLIC runtime
    PUBLIC utils
)

add_library(Monad::MonadCompiler ALIAS monad-compiler)