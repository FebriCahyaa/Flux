# Display & Rendering Intelligence (Step 8.5 — capability only)

Status: **IN PROGRESS.** Module `jni/display/DisplayIntelligence.*` (`flux::display`, NDK `FluxDisplay`,
host `flux_display`). fluxd publishes at start after the kernel and graphics steps (`jni/CapabilityHost.cpp`).
Device: NOT_TESTED.

## Scope and limits

Read-only awareness of what the panel and the compositor *can* do. It does not force refresh, boost
frames, inject into rendering, upscale, modify SurfaceFlinger, tune the GPU or implement graphics
policy. The existing refresh writer (`flux_refresh`, Step 4) and `RefreshMatcher` (B-12) are
unchanged, and they do not read these facts.

## Refresh capability ≠ FPS

`display.refresh.*` describe **panel modes**: which rates the panel supports and which mode is active.
They say nothing about how many frames a game renders. Actual FPS is measured by SessionRecorder
(`dumpsys SurfaceFlinger --latency`) and is **not** published as a capability. No fact id contains
`fps`, and each refresh fact's note says so.

## Evidence and confidence (HIGH / MEDIUM / LOW / UNKNOWN)

| Evidence | Tag | Confidence |
|---|---|---|
| `dumpsys display`, first built-in `DisplayDeviceInfo` (supported modes, `modeId`, `HdrCapabilities`) | `dumpsys` | High |
| `wm size` → `Physical size` | `wm` | High |
| `service list` (SurfaceFlinger, AIDL composer3) | `service_list` | High |
| Kernel DRM modes (`display.<connector>.modes` kernel facts), first mode | `kernel_drm` | Medium |
| Properties: `ro.surface_flinger.set_idle_timer_ms`, `…use_content_detection_for_refresh_rate`, `…has_HDR_display`, `ro.hardware.hwcomposer`, `debug.renderengine.backend` | `property` | Medium |
| Output that was collected but can't be parsed | — | Unknown, Low |
| Output not collected (command failed or was empty) | — | Unknown, None |

## Facts

Domain `display` (publisher `display`):

| Id | Meaning |
|---|---|
| `display.refresh.modes` | distinct whole-Hz panel refresh modes, e.g. `60 90 120` |
| `display.refresh.current_hz` | refresh of the active mode |
| `display.refresh.min_hz`, `display.refresh.max_hz` | range of the modes |
| `display.refresh.adaptive` | Yes/High when there are several rates at the active resolution; Yes/Medium when switching is declared by property; No/Medium for a single mode with nothing declared |
| `display.resolution` (+ `.dumpsys`, `.wm`, `.kernel_drm` when more than one source exists) | active resolution `WxH` |
| `display.hdr` (+ per-source facts) | supported HDR types (`dolby_vision hdr10 hlg hdr10_plus`); No when the list is empty |

Domain `rendering` (publisher `rendering`):

| Id | Meaning |
|---|---|
| `rendering.surfaceflinger` | SurfaceFlinger binder service registered |
| `rendering.composer` | `aidl:composer3` (service list) or `hal:<name>` (property) |
| `rendering.renderengine` | RenderEngine backend when declared |
| `rendering.frame_timing` | a frame-timing source exists (`surfaceflinger_latency`). Availability only |
| `rendering.pipeline` | graphics APIs from `graphics.interfaces`, plus the compositor backend |

## Rules

- Missing evidence gives **Unknown**, never No. No is used only for an observed absence: a single
  refresh mode, an empty HDR list, SurfaceFlinger missing from a collected service list, or the
  property `has_HDR_display=false`.
- Conflicts are resolved the same way as for graphics:
  - The highest confidence answers.
  - Equally confident sources that disagree give Unknown, with every claim listed in the note (for
    example `dumpsys` and `wm size` differing).
  - A weaker source that disagrees is recorded in the note (for example a kernel DRM first mode, or
    `has_HDR_display` against `HdrCapabilities`).
  - Per-source facts are published whenever more than one source exists.
- Display and rendering facts are `writable=false`, `verified=false` and risk Low.
- Both domains are computed first and then published, each as a snapshot. Other publishers are
  untouched.

## fluxd start sequence

kernel bootstrap → graphics → display/rendering. Each step fails soft: it writes one log line and
leaves its facts Unknown. Display runs `dumpsys display`, `wm size` and `service list` once at daemon
start, after `sys.boot_completed`, as already required by `service.sh`.

## Tests

`tests/display_intelligence_test.cpp` covers:
- parsing (the built-in display is chosen and overlays are ignored);
- display detection;
- rendering detection;
- missing interfaces: not collected, unparseable, or no SurfaceFlinger;
- unsupported features: single mode, no HDR, and adaptive declared by property;
- conflicting sources: dumpsys vs wm tie, a weaker DRM disagreement, HDR property vs dumpsys;
- publishing across three publishers with snapshot replace.
