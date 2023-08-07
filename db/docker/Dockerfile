# docker build -f docker/Dockerfile .

FROM fedora:38

RUN yum -y install \
  dnf-plugins-core

RUN dnf config-manager --enable \
  fedora-debuginfo \
  fedora-modular-debuginfo \
  updates-debuginfo

RUN yum -y update

RUN yum -y install \
  clang \
  cmake \
  ninja-build

RUN yum -y install \
  google-benchmark \
  gtest \
  liburing \
  liburing-debuginfo \
  mimalloc \
  mimalloc-debuginfo \
  valgrind

RUN yum -y install \
  boost-devel \
  cli11-devel \
  google-benchmark-devel \
  gtest-devel \
  liburing-devel \
  mimalloc-devel \
  valgrind-devel

RUN yum -y install \
  gdb \
  python3-pytest

COPY . src

ARG CC
ARG CXX
ARG CMAKE_BUILD_TYPE

RUN cd src && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} CFLAGS="-march=haswell" CXXFLAGS="-march=haswell" ASMFLAGS="-march=haswell" ./monad-core/scripts/configure.sh

RUN cd src && ./monad-core/scripts/build.sh

RUN cd src && PYTHONPATH="monad-core" ./monad-core/scripts/test.sh
