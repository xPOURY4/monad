enable_language(ASM)

option(MONAD_INTERPRETER_DEBUG "Trace instructions executed by interpreter" OFF)

add_library(monad-interpreter STATIC)

target_sources(monad-interpreter PRIVATE
  "src/monad/interpreter/call_runtime.hpp"
  "src/monad/interpreter/debug.hpp"
  "src/monad/interpreter/entry.S"
  "src/monad/interpreter/execute.cpp"
  "src/monad/interpreter/execute.hpp"
  "src/monad/interpreter/instruction_table.hpp"
  "src/monad/interpreter/instructions.hpp"
  "src/monad/interpreter/intercode.cpp"
  "src/monad/interpreter/intercode.hpp"
  "src/monad/interpreter/state.hpp"
  "src/monad/interpreter/types.hpp"
)

monad_compile_options(monad-interpreter)

if(MONAD_INTERPRETER_DEBUG)
  target_compile_definitions(monad-interpreter PRIVATE MONAD_INTERPRETER_DEBUG)
endif()

target_include_directories(monad-interpreter
    PUBLIC src/
)

target_link_libraries(monad-interpreter
    PUBLIC intx::intx
    PUBLIC evmc::evmc
    PUBLIC monad-vm::monad-vm-evm
    PUBLIC monad-vm::monad-vm-runtime
    PUBLIC monad-vm::monad-vm-utils
)

add_library(Monad::MonadInterpreter ALIAS monad-interpreter)