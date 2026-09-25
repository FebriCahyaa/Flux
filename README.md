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
- [Ecosystem: HiCo Thermal](#ecosystem-hico-thermal)
- [License](#license)

---

## How it works

Flux Tweaks is a **profile-style** module: instead of continuously steering frequencies, it applies
a coherent set of kernel and system tweaks whenever the device changes situation.

```
fluxd native monitor (binder)  ──status file──▶  fluxd (profiles)  ──▶  flux_profiler (shell)
  foreground app, screen,          inotify          picks a profile        writes sysfs / procfs
  power, thermal, audio,                            from game list,        (CPU, GPU, I/O, uclamp,
  battery, calls                                    thermal and config     touch, ...)
```

| Profile | When |
|---|---|
| **Performance** | A game from the game list is in the foreground |
| **Performance Lite** | A game runs but the device is hot, or lite mode is forced |
| **Balance** | Normal use |
| **Powersave** | Battery saver is on |

- `fluxd`'s **native monitor** talks to Android system services directly over binder (approach
  from Encore Tweaks): process and display observers for the foreground app and screen, plus light
  queries for power, thermal, audio and battery. No Java process stays running.
- [SynthesisCore](https://github.com/FebriCahyaa/SynthesisCore) runs once at boot to resolve the
  binder transaction codes of the current ROM, and takes over as a Java daemon only if the native
  monitor cannot start (or `/data/adb/.config/flux/force_java_monitor` exists).
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
- **Flux Boost** — game-time tuning beyond the Encore profile, each part switchable:
  memory headroom (earlier kswapd, larger dirty-page budget), storage latency (`rq_affinity`
  on UFS / eMMC / NVMe queues) and game priority (worker threads to nice −5 and top best-effort
  I/O class; UI / render threads Android already boosts stay as they are). Values are only
  raised, never lowered below the vendor setting; stock values are saved once per boot and
  restored when the game exits.
- **Device mitigation** — per-device rules that skip tweaks known to misbehave on some hardware.
- **Kernel awareness** — GKI and vendor kernels get different tweak sets to avoid unsafe nodes.
- **WebUI** — Material 3 Expressive: spring motion, shape-morphing loading indicator, expressive
  switches and navigation bar, segmented lists; live monitor, per-game settings, lite mode,
  Flux Boost, CPU governors, logs, 10 languages.
- **64-bit and 32-bit builds** — separate `arm64`, `arm` and `universal` zips (see Installation).

## Installation

1. Download the zip for your ROM from [Releases](https://github.com/FebriCahyaa/Flux/releases)
   (or a development build from the `Build Flux Tweaks` workflow artifacts):

   | Zip | For |
   |---|---|
   | `flux-*-arm64.zip` | 64-bit ROMs (arm64-v8a), including **64-bit-only** ROMs without a 32-bit userspace |
   | `flux-*-arm.zip` | 32-bit ROMs (armeabi-v7a) |
   | `flux-*-universal.zip` | Both; the installer picks the right `fluxd` |

   The installer shows the ROM's ABIs and refuses a zip that does not match, naming the right one.
2. Flash it in Magisk, KernelSU or APatch and reboot.
3. Open the module's WebUI to review the game list and settings.

The installer verifies the SHA-256 of every file it extracts and aborts on any mismatch.

### Updates from the root manager

`module.prop` points `updateJson` at [`update.json`](update.json) (universal),
[`update-arm64.json`](update-arm64.json) or [`update-arm.json`](update-arm.json), so each device
keeps receiving the build it installed. When a new release is
published, Magisk, KernelSU and APatch show **Update** on the module card, display the changelog
and download the zip directly from GitHub Releases. Pre-releases are not offered.

### Publishing a release

Run **Actions → Release → Run workflow** with a version such as `1.1.0` (or publish a release with
tag `v1.1.0`). The workflow builds the three zips with that version, attaches them with their
SHA-256 and a changelog generated from Conventional Commits, then commits the three update
channels and `update/changelog.md` so every installed module sees the update.

## Configuration

Settings live in `/data/adb/.config/flux/config.json` and are edited through the WebUI:

| Key | Default | Description |
|---|---|---|
| `preferences.enforce_lite_mode` | `false` | Always use Performance Lite for games |
| `preferences.use_device_mitigation` | `false` | Apply the default device mitigation rules |
| `preferences.disable_tweaks` | `false` | Keep the daemon but apply no tweaks |
| `preferences.flux_sched` | `true` | uclamp prioritisation while gaming |
| `preferences.flux_vm` | `true` | Flux Boost: memory headroom while gaming |
| `preferences.flux_io` | `true` | Flux Boost: block queue `rq_affinity` while gaming |
| `preferences.game_priority` | `true` | Flux Boost: game thread CPU / I/O priority |
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
Both CI builds and releases use the shared `.github/actions/build-module` action.

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

## Ecosystem: HiCo Thermal

[HiCo Thermal](https://github.com/FebriCahyaa/HiCo) is a Flux add-on that disables thermal
throttling only while a game runs, then restores the stock thermal stack for daily use, with a
CPU/battery temperature guard. It has no game detection of its own: it follows the
`current_profile` and `gameinfo` files fluxd writes on every profile change (Performance /
Performance Lite), so it requires Flux and refuses to install without it.

The work is split so the two never fight: Flux owns performance profiles (governors,
frequencies, GPU, scheduler); HiCo owns the thermal layer (thermal daemons, zone governors,
cooling devices, vendor thermal drivers). Uninstalling Flux restores HiCo's thermal changes, and
`flux_utility report` / *Save log* include HiCo's state, thermal zones and log.

Keep the format of `current_profile` (the `FluxProfileMode` value) and `gameinfo`
(`<package> <pid> <uid>` or `NULL 0 0`) stable: HiCo depends on it.

## Credits

Flux Tweaks is derived from [Encore Tweaks](https://github.com/Rem01Gaming/encore) by
**Rem01Gaming** (Apache License 2.0), which itself builds on KTweak by Tyler Nijmeh. The module
structure, daemon, profiler, WebUI and native binder monitor are based on Encore's source. Thank you!

## License

Flux Tweaks is licensed under the [Apache License 2.0](LICENSE). Encore Tweaks and the other
third-party components keep their own copyright notices, listed in [NOTICE.md](NOTICE.md).
