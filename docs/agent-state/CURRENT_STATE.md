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
- **Game Runtime migration Step 4 (performance lifecycle, incl. 4.5 activation bridge): CLOSED** (owner, 2026-09-29).
  `integration/game-runtime-clean` @ `3e96312`. Final review done; one recovery fix (legacy
  `compat_journal` header). Host 13/13; CI 36624510915 green. **Device validation: NOT_TESTED.**
- **Game Runtime migration Step 5 (session lifecycle): IMPLEMENTED — architecture approved (owner, 2026-09-29). Device validation: NOT_TESTED.**
  `integration/game-runtime-clean` @ `7e35fba`: `jni/session/SessionManager.*`, `jni/SessionHost.*`,
  `tests/session_manager_test.cpp`, `docs/architecture/SESSION_MODEL.md`. Main.cpp reports events only;
  SessionManager orders GameRuntime → SessionRecorder (end in reverse). `sessions.json` unchanged.
  Host 14/14; CI 36630954964 green. Device: NOT_TESTED.
- **Step 6 (Zairenkai Observatory foundation): IMPLEMENTED — architecture approved (owner).**
  `integration/game-runtime-clean` @ `b82761e`: `jni/observatory/{Event,EventStore}.*`,
  `tests/observatory_test.cpp`, `docs/architecture/{OBSERVATORY,EVENT_MODEL}.md`. Schema v1, registry
  (SESSION/RUNTIME/PERFORMANCE/TRANSACTION/RECOVERY), validation, JSONL, write/query/ordering interfaces +
  bounded memory store. Additive: **no producers wired**, SessionRecorder/sessions.json untouched, no device
  storage/retention. Host 15/15; CI 36632946443 green.
- **Step 6.5 (Observatory event integration bridge): APPROVED (owner).**
  `integration/game-runtime-clean` @ `aa88ce2`: neutral observers in Transaction / GamePerformanceRuntime /
  SessionManager (called after transitions, exceptions swallowed), `jni/bridge/ObservatoryBridge.*`,
  `jni/ObservatoryHost.*` (fluxd: bounded in-memory store, no persistence). Registry renamed to the Step 6.5
  event list. Host 16/16; CI 36634896135 green. flux.log / sessions.json / session_live.json unchanged. Device: NOT_TESTED.
- **Step 6.5.1 (transaction restore verification hardening): APPROVED (owner). Transaction Engine hardened; B-27 resolved; B-28 deferred to the kernel adapter phase.**
  `integration/game-runtime-clean` @ `a121b2c`: restore and boot recovery decide by read-back == snapshot (write
  result ignored); only verified entries leave the journal; rollback/restore notices carry restored/failed counts;
  TRANSACTION_ROLLBACK/RESTORE report ok/partial/failed. Resolves B-27. Host 16/16; CI 36665746650 green. Device: NOT_TESTED.
- **Step 7 (Zairenkai Kernel Intelligence foundation): APPROVED (owner). Kernel probing implemented; kernel writes remain disabled.**
  `integration/game-runtime-clean` @ `9e19e1a`: `jni/kernel/KernelIntelligence.*` (FluxKernel, linked, no call path),
  `tests/kernel_intelligence_test.cpp`, `docs/architecture/{KERNEL_INTELLIGENCE,CAPABILITY_MODEL}.md`. Observation only:
  Integration × Generation classification with confidence, read-only probes (15 domains), generic/Qualcomm/MediaTek
  adapters + registry. No writes, no policy. Host 17/17 PASS (gcc; clang-18 -Werror syntax OK). CI 36667540789 green. Device: NOT_TESTED.
- **Step 7.5 (capability context integration bridge): APPROVED (owner). Capability Context foundation complete.**
  `integration/game-runtime-clean` @ `468cf39`: `jni/context/CapabilityContext.*` (FluxContext), kernel
  `export_facts`/`publish`, `PerfCapabilities::context` (carried, no decision reads it), observer interface for the
  Observatory (not wired, no storage). Unknown stays Unknown; conflicts resolve Unknown at equal confidence.
  Host 18/18 PASS; clang-18 -Werror OK. CI 36668216151 green. Still no fluxd call path for observe(). Device: NOT_TESTED.
