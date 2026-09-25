#!/bin/env bash
# shellcheck disable=SC2035

if [ -z "$GITHUB_WORKSPACE" ]; then
	echo "This script should only run on GitHub action!" >&2
	exit 1
fi

# Make sure we're on right directory
cd "$GITHUB_WORKSPACE" || {
	echo "Unable to cd to GITHUB_WORKSPACE" >&2
	exit 1
}

# Version info
version="$(cat version)"
version_code="$(git rev-list HEAD --count)"
release_code="$(git rev-list HEAD --count)-$(git rev-parse --short HEAD)-release"
sed -i "s/version=.*/version=$version ($release_code)/" module/module.prop
sed -i "s/versionCode=.*/versionCode=$version_code/" module/module.prop

# Refuse to package a prebuilt that does not match its pinned checksum.
# prebuilt/synthesiscore.apk.sha256 is written by the sync workflow only after
# the APK's checksum and signing certificate were verified.
pinned_sha="$(cat prebuilt/synthesiscore.apk.sha256 2>/dev/null)"
actual_sha="$(sha256sum prebuilt/synthesiscore.apk | cut -d' ' -f1)"
if [ -z "$pinned_sha" ] || [ "$pinned_sha" != "$actual_sha" ]; then
	echo "::error::prebuilt/synthesiscore.apk ($actual_sha) does not match its pinned checksum ($pinned_sha)" >&2
	exit 1
fi

# Copy module files (binaries are added per flavor below)
cp -r ./scripts/* module/system/bin
cp gamelist.txt module
cp LICENSE module
cp NOTICE.md module
cp banner.webp module

find ./prebuilt -mindepth 1 -maxdepth 1 ! -name "README.md" -exec cp -r {} ./module \;
find ./config -mindepth 1 -maxdepth 1 ! -name "README.md" -exec cp -r {} ./module/config \;

# Remove .sh extension from scripts
find module/system/bin -maxdepth 1 -type f -name "*.sh" -exec sh -c 'mv -- "$0" "${0%.sh}"' {} \;

for abi in arm64-v8a armeabi-v7a; do
	[ -f "libs/$abi/fluxd" ] || {
		echo "::error::libs/$abi/fluxd is missing (run ndk-build first)" >&2
		exit 1
	}
done

# Three flavors, each with its own update channel so a root manager keeps a
# device on the build it installed:
#   arm64      64-bit only (arm64-v8a), including 64-bit-only ROMs
#   arm        32-bit only (armeabi-v7a), for ROMs with a 32-bit userspace
#   universal  both binaries, the installer picks the device's ABI
build_flavor() { # <flavor> <update json> <abi>...
	flavor=$1
	update_json=$2
	shift 2

	stage="$(mktemp -d)"
	cp -r module/. "$stage"
	sed -i "s#/main/update.json#/main/$update_json#" "$stage/module.prop"
	echo "$flavor" >"$stage/flavor"
	for abi in "$@"; do
		mkdir -p "$stage/libs/$abi"
		cp -r "libs/$abi/." "$stage/libs/$abi/"
	done

	# Integrity: customize.sh / verify.sh check every extracted file against these.
	bash .github/scripts/gen_sha256sum.sh "$stage" >/dev/null

	zip="flux-$version-$release_code-$flavor.zip"
	rm -f "$GITHUB_WORKSPACE/$zip"
	(cd "$stage" && zip -qr9 "$GITHUB_WORKSPACE/$zip" . -x '*placeholder*' '*.map' .shellcheckrc)
	zip -qz "$zip" <<EOZ
$version-$release_code ($flavor)
Build Date $(date +"%a %b %d %H:%M:%S %Z %Y")
EOZ
	rm -rf "$stage"
	echo "$zip"
}

zip_arm64=$(build_flavor arm64 update-arm64.json arm64-v8a)
zip_arm=$(build_flavor arm update-arm.json armeabi-v7a)
zip_universal=$(build_flavor universal update.json arm64-v8a armeabi-v7a)

{
	echo "zipName=$zip_universal"
	echo "zipArm64=$zip_arm64"
	echo "zipArm=$zip_arm"
	echo "zips<<EOF"
	printf '%s\n' "$zip_arm64" "$zip_arm" "$zip_universal"
	echo "EOF"
} >>"$GITHUB_OUTPUT"
