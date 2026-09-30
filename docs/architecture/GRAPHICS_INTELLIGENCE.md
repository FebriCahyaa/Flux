# Graphics Intelligence (Step 8 — capability only)

Status: **IN PROGRESS.** Module `jni/graphics/GraphicsIntelligence.*` (`flux::graphics`, NDK `FluxGraphics`,
host `flux_graphics`). fluxd publishes graphics facts at start right after the kernel bootstrap
(`jni/CapabilityHost.cpp`). Device: NOT_TESTED.

## Capability vs policy

| Layer | Where | This step |
|---|---|---|
| **GPU capability**: what the device has | `flux::graphics::observe()` → CapabilityContext domain `graphics` | implemented |
| **GPU policy**: what to do with it (frame pacing, refresh strategy, render scaling) | a future Graphics Policy that only *reads* the context | not implemented |

This module never decides anything and never writes anything. There is no GPU tuning or overclock, no
driver change, no render-library injection, no upscaling, no thermal logic and no WebUI.

## Evidence and confidence

Confidence levels are HIGH, MEDIUM, LOW and UNKNOWN (`Confidence::None`, printed as `unknown`).

| Evidence | Source tag | Confidence |
|---|---|---|
| Vulkan instance probe (`flux::gfx::probe_vulkan`, the CLI path). Passed in, **never created in fluxd** | `vulkan` | High |
| KGSL sysfs with an `Adreno…` `gpu_model` | `sysfs` | High (Medium without a model) |
| Mali sysfs (`sys/class/misc/mali0`, `device/gpuinfo`) | `sysfs` | Medium |
| Vendor Vulkan driver file `vendor/lib{64,}/hw/vulkan.*.so` | `driver_file` | Medium |
| Properties `ro.opengles.version`, `ro.hardware.egl`, `ro.gfx.driver.0` | `property` | Medium |
| `ro.hardware.vulkan` without a driver file; EGL HAL name as a vendor hint (`adreno`, `mali`, `powervr` only) | `property` / `egl` | Low |
| GPU frequency / load nodes: **read from the kernel's facts** (domain `gpu`), not re-probed | `kernel` | High when a readable node exists; No/Medium when the kernel found none |

## Facts (domain `graphics`, publisher `graphics`)

| Id | Meaning |
|---|---|
| `graphics.gpu.vendor` | resolved vendor (`qualcomm`, `arm`, `imagination`, …) |
| `graphics.gpu.vendor.<source>` | each source's own claim, kept for audit |
| `graphics.gpu.model`, `graphics.gpu.model.<source>` | model name; the resolved one comes from the highest-priority source that agrees with the vendor (vulkan, then sysfs) |
| `graphics.driver.vulkan_version` | raw `driverVersion` (instance probe only; the encoding is vendor-specific) |
| `graphics.driver.updatable` | updatable driver package (`ro.gfx.driver.0`) |
| `graphics.vulkan.available`, `graphics.vulkan.api_version` | Vulkan presence and API version |
| `graphics.opengles.version` | e.g. `3.2` from `ro.opengles.version` |
| `graphics.egl.driver` | EGL/GLES HAL name |
| `graphics.interfaces` | APIs with Yes: `vulkan opengles egl` |
| `graphics.gpu.freq_interface`, `graphics.gpu.load_interface` | kernel capability ids that expose GPU frequency / load |

All graphics facts have `writable=false`, `verified=false` and risk Low.

## Rules

- **Missing evidence → Unknown**, never No. No is used only for an observed absence: a Vulkan probe
  that returned `absent` or `no_device`, the kernel's GPU facts containing no readable frequency or
  load node, or `ro.gfx.driver.0` unset.
- **Vulkan unavailable** comes only from an instance probe (High). Without a probe, Vulkan is Yes at
  Medium (driver file seen), Yes at Low (property only), or Unknown. API version and driver version
  need a probe; otherwise they are Unknown.
- **Conflicting GPU sources:**
  - If the most confident sources disagree on the vendor, `graphics.gpu.vendor` and
    `graphics.gpu.model` are Unknown, and the note lists every claim.
  - If only a weaker source disagrees, the stronger source answers and the note records the
    conflict.
  - Per-source facts are always published.
  - An unrecognised EGL name (for example `meow`) is not vendor evidence.
- Publishing replaces the `graphics` snapshot, and kernel facts are untouched. Across publishers, the
  normal context rules apply (`CAPABILITY_MODEL.md`).

## Why no Vulkan instance in fluxd

Creating an instance loads the vendor driver into a long-lived root daemon. The CLI already does this
on demand (`FluxCLI.cpp`, `VulkanCollector`). Feeding that result into the context is future work, and
until then fluxd reports Vulkan from declarative evidence at Medium or Low confidence.

## Tests

`tests/graphics_intelligence_test.cpp` covers:
- GPU detection: Vulkan, KGSL, properties and kernel facts together;
- missing GPU interfaces: everything Unknown, or No when the kernel saw no GPU nodes;
- Vulkan unavailable: `absent`/`no_device` give No, declarative-only evidence gives Medium or Low,
  and nothing at all gives Unknown;
- conflicting GPU sources: an equal-confidence tie gives Unknown; a weaker disagreement is noted;
  an unknown EGL name is ignored;
- publishing: two publishers, snapshot replace, no duplicates;
- `ro.opengles.version` parsing.

## Relation to display & rendering (Step 8.5)

Panel refresh, resolution, HDR, compositor and frame-timing availability are covered in
`DISPLAY_INTELLIGENCE.md` (domains `display` and `rendering`). `rendering.pipeline` reads
`graphics.interfaces` from this module. GPU capability, display capability and rendering capability
are three separate publishers, and none of them contains policy.
