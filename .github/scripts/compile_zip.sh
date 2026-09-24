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

# Copy module files
cp -r ./libs module
cp -r ./scripts/* module/system/bin
cp gamelist.txt module
cp LICENSE module
cp NOTICE.md module
cp banner.webp module

find ./prebuilt -mindepth 1 -maxdepth 1 ! -name "README.md" -exec cp -r {} ./module \;
find ./config -mindepth 1 -maxdepth 1 ! -name "README.md" -exec cp -r {} ./module/config \;

# Remove .sh extension from scripts
find module/system/bin -maxdepth 1 -type f -name "*.sh" -exec sh -c 'mv -- "$0" "${0%.sh}"' {} \;

# Parse version info to module prop
zipName="flux-$version-$release_code.zip"
echo "zipName=$zipName" >>"$GITHUB_OUTPUT"

# Generate sha256sum for integrity checkup
bash .github/scripts/gen_sha256sum.sh "module"

cd ./module || {
	echo "Unable to cd to ./module" >&2
	exit 1
}

# Zip the file
zip -r9 ../"$zipName" * -x *placeholder* *.map .shellcheckrc
zip -z ../"$zipName" <<EOF
$version-$release_code
Build Date $(date +"%a %b %d %H:%M:%S %Z %Y")
EOF
