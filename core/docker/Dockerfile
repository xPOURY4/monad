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
  gtest \
  liburing \
  liburing-debuginfo

RUN yum -y install \
  gtest-devel \
  liburing-devel

COPY . monad-core

RUN cd monad-core && ls -al && ./scripts/configure.sh

RUN cd monad-core && ./scripts/build.sh

RUN cd monad-core && ./scripts/test.sh
