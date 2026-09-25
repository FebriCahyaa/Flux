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
while [ "$(getprop sys.boot_completed)" != "1" ]; do
	sleep 2
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


# ── Undo old per-boot appends to MIUI/HyperOS settings ───────────────────────
# Earlier versions appended SynthesisCore's package to this list on every boot.
# Remove every copy (and the "null" the first append started from).
legacy_list=$(settings get global smart_power_no_restrict_apps_list 2>/dev/null)
case "$legacy_list" in
*com.febricahyaa.synthesiscore*)
	cleaned=$(echo "$legacy_list" | tr ':' '\n' | grep -vxE 'com\.febricahyaa\.synthesiscore|null|' | paste -sd ':' -)
	if [ -n "$cleaned" ]; then
		settings put global smart_power_no_restrict_apps_list "$cleaned"
	else
		settings delete global smart_power_no_restrict_apps_list
	fi
	;;
esac

# ── SynthesisCore integrity ──────────────────────────────────────────────────
# synthesiscore.apk runs as root through app_process (it is never installed as
# an app), so it is only executed while it matches the checksum verified at
# install time. A modified APK is never run.
verify_synthesiscore() {
	local expected actual
	expected=$(cat "$MODDIR/synthesiscore.apk.sha256" 2>/dev/null)
	actual=$(sha256sum "$MODDIR/synthesiscore.apk" 2>/dev/null | cut -d' ' -f1)
	[ -n "$expected" ] && [ "$expected" = "$actual" ] && return 0

	echo "$(date): SynthesisCore integrity check FAILED (expected ${expected:-none}, got ${actual:-none}); not running it. Reinstall Flux." \
		>>"$MODULE_CONFIG/sysmon.log"
	return 1
}

# ── Resolve binder transaction codes (one-shot, before fluxd) ────────────────
# Transaction codes differ between Android versions and ROMs. fluxd's native
# monitor needs them, so SynthesisCore's --resolve mode looks them up once per
# boot (about a second). Needs protocol >= 2; "--version" prints it, and an older
# APK would misread the flag, so the probe runs under a short timeout.
SYNTHESIS_MIN_VERSION=2 # keep in sync with SYNTHESIS_CORE_MIN_VERSION in jni/include/Flux.hpp

resolve_binder_codes() {
	verify_synthesiscore || return 1

	local ver
	ver=$(timeout 10 app_process -Djava.class.path="$MODDIR/synthesiscore.apk" / --nice-name=FluxBinderResolver \
		com.febricahyaa.synthesiscore.MainKt --version 2>/dev/null | tail -n 1)
	case "$ver" in '' | *[!0-9]*) ver=1 ;; esac
	if [ "$ver" -lt "$SYNTHESIS_MIN_VERSION" ]; then
		echo "$(date): SynthesisCore protocol $ver < $SYNTHESIS_MIN_VERSION, skipping binder code resolve" >>"$MODULE_CONFIG/sysmon.log"
		return 1
	fi

	# Keep in sync with kQueries in jni/base/NativeMonitor/NativeMonitor.cpp.
	timeout 20 app_process -Djava.class.path="$MODDIR/synthesiscore.apk" / --nice-name=FluxBinderResolver \
		com.febricahyaa.synthesiscore.MainKt --resolve "$MODULE_CONFIG/binder_codes" \
		>>"$MODULE_CONFIG/sysmon.log" 2>&1 <<-EOF
		android.os.IPowerManager.Stub::TRANSACTION_isInteractive
		android.os.IPowerManager.Stub::TRANSACTION_isPowerSaveMode
		android.app.IActivityManager.Stub::TRANSACTION_registerProcessObserver
		android.app.IProcessObserver.Stub::TRANSACTION_onForegroundActivitiesChanged
		android.app.IProcessObserver.Stub::TRANSACTION_onProcessDied
		android.content.pm.IPackageManager.Stub::TRANSACTION_getNameForUid
		android.hardware.display.IDisplayManager.Stub::TRANSACTION_registerCallback
		android.hardware.display.IDisplayManagerCallback.Stub::TRANSACTION_onDisplayEvent
		android.app.INotificationManager.Stub::TRANSACTION_getZenMode
		android.os.IThermalService.Stub::TRANSACTION_getThermalHeadroom
		android.os.IThermalService.Stub::TRANSACTION_getCurrentThermalStatus
		android.media.IAudioService.Stub::TRANSACTION_isMusicActive
		android.media.IAudioService.Stub::TRANSACTION_getMode
		android.app.IActivityTaskManager.Stub::TRANSACTION_getFocusedRootTaskInfo
	EOF
}

# ── Java companion daemon (fallback) ─────────────────────────────────────────
# Only used when fluxd's native monitor cannot start (e.g. a required binder
# code is missing on this ROM, or force_java_monitor exists). SynthesisCore then
# runs as a root process (FluxSysMon) and a watchdog restarts it if it dies.
start_synthesiscore() {
	verify_synthesiscore || return 1

	# Remove stale lock from a previous session so tryLock() succeeds immediately.
	rm -f "$MODULE_CONFIG/java.lock"

	nohup app_process \
		-Djava.class.path="$MODDIR/synthesiscore.apk" / \
		--nice-name=FluxSysMon \
		com.febricahyaa.synthesiscore.MainKt \
		"$MODULE_CONFIG/synthesis_core.json" \
		"$MODULE_CONFIG/java.lock" \
		>>"$MODULE_CONFIG/sysmon.log" 2>&1 &
	echo $! >"$MODULE_CONFIG/sysmon.pid"
}

synthesiscore_alive() {
	local pid
	pid=$(cat "$MODULE_CONFIG/sysmon.pid" 2>/dev/null)
	[ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

start_java_monitor() {
	start_synthesiscore || return 1
	(
		while true; do
			sleep 10
			if ! synthesiscore_alive; then
				echo "$(date): SynthesisCore died, restarting..." >>"$MODULE_CONFIG/sysmon.log"
				# A failed integrity check will not fix itself: stop watching.
				start_synthesiscore || break
				sleep 2
			fi
		done
	) &
	echo $! >"$MODULE_CONFIG/sysmon_watchdog.pid"
}

# Drop state from the previous boot.
rm -f "$MODULE_CONFIG/binder_codes" "$MODULE_CONFIG/synthesis_core.json" \
	"$MODULE_CONFIG/monitor_mode" "$MODULE_CONFIG/sysmon.pid" "$MODULE_CONFIG/sysmon_watchdog.pid"

resolve_binder_codes

# fluxd daemonizes, then reports which monitor it uses in monitor_mode.
fluxd daemon

i=0
while [ ! -s "$MODULE_CONFIG/monitor_mode" ] && [ "$i" -lt 30 ]; do
	sleep 0.5
	i=$((i + 1))
done

if [ "$(cat "$MODULE_CONFIG/monitor_mode" 2>/dev/null)" = "native" ]; then
	echo "$(date): fluxd uses the native monitor; SynthesisCore daemon not needed" >>"$MODULE_CONFIG/sysmon.log"
else
	echo "$(date): starting the SynthesisCore daemon (monitor_mode: $(cat "$MODULE_CONFIG/monitor_mode" 2>/dev/null || echo none))" \
		>>"$MODULE_CONFIG/sysmon.log"
	start_java_monitor
fi