- **Step 7.6 (runtime capability bootstrap): APPROVED (owner).**
  `integration/game-runtime-clean` @ `0b5a7e4`: `jni/kernel/CapabilityBootstrap.*`, `jni/CapabilityHost.*`; Main.cpp calls
  `flux_capability::bootstrap()` once at start before boot recovery; GameRuntime planner carries `flux_capability::context()`.
  Failed probe: daemon continues, capabilities Unknown, one log warning. Observer hook only; no telemetry. Host 19/19 PASS;
  clang-18 -Werror OK (daemon glue checked only by CI ndk-build: spdlog submodule absent locally). CI 36669240984 green.
  **Device: NOT_TESTED** (first step whose code runs in fluxd on device: read-only sysfs/procfs probe at start).
- **Step 8 (Zairenkai Graphics Intelligence foundation): APPROVED (owner).** (Owner step numbering; corresponds to the
  capability part of master-plan Phase 7 "Graphics intelligence" — not master-plan Phase 8 Game Runtime integration.)
  `integration/game-runtime-clean` @ `3af9cc0`: `jni/graphics/GraphicsIntelligence.*` (FluxGraphics), domain `graphics`
  published by fluxd after the kernel bootstrap, **no Vulkan instance in fluxd** (declarative evidence, Medium/Low).
  GPU vendor/model/driver, Vulkan, GLES/EGL, interfaces, GPU freq/load interfaces (from kernel facts). Confidence
  HIGH/MEDIUM/LOW/UNKNOWN (`Confidence::None` now prints `unknown`). Host 20/20 PASS; clang-18 -Werror OK.
  CI 36669963849 green. Device: NOT_TESTED.
- **Step 8.5 (display and rendering capability foundation): APPROVED (owner).**
  `integration/game-runtime-clean` @ `365c8df`: `jni/display/DisplayIntelligence.*` (FluxDisplay); domains `display` and
  `rendering` published by fluxd after graphics (dumpsys display, wm size, service list, properties, kernel DRM facts).
  Refresh capability != FPS (no fps facts). Host 21/21 PASS; clang-18 -Werror OK. CI 36670838946 green. Device: NOT_TESTED.
- **Step 8.6 (runtime bottleneck observation foundation): APPROVED (owner).**
  `integration/game-runtime-clean` @ `920cb88`: `jni/bottleneck/BottleneckModel.*` (FluxBottleneck, linked, **no call path**:
  no runtime sampler exists yet). CPU/GPU/thermal/memory/storage/display, states CONFIRMED/LIKELY/POSSIBLE/UNKNOWN, evidence
  + confidence + source + timestamp on every observation, Synrei `ThermalContext` interface. Host 22/22 PASS; clang-18
  -Werror OK. CI 36674890357 green. Device: NOT_TESTED.
- **Step 8.7 (runtime metrics collector foundation): APPROVED (owner) — FOUNDATION COMPLETE, INTEGRATION PENDING.**
  `integration/game-runtime-clean` @ `e0e4bdd`: `jni/metrics/RuntimeMetrics.*` (FluxMetrics, linked, **no call path**).
  CPU/GPU/memory/storage metrics via the read-only fs seam; each with value, timestamp, source, confidence, readable,
  verified; missing/unreadable/malformed = UNKNOWN; `to_runtime_sample()` feeds BottleneckModel only. Host 23/23 PASS;
  clang-18 -Werror OK. CI 37267128969 green. Device: NOT_TESTED.
- **Step 8.8 (runtime metrics sampling lifecycle): ARCHITECTURALLY APPROVED (owner).**
  `integration/game-runtime-clean` @ `d5194ac`: `jni/metrics/RuntimeMetricsSampler.*`; `SamplerParticipant` registered last in
  fluxd's SessionManager (`jni/SessionHost.cpp`). Samples on the session tick only (no thread), 2000 ms default, clamped
  1000–60000 ms, window 120 (3–900); fresh collector per session; stops on every end reason; 3 consecutive failures stop
  sampling without affecting the session. Feeds BottleneckModel only; no fps source in fluxd yet. Host 24/24 PASS;
  clang-18 -Werror OK. CI: triggered, not monitored (owner instruction 2026-10-05). Device: NOT_TESTED.
  Synrei integration not started (owner).
