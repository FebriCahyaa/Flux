# Flux Tweaks Changelog

## Unreleased

### New
- **Flux Sched**: uclamp-based scheduler prioritisation for games on modern (GKI / 5.x) kernels,
  where schedtune no longer exists. Raises the top-app capacity floor, caps background apps,
  and restores stock values and permissions when leaving a game. Toggle in Settings → Flux Sched.
- Thermal protection falls back to the PowerManager thermal level when the device reports no headroom
- SynthesisCore version handshake with an "outdated" warning in fluxd and the WebUI
- Binder transaction codes resolved once per boot (`binder_codes`) for future native queries

### Fixed
- fluxd ignored SynthesisCore updates after startup (atomic rename raises IN_MOVED_TO)

## v1.0.0

### Initial Release
- Adaptive performance profiling
- Game auto-detection
- Battery-aware profile switching
- WebUI for configuration
- Support for Magisk, KernelSU, and APatch
- Multi-architecture support (arm64, arm, x86_64)
- SynthesisCore Prebuilt in
