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
print(f"Preview assets: {args.output}")
