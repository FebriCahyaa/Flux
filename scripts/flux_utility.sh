#!/system/bin/sh
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

# shellcheck disable=SC2317,SC3006,SC3018,SC3034,SC3057,SC3037

# Config dir
MODULE_CONFIG="/data/adb/.config/flux"

change_cpu_gov() {
	chmod 644 /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
	chmod 644 /sys/devices/system/cpu/cpufreq/policy*/scaling_governor
	chown 0:0 /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
	chown 0:0 /sys/devices/system/cpu/cpufreq/policy*/scaling_governor
	echo "$1" | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
	echo "$1" | tee /sys/devices/system/cpu/cpufreq/policy*/scaling_governor >/dev/null
}

# change_gpu_gov <governor>: live GPU governor change from the WebUI. The
# kernel's own governor is kept in the same backup file the profiler uses, so
# an empty governor (or the next balanced/powersave profile) can restore it.
change_gpu_gov() {
	backup=/dev/.flux_boost_orig
	for node in /sys/class/kgsl/kgsl-3d0/devfreq/governor /sys/class/devfreq/*gpu*/governor \
		/sys/class/devfreq/*mali*/governor /sys/class/devfreq/*g3d*/governor; do
		[ -f "$node" ] || continue
		grep -q "^gpugov $node " "$backup" 2>/dev/null ||
			echo "gpugov $node $(stat -c %a "$node") $(cat "$node")" >>"$backup"
		gov="$1"
		if [ -z "$gov" ] || ! grep -qw -- "$gov" "${node%/governor}/available_governors" 2>/dev/null; then
			gov=$(awk -v n="$node" '$1 == "gpugov" && $2 == n { print $4; exit }' "$backup")
		fi
		chmod 644 "$node"
		echo "$gov" >"$node"
		return 0
	done
}

# capabilities: which tweaks this device and kernel can actually use, as JSON.
# The WebUI hides switches for features that would do nothing here.
capabilities() {
	has() { [ -e "$1" ] && echo true || echo false; }

	kernel=$(uname -r)
	ktype=$(cat "$MODULE_CONFIG/kernel_type" 2>/dev/null)
	[ -n "$ktype" ] || ktype=unknown

	cc_avail=$(cat /proc/sys/net/ipv4/tcp_available_congestion_control 2>/dev/null)

	gpu=false
	for node in /sys/class/kgsl/kgsl-3d0/devfreq/governor /sys/class/devfreq/*gpu*/governor \
		/sys/class/devfreq/*mali*/governor /sys/class/devfreq/*g3d*/governor; do
		[ -f "$node" ] && gpu=true && break
	done

	# More than one refresh rate in the display modes
	rates=$(dumpsys display 2>/dev/null | grep -o 'fps=[0-9.]*' | sort -u | wc -l)
	refresh=false
	[ "${rates:-0}" -gt 1 ] && refresh=true

	core_ctl=false
	for node in /sys/devices/system/cpu/cpu*/core_ctl/min_cpus; do
		[ -f "$node" ] && core_ctl=true && break
	done
	sched_boost=false
	{ [ -f /proc/sys/walt/sched_boost ] || [ -f /proc/sys/kernel/sched_boost ]; } && sched_boost=true
	mali=false
	[ -n "$(find /sys/devices/platform/ -maxdepth 3 -name power_policy -path '*mali*' 2>/dev/null | head -n 1)" ] && mali=true

	printf '{"kernel":"%s","kernel_type":"%s","uclamp":%s,"schedtune":%s,"touchpanel":%s,' \
		"$kernel" "$ktype" "$(has /dev/cpuctl/top-app/cpu.uclamp.min)" "$(has /dev/stune/top-app)" "$(has /proc/touchpanel)"
	printf '"net":%s,"net_cc":"%s","refresh":%s,"gpu_governor":%s,"surface":%s,' \
		"$(has /proc/sys/net/ipv4/tcp_congestion_control)" "$cc_avail" "$refresh" "$gpu" "$(has /dev/cpuset/top-app/tasks)"
	printf '"core_ctl":%s,"sched_boost":%s,"kgsl":%s,"mali":%s}\n' \
		"$core_ctl" "$sched_boost" "$(has /sys/class/kgsl/kgsl-3d0/force_rail_on)" "$mali"
}

