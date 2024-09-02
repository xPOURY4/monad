add_library(runtime runtime.c)

monad_compile_options(runtime)

set_property(TARGET runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(runtime PUBLIC evmc::evmc)
target_include_directories(runtime PUBLIC include)