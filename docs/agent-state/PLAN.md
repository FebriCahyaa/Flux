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
Scheduled after Phase 1; no merge until the report is accepted.

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
