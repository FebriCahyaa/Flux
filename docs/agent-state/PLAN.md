# Plan

Phases follow the Zairenkai master directive §64. Each phase ends with: build, tests, diff and
permission review, schema validation, agent-state update, commit. A phase is not complete while any
part is partial. Blockers are in `BLOCKERS.md`.

| Phase | Scope | Depends on |
|---|---|---|
| 0 | Baseline audit, data inventories, migration matrix, agent state | — |
| 1 | Naming registry and clearance evidence, stellar codename registry, build-metadata design, compatibility-alias design for module id / config dir / package. **No production identifier changes.** | 0 |
| 2 | Aeyrin context and capability foundation (capability record: id, domain, supported, readable, writeable, verified, source, interface, value, range, confidence, risk, rollback) | 1, B-03 |
| 3 | Kernel Intelligence: identity probe, Integration × Generation classification with confidence, read-only capability probing, adapters, tweak contracts | 2 |
| 4 | Observatory event model (schema, IDs, severity, evidence, transaction IDs, coalescing) | 2 |
| 5 | 7-day telemetry store, sessions, installation epoch, index, retention enforcement | 4 |
| 6 | Session timeline and deterministic explanations | 5 |
| 7 | Graphics intelligence: GPU capability/adapter/policy, frame pacing, refresh strategy, bottleneck analysis, render scaling vs upscaling | 3, 6 |
| 8 | Game Runtime integration | 7, B-01, B-02 |
| 9 | Synrei thermal integration | 6, B-04 |
| 10 | WebUI Observatory | 6, 7 |
| 11 | Security and integrity hardening (allowlisted bridge, transactional executor) | 3 |
| 12 | Low-end / legacy device strategy | 3, 7 |
| 13 | Naming / source migration | 1, B-08, B-11 |
| 14 | Device validation | all runtime phases |
| 15 | Release and final architecture audit | 14 |

## Integration audit (owner decision D-09) — before Phase 8

Read-only audit of `Flux@ccr-23925ebb-375wkg` vs `main` (74 files, +11,002/−21): per-file purpose,
conflicts with frozen contracts (D-04), removal/disablement of the identity-substitution layer
(D-10), tests, and an integration report `docs/architecture/GAME_RUNTIME_INTEGRATION_REPORT.md`.
**Status: AUDIT COMPLETE (Phase 1.5).** Report written; no merge until accepted. Migration order: report §8.

## Phase 1.6 — Integration preparation: PREPARATION COMPLETE

Branch `integration/game-runtime-clean` (= main). Plan: `GAME_RUNTIME_MIGRATION_PLAN.md` §3 steps
1–10. Zygisk and all identity code excluded. Approved by owner.

| Step | Status |
|---|---|
| 1 Transaction engine decoupling | **CLOSED** — `b604eac`, `8fc92c0`, `63dd11c`; CI run 36617841919 green |
| 2 Perf planner + launch boost | **CLOSED** — `98a7a25`, `38a2f7e`, `64ce145`; CI 36619381182 green |
| 3 Profile inheritance (perf only) | **CLOSED** — `3410fbd`, `a954925`; CI 36620727531 green |
| 4 GameRuntime perf lifecycle (+4.5 activation bridge) | **CLOSED** — `af4643d`, `c37ef15`, `dd86b7c`, `3e96312`; CI 36624510915 green; device NOT_TESTED |
| 5 Session migration | **IMPLEMENTED** (architecture approved) — `7e35fba`; CI 36630954964 green; device NOT_TESTED |
| 6 Refresh target | DONE inside Step 4 (`dd86b7c`) |
| 7 Daemon wiring | DONE inside Step 4 for the performance runtime (`dd86b7c`) |
| 8–10 | NOT STARTED |

## Phase 0 findings carried forward

| Finding | Fixed in |
|---|---|
| B-05 three disagreeing GKI classifiers | Phase 3 |
| B-07 SoC code decoding mismatch, weak substring matching | Phase 2–3 |
| B-10 `flux_utility.sh` `$@` dispatch, no write allowlist | Phase 11 |
| B-12 `RefreshMatcher` (policy) inside `SessionRecorder` (observer) | Phase 6–7 |
| B-04 HiCo Max/Extreme vs thermal-safety rule | Phase 9 |
| `apply()` writes without read-back / rollback in `perfcommon` | Phase 3, 11 |
| Count-based session retention (30) | Phase 5 |

## Phase 1 — Naming and migration architecture (DONE, see PHASE_STATUS)

Deliverables (documents and, where useful, non-shipping metadata only):

1. `docs/architecture/NAMING_REGISTRY.md`: legacy → target names, per-identifier inventory
   (module ids, config dirs, packages, class names, binary names, brand strings, CI names, docs),
   compatibility-alias plan, and the clearance evidence table.
