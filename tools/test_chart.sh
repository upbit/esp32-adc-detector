#!/bin/sh
set -eu
binary=$(mktemp /tmp/test-starchart.XXXXXX)
swap_binary=$(mktemp /tmp/test-lcd-swap.XXXXXX)
trap 'rm -f "$binary" "$swap_binary"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I include src/starchart.c src/starchart_render.c test/test_starchart.c -lm -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  test/test_lcd_swap.c -o "$swap_binary"
"$swap_binary"
