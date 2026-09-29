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
| Refresh compatibility | Resolver produces a request; the daemon hands it to the profile script's `flux_refresh` (`FLUX_REFRESH_TARGET_HZ`). Script logic tested with fake `dumpsys`/`settings`; **not run on a device** |
| Zygisk backend | Contract + detection + spool file implemented and host-tested. **No Zygisk provider that reads the spool file ships with Flux**, so identity changes are not applied on a real device yet |
| Native backend | Refuses identity layers (cannot be process-scoped from the daemon) rather than rewrite anything system-wide |
| `fluxd compat_analyze <pkg> [mode]` | Implemented (read-only); compiled as a syntax check on the host, **device build not run** (no NDK here) |
| WebUI: Game Runtime section in Game Settings | Implemented, builds; **not exercised on a device** |
| Daemon lifecycle wiring (`begin` in `apply_game_profile`, `end` in `stop_session_workers`, journal replay at boot) | Implemented in `Main.cpp` via `GameRuntimeHost`; lifecycle logic host-tested (`session_test`); **daemon not run on a device, NDK build not run** |
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

## Daemon lifecycle (Phase 2)

Flux's own detection stays authoritative (SynthesisCore focus, `PIDTracker`, the 3-strike focus
check, `apply_game_profile`). The Game Runtime is called *from* those points and has no detector,
state machine or session recorder of its own.

```
boot:   lock held -> recover_at_boot()           replay + verify the journal, before any profile runs
start:  apply_game_profile()
          begin()                                 idempotent; resolves + applies the compatibility context
          Flux profile (performance / lite)       ALWAYS runs, whatever begin() reported
          after_profile_applied()                 per-game memory/touch/storage on top; re-asserted on
                                                  a later profile switch, snapshots never re-taken
end:    stop_session_workers(reason)              the single funnel every session end already used
          end()                                   restore + verify, then journal cleared
        select_profile() -> balance/powersave     Flux's own teardown, after the runtime restore
```

Ordering rationale: per-game overrides are applied *after* the profile script so they win over it,
and restored *before* Flux's teardown so the script's saved originals are what finally remain.

* `begin()` runs on every daemon wake; only the first call per `(package, pid)` activates. `end()`
  is idempotent. Another game, or the same game with a new PID, restores the previous session first
  (never two transactions). Reasons logged: `exit`, `process_death`, `focus_lost`, `switch`,
  `failure`, `daemon_stop`.
* A compatibility failure (no backend, apply/verify failed) is logged `performance_fallback=CONTINUE`
  and never blocks the Flux profile.
* A game with no entry in `game_profiles.json` resolves to mode `real` with every category
  `default`: it behaves exactly as before the Game Runtime existed.
* `disable_tweaks` disables the Game Runtime too.
* Journal: `compat_journal` (atomic rename), replaced after each transaction change. Recovery
  restores newest-first and only counts a line as restored after reading the node back. Lines that
  fail stay in the file (and are carried into later journal writes) so the next boot retries them.
  Status snapshot: `compat_status.json` (context, backend, `sustained` is always `unknown` here).
* Capability model: `service.sh` runs `fluxd capabilities` once per boot in a short-lived process.
  The daemon only reads the file and never probes drivers itself.
* Device mitigation: rules may opt a device out with `NO_GAME_MEMORY_OVERRIDE`,
  `NO_GAME_TOUCH_OVERRIDE`, `NO_GAME_STORAGE_OVERRIDE`; skips are logged.

### Refresh: one writer, explicit precedence

Three components touch `peak/min_refresh_rate`. They never run at once, by ownership rather than by
"last writer wins":

| Situation | Owner |
| --- | --- |
| Launcher / shade focused, no game | `RefreshHold` (holds the peak; releases when a game session starts) |
| Idle, no game | `flux_adaptive_refresh` (widens the range); yields while `/dev/.flux_refresh_orig` exists |
| Game session | `flux_refresh` in `flux_profiler.sh`, the only writer, marker `/dev/.flux_refresh_orig` |

Inside a game session the request is chosen in this order: the game's own `refresh` (`hz60/90/120`,
via `FLUX_REFRESH_TARGET_HZ`) > the global "highest refresh rate while gaming" option > nothing
(`real`/`adaptive` leave the panel to the adaptive logic). `flux_refresh` only uses a rate present
in the panel's real mode list (it normalises 119.99 to 120) and ignores anything else, so a 144 Hz
request on a 120 Hz panel changes nothing. The user's original values are restored when the session
ends. A refresh change is *not* an FPS unlock: `sustained` is only ever set from measured FPS.

`RefreshHold` itself is launcher-side only and was left unchanged; it cannot serve game-time
requests.

### Zygisk

The backend reports `Available` only when a Flux compatibility provider marker exists
(`/data/adb/modules/flux_compat_provider`) *and* the user opted in. A stock Zygisk module is not a
provider (it would never read the spool). None ships, so the daemon registers no Zygisk backend
and identity layers report `Unavailable`/`Failed`; identity is never shown as active.
