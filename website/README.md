# Flux website

Static site for Flux Tweaks and the module store, deployed to GitHub Pages by
`.github/workflows/deploy_website.yml` on every push to `main` that touches `website/`.

## Adding a module to the store

Add an entry to `modules.json`; the page reads versions, download links and SHA-256
from the module's own update JSON (the one Magisk / KernelSU use), so releases never
need a website change.

| Field | Meaning |
|---|---|
| `id`, `name` | Identifier and display name |
| `kind` | `module` (flashable zip) or `component` (bundled, no download button) |
| `license` | `{ "type": "open" \| "private", "label": "Apache-2.0" }` |
| `icon` | `glyph` (bolt, thermostat, insights, chip, game, speed, display), `shape` (cookie9, cookie12, flower, clover4, sunny, burst, pentagon), `tone` (primary, secondary, tertiary) |
| `tagline`, `description` | `{ "en": "...", "id": "..." }` |
| `tags` | Short labels |
| `requires` | Other module ids |
| `repo`, `releases` | GitHub links (omit `repo` for private sources) |
| `update` | `{ "universal": url, "arm64": url, "arm": url }` update JSONs |
| `githubRelease` | `owner/repo`: version from the latest GitHub release instead |

Only GitHub links from fetched data are used; everything is rendered as text.

## Local preview

```sh
python3 -m http.server --directory website 8080
```
