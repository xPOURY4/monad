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
  "src/monad/interpreter/instructions.cpp"
  "src/monad/interpreter/instructions.hpp"
  "src/monad/interpreter/intercode.cpp"
  "src/monad/interpreter/intercode.hpp"
  "src/monad/interpreter/state.hpp"
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
    PUBLIC monad-evm
    PUBLIC runtime
    PUBLIC utils
)

add_library(Monad::MonadInterpreter ALIAS monad-interpreter)