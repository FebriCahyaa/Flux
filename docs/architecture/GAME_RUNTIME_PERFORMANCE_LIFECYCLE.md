# Game Runtime Performance Lifecycle

Step 4 of `GAME_RUNTIME_MIGRATION_PLAN.md` (branch `integration/game-runtime-clean`).
Status: **Step 4 IMPLEMENTED** (incl. Step 4.5 activation bridge). Device validation: **NOT_TESTED**.

## Decision (recorded before implementation): where runtime profiles and journals live

| Item | Decision | Reason |
|---|---|---|
| Config directory | `/data/adb/.config/flux/` (unchanged) | Frozen contract D-04; no path migration before Phase 13. |
| Per-game profiles | `game_profiles.json` | Same file the old Game Runtime branch used, so existing user files keep working. Read in any format `ProfileModel` accepts (current v1 or legacy); legacy `compatibility` is ignored with a warning. |
| Shared presets | `compat_library.json` (optional) | Existing legacy library path; only `presets` are used, `identities` ignored. Merged under `game_profiles.json` (the games document wins on name clashes). |
| `gamelist.json` `lite_mode` | GAME-layer fallback for packages with no `game_profiles.json` entry | Keeps `main`'s existing per-game setting meaningful. |
| Missing files | Not an error: builtin + global only → every category `default` → empty plan | A game without a profile behaves exactly as on `main`. |
| When loaded | On every game start (small files) | Edits in the WebUI apply to the next launch without a daemon restart. |
| Performance journal | `perf_journal` (new) | Write-ahead journal of the per-game transaction. Not `compat_journal`, whose old contents may contain identity lines. |
| Launch-boost journal | `launch_journal` (new) | Separate transaction, separate journal; recovered independently. |
| Legacy `compat_journal` | Recovered once if present (node entries only); corrupted/non-node lines reported, file kept unless clean | Devices that ran the old branch may still have undone writes. |

Paths are injected (`RuntimePaths`); `Flux.hpp` will define them when the daemon is wired.

## Flow

```
game start ──> load profiles ──> resolve (ProfileModel) ──> plan (PerformancePlanner)
                                     │ errors                   │ empty
                                     ▼                          ▼
                              ResolveFailed (nothing applied)  Idle-with-profile (nothing applied)
                                                                │ operations
                                                                ▼
                                            Transaction start: snapshot → journal → apply → verify
                                                  │ fail                    │ ok
                                                  ▼                         ▼
                                     Failed (rolled back, Flux profile   Active ──> tick (launch boost deadline)
                                     unaffected: fallback)                 │        after_profile_script → reapply
                                                                           ▼
                                        game exit / process death / switch / daemon stop
                                                                           ▼
                                        restore + verify restore → journal removed if clean,
                                        kept (for boot recovery) otherwise
daemon start ──> recover perf_journal, launch_journal, legacy compat_journal
```

## Owner

