#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .pio/host-tests
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Itests/host -Isrc src/battery.c tests/battery_test.c -lm -o .pio/host-tests/battery_test
.pio/host-tests/battery_test
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -DBATTERY_EMPTY_VOLTAGE_V=3.10f -DBATTERY_FULL_VOLTAGE_V=4.16f \
    -Itests/host -Isrc src/battery.c tests/battery_test.c -lm -o .pio/host-tests/battery_manual_anchor_test
.pio/host-tests/battery_manual_anchor_test
