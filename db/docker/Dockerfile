# docker build -f docker/Dockerfile .

FROM fedora:38

COPY . src

RUN cd src && monad-core/scripts/fedora/enable_debug.sh

RUN yum -y update

RUN cd src && monad-core/scripts/fedora/install_tools.sh

RUN cd src && monad-core/scripts/fedora/install_deps.sh

RUN cd src && monad-core/scripts/fedora/install_devel.sh

RUN yum -y install \
  boost-devel \
  cli11-devel

ARG CC
ARG CXX
ARG CMAKE_BUILD_TYPE

RUN cd src && CC=${CC:?} CXX=${CXX:?} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:?} CFLAGS="-march=haswell" CXXFLAGS="-march=haswell" ASMFLAGS="-march=haswell" ./monad-core/scripts/configure.sh

RUN cd src && ./monad-core/scripts/build.sh

# Replace the line below with this when you want CI to print exactly where it is hanging at
# RUN cd src && ctest --output-on-failure -V --test-dir build

RUN cd src && PYTHONPATH="monad-core" ./monad-core/scripts/test.sh

