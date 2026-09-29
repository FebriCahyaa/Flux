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
| Zygisk provider (`jni/zygisk`) | Implemented: module, root companion, GOT patcher, wrappers. Host tested (plan contract, GOT patcher on real ELF fixtures, companion over a socketpair, wrappers with fake drivers). **Built by ndk-build only in CI; never run on a device** |
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

## Zygisk provider (Phase 4)

`jni/zygisk` is an **execution backend** for the Compatibility Engine. It never decides an override:
it applies the plan the resolver produced, to the game's own process, and reports what happened.

```
fluxd  --arm-->  plans/<package>.json + armed.list      (before the game is launched)
                          |
   game process is created (zygote fork)
                          |
   module, preAppSpecialize:  read armed.list through the module dir fd (one small read)
        not a candidate  ->  unload this library (DLCLOSE_MODULE_LIBRARY): nothing of Flux stays in the process
        candidate        ->  ask the root companion
   companion (root):  UID -> owning package (packages.list), plan, validation, process scope
        not a target     ->  unload, no status file
        target           ->  Decision {layers, concrete items}
   module, postAppSpecialize (app sandbox):  apply, verify, report
   companion:  proc/<pid>.json  (matched -> applied -> verified | failed -> ended)
                          |
fluxd, game detected:  ZygiskBackend.verify()  reads proc/<pid>.json for THIS pid and THIS transaction
```

Public API only: `jni/zygisk/include/zygisk.hpp` is topjohnwu/zygisk-module-sample's header,
API v5, vendored unmodified (see NOTICE.md). No Magisk internals, no `system_server` code
(`preServerSpecialize` is not implemented).

### Off by default, and how to switch it off by hand

The provider libraries ship in `<module>/zygisk_provider/`, which Zygisk never looks at, so a default
install injects nothing anywhere. The WebUI toggle *Use Zygisk backend* (or
`flux_utility provider enable`) copies them to `<module>/zygisk/<abi>.so` and needs a reboot; turning it
off removes that folder (`flux_utility provider disable`, reboot). A module update starts switched off again.

If anything misbehaves after enabling it, from a root shell / `adb` / a terminal app:

```
flux_utility provider off        # kill switch: touch /data/adb/modules/flux/no_provider (inert immediately for new processes)
rm -rf /data/adb/modules/flux/zygisk   # or: flux_utility provider disable, then reboot: nothing is injected any more
touch /data/adb/modules/flux/disable   # or disable the whole Flux module in the root manager, then reboot
```

If the device will not boot far enough for that, boot to safe mode (KernelSU: hold Volume Down at the logo) or
remove the module from recovery.

### Why plans are armed *before* launch

A process gets its identity when it is created, but Flux only sees a game once it is in the
foreground, long after. So the daemon arms a plan per profiled package while it runs (at start,
after every session and, through `fluxd compat_arm`, after each WebUI edit and before the WebUI's
Launch button) and disarms everything when it stops. An armed plan is **configuration, not applied
state**: nothing changes until a matching process is created, and the process-local context ends
with that process. A game that was already running when its profile was armed is reported as
*"started before its profile was armed; relaunch the game"*, never as active.

A plan left behind by a crashed daemon is inert by itself: the provider requires the plan's boot id
to equal the current boot and its `daemon_pid` to be a live process named `fluxd`.

### Spool contract, v2 (`compat_provider/plans/<package>.json`)

```json
{ "version": 2, "active": true, "package": "com.example.game",
  "transaction_id": "<boot6>-<fnv64 of package|boot|mode|scope|layers|values>",
  "boot_id": "...", "daemon_pid": 1234, "armed_at_ms": 0, "expires_at_ms": 0,
  "profile": "...", "mode": "advanced",
  "process_scope": { "mode": "main|listed|all", "processes": ["com.example.game:engine"] },
  "layers": ["device", "gpu"],
  "identities": { "device": { "MODEL": "..." }, "gpu": { "gl_renderer": "..." } } }
```

`active`, `package` and `identities` are the fields the v1 spool already had. `layers` is exactly
what the resolver required (CPU is off unless the plan lists it); the display layer never reaches the
provider (refresh has one writer, see above). The transaction id changes whenever anything in the plan
changes, so a process holding an older plan is recognisably stale. Lease: 24 h, renewed on every
re-arm; boot id and daemon liveness are the binding checks.

Also in `compat_provider/`: `provider.json` (written when the companion starts: proves the module
really loaded in this boot), `proc/<pid>.json` (per-process status written by the companion).
`<module>/armed.list` holds `package<TAB>app_id<TAB>transaction` lines for the in-process pre-filter.

### Process scoping

* The package must be the **UID's owner**, resolved by the companion from `packages.list`; a process that
  merely has a similar name is refused. Secondary Android users match by app id.
* `main` (default): only the process named exactly like the package. `listed`: exactly the names in
  `compatibility.processes`. `all`: the package's own `:sub` processes too. `com.x:remote`, `:engine`,
  `:service` are therefore untouched unless the profile says so. An empty `listed` scope is invalid.
* Non-target apps: one small read, then the library unloads itself. No companion round trip unless the
  app's UID or name is on `armed.list`. If `armed.list` (or the module dir) cannot be read, the module
  does nothing: it never guesses and never asks the companion on speculation.
