# Naming Registry and Availability Evidence (Phase 1)

Status: **design targets, not cleared.** Nothing in this document renames any production
identifier, path, module id, package, binary or UI string (D-03, D-08).

## 1. Name map

| Legacy | Target | Scope of target | Legal status |
|---|---|---|---|
| Flux (Flux Tweaks) | **Zairenkai** | Ecosystem / platform, runtime, performance, graphics, kernel, compatibility, observatory | NOT CLEARED |
| SynthesisCore | **Aeyrin** | Context and capability intelligence layer | NOT CLEARED |
| HiCo Thermal | **Synrei** | Thermal intelligence and authority | NOT CLEARED (repo is proprietary, B-11) |

Philosophy: *Understand. Adapt. Sustain.* — *Unlock capability, not illusion.* —
*Nothing is optimized before it is understood.*

## 2. Availability evidence (collected 2026-09-29)

Method notes: web results via a US web search engine; registries queried directly over HTTPS;
domains checked with DNS (`dns.google` resolve, `NXDOMAIN` = no delegation, which strongly suggests
but does **not** prove the domain is unregistered). No trademark database was queried directly.

### Zairenkai
| Source | Query | Result |
|---|---|---|
| Web search | `"Zairenkai"` | No exact match. Near terms only: "Zazenkai" (Zen meditation retreat), "Zairen" (web fiction). |
| GitHub repositories | `zairenkai` | 0 repositories |
| GitHub user/org | `github.com/zairenkai` | not confirmed (HTTP 403 to unauthenticated probe) |
| Google Play | site search | no match |
| npm / PyPI / crates.io | `zairenkai` | 404 / 404 / 404 (unclaimed) |
| Domains | `.com .dev .app .id .io` | NXDOMAIN all |
| Trademarks (PDKI/DJKI, WIPO, USPTO, EUIPO) | — | **NOT SEARCHED — owner to confirm** |

Assessment: no collision found in software/app/package/domain evidence.

### Aeyrin
| Source | Query | Result |
|---|---|---|
| Web search | `"Aeyrin"` | **Existing uses:** musician/singer "Aeyrin" (Spotify, Discogs), DeviantArt artist, Instagram handles, fictional characters |
| GitHub user | `github.com/aeyrin` | **Exists** (user account, 2 public repos per search result) |
| GitHub repositories | `aeyrin` | 0 repositories named so |
| Google Play | site search | no match |
| npm / PyPI / crates.io | `aeyrin` | 404 / 404 / 404 |
| Domains | `.com .dev .app .id .io` | NXDOMAIN all |
| Trademark (web search) | `Aeyrin trademark` | No exact mark surfaced; similar marks exist (AYR, AERIN, company "Aeyron" in software). Not authoritative. |
| Trademarks (PDKI/DJKI, WIPO, USPTO, EUIPO) | — | **NOT SEARCHED — owner to confirm** |

Assessment: **COLLISION RECORDED (non-software)** — an active music artist uses the name, the
GitHub handle is taken, and a similar-sounding software company "Aeyron" exists. Per directive §1,
the public-brand stage for Aeyrin is stopped pending owner decision (B-13). Internal/design use may
continue.

### Synrei
| Source | Query | Result |
|---|---|---|
| Web search | `"Synrei"`, `Synrei software app android` | Game character names (WoW, FFXIV), a Freesound username, and a Khasi-language word ("mortar and pestle"). No software product. |
| GitHub repositories | `synrei` | 0 repositories |
| GitHub user/org | `github.com/synrei` | not confirmed (HTTP 403) |
| Google Play | site search | no match |
| npm / PyPI / crates.io | `synrei` | 404 / 404 / 404 |
| Domains | `.com .dev .app .id .io` | NXDOMAIN all |
| Trademarks (PDKI/DJKI, WIPO, USPTO, EUIPO) | — | **NOT SEARCHED — owner to confirm** |

Assessment: no software collision found; existing common-word meaning in Khasi noted (not a
conflict, but relevant to branding).

### Sources
- https://en.wikipedia.org/wiki/Zazenkai
- https://www.royalroad.com/fiction/3632/zairen
- https://open.spotify.com/artist/1lYNQXRJn2iG16jR6e7shy
- https://www.discogs.com/artist/11043443-Aeyrin
- https://www.deviantart.com/aeyrin
- https://github.com/aeyrin
- https://www.zoominfo.com/c/aeyron/546882274
- https://worldofwarcraft.com/en-us/character/us/illidan/synrei
- https://freesound.org/people/synrei/?downloaded_sounds=1
- https://en.wikipedia.org/wiki/Tungrymbai
- https://www.euipo.europa.eu/en/trade-marks/before-applying/availability

