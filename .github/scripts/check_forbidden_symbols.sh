#!/usr/bin/env bash
# Fails when identity-compatibility or provider code reaches the build path of this branch
# (docs/architecture/GAME_RUNTIME_MIGRATION_PLAN.md, owner decision D-10).
# Scans build inputs only: jni/, scripts/, module/, build files. Docs and tests are not scanned.
set -euo pipefail
cd "${1:-.}"

paths=(jni scripts module CMakeLists.txt .github/scripts/compile_zip.sh)
excludes=(--exclude-dir=external)

# Reading the real hardware (PlatformProbe/DeviceInfo __system_property_get, VulkanProbe
# vkGetPhysicalDeviceProperties) is capability probing and allowed. What is forbidden is
# intercepting or rewriting what a process reads, which is what these markers identify.
patterns=(
	'\bResolver\b' 'ProviderPlan' '\bArming\b' '[Zz]ygisk'
	'SetStaticObjectField'                        # android.os.Build / fingerprint field writes
	'flux_wrap_'                                  # interposed property/GL/EGL/Vulkan wrappers
	'GotHook|Interpose|got_patch|plt_hook'        # GOT/PLT patching
	'IdentityProfile|identity_profile|device_profile|gpu_profile|cpu_profile'
	'resetprop[^\n]*ro\.(product|build|hardware|board|soc)'  # ro.* identity rewrite
)

status=0
for p in "${patterns[@]}"; do
	if hits=$(grep -rnE "${excludes[@]}" -- "$p" "${paths[@]}" 2>/dev/null); then
		echo "::error::forbidden pattern '$p':"
		echo "$hits"
		status=1
	fi
done

# Packaging must never carry the provider library.
if grep -nE 'libflux_zygisk|zygisk_provider' .github/scripts/compile_zip.sh module/*.sh 2>/dev/null; then
	echo "::error::provider library referenced by packaging"
	status=1
fi

[ "$status" -eq 0 ] && echo "forbidden-symbol check: clean"
exit "$status"
