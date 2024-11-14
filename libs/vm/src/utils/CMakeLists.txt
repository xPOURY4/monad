add_library(utils
    src/assert.c
    src/uint256.cpp
)

target_include_directories(utils PUBLIC include/)
target_link_libraries(utils
    PUBLIC intx::intx
)