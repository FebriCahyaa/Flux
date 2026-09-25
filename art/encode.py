#!/usr/bin/env python3
"""Encodes the renders from art/render.js into the files the module ships.

    node art/render.js /tmp/flux-art && python3 art/encode.py /tmp/flux-art
"""
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
src = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "art" / "out")

TARGETS = [
    ("banner.png", ROOT / "banner.webp", "WEBP", {"quality": 90, "method": 6}),
]
for name, dst, fmt, opts in TARGETS:
    im = Image.open(src / name)
    im.save(dst, fmt, **opts)
    print(f"{dst.relative_to(ROOT)}: {im.size[0]}x{im.size[1]} {dst.stat().st_size // 1024} KB")
