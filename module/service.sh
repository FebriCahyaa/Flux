#
# Copyright (C) 2024-2026 FebriCahyaa
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

MODDIR=$(dirname "$0")
MODULE_CONFIG="/data/adb/.config/flux"
CLEANUP_SCRIPT="/data/adb/service.d/.flux_cleanup.sh"
CPUFREQ="/sys/devices/system/cpu/cpu0/cpufreq"
SYSMON_PKG="com.febricahyaa.synthesiscore"

# Restore original module.prop
[ -f "$MODDIR/module.prop.orig" ] && {
  cp "$MODDIR/module.prop.orig" "$MODDIR/module.prop"
}

# Rotate logs: keep previous boot's sysmon.log for crash diagnosis
[ -f "$MODULE_CONFIG/sysmon.log" ] && mv "$MODULE_CONFIG/sysmon.log" "$MODULE_CONFIG/sysmon.log.prev"
rm -f "$MODULE_CONFIG/flux.log"

# Parse Governor to use
chmod 644 "$CPUFREQ/scaling_governor"
default_gov=$(cat "$CPUFREQ/scaling_governor")
echo "$default_gov" >$MODULE_CONFIG/default_cpu_gov

# Create cleanup script
[ ! -f "$CLEANUP_SCRIPT" ] && {
  mkdir -p "$(dirname $CLEANUP_SCRIPT)"
  cp "$MODDIR/cleanup.sh" "$CLEANUP_SCRIPT"
  chmod +x "$CLEANUP_SCRIPT"
}

# Wait until boot completed
while [ -z "$(getprop sys.boot_completed)" ]; do
	sleep 40
done

# Handle case when 'default_gov' is performance
default_gov_preferred_array="
scx
schedhorizon
walt
sched_pixel
sugov_ext
uag
schedplus
energy_step
schedutil
interactive
conservative
powersave
"

if [ "$default_gov" == "performance" ]; then
	for gov in $default_gov_preferred_array; do
		grep -q "$gov" "$CPUFREQ/scaling_available_governors" && {
			echo "$gov" >$MODULE_CONFIG/default_cpu_gov
			default_gov="$gov"
			break
		}
	done
fi

# Revert to normal CPU governor
echo "$default_gov" | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# ── Detect GKI vs Non-GKI kernel and cache result ────────────────────────────
KERNEL_VER=$(uname -r)
if echo "$KERNEL_VER" | grep -qE "\-android[0-9]+-"; then
	IS_GKI=1
	echo "GKI" > "$MODULE_CONFIG/kernel_type"
else
	IS_GKI=0
	echo "Non-GKI" > "$MODULE_CONFIG/kernel_type"
fi
echo "$IS_GKI" > "$MODULE_CONFIG/is_gki"

# ── Thermal headroom API availability check ───────────────────────────────────
# Android Thermal API (getThermalHeadroom) requires API level 31+ (Android 12).
# On GKI kernels, the Java-side daemon exposes this via SynthesisCore.
# Write a hint file so the WebUI can show a meaningful explanation when unavailable.
SDK=$(getprop ro.build.version.sdk 2>/dev/null || echo "0")
if [ "$SDK" -lt 31 ]; then
	echo "unsupported_api_level" > "$MODULE_CONFIG/thermal_api_status"
elif [ "$IS_GKI" -eq 1 ]; then
	echo "gki_may_vary" > "$MODULE_CONFIG/thermal_api_status"
else
	echo "supported" > "$MODULE_CONFIG/thermal_api_status"
fi

# Mitigate buggy thermal throttling on post-startup
# in old MediaTek devices.
ENABLE_PPM="/proc/ppm/enabled"
if [ -f "$ENABLE_PPM" ]; then
	echo 0 >"$ENABLE_PPM"
	sleep 1
	echo 1 >"$ENABLE_PPM"
fi

# ── Exempt SynthesisCore from battery optimizations ──────────────────────────
# MIUI/HyperOS Powerkeeper aggressively kills background app_process instances.
# We whitelist the package in Android's device idle controller and Doze so it
# is never restricted or force-stopped by the system.
exempt_synthesiscore() {
	# Standard Android battery optimization whitelist (all ROMs)
	cmd appops set "$SYSMON_PKG" RUN_IN_BACKGROUND allow  2>/dev/null || true
	cmd appops set "$SYSMON_PKG" RUN_ANY_IN_BACKGROUND allow 2>/dev/null || true
	dumpsys deviceidle whitelist +"$SYSMON_PKG" 2>/dev/null || true

	# MIUI/HyperOS Powerkeeper exemption
	# Sets the app standby bucket to ACTIVE (10) so it is never restricted.
	cmd activity set-standby-bucket "$SYSMON_PKG" active 2>/dev/null || true

	# Additional MIUI-specific exemption via Settings provider
	settings put global "smart_power_no_restrict_apps_list" \
		"$(settings get global smart_power_no_restrict_apps_list 2>/dev/null):$SYSMON_PKG" \
		2>/dev/null || true
}

