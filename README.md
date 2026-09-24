# Flux Tweaks

<p align="center">
  <img src="banner.webp" alt="Flux Tweaks" width="100%"/>
</p>

<p align="center">
  <b>Adaptive gaming and battery optimization module for rooted Android</b><br/>
  Magisk · KernelSU · APatch · arm64 / arm
</p>

- [How it works](#how-it-works)
- [Features](#features)
- [Installation](#installation)
- [Configuration](#configuration)
- [Project layout](#project-layout)
- [Building](#building)
- [SynthesisCore and supply-chain security](#synthesiscore-and-supply-chain-security)
- [License](#license)

---

## How it works

Flux Tweaks is a **profile-style** module: instead of continuously steering frequencies, it applies
a coherent set of kernel and system tweaks whenever the device changes situation.

```
SynthesisCore (app_process)  ──status file──▶  fluxd (native daemon)  ──▶  flux_profiler (shell)
  foreground app, screen,        inotify          picks a profile            writes sysfs / procfs
  power, thermal, audio,                          from game list,            (CPU, GPU, I/O, uclamp,
  battery, calls                                  thermal and config         touch, ...)
```

| Profile | When |
|---|---|
| **Performance** | A game from the game list is in the foreground |
| **Performance Lite** | A game runs but the device is hot, or lite mode is forced |
| **Balance** | Normal use |
| **Powersave** | Battery saver is on |

- [SynthesisCore](https://github.com/FebriCahyaa/SynthesisCore) observes the system through
  Android framework callbacks and publishes a small status file.
- `fluxd` (C++, [`jni/`](jni)) watches that file, the game list and the config with `inotify`, and
  decides the profile. It tracks the game's PID so the profile is dropped as soon as the game exits.
- `flux_profiler` ([`scripts/flux_profiler.sh`](scripts/flux_profiler.sh)) applies the profile with
  SoC-specific paths for Snapdragon, MediaTek, Exynos, Tensor, Unisoc and Tegra.

## Features

- **Automatic game detection** from a curated list of 500+ games, editable in the WebUI.
- **Thermal tiering** — drops to Performance Lite when thermal headroom runs low, or when the
  thermal status reaches *severe* on devices without headroom support, with debouncing.
- **Flux Sched** — uclamp scheduler prioritisation for the game on modern (GKI / 5.x) kernels,
  restoring stock values when the game exits.
- **Device mitigation** — per-device rules that skip tweaks known to misbehave on some hardware.
- **Kernel awareness** — GKI and vendor kernels get different tweak sets to avoid unsafe nodes.
- **WebUI** — live monitor, per-game settings, lite mode, CPU governors, logs, 10 languages.

## Installation

1. Download the latest `flux-*.zip` from the `Build Flux Tweaks` workflow artifacts.
2. Flash it in Magisk, KernelSU or APatch and reboot.
3. Open the module's WebUI to review the game list and settings.

The installer verifies the SHA-256 of every file it extracts and aborts on any mismatch.

## Configuration

Settings live in `/data/adb/.config/flux/config.json` and are edited through the WebUI:

| Key | Default | Description |
|---|---|---|
| `preferences.enforce_lite_mode` | `false` | Always use Performance Lite for games |
| `preferences.use_device_mitigation` | `false` | Apply the default device mitigation rules |
| `preferences.disable_tweaks` | `false` | Keep the daemon but apply no tweaks |
| `preferences.flux_sched` | `true` | uclamp prioritisation while gaming |
| `preferences.log_level` | `4` | 0 (off) – 5 (debug) |
| `cpu_governor.balance` / `.powersave` | device default | Governor per profile |

## Project layout

```
jni/              fluxd daemon (C++23, ndk-build)
scripts/          flux_profiler / flux_utility shell scripts
module/           module installer, service scripts, WebUI mount point
webui/            WebUI (Vue 3 + Vite, built with Bun)
config/           default device mitigation rules
prebuilt/         verified SynthesisCore APK (synced automatically)
gamelist.txt      default game list
.github/          build and sync workflows
```

## Building

The `Build Flux Tweaks` workflow builds the flashable zip on every push to `main` and on pull
requests. Locally:

```shell
git submodule update --init
ndk-build -j"$(nproc)"                       # NDK r29
(cd webui && bun install && bun run build)
```

`.github/scripts/compile_zip.sh` assembles the module; it expects to run in GitHub Actions.

## SynthesisCore and supply-chain security

`prebuilt/synthesiscore.apk` runs as root, so it is protected end to end:

1. **Sync** — the `Sync SynthesisCore` workflow fetches new releases and only opens a pull request
   when the APK's SHA-256 matches the published checksum, its signing certificate matches the
   pin committed in `prebuilt/synthesiscore.cert.sha256`, and its build provenance attestation
   verifies.
2. **Build** — the module build fails if the APK does not match its pinned checksum.
3. **Install** — the installer verifies the checksum of every extracted file.
4. **Boot** — `service.sh` re-checks the APK before every start and never runs a modified APK.

Setup and key rotation are described in [`prebuilt/README.md`](prebuilt/README.md).

## License

Flux Tweaks is licensed under the [Apache License 2.0](LICENSE). Third-party notices are listed in
[NOTICE.md](NOTICE.md).
