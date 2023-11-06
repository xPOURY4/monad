# docker build -f docker/Dockerfile .

FROM fedora:39

RUN yum -y install dnf-plugins-core dnf-utils

RUN yum -y update

RUN --mount=target=src src/monad-core/scripts/fedora/install_tools.sh

RUN --mount=target=src src/monad-core/scripts/fedora/install_deps.sh

RUN --mount=target=src src/monad-core/scripts/fedora/install_devel.sh

RUN yum -y install \
  cli11-devel

COPY . src

ARG CC
ARG CXX
ARG CMAKE_BUILD_TYPE

RUN cd src && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} CFLAGS="-march=haswell" CXXFLAGS="-march=haswell" ASMFLAGS="-march=haswell" ./monad-core/scripts/configure.sh

RUN cd src && ./monad-core/scripts/build.sh

# Replace the line below with this when you want CI to print exactly where it is hanging at
# RUN cd src && ctest --output-on-failure -V --test-dir build

RUN cd src && PYTHONPATH="monad-core" CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} ./monad-core/scripts/test.sh
