#!/bin/sh
set -eu
binary=$(mktemp /tmp/test-starchart.XXXXXX)
swap_binary=$(mktemp /tmp/test-lcd-swap.XXXXXX)
pipeline_binary=$(mktemp /tmp/test-lcd-pipeline.XXXXXX)
rect_binary=$(mktemp /tmp/test-lcd-rect.XXXXXX)
trap 'rm -f "$binary" "$swap_binary" "$pipeline_binary" "$rect_binary"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I include src/starchart.c src/starchart_render.c test/test_starchart.c -lm -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  test/test_lcd_swap.c -o "$swap_binary"
"$swap_binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I include src/starchart.c src/starchart_render.c test/test_lcd_pipeline.c -lm -o "$pipeline_binary"
"$pipeline_binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I include src/starchart.c src/starchart_render.c test/test_lcd_rect.c -lm -o "$rect_binary"
"$rect_binary"