* Only ordinary app processes (app id 10000–19999) are considered. App/WebView zygotes, isolated
  (sandboxed renderer) processes and system UIDs are left completely alone, with no file read and no IPC.
  IPC to the companion has a 1 s timeout so an app launch can never wait on it.

### Supported identity fields (anything else is refused, not dropped)

| Layer | Fields | Mechanism |
| --- | --- | --- |
| device | `BRAND MANUFACTURER MODEL DEVICE PRODUCT FINGERPRINT` | `android.os.Build` static fields via JNI, read back; the matching `ro.product.*`/`ro.build.fingerprint` for **native** `__system_property_get` calls from app libraries |
| cpu | `SOC_MODEL SOC_MANUFACTURER HARDWARE BOARD` | same (`Build.SOC_*` needs API 31: absent field is reported `unsupported`, not faked) |
| gpu | `gl_vendor gl_renderer gl_version egl_vendor vk_device_name vk_vendor_id vk_device_id vk_api_version vk_driver_version` | GOT patch of `glGetString`, `eglQueryString`, `vkGetPhysicalDeviceProperties(2/2KHR)`, `vkGetInstanceProcAddr` |

Not touched: `/proc/cpuinfo`, `getprop` from a shell, `SystemProperties` inside framework libraries
(system libraries are deliberately never patched for properties: the GL loader reads `ro.hardware`),
Build.VERSION, IMEI/Android ID and everything else on the "do not import" list.

### Identity is not capability

* A CPU identity that lists ISA features (`hwcap`, `neon`, `sve`, ...), or a GPU identity that lists Vulkan
  extensions, features, limits or queue families, is rejected when the library loads **and again** by the
  provider. The rest of the plan still applies; the refused layer is reported `unsupported`.
* Vulkan `apiVersion` can be reported **lower** than the real one, never higher (a request above the real
  value is clamped to real and reported). Features, extensions, limits, formats and queue families are never
  modified.
* GL: `GL_EXTENSIONS` and all other GL state are untouched. A hook is only installed for a value the
  plan carries.

### Late-loaded libraries

The public Zygisk API (including `pltHookRegister`) stops working after `postAppSpecialize`, but engines load
`libunity.so` and friends afterwards. So `GotHook.cpp` patches import slots itself (JUMP_SLOT/GLOB_DAT only,
page protection restored exactly) and the module's `dlopen`/`android_dlopen_ext` wrappers scan newly loaded
objects after each load: event driven, no thread, no polling. Libraries using packed relocations
(`DT_ANDROID_REL*`, `DT_RELR`) are counted and reported, not patched. A library whose *constructor*
queries GL before our wrapper returns is missed. Both are device-verification items.

### Verification levels (what "verified" means)

| Layer state | Meaning |
| --- | --- |
| `verified` | Build fields were read back through JNI and equal the plan; or a GL/EGL/Vulkan query in the process was actually answered by the wrapper |
| `applied` (`installed`, `armed`) | hooks are in place (slots patched now, or a library watcher waits) but no query has been answered yet: **not** verified |
| `unsupported` | refused or impossible here (unsupported field, field absent on this Android, packed relocations) |
| `failed` | write failed, read-back differed, or neither a patched slot nor a watcher exists |

The provider as a whole is shown as *Unavailable → Installed (not loaded) → Loaded → Matched → Applied →
Verified | Failed*. **Installed is not Loaded**: `Loaded` requires `provider.json` from this boot, i.e. Zygisk
really started the module. Flux never shows "Zygisk = Active".

### Failure handling

| Situation | Result |
| --- | --- |
| provider not installed / no opt-in / SDK < 26 | identity layers `Unavailable`; nothing armed; Flux performance continues |
| plan expired, other boot, daemon gone, transaction mismatch, invalid field | companion rejects; `proc/<pid>.json` says why; process runs real |
| one layer unusable | that layer is `unsupported`; the others still apply |
| Build field missing / write or read-back fails | layer `failed`, reason recorded, nothing faked |
| game started before arming, or runs an older plan | Flux reports `Failed` with "relaunch the game"; the plan is armed for the next launch |
| provider cannot report (fd reclaimed by zygote) | status stays at the last report; the daemon checks the pid is alive |
| unreadable plan / companion unreachable | the process is left untouched |

Performance, refresh, memory, touch and storage are independent of all of the above.

### Security boundaries

Application processes only. Not an anti-cheat bypass: no hook hiding, no integrity or root concealment, no
memory concealment, no detection suppression. The hooks are visible to the game and Flux makes **no claim**
that the provider is undetectable, ban-proof or anti-cheat safe. Zygisk itself, and the game's terms, are the
user's responsibility. Thermal control stays with HiCo; the provider has no thermal, memory, touch, storage or
refresh code.

### Graphics unlock is not guaranteed FPS

An unlocked graphics option is not a promise: identity compatibility only satisfies what a game *asks*; the
GPU, the panel and the thermal envelope decide what it can *sustain*. "Unlocked", "Capable" and "Sustained"
remain three separate answers. **No real-game graphics unlock has been demonstrated.**

### Validation

* Host: `provider_test`, `got_hook_test`, `interpose_test`, `fluxd_companion_test` (needs a JDK for `jni.h`),
  `zygisk_backend_test`.
* Device, test app: `tools/compat-testapp` (source only, unbuilt) + `scripts/flux_provider_validate.sh`
  (A shows profile A, B profile B, the unconfigured app the real device, `getprop` unchanged, rollback).
* Device, one real game with an evidence-backed profile: **not done**.
