#!/bin/bash

packages=(
  abseil-cpp
  boost-atomic  # boost-filesystem
  boost-context  # boost-fiber
  boost-fiber
  boost-filesystem  # boost-fiber
  boost-system  # boost-filesystem
  glibc
  glibc-common
  gmock
  google-benchmark
  gtest
  libgcc
  libstdc++
  liburing
  mimalloc
)

dnf -y install "${packages[@]}"

debuginfo-install -y "${packages[@]}"
