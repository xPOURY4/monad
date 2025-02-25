add_library(monad-interpreter STATIC)

target_sources(monad-interpreter PRIVATE
  "src/monad/interpreter/intercode.cpp"
  "src/monad/interpreter/intercode.hpp"
)

monad_compile_options(monad-interpreter)

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