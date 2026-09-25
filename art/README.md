# Flux artwork

Original artwork for Flux Tweaks, kept as source so it can be edited and re-rendered.

| Source | Output |
|---|---|
| `mascot.py` → `flux_happy.svg`, `flux_sleeping.svg` | `webui/public/flux_happy.avif`, `flux_sleeping.avif` (WebUI Home: daemon running / stopped) |
| `banner.py` → `banner.html` | `banner.webp` (module banner in Magisk / KernelSU / APatch, README) |
| `banner.py` → `icon.html` | `webui/public/icon.webp` (WebUI / shortcut icon) |

**Flux-chan**: silver-lavender bob with a cyan streak, gaming headphones, lightning hair clip and a
purple hoodie with the Flux bolt. Shapes and colours follow the Material 3 Expressive WebUI.

Re-render (development only; needs Playwright with Chromium and Pillow with AVIF):

```shell
python3 art/mascot.py && python3 art/banner.py
node art/render.js /tmp/flux-art
python3 art/encode.py /tmp/flux-art
```