- **Step 8.8.1 (FPS observation bridge): ARCHITECTURALLY APPROVED (owner); stays IN PROGRESS until CI/device validation is completed.**
  `integration/game-runtime-clean` @ `d42b866`: `jni/metrics/FpsObservation.*`; SessionRecorder publishes its existing
  per-second FPS into a read-only slot (additive: publish after write_live, clear on stop, const accessor); sampler accepts
  only valid / fresh (≤3 s) / newer observations, else UNKNOWN; bottleneck thresholds unchanged. sessions.json /
  session_live.json / FPS measurement / recorder lifecycle unchanged; no second FPS loop. Host 25/25 PASS; clang-18 -Werror
  OK. CI: triggered, not monitored. Device: NOT_TESTED. Synrei not started (owner).
- **Step 8.9 (Synrei thermal context foundation): APPROVED (owner).** Thermal mapping accepted; HiCo repository not modified; B-33B1 resolved for the current interface.
  `integration/game-runtime-clean` @ `7007422`: `jni/thermal/{ThermalContext,SynreiThermalAdapter}.*` (FluxThermal). Reads hicod's
  existing `/dev/hico/state` read-only; verified only while the hicod pid is alive and `updated` ≤ 15 s old; constraint from
  Synrei state only (safety → constrained, boost → unconstrained, else unknown; never from temperature); headroom UNKNOWN
  (not published); slope derived. Sampler records one snapshot per sample; BottleneckModel gets it via the neutral
  `ThermalContext`. Thresholds, FPS architecture, GameRuntime and SessionRecorder unchanged; zero thermal writes (tested).
  Host 26/26 PASS; clang-18 -Werror OK. CI: triggered, not monitored. Device: NOT_TESTED. HiCo repository unchanged.
- **Step 8.10 (bottleneck result integration): ARCHITECTURALLY APPROVED; implementation complete (owner). CI/device validation separate.**
  `integration/game-runtime-clean` @ `3ae57cd`: `jni/bottleneck/BottleneckResult.*`, `jni/bridge/BottleneckEvents.*`, registry types
  `BOTTLENECK_ASSESSED` / `BOTTLENECK_ANALYSIS_FAILED` (category performance, source bottleneck, schema unchanged), sampler result
  sink; fluxd writes the final per-session result to the in-memory Observatory store. One assessment per session at stop, no
  worker; primary UNKNOWN when insufficient; failures isolated (session, restore, transactions unaffected). Rules/thresholds
  unchanged; sessions.json, session_live.json, SessionRecorder, GameRuntime policy, PerformancePlanner, Synrei, kernel nodes
  unchanged. Host 27/27 PASS; clang-18 -Werror OK. CI: triggered, not monitored. Device: NOT_TESTED.
- **Step 8.11 (Observatory persistent storage + 7-day retention): IMPLEMENTATION COMPLETE (owner); CI/device validation pending.**
  `integration/game-runtime-clean` @ `1ee1c7a`: `jni/observatory/{TelemetryStore,InstallationEpoch}.*`; producers write via
  `TeeEventSink` (memory first, then `/data/adb/.config/zairenkai/telemetry/`: FORMAT, hourly JSONL segments = to_json(event),
  per-segment indexes). Retention 7 × 24 h at daemon start, hourly (main loop now wakes hourly when idle) and on SESSION_END;
  unset clock skipped, forward jump deferred. Disk failures isolated, reported once per streak as OBSERVATORY_STORAGE_FAILED
  (memory only). Installation epoch at `/data/adb/.config/zairenkai/installation.json`, written once, preserved. CLI
  `fluxd telemetry status|retention|query` (read-only; retention dry run). sessions.json / session_live.json / thresholds
  unchanged; no WebUI. Host 28/28 PASS; clang-18 -Werror OK (CLI handler and ObservatoryHost compiled against stubs:
  spdlog absent locally). CI: triggered, not monitored. Device: NOT_TESTED.
