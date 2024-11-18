add_library(utils
    src/assert.c
    src/uint256.cpp
)
monad_compile_options(utils)
set_property(TARGET utils
    PROPERTY POSITION_INDEPENDENT_CODE ON
)
target_include_directories(utils PUBLIC include/)
target_link_libraries(utils
    PUBLIC intx::intx
)