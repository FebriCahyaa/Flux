# Decisions

Append-only. Each entry: date, decision, reason, alternatives rejected. Reversing a decision means
adding a new entry that supersedes it.

## D-01 — Flux is the hub for ecosystem architecture and agent state (2026-09-29)
Architecture docs (`docs/architecture/`) and agent state (`docs/agent-state/`) live in Flux.
HiCo and SynthesisCore each carry their own `REPOSITORY_DATA_INVENTORY.md` and
`DELETION_MANIFEST.md`, because the preservation lock applies per repository and an agent working
in one repository must see its own protections.
Rejected: duplicating all docs in three repositories (drift), or keeping HiCo/SynthesisCore
without local inventories (their data would be unprotected in-repo).

## D-02 — Phase 0 is based on default branches; unmerged work is audited, not merged (2026-09-29)
Work branch `ccr-0dc934d1-0a6zta` in each repository starts from the default branch. Unmerged
branches were fetched read-only and recorded. Merging another line of work (e.g. Flux Game
Runtime) is the owner's decision (B-01, B-03).
Rejected: merging `ccr-23925ebb-375wkg` into the work branch (would mix unreviewed work into a
documentation phase and hide it from its own review).

## D-03 — New names are design targets only (2026-09-29)
Zairenkai (Flux), Aeyrin (SynthesisCore), Synrei (HiCo Thermal) are approved design targets. No
production identifier, module id, path, package or brand string changes before the Phase 1
naming registry and clearance audit, and before Phase 13 migration.

## D-04 — Frozen external contracts (2026-09-29)
Unchanged in format and location until a versioned successor is published alongside and every
consumer has moved:
`/data/adb/.config/flux/{current_profile,gameinfo,.lock,synthesis_core.json}`,
`FluxProfileMode` integer values, module ids `flux` / `hico`, `/data/adb/modules/flux`,
`update.json`, `update-arm64.json`, `update-arm.json`, `update/changelog.md`,
`com.febricahyaa.synthesiscore.MainKt` CLI (`--resolve`, `--version`, monitor mode),
`prebuilt/` pins.

## D-05 — Repository retention and runtime retention are separate (2026-09-29)
The 7-day telemetry retention (Phase 5) applies only to device-side
`/data/adb/.config/zairenkai/telemetry/`. Repository data follows the preservation lock and
`DELETION_MANIFEST.md`.

## D-06 — Documentation language (2026-09-29)
Repository documents are written in English to match the existing READMEs and code comments.

## D-07 — Commit identity (2026-09-29)
Commits in all three repositories use `FebriCahyaa <febricahya12345@gmail.com>` as author and
committer (owner instruction). Signing uses the session's configured SSH signing program.

## D-08 — Phase 1 scope (owner, 2026-09-29)
Phase 1 is limited to the naming registry and availability evidence audit. No production rename,
no path migration, no identifier changes.

## D-09 — Do not merge `Flux@ccr-23925ebb-375wkg` directly (owner, 2026-09-29)
First audit its diff against `main` and prepare an integration report; merge decision follows.

## D-10 — Zygisk provider preserved temporarily under constraints (owner, 2026-09-29)
Must remain optional, process-scoped, compatibility-only; no hardware spoofing, no anti-cheat
bypass, no anti-detection. The identity-substitution layer on the unmerged branch does not meet
"no hardware spoofing" and must not enter the integration line as is.

## D-11 — Capability schema v4 ownership (owner, 2026-09-29)
Owned by current SynthesisCore until the migration architecture is documented.

## D-12 — HiCo README is a historical phase document (owner, 2026-09-29)
Preserve; never remove.

## D-13 — Aeyrin public-brand stage stopped (2026-09-29)
Phase 1 evidence found existing non-software uses and a taken GitHub handle (B-13). Internal
design use continues; public branding waits for owner decision and trademark search.
