#!/usr/bin/env python3
"""Dump one glyph as ASCII art to see exactly where ink is cut."""
import re
import sys
from pathlib import Path

header = Path("include/starchart_font.h").read_text()
glyphs = [tuple(int(v) for v in m)
          for m in re.findall(r"\{(\d+), (\d+), (\d+), (\d+), (\d+)\},", header)]
blob = header.split("chart_font_data[] = {", 1)[1].split("};", 1)[0]
data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", blob))

want_char, want_size = sys.argv[1], int(sys.argv[2])
for code, offset, size, width, height in glyphs:
    if chr(code) != want_char or size != want_size:
        continue
    px = []
    for i in range(width * height):
        byte = data[offset + i // 2]
        px.append(byte >> 4 if i % 2 == 0 else byte & 15)
    print(f"glyph {want_char!r} size={size} width={width} height={height}")
    ramp = " .:-=+*#%@"
    for y in range(height):
        row = "".join(ramp[min(px[y * width + x] * 9 // 15, 9)] for x in range(width))
        print(f"  y={y:2d} |{row}|")
    break
