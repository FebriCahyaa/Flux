# Flux Tweaks Changelog

## Unreleased

### New
- **HiCo Thermal sync**: when HiCo is installed and not switched off, Flux leaves the thermal
  zone governors and MediaTek EARA thermal to HiCo, so HiCo's safety guard controls them and
  restores the exact stock values
- **Touch response on every device**: Android's InputReader and InputDispatcher threads join the
  game's top-app cgroups while gaming (restored afterwards). The panel's own game mode is still
  used on OPPO / realme / OnePlus, and now on Samsung panels whose driver lists `set_game_mode`
- Flux Boost pauses KSM page scanning during games; Chipset boost switches the kernel to per-CPU
  workqueues. Both are restored when leaving the game
- **Kernel-aware tuning**: Flux now tells GKI (Android 12+, Linux 5.10+), Non-GKI vendor and
  legacy (4.14 and older) kernels apart. On Linux 5.13+ the CFS tunables and sched features are
  written to their new debugfs location, so they apply on GKI 5.15 / 6.1 too
- **Surface boost** (all devices): SurfaceFlinger and the display composer HAL threads join the
  game's top-app cgroups (cpuset, schedtune or cpuctl) while gaming and go back afterwards
- **Chipset boost**: Qualcomm core_ctl (all cores online), sched_boost (conservative on WALT, HMP
  boost on older kernels), Adreno kgsl bus/rail/no-nap and ARM Mali always_on for the game
  session; not in Lite mode; every value is saved and restored
- The WebUI hides tweaks the device cannot use (touch panel, refresh rate, GPU governor, Flux
  Sched without uclamp…) and shows the detected kernel type
- **Game Tweaks** (Settings → Game Tweaks): network latency (BBR, ECN, TCP Fast Open), touch
  panel game mode (OPPO / realme / OnePlus), optional max refresh rate while gaming and memory
  cache drop at game start. Each one can be switched off; network values and the refresh rate are
  restored when the game exits
- **GPU Governor** (Settings → GPU Governor): pick the GPU devfreq governor (kgsl, Mali, g3d) for
  the balanced and powersave profiles, or keep the kernel default. Games keep the kernel's governor
- **Confirmations and notifications**: risky choices (Disable tweaks, Enforce Lite mode,
  performance CPU/GPU governor, max refresh rate, Trace logging) ask first with an info, warning
  or danger dialog; saved settings show a snackbar
- Redesigned Lite mode, Disable tweaks and CPU governor pages, and a more varied Settings list
  with new icons and an "On" badge for Lite mode and Disable tweaks
- **Game session stats**: play time, FPS (average, 1% low, drops, stability) and CPU / battery
  temperatures for every game session, recorded by fluxd once per second (paused while the
  screen is off). The Monitor shows the running session live with a 60-second FPS chart and
  keeps a history of the last 30 sessions, each with frame-rate and temperature charts and the
  time spent in Performance Lite
- **Monitor redesign**: live session card, session history and detail pages in the Material 3
  Expressive style
- **Flux Boost** (Settings → Flux Boost): memory headroom, storage latency and game thread
  priority while gaming, beyond the Encore profile. Each part can be switched off; values are only
  raised and the stock values are restored when the game exits
- **Material 3 Expressive WebUI**: spring motion throughout, shape-morphing loading indicator,
  expressive switches (icon handle that grows when pressed), new navigation bar, segmented lists
  that morph when pressed, redesigned Home with the active profile as a connected button group
- **64-bit and 32-bit builds**: `arm64`, `arm` and `universal` zips with their own update
  channels; the installer refuses a zip that does not match the ROM
- **Native system monitor** in fluxd (binder observers via libbinder_ndk, approach from Encore
  Tweaks): no always-on Java process (about 100 MB of RAM saved) and no watchdog; SynthesisCore
  only resolves binder codes at boot and remains as an automatic fallback
- **Flux Sched**: uclamp-based scheduler prioritisation for games on modern (GKI / 5.x) kernels,
  where schedtune no longer exists. Raises the top-app capacity floor, caps background apps,
  and restores stock values and permissions when leaving a game. Toggle in Settings → Flux Sched.
- Thermal protection falls back to the PowerManager thermal level when the device reports no headroom
- SynthesisCore version handshake with an "outdated" warning in fluxd and the WebUI
- Binder transaction codes resolved once per boot (`binder_codes`) for future native queries
- **Updates in the root manager**: `updateJson` + release workflow; Magisk, KernelSU and APatch
  offer new releases with their changelog and download them directly
- Redesigned Telegram build notifications
- Device report (`flux_utility report`, included in *Save log*): ROM, kernel features, vendor
  daemons and SynthesisCore capabilities, for bug reports and per-ROM tuning
- **HiCo Thermal integration**: the thermal add-on follows Flux's game profiles; uninstalling Flux
  restores its thermal changes, and the device report / *Save log* include HiCo's state and log

### Fixed
- Monitor: the profile and thermal history charts never drew a line (SVG polylines do not accept
  percentages). The profile history, a flat line most of the time, is removed; the thermal
  headroom chart now uses the shared chart component and only appears once it has samples
- A switched-off Flux feature could stay off after being turned back on: clearing the previous
  `FLUX_*` variables skipped every second one
- Uninstalling now restores the kernel values Flux changed during a game instead of only
  deleting the backups
- GKI profile wrote `io_uring_disabled=1` believing it enabled io_uring (it restricts it; Android
  disables io_uring on purpose) and `sched_cfs_bandwidth_slice_us=0` (below the kernel minimum);
  both writes are removed
- NOTICE.md was empty; Encore Tweaks and all third-party components are now credited
- fluxd ignored SynthesisCore updates after startup (atomic rename raises IN_MOVED_TO)
- Uninstalling left Flux's symlinks in the KernelSU/APatch bin directories, its config and the
  boot cleanup hook behind (only Encore leftovers were removed)
- Boot waited up to 40 s after `sys.boot_completed` before starting Flux
- Removed battery exemptions aimed at SynthesisCore's package: it runs via app_process and is
  never installed, so they had no effect; one of them appended to a MIUI/HyperOS setting on every
  boot, which is now cleaned up
- WebUI build dependencies updated (vite, rollup, postcss advisories)

## v1.0.0

### Initial Release
- Adaptive performance profiling
- Game auto-detection
- Battery-aware profile switching
- WebUI for configuration
- Support for Magisk, KernelSU, and APatch
- Multi-architecture support (arm64, arm, x86_64)
- SynthesisCore Prebuilt in