# Best-effort ROM family from well-known vendor properties.
# Custom ROMs for Xiaomi devices often keep vendor props such as
# ro.miui.ui.version.name, so the props alone do not mean MIUI/HyperOS: also
# require the MIUI framework to be present.
has_miui_framework() {
	for jar in /system/framework/miui-framework.jar /system_ext/framework/miui-framework.jar \
		/system/framework/miui-services.jar /system_ext/framework/miui-services.jar; do
		[ -e "$jar" ] && return 0
	done
	[ -d /system/app/MiuiSystemUI ] || [ -d /system_ext/priv-app/MiuiSystemUI ] ||
		[ -d /system/priv-app/MiuiSystemUI ]
}

detect_rom() {
	local base
	if [ -n "$(getprop ro.lineage.version)" ]; then
		base="LineageOS-based $(getprop ro.lineage.version)"
	else
		base="AOSP-based ($(getprop ro.build.display.id))"
	fi

	if [ -n "$(getprop ro.mi.os.version.name)$(getprop ro.miui.ui.version.name)" ]; then
		if has_miui_framework; then
			if [ -n "$(getprop ro.mi.os.version.name)" ]; then
				echo "HyperOS $(getprop ro.mi.os.version.name)"
			else
				echo "MIUI $(getprop ro.miui.ui.version.name)"
			fi
			return
		fi
		base="$base, Xiaomi vendor props"
	fi

	if [ -n "$(getprop ro.build.version.oplusrom)" ] && [ -e /system/framework/oplus-framework.jar ]; then
		echo "ColorOS / OxygenOS $(getprop ro.build.version.oplusrom)"
	elif [ -n "$(getprop ro.build.version.oneui)" ]; then
		echo "One UI $(getprop ro.build.version.oneui)"
	elif [ -n "$(getprop ro.build.version.emui)" ]; then
		echo "EMUI / HarmonyOS $(getprop ro.build.version.emui)"
	elif [ -n "$(getprop ro.vivo.os.version)" ]; then
		echo "OriginOS / Funtouch OS $(getprop ro.vivo.os.version)"
	else
		echo "$base"
	fi
}

# report: device diagnostics for bug reports and per-ROM tuning. Contains no
# serial numbers or accounts: only build, kernel, vendor-daemon and Flux data.
report() {
	section() { printf '\n== %s ==\n' "$1"; }
	node() { [ -e "$1" ] && printf '%s = %s\n' "$1" "$(head -c 200 "$1" 2>/dev/null | tr '\n' ' ')"; }

	section "Flux"
	echo "module: $(awk -F'=' '/^version=/ {print $2}' /data/adb/modules/flux/module.prop)"
	echo "profile: $(cat "$MODULE_CONFIG/current_profile" 2>/dev/null)"
	apk=/data/adb/modules/flux/synthesiscore.apk
	expected=$(cat "$apk.sha256" 2>/dev/null)
	actual=$(sha256sum "$apk" 2>/dev/null | cut -d' ' -f1)
	if [ -n "$expected" ] && [ "$expected" = "$actual" ]; then integrity=ok; else integrity=FAILED; fi
	tag=$(sed -n 's/.*"tag": *"\([^"]*\)".*/\1/p' /data/adb/modules/flux/synthesiscore.json 2>/dev/null)
	echo "synthesiscore: ${tag:-unknown} (sha256 $(echo "$actual" | cut -c1-16)…, integrity $integrity)"

	section "Device"
	echo "model: $(getprop ro.product.brand) $(getprop ro.product.model) ($(getprop ro.product.device))"
	echo "soc: $(getprop ro.soc.manufacturer) $(getprop ro.soc.model) / $(getprop ro.board.platform)"
	echo "android: $(getprop ro.build.version.release) (SDK $(getprop ro.build.version.sdk)), patch $(getprop ro.build.version.security_patch)"
	echo "rom: $(detect_rom)"
	echo "fingerprint: $(getprop ro.build.fingerprint)"

	section "ROM properties"
	getprop | grep -iE 'mi\.os|miui|oplus|oneui|emui|vivo\.os|flyme|nothing|powerkeeper|joyose' | head -40

	section "Kernel"
	echo "uname: $(uname -r -m)"
	echo "gki: $(cat "$MODULE_CONFIG/is_gki" 2>/dev/null)"
	for n in /dev/cpuctl/top-app/cpu.uclamp.min /dev/cpuctl/top-app/cpu.uclamp.latency_sensitive \
		/dev/stune/top-app/schedtune.boost /dev/cpuset/top-app/cpus /dev/cpuset/background/cpus; do
		node "$n"
	done
	for p in /sys/devices/system/cpu/cpufreq/policy*; do
		[ -d "$p" ] && echo "$(basename "$p"): cpus=$(cat "$p/related_cpus") gov=$(cat "$p/scaling_governor") avail=[$(cat "$p/scaling_available_governors")]"
	done
	echo "thermal zones: $(ls -d /sys/class/thermal/thermal_zone* 2>/dev/null | wc -l)"

	section "Vendor services"
	ps -A -o NAME 2>/dev/null | grep -iE 'joyose|powerkeeper|thermal|horae|orms|hans|athena|perfd|perf-hal|power|gameturbo|migt|scx' | sort -u

	section "Vendor nodes"
	node /sys/class/thermal/thermal_message/sconfig
	[ -d /sys/module/migt/parameters ] && echo "migt: $(ls /sys/module/migt/parameters | tr '\n' ' ')"
	[ -d /proc/touchpanel ] && echo "touchpanel: $(ls /proc/touchpanel | tr '\n' ' ')"
	node /proc/oplus_scheduler/sched_assist/sched_assist_enabled
	node /proc/ppm/enabled

	section "HiCo Thermal"
	if [ -x /data/adb/modules/hico/system/bin/hicod ]; then
		/data/adb/modules/hico/system/bin/hicod status 2>&1
		echo "-- thermal"
		/data/adb/modules/hico/system/bin/hicod zones 2>&1
	else
		echo "not installed"
	fi

	section "System monitor"
	echo "mode: $(cat "$MODULE_CONFIG/monitor_mode" 2>/dev/null || echo unknown)"
	cat "$MODULE_CONFIG/synthesis_core.json" 2>/dev/null
	echo "-- binder codes"
	cat "$MODULE_CONFIG/binder_codes" 2>/dev/null
	echo "-- capabilities"
	if [ "$integrity" = ok ]; then
		timeout 15 app_process -Djava.class.path="$apk" / com.febricahyaa.synthesiscore.MainKt --capabilities 2>&1 | grep -v '^WARNING'
	else
		echo "skipped: APK integrity check failed"
	fi
}

