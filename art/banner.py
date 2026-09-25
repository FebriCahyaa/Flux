#!/usr/bin/env python3
"""Writes art/banner.html, the 1280x640 module banner (render with art/render.js).

Material 3 Expressive: expressive shapes (same polar curves as the WebUI),
emphasized Google Sans Flex headline, the Flux mascot (webui/public/flux_happy.avif) in a cookie shape.

    python3 art/banner.py
"""
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
FONT = "../webui/src/assets/fonts/googlesansflex-latin.woff2"  # relative to art/


def shape(fn, points=72):
    rs = [fn(2 * math.pi * i / points) for i in range(points)]
    m = max(rs)
    pts = []
    for i, r in enumerate(rs):
        a = 2 * math.pi * i / points - math.pi / 2
        pts.append(f"{50 + 50 * r / m * math.cos(a):.2f}% {50 + 50 * r / m * math.sin(a):.2f}%")
    return "polygon(" + ", ".join(pts) + ")"


cookie12 = shape(lambda t: 1 + 0.045 * math.cos(12 * t))
burst = shape(lambda t: 1 + 0.16 * math.cos(12 * t))
clover = shape(lambda t: 1 + 0.22 * abs(math.cos(2 * t)) ** 0.7)
flower = shape(lambda t: 1 + 0.18 * math.cos(6 * t) ** 3 + 0.04)

html = f"""<!doctype html>
<html><head><meta charset="utf-8"><style>
@font-face {{ font-family: 'Google Sans Flex'; src: url('{FONT}') format('woff2'); font-weight: 1 1000; }}
* {{ margin: 0; box-sizing: border-box; }}
body {{ width: 1280px; height: 640px; overflow: hidden; font-family: 'Google Sans Flex', sans-serif;
  background: radial-gradient(120% 140% at 85% 20%, #4a36c9 0%, #2a1d7a 45%, #150f3d 100%); color: #f3efff; }}
.shape {{ position: absolute; }}
.s1 {{ width: 620px; height: 620px; right: -170px; top: -120px; clip-path: {flower}; background: #6a4cf5; opacity: .35; transform: rotate(12deg); }}
.s2 {{ width: 300px; height: 300px; left: -110px; bottom: -130px; clip-path: {burst}; background: #5fd6e6; opacity: .22; }}
.s3 {{ width: 120px; height: 120px; left: 600px; top: 60px; clip-path: {clover}; background: #ffd166; opacity: .9; transform: rotate(20deg); }}
.s4 {{ width: 54px; height: 54px; left: 690px; top: 470px; clip-path: {burst}; background: #ff8fb1; }}
.mascot {{ position: absolute; right: 90px; top: 70px; width: 470px; height: 470px; clip-path: {cookie12};
  background: linear-gradient(160deg, #e9e3ff, #b9adff); }}
.mascot img {{ position: absolute; width: 440px; left: 15px; top: 30px; }}
.content {{ position: absolute; left: 88px; top: 108px; width: 620px; }}
.by {{ display: inline-flex; align-items: center; gap: 10px; padding: 8px 18px 8px 10px; border-radius: 999px;
  background: rgba(255,255,255,.12); font-size: 22px; font-weight: 600; letter-spacing: .01em; }}
.by i {{ width: 26px; height: 26px; border-radius: 999px; background: #5fd6e6; display: inline-block; }}
h1 {{ font-size: 124px; line-height: .92; font-weight: 800; letter-spacing: -.035em; margin-top: 30px; font-stretch: 92%; }}
h1 span {{ color: #b9adff; }}
p {{ font-size: 36px; font-weight: 500; margin-top: 22px; color: #d9d2ff; }}
.chips {{ display: flex; gap: 12px; margin-top: 40px; }}
.chip {{ padding: 12px 22px; border-radius: 999px; font-size: 22px; font-weight: 650; }}
.c1 {{ background: #5fd6e6; color: #0c2f35; }}
.c2 {{ background: #f3efff; color: #2a1d7a; }}
.c3 {{ background: rgba(255,255,255,.14); color: #f3efff; border-radius: 16px; }}
.foot {{ position: absolute; left: 88px; bottom: 34px; font-size: 18px; color: rgba(243,239,255,.55); font-weight: 500; }}
</style></head><body>
<div class="shape s1"></div><div class="shape s2"></div><div class="shape s3"></div><div class="shape s4"></div>
<div class="mascot"><img src="../webui/public/flux_happy.avif" alt=""></div>
<div class="content">
  <span class="by"><i></i>FebriCahyaa</span>
  <h1>Flux<br><span>Tweaks</span></h1>
  <p>Performance, evolved.</p>
  <div class="chips"><span class="chip c1">Flux Boost</span><span class="chip c2">Game-aware</span><span class="chip c3">arm64 · arm</span></div>
</div>
<div class="foot">© SynthesisCore Project</div>
</body></html>
"""
(HERE / "banner.html").write_text(html)

print("wrote", HERE / "banner.html")
