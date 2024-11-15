add_library(runtime
    exit.cpp
    runtime.cpp)

monad_compile_options(runtime)

set_property(TARGET runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(runtime
    PUBLIC evmc::evmc
    PUBLIC utils
)
target_include_directories(runtime PUBLIC include)