save_logs() {
	report_dir="$MODULE_CONFIG/flux_bugreport_temp"
	mkdir -p "$report_dir/pstore"

	device=$(getprop ro.product.vendor.device)
	[ -z "$device" ] && device=$(getprop ro.product.device)
	log_file="flux_bugreport_${device:-device}_$(date +"%Y-%m-%d_%H-%M-%S").tar.gz"
	SOC="Unknown"

	case $(<$MODULE_CONFIG/soc_recognition) in
	1) SOC="MediaTek" ;;
	2) SOC="Snapdragon" ;;
	3) SOC="Exynos" ;;
	4) SOC="Unisoc" ;;
	5) SOC="Tensor" ;;
	6) SOC="Intel" ;;
	7) SOC="Tegra" ;;
	8) SOC="Kirin" ;;
	esac

	{
		echo "*****************************************************"
		echo "Flux Tweaks Log"
		echo "Module Version: $(awk -F'=' '/version=/ {print $2}' /data/adb/modules/flux/module.prop)"
		echo "Chipset: $SOC $(getprop ro.board.platform)"
		echo "Fingerprint: $(getprop ro.build.fingerprint)"
		echo "Android SDK: $(getprop ro.build.version.sdk)"
		echo "Kernel: $(uname -r -m)"
		echo "*****************************************************"
		echo ""
		[ -f "$MODULE_CONFIG/flux.log" ] && cat "$MODULE_CONFIG/flux.log"
	} >"$report_dir/flux.log"

	# Earlier parts of the log: rotated at 2 MB (flux.1.log) and the previous boot (flux.prev.log)
	for f in flux.1.log flux.prev.log; do
		[ -f "$MODULE_CONFIG/$f" ] && cp "$MODULE_CONFIG/$f" "$report_dir/"
	done

	# Settings and state: what Flux was configured to do and what it was doing
	mkdir -p "$report_dir/state"
	for f in config.json device_mitigation.json current_profile gameinfo synthesis_core.json \
		session_live.json sessions.json soc_recognition binder_codes monitor_mode; do
		[ -f "$MODULE_CONFIG/$f" ] && cp "$MODULE_CONFIG/$f" "$report_dir/state/"
	done
	[ -f "$MODULE_CONFIG/gamelist.json" ] && echo "$(grep -c '"lite_mode"' "$MODULE_CONFIG/gamelist.json") games" >"$report_dir/state/gamelist_count.txt"
	# Stock values saved by Flux Sched / Flux Boost, and the boosted game threads
	for f in .flux_sched_orig .flux_boost_orig .flux_game_prio; do
		[ -f "/dev/$f" ] && cp "/dev/$f" "$report_dir/state/${f#.}.txt"
	done
	{
		for p in ro.product.vendor.device ro.product.model ro.build.display.id ro.modversion \
			ro.mi.os.version.name ro.mi.os.version.incremental ro.miui.ui.version.name \
			ro.lineage.version ro.build.version.release ro.build.version.incremental ro.product.cpu.abilist; do
			echo "$p=$(getprop "$p")"
		done
		echo "uptime=$(cat /proc/uptime)"
	} >"$report_dir/state/props.txt"

	# HiCo Thermal (Flux add-on), when installed
	hicod=/data/adb/modules/hico/system/bin/hicod
	if [ -x "$hicod" ]; then
		{
			echo "== status"; "$hicod" status
			echo; echo "== device"; "$hicod" device
			echo; echo "== monitor"; "$hicod" monitor --once
		} >"$report_dir/hico_state.txt" 2>&1
	fi

	[ -f "$MODULE_CONFIG/sysmon.log" ] && cp "$MODULE_CONFIG/sysmon.log" "$report_dir/"
	[ -f "$MODULE_CONFIG/sysmon.log.prev" ] && cp "$MODULE_CONFIG/sysmon.log.prev" "$report_dir/"
	[ -f /data/adb/.config/hico/hico.log ] && cp /data/adb/.config/hico/hico.log "$report_dir/"
	report >"$report_dir/device_report.txt" 2>&1
	cp -r /sys/fs/pstore/. "$report_dir/pstore/" 2>/dev/null

	(
		cd "$report_dir"
		[ -f "$log_file" ] && rm -f "$log_file"
		tar -czf "$log_file" .
	)

	target_dir="/sdcard/Download"
	[ ! -d "$target_dir" ] && mkdir -p "$target_dir"

	if [ -f "$report_dir/$log_file" ]; then
		cp "$report_dir/$log_file" "$target_dir/$log_file"
		echo "$target_dir/$log_file"
		rm -rf "$report_dir"
		return 0
	else
		rm -rf "$report_dir"
		return 1
	fi
}

