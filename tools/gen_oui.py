#!/usr/bin/env python3
"""从 Wireshark manuf 生成可疑厂商 OUI 表 src/oui.c。

用法：python3 tools/gen_oui.py [manuf 文件路径]
不给路径时从 wireshark.org 下载。
"""
import re
import sys
import urllib.request
from pathlib import Path

MANUF_URL = "https://www.wireshark.org/download/automated/data/manuf"
OUT = Path(__file__).resolve().parent.parent / "src" / "oui.c"

# (显示名, 匹配 manuf 长名称的正则)
VENDORS = [
    ("Hikvision", r"Hikvision"),
    ("Ezviz", r"Ezviz"),
    ("Dahua", r"Dahua"),
    ("Uniview", r"Uniview"),
    ("Tuya", r"Tuya"),
    ("Espressif", r"Espressif"),
    ("Xiaomi", r"Xiaomi"),
    ("AltoBeam", r"AltoBeam"),
    ("Bilian", r"Bilian Electronic"),
    ("Bouffalo", r"Bouffalo"),
    ("Beken", r"^Beken"),
    ("Wyze", r"Wyze"),
    ("Arlo", r"^Arlo Technology"),
    ("Reolink", r"Reolink"),
    ("SigmaStar", r"Sigmastar"),
    ("Ingenic", r"Ingenic Semiconductor"),
]


def main():
    if len(sys.argv) > 1:
        text = Path(sys.argv[1]).read_text(encoding="utf-8")
    else:
        text = urllib.request.urlopen(MANUF_URL).read().decode("utf-8")

    entries = {}
    for line in text.splitlines():
        if line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 3:
            continue
        prefix = cols[0].strip()
        if len(prefix) != 8:  # 只要 24 位 MA-L，形如 AA:BB:CC
            continue
        for idx, (_, pattern) in enumerate(VENDORS):
            if re.search(pattern, cols[2], re.IGNORECASE):
                entries[int(prefix.replace(":", ""), 16)] = idx
                break

    lines = [
        "// 由 tools/gen_oui.py 从 Wireshark manuf 生成，不要手改。",
        "#include <stdlib.h>",
        "",
        '#include "oui.h"',
        "",
        "static const char *const vendors[] = {",
        *[f'    "{name}",' for name, _ in VENDORS],
        "};",
        "",
        "static const struct {",
        "    uint32_t oui;",
        "    uint8_t vendor;",
        "} table[] = {",
        *[f"    {{0x{oui:06X}, {idx}}}," for oui, idx in sorted(entries.items())],
        "};",
        "",
        "static int cmp(const void *key, const void *elem)",
        "{",
        "    uint32_t a = *(const uint32_t *)key, b = *(const uint32_t *)elem;",
        "    return a < b ? -1 : a > b;",
        "}",
        "",
        "const char *oui_lookup(const uint8_t mac[6])",
        "{",
        "    uint32_t key = (uint32_t)mac[0] << 16 | mac[1] << 8 | mac[2];",
        "    const void *hit = bsearch(&key, table, sizeof(table) / sizeof(table[0]), sizeof(table[0]), cmp);",
        "    return hit ? vendors[((const typeof(table[0]) *)hit)->vendor] : NULL;",
        "}",
        "",
    ]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"{OUT}: {len(entries)} entries")


if __name__ == "__main__":
    main()
