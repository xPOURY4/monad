#!/bin/bash

packages=(
  libabsl-dev
  libbenchmark-dev
  libgmock-dev
  libgtest-dev
  libmimalloc-dev
  liburing-dev
)

apt install -y "${packages[@]}"
