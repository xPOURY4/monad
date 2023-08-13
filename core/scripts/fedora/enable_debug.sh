#!/bin/bash

yum -y install \
  dnf-plugins-core

dnf config-manager --enable \
  fedora-debuginfo \
  fedora-modular-debuginfo \
  updates-debuginfo
