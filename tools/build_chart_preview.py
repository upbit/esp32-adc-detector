#!/usr/bin/env python3
"""Compile the exact firmware model/renderer and export inspectable simulation assets."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=Path("docs/preview"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
args.output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="chart-preview-") as temp:
    temp = Path(temp)
    binary = temp / "preview"
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
                    str(root / "src/starchart.c"), str(root / "src/starchart_render.c"),
                    str(root / "tools/chart_preview.c"), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(temp)], check=True)
    frames = [Image.open(path).convert("RGB") for path in sorted(temp.glob("frame-*.ppm"))]
    frames[20].save(args.output / "starchart-sim.png")
    frames[-1].save(args.output / "starchart-wait.png")
    frames[20].resize((960, 960), Image.Resampling.NEAREST).save(args.output / "starchart-sim-3x.png")
    frames[0].save(args.output / "starchart-sim.gif", save_all=True, append_images=frames[1:],
                   duration=500, loop=0, optimize=False)
    # Contact sheet of the text and state pages, so every case is reviewed at once.
    cases = sorted(temp.glob("case-*.ppm"))
    if cases:
        images = [(path.stem[5:], Image.open(path).convert("RGB")) for path in cases]
        columns = 5
        rows = (len(images) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * 320, rows * 320), (0, 0, 0))
        for index, (_name, image) in enumerate(images):
            sheet.paste(image, ((index % columns) * 320, (index // columns) * 320))
        sheet.save(args.output / "starchart-cases.png")
        for name, image in images:
            image.save(args.output / f"case-{name}.png")
        print("cases: " + ", ".join(name for name, _ in images))
print(f"Preview assets: {args.output}")
