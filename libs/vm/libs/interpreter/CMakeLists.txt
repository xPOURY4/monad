enable_language(ASM)

option(MONAD_VM_INTERPRETER_DEBUG "Trace instructions executed by interpreter" OFF)

add_library(monad-vm-interpreter STATIC)

target_sources(monad-vm-interpreter PRIVATE
  "src/monad/vm/interpreter/call_runtime.hpp"
  "src/monad/vm/interpreter/debug.hpp"
  "src/monad/vm/interpreter/entry.S"
  "src/monad/vm/interpreter/execute.cpp"
  "src/monad/vm/interpreter/execute.hpp"
  "src/monad/vm/interpreter/instruction_table.hpp"
  "src/monad/vm/interpreter/instructions.hpp"
  "src/monad/vm/interpreter/intercode.cpp"
  "src/monad/vm/interpreter/intercode.hpp"
  "src/monad/vm/interpreter/types.hpp"
)

monad_compile_options(monad-vm-interpreter)

if(MONAD_VM_INTERPRETER_DEBUG)
  target_compile_definitions(monad-vm-interpreter PRIVATE MONAD_VM_INTERPRETER_DEBUG)
endif()

target_include_directories(monad-vm-interpreter
    PUBLIC src/
)

target_link_libraries(monad-vm-interpreter
    PUBLIC intx::intx
    PUBLIC evmc::evmc
    PUBLIC monad-vm::monad-vm-evm
    PUBLIC monad-vm::monad-vm-runtime
    PUBLIC monad-vm::monad-vm-utils
    PRIVATE monad-vm::monad-vm-core
)

add_library(monad-vm::monad-vm-interpreter ALIAS monad-vm-interpreter)