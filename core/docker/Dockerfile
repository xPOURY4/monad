# docker build -f docker/Dockerfile .

FROM ubuntu:22.04

RUN apt update

RUN apt upgrade -y

RUN apt install -y apt-utils

RUN apt install -y dialog

RUN apt install -y ca-certificates curl gnupg software-properties-common wget

RUN --mount=target=src src/scripts/ubuntu-build/install-gcc.sh

RUN --mount=target=src src/scripts/ubuntu-build/install-clang.sh

RUN --mount=target=src src/scripts/ubuntu-build/install-boost.sh

RUN --mount=target=src src/scripts/ubuntu-build/install-cmake.sh

RUN --mount=target=src src/scripts/ubuntu-build/install-tools.sh

RUN --mount=target=src src/scripts/ubuntu-build/install-deps.sh

COPY . src

ARG CC
ARG CXX
ARG CMAKE_BUILD_TYPE

RUN cd src && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} CFLAGS="-march=haswell" CXXFLAGS="-march=haswell" ASMFLAGS="-march=haswell" ./scripts/configure.sh

RUN cd src && ./scripts/build.sh

RUN cd src && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} ./scripts/test.sh
