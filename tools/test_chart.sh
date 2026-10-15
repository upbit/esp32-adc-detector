#!/bin/sh
set -eu
binary=$(mktemp /tmp/test-starchart.XXXXXX)
swap_binary=$(mktemp /tmp/test-lcd-swap.XXXXXX)
rect_binary=$(mktemp /tmp/test-lcd-rect.XXXXXX)
trap 'rm -f "$binary" "$swap_binary" "$rect_binary"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I include src/starchart.c src/starchart_render.c test/test_starchart.c -lm -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  test/test_lcd_swap.c -o "$swap_binary"
"$swap_binary"
for diagnostic in 0 1; do
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DCHART_LCD_BOOT_DIAGNOSTIC=$diagnostic \
    -I include src/starchart.c src/starchart_render.c test/test_lcd_rect.c -lm -o "$rect_binary"
  "$rect_binary"
done
# The shift compensation and its diagnostics must also build and pass.
for shift in 0 1; do
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DCHART_LCD_SHIFT_BITS=$shift -DCHART_LCD_SHIFT_DIAGNOSTIC=1 \
    -I include src/starchart.c src/starchart_render.c test/test_lcd_rect.c -lm -o "$rect_binary"
  "$rect_binary"
done
