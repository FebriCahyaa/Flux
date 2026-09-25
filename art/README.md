# Flux artwork

The module banner is kept as source so it can be edited and re-rendered.

| Source | Output |
|---|---|
| `banner.py` → `banner.html` | `banner.webp` (module banner in Magisk / KernelSU / APatch, README) |

The banner uses the Flux mascot from `webui/public/flux_happy.avif`; replace that file to change the
character everywhere (WebUI Home, banner after a re-render).

Re-render (development only; needs Playwright with Chromium and Pillow):

```shell
python3 art/banner.py
node art/render.js /tmp/flux-art
python3 art/encode.py /tmp/flux-art
```
