# Prebuilt

| File | Purpose |
|---|---|
| `synthesiscore.apk` | [SynthesisCore](https://github.com/FebriCahyaa/SynthesisCore), the system monitor fluxd reads its state from |
| `synthesiscore.apk.sha256` | Pinned SHA-256 of the APK above |
| `synthesiscore.json` | Release tag, checksum and signing certificate of the synced APK |

**Do not update these files by hand.** The `Sync SynthesisCore` workflow
(`.github/workflows/sync_synthesiscore.yml`) replaces them through a pull request, only after the
downloaded release APK passed every check:

1. its SHA-256 matches the checksum published with the release;
2. it is signed by exactly one certificate, equal to the pinned `SYNTHESISCORE_CERT_SHA256`
   (a tampered or re-signed APK fails here even if its checksum file was replaced as well);
3. optionally (`VERIFY_ATTESTATION=true`), its GitHub build provenance attestation verifies.

The checksum is then enforced twice more:

- **At build time** — `compile_zip.sh` refuses to package an APK that does not match
  `synthesiscore.apk.sha256`.
- **On the device** — `service.sh` re-hashes the installed APK on every boot and never runs it as
  root if it was modified.

## One-time setup

1. In SynthesisCore, publish a release (its workflow signs the APK). The release notes show the
   *Signing certificate SHA-256*; it is also attached as `SynthesisCore-vX.Y.Z.apk.cert.sha256`.
2. In Flux: **Settings → Secrets and variables → Actions → Variables**, add
   `SYNTHESISCORE_CERT_SHA256` with that value.
3. Optional:
   - `SYNTHESISCORE_TOKEN` (secret) — a read-only token, needed only if SynthesisCore is private.
   - `FLUX_DISPATCH_TOKEN` (secret, in **SynthesisCore**) — a token allowed to dispatch workflows
     on Flux, so a new release is synced immediately instead of at the next daily run.
   - `VERIFY_ATTESTATION=true` (variable) — also require the build provenance attestation.

To rotate the signing key, update `SYNTHESISCORE_CERT_SHA256` in the same change as the release
signed with the new key; until then the sync refuses the new APK.
