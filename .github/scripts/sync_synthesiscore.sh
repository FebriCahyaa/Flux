#!/usr/bin/env bash
# =============================================================================
# sync_synthesiscore.sh — Fetch a SynthesisCore release and verify it
#
# Downloads SynthesisCore-<tag>.apk from a GitHub release and only accepts it if
#   1. its SHA-256 matches the published .sha256 file, and
#   2. it is signed by exactly one certificate whose SHA-256 digest equals the
#      pinned SYNTHESISCORE_CERT_SHA256 (a tampered or re-signed APK fails here,
#      even if the attacker also replaced the checksum file), and
#   3. optionally, its GitHub build provenance attestation verifies.
# On success it updates prebuilt/synthesiscore.apk, the pinned checksum and the
# manifest, and reports whether anything changed.
#
# Environment:
#   SOURCE_REPO                owner/repo of SynthesisCore            (required)
#   SYNTHESISCORE_CERT_SHA256  pinned signing certificate digest      (required)
#   REQUESTED_TAG              release tag, default: latest release
#   VERIFY_ATTESTATION         "true" to also require an attestation
#   GH_TOKEN                   token that can read SOURCE_REPO releases
#   GITHUB_OUTPUT              set by Actions; receives changed/tag/sha256
# =============================================================================

set -euo pipefail

PREBUILT_DIR="prebuilt"
APK="$PREBUILT_DIR/synthesiscore.apk"
PIN="$APK.sha256"
MANIFEST="$PREBUILT_DIR/synthesiscore.json"

fail() {
    echo "::error::$*" >&2
    exit 1
}

output() {
    [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "$1=$2" >>"$GITHUB_OUTPUT"
    echo "$1=$2"
}

# Normalise "AB:CD:..." / "abcd..." to 64 lowercase hex characters.
normalize_digest() {
    tr -d ': \n' <<<"$1" | tr '[:upper:]' '[:lower:]'
}

[[ -n "${SOURCE_REPO:-}" ]] || fail "SOURCE_REPO is not set"

pinned_cert=$(normalize_digest "${SYNTHESISCORE_CERT_SHA256:-}")
if [[ ! "$pinned_cert" =~ ^[0-9a-f]{64}$ ]]; then
    fail "Repository variable SYNTHESISCORE_CERT_SHA256 must hold the 64-hex SHA-256 digest of the SynthesisCore signing certificate (see prebuilt/README.md)"
fi

tag="${REQUESTED_TAG:-}"
if [[ -z "$tag" ]]; then
    tag=$(gh release view --repo "$SOURCE_REPO" --json tagName --jq .tagName) ||
        fail "Could not find the latest release of $SOURCE_REPO"
fi
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.]+)?$ ]] || fail "Unexpected tag '$tag'"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

asset="SynthesisCore-$tag.apk"
echo "Downloading $asset from $SOURCE_REPO@$tag"
gh release download "$tag" --repo "$SOURCE_REPO" --dir "$work" \
    --pattern "$asset" --pattern "$asset.sha256" ||
    fail "Release $tag has no $asset / $asset.sha256"

# 1. Checksum
(cd "$work" && sha256sum --check --strict "$asset.sha256") || fail "SHA-256 mismatch for $asset"
sha256=$(sha256sum "$work/$asset" | cut -d' ' -f1)

# 2. Signing certificate pin
build_tools="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}/build-tools"
[[ -d "$build_tools" ]] || fail "Android build-tools not found (ANDROID_HOME)"
apksigner="$build_tools/$(ls "$build_tools" | sort -V | tail -1)/apksigner"

certs=$("$apksigner" verify --print-certs "$work/$asset") || fail "APK signature does not verify"
signers=$(grep -c '^Signer #[0-9]* certificate SHA-256 digest:' <<<"$certs" || true)
[[ "$signers" -eq 1 ]] || fail "Expected exactly one signer, found $signers"
actual_cert=$(normalize_digest "$(sed -n 's/^Signer #1 certificate SHA-256 digest: //p' <<<"$certs")")
[[ "$actual_cert" == "$pinned_cert" ]] ||
    fail "Signing certificate $actual_cert does not match the pinned certificate $pinned_cert"

# 3. Build provenance (optional)
if [[ "${VERIFY_ATTESTATION:-false}" == "true" ]]; then
    gh attestation verify "$work/$asset" --repo "$SOURCE_REPO" >/dev/null ||
        fail "Build provenance attestation does not verify"
    echo "Attestation verified"
fi

echo "Verified $asset (sha256 $sha256, signer $actual_cert)"
output tag "$tag"
output sha256 "$sha256"

if [[ -f "$APK" ]] && [[ "$(sha256sum "$APK" | cut -d' ' -f1)" == "$sha256" ]]; then
    echo "prebuilt already at $tag"
    output changed false
    exit 0
fi

install -m 0644 "$work/$asset" "$APK"
echo "$sha256" >"$PIN"
cat >"$MANIFEST" <<EOF
{
  "repository": "$SOURCE_REPO",
  "tag": "$tag",
  "asset": "$asset",
  "sha256": "$sha256",
  "certificate_sha256": "$actual_cert"
}
EOF
output changed true
