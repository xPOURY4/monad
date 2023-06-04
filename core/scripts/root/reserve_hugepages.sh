#!/bin/bash

echo 4 > /sys/devices/system/node/node${1}/hugepages/hugepages-1048576kB/nr_hugepages
