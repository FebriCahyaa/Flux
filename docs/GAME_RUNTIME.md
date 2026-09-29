# Flux Game Runtime

Flux Compatibility Engine (FCE) lives in `jni/compat/` and is a pure-logic library behind
injectable seams (`Io`, `RealHardware`). The daemon links it; the host tests in
`tests/compat_test.cpp` run the same code without a device.

```
analyze -> requirement vs real hardware -> gap -> minimum override -> plan
        -> transaction (snapshot, apply, verify) -> game runs -> restore, verify restore
```

## Status of each part

| Area | State |
| --- | --- |
| Capability model → `RealHardware` | Implemented, host-tested (`hardware_from_model`) |
| Profile inheritance (global → preset → game → runtime), validation | Implemented, host-tested |
| Universal resolver (known db + generic analysis, minimum override, honest blockers) | Implemented, host-tested |
| Unlocked / Capable / Sustained as three separate answers | Implemented (`sustained` is set from live FPS, never by the resolver) |
| Runtime transactions (rollback, restore, verify, journal, crash recovery) | Implemented, host-tested |
| Memory / storage / touch (input boost) / launch boost planning | Implemented as node writes, host-tested against a fake filesystem; **not run on a device** |
| Refresh compatibility | Resolver produces a request (`refresh_request_hz`); **not yet wired to `RefreshHold`** |
| Zygisk backend | Contract + detection + spool file implemented and host-tested. **No Zygisk provider that reads the spool file ships with Flux**, so identity changes are not applied on a real device yet |
| Native backend | Refuses identity layers (cannot be process-scoped from the daemon) rather than rewrite anything system-wide |
| `fluxd compat_analyze <pkg> [mode]` | Implemented (read-only); compiled as a syntax check on the host, **device build not run** (no NDK here) |
| WebUI: Game Runtime section in Game Settings | Implemented, builds; **not exercised on a device** |
| Daemon hook (`GameRuntime::activate` on foreground game, `deactivate` on exit, journal replay at boot) | **Designed, not wired into `Main.cpp`** |
| Per-thread touch scheduling (InputReader/Dispatcher priorities) | **Designed, not implemented**; touch profiles currently only set the cpu_boost input-boost node when present |
| ZRAM size/algorithm per game | **Not implemented**; `zram_tune` keeps owning ZRAM |
| CPU/GPU identity beyond string fields | Identity profiles cannot carry ISA features or Vulkan extensions (rejected at load) |

## Documents (all under `/data/adb/.config/flux/`, all optional)

- `game_profiles.json` – `{ "<package>": { "performance": {...}, "compatibility": {...} } }`
- `compat_library.json` – `{ "presets": {...}, "identities": { "<name>": { "layer": "device|cpu|gpu", "fields": {...} } } }`
- `compat_games.json` – curated requirements: `{ "games": { "<package>": { "target_fps": 120, "confidence": "high", "gates": [ { "kind": "device_identity", "identity": "<name>" } ] } } }`
- `compat_zygisk_optin` – presence file: the user opted in to the Zygisk backend

Flux ships no identity values and no curated game requirements: an identity is only ever what the
user (or a maintainer with a verified source) puts in the library. Gate kinds: `none`,
`unknown`, `device_identity`, `cpu_identity`, `gpu_identity`, `display_refresh`,
`server_controlled`, `entitlement`, `hardware_insufficient`.

### Profile fields

```json
{
  "package": "com.example.game",
  "extends": "competitive",
  "performance": { "profile": "performance", "memory": "gaming_plus", "touch": "responsive_plus",
                   "storage": "gaming", "refresh": "hz120", "launch_boost": true },
  "compatibility": { "mode": "auto", "device_profile": "real_device", "cpu_profile": "real_cpu",
                     "gpu_profile": "real_gpu", "display_profile": "real_display" }
}
```

Every field is optional and inherits. `default` for memory/touch/storage means *leave what Flux
already does alone*; nothing is written.

## Rules the engine enforces (and tests)

- **Minimum override**: only layers a gate names are in the plan. No gate → nothing is spoofed.
- **Unknown game**: no evidence, no override; the analysis reports the real capabilities and
  recommends creating a profile.
- **Auto never applies an unlock the real hardware cannot back** (e.g. 120 Hz target on a 60 Hz
  panel). Explicit modes apply, with a warning.
- **Identity is not capability**: identity profiles that list CPU ISA features or GPU
  extensions/features are rejected.
- **Nothing system-wide**: the native backend refuses identity layers; the Zygisk backend writes
  one spool file per package and only for a validated package name.
- **Unavailable nodes are never written**; categories the device mitigation forbids are skipped and
  logged (`skipped by device mitigation`).
- **Failure isolation**: a compatibility backend failure never removes the performance profile.
- **Crash recovery**: `Watchdog::recover` replays the journal (absolute paths only).

## Not an anti-cheat bypass

Process-resident compatibility hooks are visible to a game. Flux makes no claim that they are
undetectable, ban-proof or anti-cheat safe, and implements no concealment.

## COPG

[COPG](https://github.com/AlirezaParsi/COPG) was studied only as a reference for concepts
(per-app device/CPU/GPU profiles, refresh compatibility, a reusable profile library, no-reboot
management). No COPG source code is used in Flux, so no Apache-2.0 attribution obligation arises
from it. Its unrelated features (IMEI/SIM/GPS/Android ID spoofing, DRM or proxy hiding,
anti-detection) are intentionally absent.