logcat() {
	# Clear screen
	echo -ne "\e[H\e[2J\e[3J"

	# Trap CTRL+C and exit gracefully
	trap 'echo -ne "\e[H\e[2J\e[3J"; exit 0' INT

	# Detect SoC
	SOC="Unknown"
	case $(<$MODULE_CONFIG/soc_recognition) in
	1) SOC="MediaTek" ;;
	2) SOC="Snapdragon" ;;
	3) SOC="Exynos" ;;
	4) SOC="Unisoc" ;;
	5) SOC="Tensor" ;;
	6) SOC="Intel" ;;
	7) SOC="Tegra" ;;
	8) SOC="Kirin" ;;
	esac

	# Header
	echo -e "\e[1;36m┌────────────────────────────────────────────┐"
	echo -e "│          \e[1;37mFlux Tweaks Log Viewer\e[1;36m          │"
	echo -e "└────────────────────────────────────────────┘\e[0m"

	# Info block
	echo -e "
\e[1;32mModule Version:\e[0m $(awk -F'=' '/version=/ {print $2}' /data/adb/modules/flux/module.prop)
\e[1;32mChipset:\e[0m        $SOC $(getprop ro.board.platform)
\e[1;32mFingerprint:\e[0m    $(getprop ro.build.fingerprint)
\e[1;32mAndroid SDK:\e[0m    $(getprop ro.build.version.sdk)
\e[1;32mKernel:\e[0m         $(uname -r -m)

\e[1;33m[Log Stream Started — press CTRL+C to exit]\e[0m
"

	# Tail log
	tail -f $MODULE_CONFIG/flux.log | while read -r line; do
		timestamp="${line:0:23}"
		level_char=$(echo "$line" | awk '{print $3}')
		msg="${line:24}"

		# Set color based on level
		case "$level_char" in
		W) level_color="\e[1;33m" ;; # Yellow
		E) level_color="\e[1;31m" ;; # Red
		C) level_color="\e[1;31m" ;; # Red
		*) level_color="\e[0m" ;;    # Default
		esac

		echo -e "\e[1;32m$timestamp\e[0m ${level_color}${msg}\e[0m"
	done
}

# shellcheck disable=SC2068
$@
