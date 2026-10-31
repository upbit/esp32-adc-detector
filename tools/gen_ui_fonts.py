#!/usr/bin/env python3
"""Generate the dashboard's small font subsets from LVGL's bundled OFL fonts.

Usage: python3 tools/gen_ui_fonts.py [path/to/lv_font_conv.js]
The generated C files are committed; firmware builds do not need Node.js.
"""

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
LVGL = ROOT / "managed_components" / "lvgl__lvgl"


def main():
    converter = ["node", sys.argv[1]] if len(sys.argv) == 2 else ["npx", "--no-install", "lv_font_conv"]
    bold = LVGL / "tests/src/test_files/fonts/Montserrat-Bold.ttf"
    chinese = LVGL / "scripts/built_in_font/SourceHanSansSC-Normal.otf"
    fonts = [
        ("text_16", chinese, 16, "充电中待机放池流压量趋势过去小时功率入输出读取失败以来"),
        ("numbers_36", bold, 36, "+-.0123456789"),
        ("soc_88", bold, 88, "0123456789"),
    ]
    for name, source, size, symbols in fonts:
        output = ROOT / "src/fonts" / f"font_ui_{name}.c"
        subprocess.run(
            converter + ["--font", str(source), "--size", str(size), "--bpp", "4",
                         "--format", "lvgl", "--no-compress", "--no-kerning",
                         "--lv-include", "lvgl.h", "--lv-font-name", f"font_ui_{name}",
                         "--symbols", symbols, "-o", str(output)]
                        + (["-r", "0x20-0x7E"] if name == "text_16" else []),
            check=True,
        )
        output.write_text(output.read_text().rstrip() + "\n")
        print(output.relative_to(ROOT))
    licenses = LVGL / "scripts/built_in_font/font_license"
    for family, filename in [("Montserrat", "OFL.txt"), ("SourceHanSansSC", "LICENSE.txt")]:
        text = (licenses / family / filename).read_text()
        output = ROOT / "docs/fonts" / f"OFL-{family}.txt"
        output.write_text("\n".join(line.rstrip() for line in text.splitlines()) + "\n")


if __name__ == "__main__":
    main()
