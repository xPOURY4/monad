# docker build -f docker/Dockerfile .

FROM fedora:39

RUN yum -y install dnf-plugins-core dnf-utils

RUN yum -y update

RUN --mount=target=monad-core monad-core/scripts/fedora/install_tools.sh

RUN --mount=target=monad-core monad-core/scripts/fedora/install_deps.sh

RUN --mount=target=monad-core monad-core/scripts/fedora/install_devel.sh

COPY . monad-core

ARG CC
ARG CXX
ARG CMAKE_BUILD_TYPE

RUN cd monad-core && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} CFLAGS="-march=haswell" CXXFLAGS="-march=haswell" ASMFLAGS="-march=haswell" ./scripts/configure.sh

RUN cd monad-core && ./scripts/build.sh

RUN cd monad-core && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} ./scripts/test.sh
