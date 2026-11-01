#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .pio/host-tests
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Dtime=test_time -Wall -Wextra -Werror \
    -fsanitize=address,undefined -g -Itests/network/include -Isrc \
    tests/network/network_test.c src/time/clock.c -o .pio/host-tests/network_test
.pio/host-tests/network_test
JSON_DIR=$(node -p 'require("./.pio/build/esp32-s3-n8r8/project_description.json").build_component_info.json.dir + "/cJSON"')
cc -std=gnu11 -Wno-deprecated-declarations -fsanitize=address,undefined -g \
    -I"$JSON_DIR" -c "$JSON_DIR/cJSON.c" -o .pio/host-tests/cJSON.o
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -Itests/network/include -Isrc -I"$JSON_DIR" \
    tests/network/portal_api_test.c .pio/host-tests/cJSON.o \
    -o .pio/host-tests/portal_api_test
.pio/host-tests/portal_api_test
