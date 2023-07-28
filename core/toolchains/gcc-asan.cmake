# This toolchain is for an address sanitising GCC with libstdc++ on Linux
set(CMAKE_ASM_FLAGS_INIT "-march=haswell")
set(CMAKE_C_FLAGS_INIT "-march=haswell")
set(CMAKE_CXX_FLAGS_INIT "-march=haswell -fsanitize=address -fsanitize=undefined -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g")
