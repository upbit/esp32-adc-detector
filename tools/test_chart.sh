#!/bin/sh
set -eu
binary=$(mktemp /tmp/test-starchart.XXXXXX)
trap 'rm -f "$binary"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I include src/starchart.c src/starchart_render.c test/test_starchart.c -lm -o "$binary"
"$binary"