- **Step 8.12 (Observatory historical analysis & explanation layer): IMPLEMENTED (owner); CI/device validation pending.**
  `integration/game-runtime-clean` @ `0c23fc0`: `jni/observatory/{SessionTimeline,Explanation,ObservatoryAnalyzer,TelemetryCli}.*`,
  additive read-only `PersistentEventStore::newest_timestamp()`, `fluxd telemetry session|analyze|history` (read-only; existing
  commands moved unchanged into `TelemetryCli`). Session reconstruction, transaction / recovery / bottleneck (as recorded) /
  thermal (correlation only) / FPS (Hz ≠ FPS) explanations with evidence references, bounded 7-day history patterns (≥ 2
  occurrences, confidence ≤ medium). Deterministic, no clock. Telemetry format v1, retention, epoch, event schema v1,
  sessions.json, session_live.json, thresholds unchanged. Host 29/29 PASS; clang-18 -Werror OK; FluxCLI handler compiled
  against stubs. CI: triggered, NOT checked. Device: NOT_TESTED.
  Limitation recorded: Synrei state transitions and per-sample FPS are not persisted as events, so the analysis can use only
  the bottleneck assessment's thermal/FPS evidence.
- **Step 8.13 (decision & policy foundation): APPROVED architecturally (owner); no executor, no call path.**
  `integration/game-runtime-clean` @ `7f84189`: `jni/policy/{PolicyEvidence,PolicyConstraint,PolicyDecision,DecisionEngine}.*`
  (FluxPolicy linked, **no call path, no executor**). Actions NO_ACTION/OBSERVE/MITIGATE/BOOST/RESTORE; hierarchy RESTORE >
  NO_ACTION > MITIGATE > BOOST; Synrei safety forbids BOOST; unknown/stale thermal, conflicting/insufficient bottleneck → OBSERVE;
  capability gating (unsupported/unreadable/non-writable blocked, writable-unverified restricted) — with today's verified=false
  kernel facts, device decisions stay OBSERVE. Pure, deterministic, no I/O; no new event types (registry 19, schema v1);
  B-35 untouched. Host 30/30 PASS; clang-18 -Werror (C++20 and C++2b) OK. Android CI: triggered, NOT checked. Device: NOT_TESTED.
- **Step 8.14 (capability verification foundation): IN PROGRESS — implementation complete on host, device NOT_TESTED.**
  `integration/game-runtime-clean` @ `181af15`: `jni/kernel/CapabilityVerification.*`; fluxd `flux_capability::verify()` once per start
  after GameRuntime recovery, before profile scripts. Transactional proof via the existing Transaction Engine (snapshot,
  write-ahead journal `/data/adb/.config/zairenkai/verification.journal`, test write, exact read-back, restore, exact read-back;
  leftover journal replayed with `runtime::recover()` first). Adapters: cpufreq scaling_max_freq, KGSL max_gpuclk, devfreq max_freq
  (next lower available frequency), block read_ahead_kb, vm.swappiness. Everything else `no_safe_verifier`; thermal refused;
  skipped while Synrei is boost/relaxed/safety. Host 31/31 PASS; clang-18 -Werror OK; CapabilityHost glue compiled against stubs.
  Android CI: triggered, NOT checked. **This is the first step that writes kernel nodes at daemon start (temporarily, then restored).**
- **Phase 4B (controlled policy executor): IN PROGRESS — implementation complete on host, no fluxd call path, device NOT_TESTED.**
  `integration/game-runtime-clean` @ `eb329aa`: `jni/policy/PolicyExecutor.*`. Executes an approved PolicyDecision exactly (no upgrade);
  MITIGATE/BOOST → RuntimePlan(domain policy) of NodeWriteOperations through the existing Transaction Engine; RESTORE = finish() of the
  executor-owned transaction. Trusted operations only: cpufreq scaling_max_freq, KGSL max_gpuclk, devfreq max_freq (one listed step:
  next lower / highest within known max). Gating: supported, readable, writable, verified, rollback, risk Low/Medium, not planner-owned;
  decision constraints, Synrei safety (BOOST), runtime state, idempotency. Existing TRANSACTION_* events; registry 19, schema v1.
  Host 32/32 PASS; clang-18 -Werror OK. Android CI: triggered, NOT checked.