`flux::perf::GamePerformanceRuntime` (`jni/perf/GamePerformanceRuntime.{hpp,cpp}`) owns one
game's performance lifecycle. It does **not** detect games, track PIDs or model sessions; the
caller (today's `Main.cpp` lifecycle, later Session) tells it when a game starts and ends.

## Implementation

| Event | `GamePerformanceRuntime` call | Behaviour |
|---|---|---|
| daemon start | `recover()` | replays `perf_journal`, `launch_journal`, legacy `compat_journal`; removes each only when clean |
| game start | `on_game_start(pkg, pid, now)` | load → resolve → plan → transaction (+ launch boost); idempotent for same pkg+pid; another game or a new pid ends the previous context (`Switch`) first |
| main loop | `tick(now)` | ends launch boost at its deadline; per-game values stay |
| profile script re-ran | `after_profile_script()` | re-applies per-game values (originals kept) |
| game exit / process death / daemon stop | `on_game_end(reason)` | cancels boost, restores + verifies; journal removed when clean, kept otherwise |

States: `Idle` (nothing applied), `Active`, `Failed` (transaction rolled back; Flux profile
unaffected), `ResolveFailed` (unreadable document, unknown profile/parent, cycle, or rejected plan —
fail closed, nothing applied). Refresh target is exposed via `refresh_target_hz()` only while
active.

Compatibility fields in legacy documents are reported in `warnings()` and never resolved.

## Daemon wiring (`dd86b7c`)

| fluxd point | Call |
|---|---|
| `flux_main_daemon` start, before `run_perfcommon()` | `host().on_daemon_start()` → recover journals |
| `apply_game_profile`, before the profile script | `set_enabled(!disable_tweaks)`, `on_game_active(pkg, tracked_pid)` |
| after `apply_performance[_lite]_profile` | `on_profile_applied()` (re-apply) |
| main `poll` | 1 s timeout only while a launch boost is running; `tick(now)` |
| PID death / focus loss / abort / daemon stop | `stop_session_workers(reason)` → `on_game_end(reason)` |
| `set_profiler_env_vars` | `FLUX_REFRESH_TARGET_HZ` + `FLUX_REFRESH_ENABLED` from `refresh_request()` |

Device adapters (`jni/perf/RuntimeHost.cpp`): node `Io` (no create, bounded reads), `FileStore`
(temp + fsync + rename, refuses symlink/dir targets), read-only probe (readable nodes, internal
block queues excluding loop/ram/zram/dm/md/sr/nbd/boot/rpmb/removable), `dumpsys display` rate
parser (119.99 → 120). Glue: `jni/GameRuntimeHost.{hpp,cpp}`; paths in `Flux.hpp`.

## Final review (Step 4 closure)

| Area | Verified by | Result |
|---|---|---|
| Main.cpp lifecycle integration | diff review: recover before `run_perfcommon()`; activation before the profile script in `apply_game_profile`; `on_profile_applied()` after all three script calls; `stop_session_workers(reason)` on every exit path; poll timeout only while boosting | OK |
| Journal recovery path | `transaction_test`, `game_runtime_test`, `runtime_host_test` | OK after fix below |
| Game start activation | `game_runtime_test`, `runtime_host_test` (idempotent per pkg+pid) | OK |
| Game exit restore | same; journal removed only when clean | OK |
| Process death restore | `game_runtime_test` (incl. launch boost); Main.cpp passes `ProcessDeath` from PID-tracker and abort paths | OK |
| Daemon restart recovery | `game_runtime_test`, `runtime_host_test`; unrecoverable journal kept | OK |
| Refresh bridge | `runtime_host_test` + `refresh_script_test.sh` (supported applied, unsupported ignored, garbage ignored, restore) | OK |
| Adapter safety | `runtime_host_test` on real temp files: no node creation, bounded reads, symlink/dir journal targets refused, virtual/removable block devices excluded | OK |
| Rollback behaviour | transaction/planner/runtime/adapter tests incl. ENOSPC write | OK |
| Forbidden symbols (Resolver, provider, Zygisk, identity) | CI gate | clean |

**Fix made during review:** the old branch's `compat_journal` starts with a
`#flux-compat-journal v1` header. It was treated as a corrupted line, so the legacy journal's node
entries were restored but the file was never removed and recovery reported "not clean" on every
boot. `journal::parse` now accepts that exact header as a legacy (version 0) journal; test added
(`transaction_test`). No behaviour change for current journals.

**Diagnostics available:** every recovery logs `restored/found, failed, corrupted, journal kept|removed`
(tag `GameRuntime` in `flux.log`); lifecycle logs transaction id and state, launch-boost end reason,
`ended (<reason>)`, `restore incomplete, journal kept`, resolve/plan errors, and ignored legacy
compatibility warnings.

## Known limitations

- A daemon restart while a game is already running starts a (bounded, ≤ 8 s) launch boost for it,
  because fluxd cannot tell a fresh launch from a re-detected process. Harmless but not a "launch".
- Per-game values stay while the screen is off during a game (same as the existing profile tier).
- The capability probe runs once per daemon start; hot-plugged block devices are not re-probed.
- No WebUI for `game_profiles.json` yet; profiles are edited as files.
- Device validation: NOT_TESTED.

## Status

| IMPLEMENTED | NOT_IMPLEMENTED |
|---|---|
| lifecycle owner + daemon wiring (start/recover/active/reapply/tick/end) | Session model (Step 5) |
| real adapters + capability probe | device validation |
| refresh bridge → `flux_refresh` (per-game target, unsupported ignored) | WebUI for per-game profiles |
| host tests: daemon lifecycle, refresh bridge, adapters, shell refresh test | |

CI: https://github.com/FebriCahyaa/Flux/actions/runs/36623380177 (host 13/13, ndk-build arm64+arm, packaging).

Session model, compatibility identity, Resolver and Zygisk are not part of this step.