### Owner trademark checklist (not performed here)
PDKI/DJKI (pdki-indonesia.dgip.go.id), WIPO Global Brand Database, USPTO trademark search, EUIPO
eSearch — classes 9 (software) and 42 (software services), exact and phonetic variants
(`Zairenkai/Zairen`, `Aeyrin/Aeyron/Aerin`, `Synrei/Synre`).

## 3. Legacy identifier inventory (files referencing, case-insensitive, excluding HiCo datasets)

| Identifier | Flux | SynthesisCore | HiCo | Kind | Migration class |
|---|---|---|---|---|---|
| `flux` (any) | 142 | 6 | 38 | brand/code | staged |
| `Flux Tweaks` | 34 | 1 | 13 | brand string | brand (after clearance) |
| `fluxd` | 42 | 1 | 9 | binary name | alias required |
| `flux_profiler` / `flux_utility` | 14 / 13 | 0 / 0 | 1 / 0 | binaries on $PATH | alias required |
| `/data/adb/.config/flux` | 19 | 1 | 6 | runtime path | **frozen contract** (D-04) |
| `modules/flux` (module id `flux`) | 13 | 0 | 5 | module id | **frozen** — needs handover design |
| `SynthesisCore` | 59 | 39 | 1 | brand/code | staged |
| `com.febricahyaa.synthesiscore` | 4 | 30 | 0 | package + `MainKt` class | **frozen** (cert pin, service.sh) |
| `FluxSysMon` | 1 | 1 | 0 | process nice-name | alias |
| `HiCo` | 20 | 1 | 174 | brand/code | staged, proprietary |
| `hicod` | 5 | 0 | 58 | binary name | alias required |
| `/data/adb/.config/hico`, `modules/hico` | 2 / 3 | 0 | 8 / 7 | runtime path / module id | **frozen** |
| `update*.json`, `HiCo-Release` update URL | Flux root | — | module.prop | update channels | **frozen** until channel migration |

## 4. Compatibility-alias design (design only, Phase 13 implements)

Order per directive §49: legacy → alias → new implementation → migration → deprecation → removal.

1. **Brand strings** (README, WebUI titles, module `name=`): change only after clearance; keep
   "formerly Flux Tweaks" wording for one release generation.
2. **Binaries**: ship new names as symlinks to the existing binaries first (`zairenkaid → fluxd`);
   `fluxd` remains the real file until all callers (service.sh, HiCo, WebUI) move.
3. **Runtime paths**: `/data/adb/.config/flux` stays authoritative; new telemetry may start under
   `/data/adb/.config/zairenkai/telemetry/` (Phase 5). Config migration = copy + verify + marker,
   never move/delete.
4. **Module id**: unchanged. A new id would orphan installs and update channels; requires a
   separate handover design (installer detects `flux`, migrates, keeps `flux` update channel).
5. **Android package / class**: unchanged (`com.febricahyaa.synthesiscore.MainKt`); rename would
   change the signing/cert pin chain and service.sh invocation.
6. **Frozen HiCo contracts** (`current_profile`, `gameinfo`, `.lock`, `synthesis_core.json`):
   unchanged; successors published alongside.

## 5. Build identity and stellar codename registry (design)

Format: `ZAIRENKAI-G<nn>-<CODENAME>-<semver>`, e.g. `ZAIRENKAI-G02-VEGA-2.0.0`.

- **Generation** (`G01`, `G02`, …) = software architecture generation. Flux 1.x line = **G01**
  (retroactive label, no history rewrite). First Zairenkai architecture release = **G02**.
- **Codename** = identifier only; implies nothing about performance. Not astronomical population
  terminology.
- **Semver** continues from current `1.4.1`; no reset. Whether G02 starts at `2.0.0` is decided at
  release (Phase 15).

| Codename | Star | Status |
|---|---|---|
| VEGA | α Lyrae | reserved for G02 |
| RIGEL | β Orionis | available |
| ALTAIR | α Aquilae | available |
| SIRIUS | α Canis Majoris | available |
| DENEB | α Cygni | available |
| ARCTURUS | α Boötis | available |

Build metadata to expose (Phase 15 implementation): ecosystem version, generation, codename, git
commit, build timestamp, ABI, flavor, kernel-compatibility metadata — via `fluxd --version`,
`module.prop` (description only; `version=` format kept for root managers), WebUI About.
