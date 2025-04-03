add_library(monad-evm STATIC)

set_target_properties(monad-evm PROPERTIES LINKER_LANGUAGE CXX)

target_sources(monad-evm PRIVATE
    "src/monad/evm/opcodes.hpp"
    "src/monad/evm/opcodes_xmacro.hpp"
)

target_include_directories(monad-evm
    PUBLIC src
)

target_link_libraries(monad-evm
    PUBLIC evmc::evmc
    PUBLIC monad-vm::monad-vm-core
)