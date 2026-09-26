# Flux Tweaks Changelog

## Unreleased

### Fixed
- **Performance Lite never engaged on hot devices**: Android's thermal headroom grows with heat
  (1.0 = severe throttling) but was read as "headroom left", so Flux saw a phone at 96 °C as cool.
  fluxd and SynthesisCore now publish the headroom left. Lite also starts at thermal status
  "severe" or when the CPU averages 90 °C for 5 s (back below 82 °C), which still works while
  HiCo Thermal has the thermal HAL stopped. The Monitor's thermal ring reads correctly too
- Render threads on fast cores no longer moves the game's other threads off the little cores on
  two-cluster phones (4+4, 2+6): all of MLBB on the four big cores of a Redmi Note 13 Pro 5G ran
  at 96 °C. Phones with little / big / prime clusters keep it

### New
- **Render threads on fast cores** (Game tweaks, on): fluxd finds the game's render threads
  (Unity UnityMain / UnityGfxDevice, Unreal GameThread / RenderThread / RHIThread, GLThread, or
  the main thread of NativeActivity games) and pins them to the fastest cores with nice -15; on
  phones with little / big / prime clusters the game's other threads also leave the little cores. Clusters are
  read from `cpu_capacity` (or max frequency), so the same rule fits 4+4, 1+3+4, 2+6 and other
  layouts. Re-applied every 3 s for new threads; every thread gets its original affinity, nice
  and policy back when the game closes. Optional **realtime** mode adds SCHED_FIFO 15 (never in
  Lite). Stops by itself when SELinux denies it
- **GPU power lock** (opt-in): Adreno clock, bus and rail held on while gaming even with Stable clocks
- **Adreno Reflex** (opt-in): unsignaled-buffer latching, GL backpressure, no HWUI render-ahead,
  triple-buffered EGL; while gaming adrenoboost, ringbuffer-level preemption, dispatcher burst,
  context-aware DCVS and GPU counters off where the kernel has them. Off restores every value
- **Graphics pipeline** (opt-in): threaded RenderEngine where the ROM has no backend of its own,
  HWUI performance hints and HWC composition prediction
- **Adaptive refresh rate** (opt-in): removes a vendor pin of `min_refresh_rate` at the peak, so
  the panel can drop to its lowest mode (>= 60 Hz) on static content; frame rate override on,
  idle drop after 3 s, touch timer 200 ms. Games keep their own refresh handling
- **Zram sized to RAM** (opt-in): 3/4 of RAM up to 4 GB, half above, at most 6 GB, lz4;
  vendor writeback setups are left alone
- **I/O prefetch**: read-ahead of UFS / eMMC and the dm devices over them raised to 512 KB and
  I/O accounting off while gaming (Flux Boost)
- **CPU input boost**: msm `cpu_boost` and Sultan's `cpu_input_boost` get a mid-frequency touch
  boost while gaming, where the kernel has them
- **Device rules per chipset**, applied automatically: Snapdragon 888 / 8 Gen 1, Parrot
  (7s Gen 2, 6 Gen 1), Exynos 2100 / 2200 and Dimensity 9000 get no performance governor, and the
  Snapdragon ones no GPU power lock. The Device mitigation page lists the rules that match
- **Stable clocks** (Flux Boost, on by default): the performance profile no longer pins CPU, GPU
  and memory bus at their highest clock (min = max, performance governor) for the whole game.
  The highest clocks stay reachable, but the vendor governor moves them from a mid floor, and the
  Adreno / Mali power rails may power down between frames. Much less heat in long sessions, so
  thermal throttling starts later and FPS stays steadier. Switch it off to pin clocks as before
- **Refresh rate follows the game** (Game tweaks): starts at the panel's highest mode, then settles
  on the smallest mode that still shows every frame the game renders, measured from the game's
  own frames (e.g. 90 Hz for MLBB's 90 FPS on a 120 Hz panel); when the game hits the panel limit
  the next mode is tried once and kept only if the game uses it
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
- **Performance Lite never engaged in games**: the audio guard ran before the game profile and
  returned whenever audio played — always, in a game — so thermal pressure was never checked
  in-game (field report: 94 °C CPU, 0 s in Lite). The game tier is evaluated first again; the
  guard only holds the performance tier while no game is in focus, and logs that once
- **FPS read as 1–5 in lobbies**: the Snapdragon source counted frames sent to the panel, which
  drops when the ROM lowers the refresh rate on a still screen. Sessions now measure the game's
  own frames from SurfaceFlinger (`dumpsys SurfaceFlinger --latency` on the game's layer, run
  without a shell); the panel rate stays as the fallback
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
