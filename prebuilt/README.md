# Prebuilt

| File | Purpose |
|---|---|
| `synthesiscore.apk` | [SynthesisCore](https://github.com/FebriCahyaa/SynthesisCore), the system monitor fluxd reads its state from |
| `synthesiscore.apk.sha256` | Pinned SHA-256 of the APK above |
| `synthesiscore.cert.sha256` | Pinned SHA-256 of the SynthesisCore signing certificate (`CN=FebriCahyaa, O=SynthesisCore`) |
| `synthesiscore.json` | Release tag, checksum and signing certificate of the synced APK |

**Do not update these files by hand.** The `Sync SynthesisCore` workflow
(`.github/workflows/sync_synthesiscore.yml`) replaces them through a pull request, only after the
downloaded release APK passed every check:

1. its SHA-256 matches the checksum published with the release;
2. it is signed by exactly one certificate, equal to the pin in `synthesiscore.cert.sha256`
   (a tampered or re-signed APK fails here even if its checksum file was replaced as well);
3. its GitHub build provenance attestation (Sigstore) verifies, proving it was built by the
   SynthesisCore release workflow from a commit in that repository.

The checksum is then enforced twice more:

- **At build time** — `compile_zip.sh` refuses to package an APK that does not match
  `synthesiscore.apk.sha256`.
- **On the device** — `service.sh` re-hashes the installed APK on every boot and never runs it as
  root if it was modified.

## Setup

Nothing is required: the certificate pin is committed here, and SynthesisCore is public, so the
workflow's own token can read its releases. The daily run picks up new releases.

Optional:

- `FLUX_DISPATCH_TOKEN` (secret, in **SynthesisCore**) — a fine-grained token with *Actions:
  write* (or *Contents: write*) on Flux, so a release is synced immediately.
- `VERIFY_ATTESTATION=false` (variable) — skip the attestation check, e.g. for releases made
  before the release workflow existed.
- `SYNTHESISCORE_TOKEN` (secret) — a read-only token, only if SynthesisCore becomes private.

The certificate pin is public information, not a secret. To read it from a signed APK:

```shell
apksigner verify --print-certs synthesiscore.apk | grep 'SHA-256 digest'
```

### Rotating the signing key

Update `synthesiscore.cert.sha256` in a reviewed commit before (or together with) the first
release signed with the new key; until then the sync refuses the new APK. The repository variable
`SYNTHESISCORE_CERT_SHA256` can override the file temporarily.