2. Clearance evidence: web and GitHub search, Google Play, package registries (npm, PyPI, Maven,
   crates), domains, and trademark databases (PDKI/DJKI, WIPO Global Brand Database, USPTO). Every
   row records source, date, query and result. Trademark rows are marked "owner to confirm" unless
   the database itself was queried. A collision stops the public-brand stage (B-08).
3. Stellar codename registry and build identity format `ZAIRENKAI-G<nn>-<CODENAME>-<semver>`, with
   the rule that generation ≠ codename ≠ semver, and no reset of version history (current `1.4.1`).
4. Build-metadata design (version, generation, codename, commit, build time, ABI, flavor) for
   `fluxd --version`, `module.prop` and the WebUI About page — design only.

Out of scope for Phase 1: any rename in code, paths, module ids, packages or UI strings.

## Next phase: Phase 2 — Aeyrin context / capability foundation
Or, if the owner prefers, the integration audit above first.

## Step 5 preparation — Session migration (not started)

Current owners after Step 4: `Main.cpp` owns game detection, PID tracking, 3-strike focus loss and
DND; `GamePerformanceRuntime` owns the per-game performance transaction; `SessionRecorder` owns
statistics (and still `RefreshMatcher`, B-12).

Proposed Step 5 scope (for owner approval):
1. A neutral `Session` value (id, package, pid, uid, start/end time, end reason, profile explanation,
   transaction ids, recovery result) created by the lifecycle, not by detection.
2. Session owns ordering of start/end across GameRuntime, SessionRecorder and RenderBooster so
   `stop_session_workers(reason)` becomes one owner; Main.cpp keeps detection only.
3. Port from the old branch only the idempotent begin/end, one-session-at-a-time and
   `EndReason` semantics — not `Analyze`, `Arming`, provider status or compatibility context.
4. No telemetry storage yet (Phase 5); Session exposes data for the Observatory later.
5. Tests first: begin idempotency, switch, process death, daemon stop, crash then recovery,
   ordering of worker stop vs restore.

Owner decision: keep `sessions.json` unchanged until Phase 5 (applied in `7e35fba`).

## Step 6 — Zairenkai Observatory foundation (IMPLEMENTED, approved)

Done (`b82761e`): event schema v1, registry, validation, serialisation, storage interfaces, memory store,
host tests, docs. Not in scope: producers, device storage, retention, timeline, WebUI, KERNEL/GRAPHICS/THERMAL
categories. Integration points listed in `docs/architecture/OBSERVATORY.md`.
Suggested next: wire producers (SessionManager, GamePerformanceRuntime, Transaction, recovery) to an
`EventSink`, then Phase 5 storage + 7-day retention under `/data/adb/.config/zairenkai/telemetry/`.

## Step 6.5 — Observatory event integration bridge (APPROVED)

Done (`aa88ce2`): observers + bridge + fluxd in-memory store; 16 event types emitted; failure isolation
tested. Not in scope: persistence/retention (Phase 5), reading events out of fluxd (CLI/export), WebUI,
GPU/Thermal events. Open finding B-27 (rollback of a node whose write failed is reported incomplete).

## Step 6.5.1 — Transaction restore verification hardening (APPROVED)

Done (`a121b2c`): Transaction Engine only (+ bridge result mapping). Restored ⇔ read-back equals snapshot;
journal kept when unverifiable or different; SUCCESS/PARTIAL/FAILED outcome in events.
Known limitation: `recover()` compares raw read-back against view-normalised snapshots (no current user).

## Step 7 — Zairenkai Kernel Intelligence foundation (APPROVED)

Done (`9e19e1a`): kernel identity + two-axis classification with confidence (B-05 in the model), capability record,
read-only probes (CPUFreq, policy, governor, uclamp, scheduler, cpuset, cgroup, DevFreq, GPU, thermal, ZRAM, swap,
I/O scheduler, input boost, display refresh), adapters generic/Qualcomm/MediaTek + registry (full-match platform
patterns, B-07). Not in scope: writes/verification, tweak contracts, policy, daemon call path, KERNEL events, export,
WebUI, retiring `kernel_type`/`is_gki` writers. Next candidates: call path + Observatory KERNEL snapshot; B-28 in the
adapter write phase.

## Step 7.5 — Capability context integration bridge (APPROVED)

Done (`468cf39`): shared `CapabilityContext` (publish snapshot per publisher, resolve by confidence, Unknown preserved,
conflicts visible, no field merging); kernel export 1:1; planner receives the context (no policy change, tested);
Observatory observer interface only. Not in scope: kernel writes, policy, thermal, GameRuntime lifecycle, fluxd call
path, KERNEL/CAPABILITY events or storage, graphics/Synrei producers, SynthesisCore schema v4 (D-11).

