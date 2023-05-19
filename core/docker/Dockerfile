# docker build -f docker/Dockerfile .

FROM fedora:38

RUN yum -y update

RUN yum -y install \
  clang \
  cmake \
  ninja-build

RUN yum -y install \
  gtest \
  liburing \
  rocksdb

RUN yum -y install \
  gtest-devel \
  liburing-devel \
  rocksdb-devel

COPY . monad-core

RUN cd monad-core && ls -al && ./scripts/configure.sh

RUN cd monad-core && ./scripts/build.sh
