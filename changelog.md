# Flux Tweaks Changelog

## Unreleased

### New
- **Flux Sched**: uclamp-based scheduler prioritisation for games on modern (GKI / 5.x) kernels,
  where schedtune no longer exists. Raises the top-app capacity floor, caps background apps,
  and restores stock values and permissions when leaving a game. Toggle in Settings → Flux Sched.
- Thermal protection falls back to the PowerManager thermal level when the device reports no headroom
- SynthesisCore version handshake with an "outdated" warning in fluxd and the WebUI
- Binder transaction codes resolved once per boot (`binder_codes`) for future native queries
- **Updates in the root manager**: `updateJson` + release workflow; Magisk, KernelSU and APatch
  offer new releases with their changelog and download them directly
- Redesigned Telegram build notifications

### Fixed
- fluxd ignored SynthesisCore updates after startup (atomic rename raises IN_MOVED_TO)
- Uninstalling left Flux's symlinks in the KernelSU/APatch bin directories, its config and the
  boot cleanup hook behind (only Encore leftovers were removed)
- Boot waited up to 40 s after `sys.boot_completed` before starting Flux
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