- **Phase 4C (live decision integration): IN PROGRESS — host complete, Android CI not checked, device NOT_TESTED.**
  `integration/game-runtime-clean` @ `227d90d`. `jni/policy/LivePolicyController.*` is the `live_policy`
  session participant, registered last: it evaluates once per fresh RuntimeMetrics sample, and its
  PolicyExecutor restore runs before GameRuntime's restore. Journal: `/data/adb/.config/zairenkai/policy.journal`.
  B-38 is resolved in the DecisionEngine. Host 33/33 PASS (new `live_policy_test`). Doc: `CONTROL_EXECUTION.md`.
  B-37 (host only), B-26, B-33A/B/C, B-34C, B-35 (deferred) and B-36 are unchanged.
- **Phase 4.5 (brand & namespace migration): IN PROGRESS — host complete, Android CI not checked, device NOT_TESTED.**
  `integration/game-runtime-clean` @ `5f6135a`. Public identity is now Zairenkai, with Synrei Thermal Intelligence
  and Zairenkai Intelligence; Aeyrin stays internal. `zairenkai` is a symlink alias of `fluxd`. All compatibility
  identifiers are frozen: module ID `flux`, `fluxd`, `/data/adb/.config/flux`, HiCo paths, `/dev/hico/state`,
  `com.febricahyaa.synthesiscore`, update channel, log tag `FluxTweaks`, schema v1. The config compatibility
  helper (`jni/brand/ConfigNamespace`) exists but is not called by fluxd. Host 35/35 PASS.
  Docs: `BRAND_MIGRATION.md` and `brand_identifiers.tsv`.
- **Next:** owner review of Phase 4C and 4.5; then the device validation pass (B-37 first, then the first live execution).
- **Working rule (owner, 2026-10-05):** do not wait for / monitor CI completion; trigger it and report it as unchecked.
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

- **Phase 4.5B (HiCo → Synrei Thermal Intelligence, HiCo repo): IN PROGRESS — host complete, CI not checked, device NOT_TESTED.**
  HiCo `ccr-0dc934d1-0a6zta` @ `b7f7845c`. Public labels are rebranded. Frozen: module ID `hico`, `hicod`,
  `/data/adb/.config/hico`, `/dev/hico/state`, `HICO_TAG`, config, state and JSON keys, update channel
  HiCo-Release, database and all data, licence and EULA. Thermal behavior and the Zairenkai contract are unchanged.
  HiCo ctest 3/3 PASS (unit, cli, new synrei_brand). Python tests: 20/21 PASS. `ingest_test` fails identically
  at the base commit (PyYAML `'true'` vs `True` difference in this environment). Doc: `SYNREI_BRAND_MIGRATION.md`.
  Open: the built `module/webroot` bundle still shows HiCo labels until the next WebUI build; EULA, CI names and
  repository names are unchanged (owner/legal decisions).

- **Phase 4.5C (SynthesisCore → Zairenkai Intelligence, SynthesisCore repo): IN PROGRESS — source complete, CI not checked, device NOT_TESTED, release APK not rebuilt.**
  SynthesisCore `ccr-0dc934d1-0a6zta` @ `2fe71b6`.
  - Rebranded: README title and identity section, `app_name`, usage banner, release title, issue-template text. Aeyrin stays internal.
  - Frozen: package and applicationId `com.febricahyaa.synthesiscore`, the `MainKt` entry and modes, `synthesis_version 3`,
    release asset names `SynthesisCore-<tag>.apk*`, the `synthesiscore-release` event, the signing certificate and pin, `rootProject.name`.
  - Runtime change: one help-text line.
  - Tests: `tests/brand_migration_test.sh` PASS, twice. It is added to `ci.yml`.
  - Gradle/Kotlin unit tests were not run locally (no Android SDK in the container); CI is not checked.
  - Open: the APK label and banner reach devices only after the next signed release plus the Zairenkai prebuilt sync.
  - Device blockers (B-37, B-39, …) unchanged.
