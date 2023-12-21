# docker build -f docker/Dockerfile .

FROM ubuntu:22.04 as base

RUN apt update

RUN apt upgrade -y

RUN apt install -y apt-utils

RUN apt install -y dialog

RUN apt install -y ca-certificates curl gnupg software-properties-common wget

COPY scripts/ubuntu-build/ /opt

RUN /opt/install-gcc.sh
RUN /opt/install-clang.sh
RUN /opt/install-boost.sh
RUN /opt/install-cmake.sh
RUN /opt/install-tools.sh
RUN /opt/install-deps.sh

FROM base as build_and_test

COPY . src

ARG CC
ARG CXX
ARG CMAKE_BUILD_TYPE

RUN cd src && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} CFLAGS="-march=haswell" CXXFLAGS="-march=haswell" ASMFLAGS="-march=haswell" ./scripts/configure.sh

RUN cd src && ./scripts/build.sh

RUN cd src && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} ./scripts/test.sh
