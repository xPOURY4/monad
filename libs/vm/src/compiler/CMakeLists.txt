add_library(monad-compiler-vm SHARED lib.cpp)

target_link_libraries(monad-compiler-vm PRIVATE evmc::evmc)