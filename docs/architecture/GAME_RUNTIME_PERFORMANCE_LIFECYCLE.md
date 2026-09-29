# Game Runtime Performance Lifecycle

Step 4 of `GAME_RUNTIME_MIGRATION_PLAN.md` (branch `integration/game-runtime-clean`).
Status: **Step 4 IN PROGRESS.**

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
