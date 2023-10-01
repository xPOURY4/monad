#!/bin/bash

packages=(
  abseil-cpp
  abseil-cpp-debuginfo

  boost-atomic  # boost-filesystem
  boost-atomic-debuginfo

  boost-context  # boost-fiber
  boost-context-debuginfo

  boost-fiber
  boost-fiber-debuginfo

  boost-filesystem  # boost-fiber
  boost-filesystem-debuginfo

  boost-system  # boost-filesystem
  boost-system-debuginfo

  glibc
  glibc-debuginfo

  glibc-common
  glibc-common-debuginfo

  gmock
  gmock-debuginfo

  google-benchmark
  google-benchmark-debuginfo

  gtest
  gtest-debuginfo

  libgcc
  libgcc-debuginfo

  libstdc++
  libstdc++-debuginfo

  liburing
  liburing-debuginfo

  mimalloc
  mimalloc-debuginfo
)

yum -y install "${packages[@]}"
