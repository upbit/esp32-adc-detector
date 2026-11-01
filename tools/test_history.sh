#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .pio/host-tests
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Itests/history/include -Isrc src/history.c tests/history/history_test.c -lm -o .pio/host-tests/history_test
.pio/host-tests/history_test
