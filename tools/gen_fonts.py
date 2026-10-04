#!/usr/bin/env python3
"""用 lv_font_conv 把像素字体转成 LVGL 1bpp 字体，字符集为 ASCII + GB2312 + 常用标点。

用法：python3 tools/gen_fonts.py <name> <ttf> <size> [<name> <ttf> <size> ...]
例：  python3 tools/gen_fonts.py fusion10 fusion-pixel-10px-proportional-zh_hans.ttf 10 ark12 ark-pixel-12px-proportional-zh_hans.ttf 12
输出 src/fonts/font_<name>.c，符号名 font_<name>。需要 npx lv_font_conv。
"""
import subprocess
import sys
from pathlib import Path

OUT_DIR = Path(__file__).resolve().parent.parent / "src" / "fonts"


def gb2312_chars():
    chars = []
    for hi in range(0xB0, 0xF8):  # 一级 + 二级汉字
        for lo in range(0xA1, 0xFF):
            try:
                chars.append(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return "".join(chars)


def main():
    args = sys.argv[1:]
    if not args or len(args) % 3:
        sys.exit(__doc__)
    symbols = gb2312_chars() + "。，、：；？！（）《》“”‘’—…·"
    OUT_DIR.mkdir(exist_ok=True)
    for name, ttf, size in zip(args[0::3], args[1::3], args[2::3]):
        out = OUT_DIR / f"font_{name}.c"
        subprocess.run(
            ["npx", "lv_font_conv", "--font", ttf, "--size", size, "--bpp", "1", "--format", "lvgl",
             "--no-compress", "--lv-include", "lvgl.h", "--lv-font-name", f"font_{name}",
             "-r", "0x20-0x7E", "--symbols", symbols, "-o", str(out)],
            check=True,
        )
        print(f"{out}: {out.stat().st_size // 1024} KB source")


if __name__ == "__main__":
    main()
