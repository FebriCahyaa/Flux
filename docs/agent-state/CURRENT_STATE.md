# Current State

Read this first when resuming. Then read `PHASE_STATUS.md`, `BLOCKERS.md`, `PLAN.md`, run
`git status`, `git branch`, `git log -10` in all three repositories.

## Where the programme is

- **Phase 0 (baseline audit): complete — documentation only.** No source, script, data, path,
  identifier or UI string was changed in any repository.
- **Phase 1 (naming registry + availability evidence): complete — documentation only.**
  `docs/architecture/NAMING_REGISTRY.md`. Aeyrin public brand stopped (B-13); trademark databases
  owner-pending (B-14). Owner decisions D-08…D-12 recorded.
- **Phase 1.5 (Game Runtime integration audit): AUDIT COMPLETE — not integrated.**
  `docs/architecture/GAME_RUNTIME_INTEGRATION_REPORT.md`. Nothing merged or cherry-picked; branch
  `ccr-23925ebb-375wkg` preserved. Identity layers marked REMOVE OR REDESIGN (B-15).
- **Phase 1.6 (Game Runtime integration preparation): PREPARATION COMPLETE.**
  `docs/architecture/GAME_RUNTIME_MIGRATION_PLAN.md`. Branch `integration/game-runtime-clean`
  created from `main` @ `b75491c` and pushed, **no commits on it**. No production code changed.
- **Game Runtime migration Step 1 (transaction engine): CLOSED.**
  `integration/game-runtime-clean` @ `63dd11c` (code `b604eac`, build `8fc92c0`). `FluxRuntime`
  linked into `fluxd` but not called; forbidden-symbol CI gate active. CI run 36617841919 green
  (host 8/8, ndk-build arm64+arm, WebUI, 3 zips). Device: NOT_TESTED.
- **Game Runtime migration Step 2 (performance planner + launch boost): CLOSED** (owner, 2026-09-29).
  `integration/game-runtime-clean` @ `38a2f7e` (code `98a7a25`): `jni/perf/PerformancePlanner.*`,
  `tests/performance_planner_test.cpp`, `docs/architecture/PERFORMANCE_PLANNER.md`. Host 9/9 PASS;
  CI run 36619381182 green (host, forbidden-symbol gate, ndk-build arm64+arm, WebUI, 3 zips).
  `FluxPerf` linked into `fluxd`, **no call path** (needs Session). Device: NOT_TESTED.
- **Game Runtime migration Step 3 (performance profile inheritance): CLOSED** (owner, 2026-09-29).
  `integration/game-runtime-clean` @ `a954925` (code `3410fbd`): `jni/perf/ProfileModel.*`,
  `tests/profile_model_test.cpp`, `docs/architecture/PERFORMANCE_PROFILE_MODEL.md`. Host 10/10 PASS;
  CI run 36620727531 green. Not loaded/called by `fluxd`. Device: NOT_TESTED.
- **Game Runtime migration Step 4 (performance lifecycle, incl. 4.5 activation bridge): IMPLEMENTED.**
  `integration/game-runtime-clean` @ `3e96312`. Final review done; one recovery fix (legacy
  `compat_journal` header). Host 13/13; CI 36624510915 green. **Device validation: NOT_TESTED.**
- **Next: Step 5 — Session migration** (preparation recorded in PLAN.md; not started).
- Phase 8 and Phase 13 are blocked (`BLOCKERS.md` B-01, B-02, B-08, B-11).

## Repositories and branches

| Repository | Local path (cloud session) | Base | Work branch | Phase 0 commit |
|---|---|---|---|---|
| `FebriCahyaa/Flux` | `/home/user/Flux` | `main` @ `b75491c` | `ccr-0dc934d1-0a6zta` | the commit that adds this file |
| `FebriCahyaa/HiCo` | `/home/user/HiCo` | `main` @ `a61cad0` | `ccr-0dc934d1-0a6zta` | `d7b725e` |
| `FebriCahyaa/SynthesisCore` | `/home/user/SynthesisCore` | `master` @ `7cbdda2` | `ccr-0dc934d1-0a6zta` | `7b6e1cd` |

Commit identity: `FebriCahyaa <febricahya12345@gmail.com>` (D-07).

## Phase 0 outputs

Flux:
- `docs/architecture/ZAIRENKAI_BASELINE.md` — measured architecture of all three components
- `docs/architecture/REPOSITORY_DATA_INVENTORY.md` — every tracked path, consumers, class
- `docs/architecture/MIGRATION_MATRIX.md` — per-subsystem REUSABLE/MODIFY/REPLACE/DEPRECATE/UNKNOWN/NEW
- `docs/architecture/DELETION_MANIFEST.md` — empty (no deletions proposed)
- `docs/agent-state/*` — this state

HiCo and SynthesisCore: `docs/architecture/REPOSITORY_DATA_INVENTORY.md`,
`docs/architecture/DELETION_MANIFEST.md`.

## Tests at the end of Phase 0

Flux host 7/7 PASS, Flux WebUI build PASS, HiCo ctest 2/2 PASS, HiCo Python 15/15 PASS,
HiCo device DB check PASS. Android builds, SynthesisCore unit tests, all device behaviour:
NOT_TESTED. Details: `VALIDATION.md`.

## Known limitations of this audit

- Shallow clones: ahead/behind counts are exact for the fetched depth (60 for Flux, 40 for HiCo);
  HiCo branches all show 0 commits ahead of `main`.
- HiCo evidence/database validators and SynthesisCore Gradle tests were not run here (CI runs
  them).
- Dependabot branches in SynthesisCore were listed, not reviewed.
- The unmerged Flux Game Runtime line was read, not built or tested.

## Resume checklist for Phase 1

1. `git -C <repo> fetch origin` and check whether `Flux@ccr-23925ebb-375wkg` or
   `SynthesisCore@claude/hico-thermal-game-9ree3l` were merged (B-01, B-03). If so, re-inventory
   the new paths before any other work.
2. Confirm the work branch still descends from the base commits above.
3. Work only in documents for Phase 1; update `PHASE_STATUS.md`, `DECISIONS.md`,
   `VALIDATION.md` and this file at the end.
