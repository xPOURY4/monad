add_library(monad-compiler STATIC)

target_sources(monad-compiler PRIVATE
    "src/monad/compiler/transactional_unordered_map.hpp"
    "src/monad/compiler/types.hpp"
    # IR
    "src/monad/compiler/ir/basic_blocks.cpp"
    "src/monad/compiler/ir/basic_blocks.hpp"
    "src/monad/compiler/ir/instruction.hpp"
    "src/monad/compiler/ir/local_stacks.cpp"
    "src/monad/compiler/ir/local_stacks.hpp"
    # polymorphic types
    "src/monad/compiler/ir/poly_typed.hpp"
    "src/monad/compiler/ir/poly_typed.cpp"
    "src/monad/compiler/ir/poly_typed/block.hpp"
    "src/monad/compiler/ir/poly_typed/exceptions.hpp"
    "src/monad/compiler/ir/poly_typed/infer.cpp"
    "src/monad/compiler/ir/poly_typed/infer.hpp"
    "src/monad/compiler/ir/poly_typed/infer_state.cpp"
    "src/monad/compiler/ir/poly_typed/infer_state.hpp"
    "src/monad/compiler/ir/poly_typed/kind.cpp"
    "src/monad/compiler/ir/poly_typed/kind.hpp"
    "src/monad/compiler/ir/poly_typed/strongly_connected_components.cpp"
    "src/monad/compiler/ir/poly_typed/strongly_connected_components.hpp"
    "src/monad/compiler/ir/poly_typed/subst_map.cpp"
    "src/monad/compiler/ir/poly_typed/subst_map.hpp"
    "src/monad/compiler/ir/poly_typed/unify.cpp"
    "src/monad/compiler/ir/poly_typed/unify.hpp"
    # x86 backend
    "src/monad/compiler/ir/x86.hpp"
    "src/monad/compiler/ir/x86.cpp"
    "src/monad/compiler/ir/x86/emitter.cpp"
    "src/monad/compiler/ir/x86/emitter.hpp"
    "src/monad/compiler/ir/x86/types.hpp"
    "src/monad/compiler/ir/x86/virtual_stack.cpp"
    "src/monad/compiler/ir/x86/virtual_stack.hpp"
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
    PUBLIC monad-vm::monad-vm-evm
    PUBLIC monad-vm::monad-vm-runtime
    PUBLIC monad-vm::monad-vm-utils
)

add_library(Monad::MonadCompiler ALIAS monad-compiler)