exempt_synthesiscore

# ── Start SynthesisCore companion daemon with watchdog ───────────────────────
# Powerkeeper or other battery management on MIUI/HyperOS ROMs may kill the
# app_process companion daemon. The watchdog loop detects this and restarts it
# so fluxd always has a live Java lock to wait on.
start_synthesiscore() {
	# Remove stale lock from a previous session so tryLock() succeeds immediately.
	rm -f "$MODULE_CONFIG/java.lock"

	nohup app_process \
		-Djava.class.path="$MODDIR/synthesiscore.apk" / \
		--nice-name=FluxSysMon \
		com.febricahyaa.synthesiscore.MainKt \
		"$MODULE_CONFIG/synthesis_core.json" \
		"$MODULE_CONFIG/java.lock" \
		>>"$MODULE_CONFIG/sysmon.log" 2>&1 &
	echo $! > "$MODULE_CONFIG/sysmon.pid"
}

synthesiscore_alive() {
	local pid
	pid=$(cat "$MODULE_CONFIG/sysmon.pid" 2>/dev/null)
	[ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

# ── Resolve binder transaction codes (one-shot) ──────────────────────────────
# Transaction codes differ between Android versions and ROMs. SynthesisCore's
# --resolve mode looks them up once per boot so native code can issue binder
# calls directly. Entries missing on this ROM are logged and omitted.
# Runs in the background under a timeout so it can never block boot.
#
# Only runs when the running APK reports synthesis_version >= SYNTHESIS_MIN_VERSION:
# an older APK does not understand --resolve and would treat it as an output path.
SYNTHESIS_MIN_VERSION=2  # keep in sync with SYNTHESIS_CORE_MIN_VERSION in jni/include/Flux.hpp

synthesiscore_version() {
	local ver
	ver=$(sed -n 's/^synthesis_version \([0-9][0-9]*\)$/\1/p' "$MODULE_CONFIG/synthesis_core.json" 2>/dev/null)
	echo "${ver:-1}"
}

resolve_binder_codes() {
	timeout 15 app_process \
		-Djava.class.path="$MODDIR/synthesiscore.apk" / \
		--nice-name=FluxBinderResolver \
		com.febricahyaa.synthesiscore.MainKt \
		--resolve "$MODULE_CONFIG/binder_codes" \
		>>"$MODULE_CONFIG/sysmon.log" 2>&1 <<-EOF
		android.os.IPowerManager.Stub::TRANSACTION_isInteractive
		android.os.IPowerManager.Stub::TRANSACTION_isPowerSaveMode
		android.app.INotificationManager.Stub::TRANSACTION_getZenMode
		android.media.IAudioService.Stub::TRANSACTION_isMusicActive
		android.os.IThermalService.Stub::TRANSACTION_getThermalHeadroom
		android.os.IThermalService.Stub::TRANSACTION_getCurrentThermalStatus
		android.app.IActivityTaskManager.Stub::TRANSACTION_getFocusedRootTaskInfo
	EOF
}

resolve_binder_codes_when_supported() {
	# Wait (max ~10 s) for the fresh daemon to write its status file.
	local i=0
	while [ "$i" -lt 20 ] && ! grep -q '^synthesis_version ' "$MODULE_CONFIG/synthesis_core.json" 2>/dev/null; do
		sleep 0.5
		i=$((i + 1))
	done

	local ver
	ver=$(synthesiscore_version)
	if [ "$ver" -ge "$SYNTHESIS_MIN_VERSION" ]; then
		resolve_binder_codes
	else
		echo "$(date): SynthesisCore synthesis_version $ver < $SYNTHESIS_MIN_VERSION, skipping binder code resolve" \
			>>"$MODULE_CONFIG/sysmon.log"
	fi
}

# Drop status/codes from the previous boot so the version check only sees
# output written by the APK that is about to start.
rm -f "$MODULE_CONFIG/binder_codes" "$MODULE_CONFIG/synthesis_core.json"

start_synthesiscore
sleep 1  # Buffer for lock acquisition

resolve_binder_codes_when_supported &

# Watchdog: restart SynthesisCore if killed (runs in background)
(
	while true; do
		sleep 10
		if ! synthesiscore_alive; then
			echo "$(date): SynthesisCore died, restarting..." >> "$MODULE_CONFIG/sysmon.log"
			exempt_synthesiscore
			start_synthesiscore
			sleep 2
		fi
	done
) &
echo $! > "$MODULE_CONFIG/sysmon_watchdog.pid"

fluxd daemon