## Step 7.6 — Runtime capability bootstrap (APPROVED)

Done (`0b5a7e4`): fluxd start -> Kernel Intelligence probe -> CapabilityContext publish -> engines query (const view).
Failure never stops the daemon; unavailable stays Unknown; repeat replaces the snapshot, failed repeat keeps the last good.
Not in scope: writes, performance/thermal decisions, periodic re-probe, Observatory events/persistence, SynthesisCore schema.

## Step 8 — Zairenkai Graphics Intelligence foundation (APPROVED)

Done (`3af9cc0`): read-only graphics capability facts (domain `graphics`), per-source GPU claims with conflict handling,
published in fluxd after the kernel bootstrap. Capability separate from policy. Not in scope: GPU policy/tuning/overclock,
driver changes, render injection, upscaling, thermal, WebUI, a Vulkan instance inside fluxd, feeding CLI Vulkan probes into
the context, SynthesisCore schema v4 mapping (B-29).

## Step 8.5 — Display and rendering capability foundation (APPROVED)

Done (`365c8df`): display (refresh modes/current/min/max/adaptive, resolution, HDR) and rendering (SurfaceFlinger,
composer, RenderEngine, frame-timing availability, pipeline) capability facts, conflict handling, fluxd start publish.
Not in scope: graphics policy, refresh forcing, frame boosting, injection, upscaling, SurfaceFlinger changes; moving
`RefreshMatcher`/`flux_refresh` onto these facts (B-12 remains).

## Step 8.6 — Runtime bottleneck observation foundation (APPROVED)

Done (`920cb88`): evidence-based assessment model + thermal context interface + facts export (domain `bottleneck`, not
published). Not in scope: optimisation actions, policy engine, WebUI, profile/GPU/thermal changes, a runtime sampler
(CPU/GPU utilisation, PSI), a Synrei adapter, Observatory events (B-33).

## Step 8.7 — Runtime metrics collector foundation (APPROVED — foundation complete, integration pending)

Done (`e0e4bdd`): read-only collector + bottleneck input mapping. Not in scope: optimisation, policy, kernel/GPU/thermal
changes, a periodic session sampler (call path), Synrei adapter, Observatory events, using metric confidence/verified as
evidence weights in BottleneckModel.

## Step 8.8 — Runtime metrics sampling lifecycle (ARCHITECTURALLY APPROVED)

Done (`d5194ac`): session-owned sampler lifecycle, bounded scheduling, BottleneckModel forwarding, sample observer
interface. Not in scope: Synrei thermal context (B-33B, owner: not yet), device threshold calibration (B-33C), an fps
source from SessionRecorder, using the assessment anywhere, Observatory events/persistence, policy of any kind.

## Step 8.8.1 — FPS observation bridge (ARCHITECTURALLY APPROVED; IN PROGRESS until CI/device validation)

Done (`d42b866`): read-only FPS observation from SessionRecorder into the sampler/bottleneck input with freshness and
ordering rules. Not in scope: SessionRecorder redesign, a second FPS collector, threshold changes, Synrei (B-33B),
calibration (B-33C), using or exporting the assessment.

## Step 8.9 — Synrei thermal context foundation (APPROVED)

Done (`7007422`): neutral thermal context boundary, Synrei adapter over `/dev/hico/state`, per-session thermal history as
BottleneckModel evidence. Not in scope: thermal control/limits/protection changes, thermal writes, HiCo changes (headroom /
cap ratio publication would need a HiCo-side decision), threshold calibration (B-33C), using or exporting assessments.

## Step 8.10 — Bottleneck result integration (ARCHITECTURALLY APPROVED; implementation complete)

Done (`3ae57cd`): neutral BottleneckResult, Observatory events for completed/failed assessments, emission once at session end.
Not in scope: any consumer/policy of the result, persistence/retention/export of events, WebUI, threshold calibration (B-33C).

## Step 8.11 — Observatory persistent storage and 7-day retention (IMPLEMENTATION COMPLETE; validation pending)

Done (`1ee1c7a`): persistent telemetry store, indexes/queries, rolling 7 × 24 h retention with clock safety, installation
epoch, failure isolation, read-only CLI hooks. Not in scope: WebUI/dashboard/export UI, sessions.json changes, threshold
changes, optimisation policy, cross-process locking for the CLI (read-only by design).

## Step 8.12 — Observatory historical analysis & explanation layer (IN PROGRESS: implementation complete, validation pending)

Done (`0c23fc0`): read-only session timelines, evidence-backed explanations (lifecycle, transaction, recovery, bottleneck,
thermal, FPS), deterministic summaries, bounded 7-day history patterns, read-only CLI. Not in scope: WebUI/dashboard, new
event producers (Synrei transitions, per-sample FPS — would be a separate owner decision), optimisation/policy, telemetry
format/retention changes.
