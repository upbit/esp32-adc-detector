#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cmake -S tests/ui -B .pio/host-network-ui -DCMAKE_BUILD_TYPE=Debug
cmake --build .pio/host-network-ui -j 8
cd .pio/host-network-ui
./network_ui_test
