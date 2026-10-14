#!/usr/bin/env python3
"""Measure baked glyph ink bounds; proves whether the box clips real pixels."""
import re
import sys
from pathlib import Path

header = Path(sys.argv[1] if len(sys.argv) > 1 else "include/starchart_font.h").read_text()
glyphs = [tuple(int(v) for v in m)
          for m in re.findall(r"\{(\d+), (\d+), (\d+), (\d+), (\d+)\},", header)]
blob = header.split("chart_font_data[] = {", 1)[1].split("};", 1)[0]
data = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", blob))


def nibbles(offset, count):
    out = []
    for i in range(count):
        byte = data[offset + i // 2]
        out.append(byte >> 4 if i % 2 == 0 else byte & 15)
    return out


print(f"{len(glyphs)} glyphs, {len(data)} bytes of bitmap")
for size in sorted({g[2] for g in glyphs}):
    rows = [g for g in glyphs if g[2] == size]
    top_touch = bottom_touch = left_touch = right_touch = 0
    min_top, max_bottom = 99, -1
    worst = []
    for code, offset, _s, width, height in rows:
        px = nibbles(offset, width * height)
        ink = [(x, y) for y in range(height) for x in range(width) if px[y * width + x]]
        if not ink:
            continue
        xs = [p[0] for p in ink]
        ys = [p[1] for p in ink]
        t, b, l, r = min(ys), max(ys), min(xs), max(xs)
        min_top, max_bottom = min(min_top, t), max(max_bottom, b)
        if t == 0:
            top_touch += 1
            worst.append((chr(code), "top"))
        if b == height - 1:
            bottom_touch += 1
            worst.append((chr(code), "bottom"))
        if l == 0:
            left_touch += 1
        if r == width - 1:
            right_touch += 1
    print(f"\nsize {size}px  box height {rows[0][4]}  glyphs {len(rows)}")
    print(f"  ink rows used: {min_top}..{max_bottom}  (slack top {min_top}, "
          f"bottom {rows[0][4] - 1 - max_bottom})")
    print(f"  touching box edge: top {top_touch}, bottom {bottom_touch}, "
          f"left {left_touch}, right {right_touch}")
    if worst:
        shown = ", ".join(f"{c!r}:{side}" for c, side in worst[:14])
        print(f"  edge-touching samples: {shown}")
