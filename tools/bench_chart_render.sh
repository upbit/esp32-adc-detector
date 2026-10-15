#!/bin/sh
set -eu
binary=$(mktemp /tmp/bench-chart-render.XXXXXX)
trap 'rm -f "$binary"' EXIT
cc -std=c11 -O2 -Wall -Wextra -Werror -I include \
  src/starchart.c src/starchart_render.c tools/bench_chart_render.c -lm -o "$binary"
"$binary"
