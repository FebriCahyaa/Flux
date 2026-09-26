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

###################################
# Variables
###################################

# Config dir
MODULE_CONFIG="/data/adb/.config/flux"

# SoC recognition
SOC=$(<$MODULE_CONFIG/soc_recognition)

# Default CPU Governor
DEFAULT_CPU_GOV="$FLUX_BALANCED_CPUGOV"

# ──────────────────────────────────────────────────────────────────────────────
# Kernel type: GKI / Non-GKI / Legacy
#
# GKI (Generic Kernel Image, Android 12+ on Linux 5.10 and later) kernels are
# built from the Android common kernel and tagged "-android<release>-" in
# `uname -r`, e.g. 5.15.123-android13-8-00123-g...  Vendor features live in
# loadable modules (WALT, core_ctl, kgsl), scheduler tunables moved from
# /proc/sys/kernel to debugfs (Linux 5.13+), and uclamp replaces schedtune.
#
# Non-GKI kernels are vendor/OEM trees (e.g. 4.19.191-perf+, 5.4.210-qgki).
# "android11-5.4" builds are GKI 1.0 and still vendor-specific, so they count
# as Non-GKI here.
#
# Legacy kernels are older than 4.19 (msm-3.18 / 4.4 / 4.9 era, HMP or early
# EAS with schedtune). Every tweak below still checks that its node exists.
# ──────────────────────────────────────────────────────────────────────────────

detect_kernel_type() {
	KERNEL_VER=$(uname -r)
	kmaj=${KERNEL_VER%%.*}
	kmin=${KERNEL_VER#*.}
	kmin=${kmin%%.*}
	case "$kmaj$kmin" in '' | *[!0-9]*) kmaj=0 kmin=0 ;; esac
	KVER=$((kmaj * 100 + kmin))

	IS_GKI=0
	if [ "$KVER" -ge 510 ] && echo "$KERNEL_VER" | grep -qE -- "-android(1[2-9]|[2-9][0-9])-"; then
		IS_GKI=1
		KERNEL_TYPE=gki
	elif [ "$KVER" -lt 419 ]; then
		KERNEL_TYPE=legacy
	else
		KERNEL_TYPE=non_gki
	fi
	# Cache result for logging / daemon / WebUI consumption
	echo "$IS_GKI" >"$MODULE_CONFIG/is_gki"
	echo "$KERNEL_TYPE" >"$MODULE_CONFIG/kernel_type"
}

# Wrapper: apply only on Non-GKI kernels
apply_non_gki() {
	[ "$IS_GKI" -eq 1 ] && return 0
	apply "$1" "$2"
}

# Wrapper: apply only on GKI kernels
apply_gki() {
	[ "$IS_GKI" -eq 0 ] && return 0
	apply "$1" "$2"
}

# Detect at script startup
detect_kernel_type

# Clock pinning (performance_profile sets it); other profiles never pin.
PIN_MAX=0

# HiCo Thermal owns the thermal layer when it is installed and not switched off:
# it unlocks thermal during games under its own safety guard and restores the
# exact stock values afterwards. Flux then leaves thermal nodes alone, so the
# two never write the same node and HiCo's guard is not bypassed.
hico_active() {
	[ -f /data/adb/modules/hico/module.prop ] || return 1
	[ -f /data/adb/modules/hico/disable ] && return 1
	[ -f /data/adb/modules/hico/remove ] && return 1
	! grep -q '^mode=off' /data/adb/.config/hico/hico.conf 2>/dev/null
}
HICO_ACTIVE=0
hico_active && HICO_ACTIVE=1

# Just a note that lite mode is now controlled by script arg, check case
# statement on the EOF and performance_profile() function.

# FLUX_* variables is set by daemon, see 'jni/src/FluxUtility/Profiler.cpp'.

###################################
# Common Function
###################################

apply() {
	[ ! -f "$2" ] && return 1
	chmod 644 "$2" >/dev/null 2>&1
	echo "$1" >"$2" 2>/dev/null
	chmod 444 "$2" >/dev/null 2>&1
}

write() {
	[ ! -f "$2" ] && return 1
	chmod 644 "$2" >/dev/null 2>&1
	echo "$1" >"$2" 2>/dev/null
}

# sched_feature <FEATURE>: debugfs moved the file in Linux 5.13
# (/sys/kernel/debug/sched_features -> /sys/kernel/debug/sched/features)
sched_feature() {
	for f in /sys/kernel/debug/sched_features /sys/kernel/debug/sched/features; do
		[ -f "$f" ] && echo "$1" >"$f" 2>/dev/null
	done
}

change_cpu_gov() {
	chmod 644 /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
	chmod 644 /sys/devices/system/cpu/cpufreq/policy*/scaling_governor
	chown 0:0 /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
	chown 0:0 /sys/devices/system/cpu/cpufreq/policy*/scaling_governor
	echo "$1" | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
	echo "$1" | tee /sys/devices/system/cpu/cpufreq/policy*/scaling_governor >/dev/null
}

###################################
# Frequency fetching
###################################

which_maxfreq() {
	tr ' ' '\n' <"$1" | sort -nr | head -n 1
}

which_minfreq() {
	tr ' ' '\n' <"$1" | grep -v '^[[:space:]]*$' | sort -n | head -n 1
}

which_midfreq() {
	total_opp=$(wc -w <"$1")
	mid_opp=$(((total_opp + 1) / 2))
	tr ' ' '\n' <"$1" | grep -v '^[[:space:]]*$' | sort -nr | head -n $mid_opp | tail -n 1
}

# MediaTek gpufreq
# Returns OPP index of the frequency

mtk_gpufreq_minfreq_index() {
	awk -F'[][]' '{print $2}' "$1" | tail -n 1
}

mtk_gpufreq_midfreq_index() {
	total_opp=$(wc -l <"$1")
	mid_opp=$(((total_opp + 1) / 2))
	awk -F'[][]' '{print $2}' "$1" | head -n $mid_opp | tail -n 1
}

###################################
# Frequency settings
###################################

cpufreq_ppm_max_perf() {
	cluster=-1
	for path in /sys/devices/system/cpu/cpufreq/policy*; do
		((cluster++))
		cpu_maxfreq=$(<"$path/cpuinfo_max_freq")
		write "$cluster $cpu_maxfreq" /proc/ppm/policy/hard_userlimit_max_cpu_freq

		[ $PIN_MAX -eq 0 ] && {
			cpu_midfreq=$(which_midfreq "$path/scaling_available_frequencies")
			write "$cluster $cpu_midfreq" /proc/ppm/policy/hard_userlimit_min_cpu_freq
			continue
		}

		write "$cluster $cpu_maxfreq" /proc/ppm/policy/hard_userlimit_min_cpu_freq
	done
}

cpufreq_max_perf() {
	for path in /sys/devices/system/cpu/*/cpufreq; do
		cpu_maxfreq=$(<"$path/cpuinfo_max_freq")
		apply "$cpu_maxfreq" "$path/scaling_max_freq"

		[ $PIN_MAX -eq 0 ] && {
			cpu_midfreq=$(which_midfreq "$path/scaling_available_frequencies")
			apply "$cpu_midfreq" "$path/scaling_min_freq"
			continue
		}

		apply "$cpu_maxfreq" "$path/scaling_min_freq"
	done
	chmod -f 444 /sys/devices/system/cpu/cpufreq/policy*/scaling_*_freq
}

cpufreq_ppm_unlock() {
	cluster=0
	for path in /sys/devices/system/cpu/cpufreq/policy*; do
		cpu_maxfreq=$(<"$path/cpuinfo_max_freq")
		cpu_minfreq=$(<"$path/cpuinfo_min_freq")
		write "$cluster $cpu_maxfreq" /proc/ppm/policy/hard_userlimit_max_cpu_freq
		write "$cluster $cpu_minfreq" /proc/ppm/policy/hard_userlimit_min_cpu_freq
		((cluster++))
	done
}

cpufreq_unlock() {
	for path in /sys/devices/system/cpu/*/cpufreq; do
		cpu_maxfreq=$(<"$path/cpuinfo_max_freq")
		cpu_minfreq=$(<"$path/cpuinfo_min_freq")
		write "$cpu_maxfreq" "$path/scaling_max_freq"
		write "$cpu_minfreq" "$path/scaling_min_freq"
	done
	chmod -f 644 /sys/devices/system/cpu/cpufreq/policy*/scaling_*_freq
}

devfreq_max_perf() {
	[ ! -f "$1/available_frequencies" ] && return 1
	max_freq=$(which_maxfreq "$1/available_frequencies")
	apply "$max_freq" "$1/max_freq"
	apply "$max_freq" "$1/min_freq"
}

devfreq_mid_perf() {
	[ ! -f "$1/available_frequencies" ] && return 1
	max_freq=$(which_maxfreq "$1/available_frequencies")
	mid_freq=$(which_midfreq "$1/available_frequencies")
	apply "$max_freq" "$1/max_freq"
	apply "$mid_freq" "$1/min_freq"
}

devfreq_unlock() {
	[ ! -f "$1/available_frequencies" ] && return 1
	max_freq=$(which_maxfreq "$1/available_frequencies")
	min_freq=$(which_minfreq "$1/available_frequencies")
	write "$max_freq" "$1/max_freq"
	write "$min_freq" "$1/min_freq"
}

devfreq_min_perf() {
	[ ! -f "$1/available_frequencies" ] && return 1
	freq=$(which_minfreq "$1/available_frequencies")
	apply "$freq" "$1/min_freq"
	apply "$freq" "$1/max_freq"
}

qcom_cpudcvs_max_perf() {
	[ ! -f "$1/available_frequencies" ] && return 1
	freq=$(which_maxfreq "$1/available_frequencies")
	apply "$freq" "$1/hw_max_freq"
	apply "$freq" "$1/hw_min_freq"
}

qcom_cpudcvs_mid_perf() {
	[ ! -f "$1/available_frequencies" ] && return 1
	max_freq=$(which_maxfreq "$1/available_frequencies")
	mid_freq=$(which_midfreq "$1/available_frequencies")
	apply "$max_freq" "$1/hw_max_freq"
	apply "$mid_freq" "$1/hw_min_freq"
}

qcom_cpudcvs_unlock() {
	[ ! -f "$1/available_frequencies" ] && return 1
	max_freq=$(which_maxfreq "$1/available_frequencies")
	min_freq=$(which_minfreq "$1/available_frequencies")
	write "$max_freq" "$1/hw_max_freq"
	write "$min_freq" "$1/hw_min_freq"
}

qcom_cpudcvs_min_perf() {
	[ ! -f "$1/available_frequencies" ] && return 1
	freq=$(which_minfreq "$1/available_frequencies")
	apply "$freq" "$1/hw_min_freq"
	apply "$freq" "$1/hw_max_freq"
}

###################################
# Device-specific performance profile
###################################

mediatek_performance() {
	# PPM policies
	if [ -d /proc/ppm ]; then
		grep -E "PWR_THRO|THERMAL" /proc/ppm/policy_status | while read -r row; do
			apply "${row:1:1} 0" /proc/ppm/policy_status
		done
	fi

	# Force off FPSGO
	apply 0 /sys/kernel/fpsgo/common/force_onoff

	# MTK Power and CCI mode
	apply 1 /proc/cpufreq/cpufreq_cci_mode
	apply 3 /proc/cpufreq/cpufreq_power_mode

	# DDR Boost mode
	apply 1 /sys/devices/platform/boot_dramboost/dramboost/dramboost

	# EAS/HMP Switch
	apply 0 /sys/devices/system/cpu/eas/enable

	# Disable GED KPI
	apply 0 /sys/module/sspm_v3/holders/ged/parameters/is_GED_KPI_enabled

	# GPU Frequency
	apply 0 /proc/gpufreq/gpufreq_opp_freq
	apply -1 /proc/gpufreqv2/fix_target_opp_index

	[ $PIN_MAX -eq 1 ] && {
		if [ -d /proc/gpufreqv2 ]; then
			apply 0 /proc/gpufreqv2/fix_target_opp_index
		else
			gpu_freq=$(sed -n 's/.*freq = \([0-9]\{1,\}\).*/\1/p' /proc/gpufreq/gpufreq_opp_dump | head -n 1)
			apply "$gpu_freq" /proc/gpufreq/gpufreq_opp_freq
		fi
	}

	# Disable GPU Power limiter
	[ -f "/proc/gpufreq/gpufreq_power_limited" ] && {
		for setting in ignore_batt_oc ignore_batt_percent ignore_low_batt ignore_thermal_protect ignore_pbm_limited; do
			apply "$setting 1" /proc/gpufreq/gpufreq_power_limited
		done
	}

	# Disable battery current limiter
	apply "stop 1" /proc/mtk_batoc_throttling/battery_oc_protect_stop

	# DRAM Frequency
	apply 0 /sys/kernel/helio-dvfsrc/dvfsrc_force_vcore_dvfs_opp

	for path in /sys/devices/platform/*.dvfsrc; do
		apply 0 "$path/helio-dvfsrc/dvfsrc_req_ddr_opp"
	done

	if [ $PIN_MAX -eq 1 ]; then
		devfreq_max_perf /sys/class/devfreq/mtk-dvfsrc-devfreq
	else
		devfreq_mid_perf /sys/class/devfreq/mtk-dvfsrc-devfreq
	fi

	# Eara Thermal (HiCo's MediaTek backend handles it when installed)
	[ "$HICO_ACTIVE" -eq 0 ] && apply 0 /sys/kernel/eara_thermal/enable
}

snapdragon_performance() {
	# Qualcomm CPU Bus and DRAM frequencies
	[ -z "$FLUX_DISABLE_DDR_TWEAK" ] && {
		# Latency Nodes
		for path in /sys/class/devfreq/*memlat* \
			/sys/class/devfreq/*latfloor* \
			/sys/class/devfreq/*ddr-lat*; do

			if [ $PIN_MAX -eq 1 ]; then
				devfreq_max_perf "$path"
			else
				devfreq_mid_perf "$path"
			fi
		done

		for component in DDR LLCC L3; do
			path="/sys/devices/system/cpu/bus_dcvs/$component"
			if [ "$PIN_MAX" -eq 1 ]; then
				qcom_cpudcvs_max_perf "$path"
			else
				qcom_cpudcvs_mid_perf "$path"
			fi
		done
	}

	# GPU tweak
	gpu_path="/sys/class/kgsl/kgsl-3d0/devfreq"
	if [ "$PIN_MAX" -eq 1 ]; then
		devfreq_max_perf "$gpu_path"
	else
		devfreq_unlock "$gpu_path"
	fi

	# Disable GPU Bus split
	apply 0 /sys/class/kgsl/kgsl-3d0/bus_split

	# Force GPU clock on (device rules can forbid holding the GPU on)
	[ -z "$FLUX_NO_GPU_POWER_LOCK" ] && apply 1 /sys/class/kgsl/kgsl-3d0/force_clk_on
}

tegra_performance() {
	gpu_path="/sys/kernel/tegra_gpu"
	if [ -d "$gpu_path" ]; then
		max_freq=$(which_maxfreq "$gpu_path/available_frequencies")
		apply "$max_freq" "$gpu_path/gpu_cap_rate"

		if [ $PIN_MAX -eq 1 ]; then
			apply "$max_freq" "$gpu_path/gpu_floor_rate"
		else
			min_freq=$(which_minfreq "$gpu_path/available_frequencies")
			apply "$min_freq" "$gpu_path/gpu_floor_rate"
		fi
	fi
}

exynos_performance() {
	# GPU Frequency
	gpu_path="/sys/kernel/gpu"
	[ -d "$gpu_path" ] && {
		max_freq=$(which_maxfreq "$gpu_path/gpu_available_frequencies")
		apply "$max_freq" "$gpu_path/gpu_max_clock"

		if [ $PIN_MAX -eq 1 ]; then
			apply "$max_freq" "$gpu_path/gpu_min_clock"
		else
			min_freq=$(which_minfreq "$gpu_path/gpu_available_frequencies")
			apply "$min_freq" "$gpu_path/gpu_min_clock"
		fi
	}

	mali_sysfs=$(find /sys/devices/platform/ -iname "*.mali" -print -quit 2>/dev/null)
	apply always_on "$mali_sysfs/power_policy"

	# DRAM and Buses Frequency
	[ -z "$FLUX_DISABLE_DDR_TWEAK" ] && {
		for path in /sys/class/devfreq/*devfreq_mif*; do
			if [ $PIN_MAX -eq 0 ]; then
				devfreq_mid_perf "$path"
			else
				devfreq_max_perf "$path"
			fi
		done &
	}
}

unisoc_performance() {
	# GPU Frequency
	gpu_path=$(find /sys/class/devfreq/ -type d -iname "*.gpu" -print -quit 2>/dev/null)
	[ -n "$gpu_path" ] && {
		if [ $PIN_MAX -eq 1 ]; then
			devfreq_max_perf "$gpu_path"
		else
			devfreq_unlock "$gpu_path"
		fi
	}
}

tensor_performance() {
	# GPU Frequency
	gpu_path=$(find /sys/devices/platform/ -type d -iname "*.mali" -print -quit 2>/dev/null)
	[ -n "$gpu_path" ] && {
		max_freq=$(which_maxfreq "$gpu_path/available_frequencies")
		apply "$max_freq" "$gpu_path/scaling_max_freq"

		if [ $PIN_MAX -eq 1 ]; then
			apply "$max_freq" "$gpu_path/scaling_min_freq"
		else
			min_freq=$(which_minfreq "$gpu_path/available_frequencies")
			apply "$min_freq" "$gpu_path/scaling_min_freq"
		fi
	}

	# DRAM frequency
	[ -z "$FLUX_DISABLE_DDR_TWEAK" ] && {
		for path in /sys/class/devfreq/*devfreq_mif*; do
			if [ $PIN_MAX -eq 0 ]; then
				devfreq_mid_perf "$path"
			else
				devfreq_max_perf "$path"
			fi
		done &
	}
}

###################################
# Device-specific normal profile
###################################

mediatek_normal() {
	# PPM policies
	if [ -d /proc/ppm ]; then
		grep -E "PWR_THRO|THERMAL" /proc/ppm/policy_status | while read -r row; do
			apply "${row:1:1} 1" /proc/ppm/policy_status
		done
	fi

	# Free FPSGO
	apply 2 /sys/kernel/fpsgo/common/force_onoff

	# MTK Power and CCI mode
	apply 0 /proc/cpufreq/cpufreq_cci_mode
	apply 0 /proc/cpufreq/cpufreq_power_mode

	# DDR Boost mode
	apply 0 /sys/devices/platform/boot_dramboost/dramboost/dramboost

	# EAS/HMP Switch
	apply 2 /sys/devices/system/cpu/eas/enable

	# Enable GED KPI
	apply 1 /sys/module/sspm_v3/holders/ged/parameters/is_GED_KPI_enabled

	# GPU Frequency
	write 0 /proc/gpufreq/gpufreq_opp_freq
	write -1 /proc/gpufreqv2/fix_target_opp_index

	# Reset min freq via GED
	if [ -d /proc/gpufreqv2 ]; then
		min_oppfreq=$(mtk_gpufreq_minfreq_index /proc/gpufreqv2/gpu_working_opp_table)
	else
		min_oppfreq=$(mtk_gpufreq_minfreq_index /proc/gpufreq/gpufreq_opp_dump)
	fi

	apply "$min_oppfreq" /sys/kernel/ged/hal/custom_boost_gpu_freq

	# GPU Power limiter
	[ -f "/proc/gpufreq/gpufreq_power_limited" ] && {
		for setting in ignore_batt_oc ignore_batt_percent ignore_low_batt ignore_thermal_protect ignore_pbm_limited; do
			apply "$setting 0" /proc/gpufreq/gpufreq_power_limited
		done
	}

	# Enable battery current limiter
	apply "stop 0" /proc/mtk_batoc_throttling/battery_oc_protect_stop

	# DRAM Frequency
	for path in /sys/devices/platform/*.dvfsrc; do
		apply -1 "$path/helio-dvfsrc/dvfsrc_req_ddr_opp"
	done

	write -1 /sys/kernel/helio-dvfsrc/dvfsrc_force_vcore_dvfs_opp
	devfreq_unlock /sys/class/devfreq/mtk-dvfsrc-devfreq

	# Eara Thermal
	[ "$HICO_ACTIVE" -eq 0 ] && apply 1 /sys/kernel/eara_thermal/enable
}

snapdragon_normal() {
	# Qualcomm CPU Bus and DRAM frequencies
	[ -z "$FLUX_DISABLE_DDR_TWEAK" ] && {
		# Latency Nodes
		for path in /sys/class/devfreq/*memlat* \
			/sys/class/devfreq/*latfloor* \
			/sys/class/devfreq/*ddr-lat*; do
			devfreq_unlock "$path"
		done

		for component in DDR LLCC L3; do
			qcom_cpudcvs_unlock /sys/devices/system/cpu/bus_dcvs/$component
		done
	}

	# Revert GPU tweak
	devfreq_unlock /sys/class/kgsl/kgsl-3d0/devfreq

	# Enable back GPU Bus split
	apply 1 /sys/class/kgsl/kgsl-3d0/bus_split

	# Free GPU clock on/off
	apply 0 /sys/class/kgsl/kgsl-3d0/force_clk_on
}

tegra_normal() {
	gpu_path="/sys/kernel/tegra_gpu"
	[ -d "$gpu_path" ] && {
		max_freq=$(which_maxfreq "$gpu_path/available_frequencies")
		min_freq=$(which_minfreq "$gpu_path/available_frequencies")
		write "$max_freq" "$gpu_path/gpu_cap_rate"
		write "$min_freq" "$gpu_path/gpu_floor_rate"
	}
}

exynos_normal() {
	# GPU Frequency
	gpu_path="/sys/kernel/gpu"
	[ -d "$gpu_path" ] && {
		max_freq=$(which_maxfreq "$gpu_path/gpu_available_frequencies")
		min_freq=$(which_minfreq "$gpu_path/gpu_available_frequencies")
		write "$max_freq" "$gpu_path/gpu_max_clock"
		write "$min_freq" "$gpu_path/gpu_min_clock"
	}

	mali_sysfs=$(find /sys/devices/platform/ -iname "*.mali" -print -quit 2>/dev/null)
	apply coarse_demand "$mali_sysfs/power_policy"

	# DRAM frequency
	[ -z "$FLUX_DISABLE_DDR_TWEAK" ] && {
		for path in /sys/class/devfreq/*devfreq_mif*; do
			devfreq_unlock "$path"
		done &
	}
}

unisoc_normal() {
	# GPU Frequency
	gpu_path=$(find /sys/class/devfreq/ -type d -iname "*.gpu" -print -quit 2>/dev/null)
	[ -n "$gpu_path" ] && devfreq_unlock "$gpu_path"
}

tensor_normal() {
	# GPU Frequency
	gpu_path=$(find /sys/devices/platform/ -type d -iname "*.mali" -print -quit 2>/dev/null)
	[ -n "$gpu_path" ] && {
		max_freq=$(which_maxfreq "$gpu_path/available_frequencies")
		min_freq=$(which_minfreq "$gpu_path/available_frequencies")
		write "$max_freq" "$gpu_path/scaling_max_freq"
		write "$min_freq" "$gpu_path/scaling_min_freq"
	}

	# DRAM frequency
	[ -z "$FLUX_DISABLE_DDR_TWEAK" ] && {
		for path in /sys/class/devfreq/*devfreq_mif*; do
			devfreq_unlock "$path"
		done &
	}
}

###################################
# Device-specific powersave profile
###################################

mediatek_powersave() {
	# Set MTK CPU Power mode to low power
	apply 1 /proc/cpufreq/cpufreq_power_mode

	# GPU Frequency
	if [ -d /proc/gpufreqv2 ]; then
		min_gpufreq_index=$(mtk_gpufreq_minfreq_index /proc/gpufreqv2/gpu_working_opp_table)
		apply "$min_gpufreq_index" /proc/gpufreqv2/fix_target_opp_index
	else
		gpu_freq=$(sed -n 's/.*freq = \([0-9]\{1,\}\).*/\1/p' /proc/gpufreq/gpufreq_opp_dump | tail -n 1)
		apply "$gpu_freq" /proc/gpufreq/gpufreq_opp_freq
	fi
}

snapdragon_powersave() {
	# GPU Frequency
	# There's some report that this causes no video issue after the phone went sleep and awaken
	if [ -z "$FLUX_QCOM_NO_GPU_POWERSAVE" ]; then
		devfreq_min_perf /sys/class/kgsl/kgsl-3d0/devfreq
	fi
}

tegra_powersave() {
	gpu_path="/sys/kernel/tegra_gpu"
	[ -d "$gpu_path" ] && {
		freq=$(which_minfreq "$gpu_path/available_frequencies")
		apply "$freq" "$gpu_path/gpu_floor_rate"
		apply "$freq" "$gpu_path/gpu_cap_rate"
	}
}

exynos_powersave() {
	# GPU Frequency
	gpu_path="/sys/kernel/gpu"
	[ -d "$gpu_path" ] && {
		freq=$(which_minfreq "$gpu_path/gpu_available_frequencies")
		apply "$freq" "$gpu_path/gpu_min_clock"
		apply "$freq" "$gpu_path/gpu_max_clock"
	}
}

unisoc_powersave() {
	# GPU Frequency
	gpu_path=$(find /sys/class/devfreq/ -type d -iname "*.gpu" -print -quit 2>/dev/null)
	[ -n "$gpu_path" ] && devfreq_min_perf "$gpu_path"
}

tensor_powersave() {
	# GPU Frequency
	gpu_path=$(find /sys/devices/platform/ -type d -iname "*.mali" -print -quit 2>/dev/null)
	[ -n "$gpu_path" ] && {
		freq=$(which_minfreq "$gpu_path/available_frequencies")
		apply "$freq" "$gpu_path/scaling_min_freq"
		apply "$freq" "$gpu_path/scaling_max_freq"
	}
}

###################################
# Flux Sched (uclamp)
###################################

# Scheduler prioritisation for the foreground app on kernels with utilization
# clamping (uclamp, Linux 5.3+ / all GKI kernels), where /dev/stune no longer
# exists. Uses the Android cpuctl cgroups:
#   top-app      uclamp.min raises the capacity floor of the game's tasks, so the
#                scheduler places them on faster cores and cpufreq ramps sooner;
#                uclamp.latency_sensitive (Android common kernel) prefers idle CPUs
#   background / system-background
#                uclamp.max caps how much capacity background work may request,
#                so it cannot pull frequencies up while a game runs
#
# The original value and file mode of every node are saved once per boot and
# restored when leaving a game, so the PowerHAL and init can manage uclamp
# again outside of games. Set FLUX_SCHED_DISABLED=1 to only restore.

FLUX_SCHED_ROOT="/dev/cpuctl"
# /dev is tmpfs: the backup survives daemon restarts but not a reboot.
FLUX_SCHED_BACKUP="/dev/.flux_sched_orig"
FLUX_SCHED_GROUPS="top-app foreground background system-background"
FLUX_SCHED_ATTRS="cpu.uclamp.min cpu.uclamp.max cpu.uclamp.latency_sensitive"

flux_sched_supported() {
	[ -f "$FLUX_SCHED_ROOT/top-app/cpu.uclamp.min" ]
}

# Save "<node> <mode> <value>" for every existing node, once per boot.
flux_sched_backup() {
	flux_sched_supported || return 0
	[ -f "$FLUX_SCHED_BACKUP" ] && return 0

	tmp="$FLUX_SCHED_BACKUP.tmp"
	: >"$tmp"
	for group in $FLUX_SCHED_GROUPS; do
		for attr in $FLUX_SCHED_ATTRS; do
			node="$FLUX_SCHED_ROOT/$group/$attr"
			[ -f "$node" ] || continue
			echo "$node $(stat -c %a "$node") $(cat "$node")" >>"$tmp"
		done
	done
	mv "$tmp" "$FLUX_SCHED_BACKUP"
}

# Restore saved values and file modes (undoes the 444 lock set by apply).
flux_sched_restore() {
	[ -f "$FLUX_SCHED_BACKUP" ] || return 0
	while read -r node mode value; do
		[ -f "$node" ] || continue
		chmod 644 "$node" >/dev/null 2>&1
		echo "$value" >"$node" 2>/dev/null
		chmod "$mode" "$node" >/dev/null 2>&1
	done <"$FLUX_SCHED_BACKUP"
}

# flux_sched <performance|lite|powersave|balance>
flux_sched() {
	flux_sched_supported || return 0
	flux_sched_backup
	flux_sched_restore
	[ -n "$FLUX_SCHED_DISABLED" ] && return 0

	# top_min: top-app capacity floor (%); bg_max: background capacity cap (%)
	case "$1" in
	performance) top_min=20 bg_max=50 latency=1 ;;
	lite) top_min=5 bg_max=40 latency=1 ;; # thermal tier: less heat from boosting
	powersave) top_min="" bg_max=30 latency=0 ;;
	*) return 0 ;; # balance: stock values
	esac

	[ -n "$top_min" ] && apply "$top_min" "$FLUX_SCHED_ROOT/top-app/cpu.uclamp.min"
	apply "$latency" "$FLUX_SCHED_ROOT/top-app/cpu.uclamp.latency_sensitive"
	for group in background system-background; do
		apply "$bg_max" "$FLUX_SCHED_ROOT/$group/cpu.uclamp.max"
	done
}

###################################
# Flux Boost
###################################

# Game-time tuning beyond the stock Encore profile. Every node is saved once
# per boot ("<group> <node> <mode> <value>") before it is first changed, and
# restored when leaving a game, so daily use keeps the vendor values:
#   vm        earlier background reclaim and larger dirty-page budget: fewer
#             direct-reclaim and writeback stalls (frame drops) while loading
#   io        block queues complete I/O on the CPU that submitted it (the
#             game's), which keeps completions off busy little cores
#   priority  the game's worker threads get a higher CPU (nice) and I/O
#             (best-effort, highest) priority than other apps; threads that
#             Android already boosts further (UI / render) are left as they are
# Values are only ever raised, never lowered below the vendor setting.
# FLUX_VM_DISABLED / FLUX_IO_DISABLED / FLUX_PRIORITY_DISABLED: restore only.

FLUX_BOOST_BACKUP="/dev/.flux_boost_orig"
FLUX_GAME_PRIO="/dev/.flux_game_prio"

# flux_boost_save <group> <node>... : remember the stock value once per boot
flux_boost_save() {
	group=$1
	shift
	for node in "$@"; do
		[ -f "$node" ] || continue
		grep -q "^$group $node " "$FLUX_BOOST_BACKUP" 2>/dev/null && continue
		echo "$group $node $(stat -c %a "$node") $(cat "$node")" >>"$FLUX_BOOST_BACKUP"
	done
}

# flux_boost_restore <group> : put back every saved node of the group
flux_boost_restore() {
	[ -f "$FLUX_BOOST_BACKUP" ] || return 0
	while read -r group node mode value; do
		[ "$group" = "$1" ] && [ -f "$node" ] || continue
		chmod 644 "$node" >/dev/null 2>&1
		echo "$value" >"$node" 2>/dev/null
		chmod "$mode" "$node" >/dev/null 2>&1
	done <"$FLUX_BOOST_BACKUP"
}

# raise_to <value> <node> : apply only when the current value is lower
raise_to() {
	[ -f "$2" ] || return 0
	cur=$(cat "$2" 2>/dev/null)
	case "$cur" in '' | *[!0-9]*) return 0 ;; esac
	[ "$cur" -lt "$1" ] && apply "$1" "$2"
	return 0
}

flux_block_queues() {
	for dir in /sys/block/sd* /sys/block/mmcblk* /sys/block/nvme*; do
		[ -f "$dir/queue/rq_affinity" ] && echo "$dir/queue/rq_affinity"
	done
}

flux_vm() {
	nodes="/proc/sys/vm/watermark_scale_factor /proc/sys/vm/dirty_ratio /proc/sys/vm/dirty_background_ratio \
/sys/kernel/mm/ksm/run"
	# shellcheck disable=SC2086
	flux_boost_save vm $nodes
	flux_boost_restore vm
	[ "$1" = boost ] && [ -z "$FLUX_VM_DISABLED" ] || return 0
	raise_to 50 /proc/sys/vm/watermark_scale_factor
	raise_to 30 /proc/sys/vm/dirty_ratio
	raise_to 10 /proc/sys/vm/dirty_background_ratio
	# Pause KSM page merging (ksmd scans memory in the background;
	# Documentation/admin-guide/mm/ksm.rst: 0 stops it, merged pages stay merged)
	apply 0 /sys/kernel/mm/ksm/run
}

# UFS (sd*), eMMC / SD (mmcblk*) and the dm devices on top of them (userdata is
# dm-crypt / dm-default-key: file reads go through the dm device's read-ahead)
flux_block_prefetch() {
	for dir in /sys/block/sd* /sys/block/mmcblk* /sys/block/dm-*; do
		case "$dir" in *rpmb | *boot[0-9]) continue ;; esac
		for node in read_ahead_kb iostats; do
			[ -f "$dir/queue/$node" ] && echo "$dir/queue/$node"
		done
	done
}

flux_io() {
	queues=$(flux_block_queues)
	prefetch=$(flux_block_prefetch)
	# shellcheck disable=SC2086
	flux_boost_save io $queues $prefetch
	flux_boost_restore io
	[ "$1" = boost ] && [ -z "$FLUX_IO_DISABLED" ] || return 0
	for node in $queues; do
		raise_to 2 "$node"
	done
	# Larger read-ahead while gaming: asset and texture streaming reads ahead in one
	# request (Documentation/ABI/stable/sysfs-block); I/O accounting off saves a
	# per-request timestamp. Both are restored when the game ends.
	for node in $prefetch; do
		case "$node" in
		*/read_ahead_kb) raise_to 512 "$node" ;;
		*/iostats) apply 0 "$node" ;;
		esac
	done
}

# nice value of a thread (field 19 of /proc/<pid>/task/<tid>/stat; comm may contain spaces)
thread_nice() {
	stat=$(cat "$1/stat" 2>/dev/null) || return 1
	# shellcheck disable=SC2086
	set -- ${stat##*) }
	shift 16
	echo "$1"
}

flux_priority_restore() {
	[ -f "$FLUX_GAME_PRIO" ] || return 0
	while read -r tid orig; do
		[ -d "/proc/$tid" ] || continue
		cur=$(thread_nice "/proc/$tid") || continue
		# toybox renice takes an increment
		[ "$cur" != "$orig" ] && renice -n $((orig - cur)) -p "$tid" >/dev/null 2>&1
		ionice -c 0 -p "$tid" >/dev/null 2>&1
	done <"$FLUX_GAME_PRIO"
	rm -f "$FLUX_GAME_PRIO"
}

flux_priority() {
	flux_priority_restore
	[ "$1" = boost ] && [ -z "$FLUX_PRIORITY_DISABLED" ] || return 0
	# shellcheck disable=SC2153 # set by fluxd (Profiler.cpp)
	pid=$FLUX_GAME_PID
	case "$pid" in '' | 0 | *[!0-9]*) return 0 ;; esac
	[ -d "/proc/$pid/task" ] || return 0

	: >"$FLUX_GAME_PRIO"
	for task in /proc/"$pid"/task/*; do
		tid=${task##*/}
		nice=$(thread_nice "$task") || continue
		# Worker threads (nice > -5) move up to -5; UI / render threads Android runs higher stay put.
		if [ "$nice" -gt -5 ]; then
			echo "$tid $nice" >>"$FLUX_GAME_PRIO"
			renice -n $((-5 - nice)) -p "$tid" >/dev/null 2>&1
		fi
		ionice -c 2 -n 0 -p "$tid" >/dev/null 2>&1
	done
}

# flux_boost <boost|restore>
flux_boost() {
	flux_vm "$1"
	flux_io "$1"
	flux_priority "$1"
}

###################################
# Game tweaks: network, touch, refresh rate, GPU governor
###################################

# Each part can be switched off in the WebUI (Settings → Game tweaks); a part
# that is off is restored to the values saved before Flux first changed it.

FLUX_NET_NODES="/proc/sys/net/ipv4/tcp_congestion_control /proc/sys/net/ipv4/tcp_low_latency \
/proc/sys/net/ipv4/tcp_ecn /proc/sys/net/ipv4/tcp_fastopen /proc/sys/net/ipv4/tcp_sack \
/proc/sys/net/ipv4/tcp_timestamps"

# Low-latency TCP (congestion control, ECN, Fast Open). FLUX_NET_DISABLED: stock values.
flux_net() {
	# shellcheck disable=SC2086
	flux_boost_save net $FLUX_NET_NODES
	if [ -n "$FLUX_NET_DISABLED" ]; then
		flux_boost_restore net
		return 0
	fi
	for algo in bbr3 bbr2 bbrplus bbr westwood cubic; do
		if grep -q "$algo" /proc/sys/net/ipv4/tcp_available_congestion_control; then
			apply "$algo" /proc/sys/net/ipv4/tcp_congestion_control
			break
		fi
	done
	apply 1 /proc/sys/net/ipv4/tcp_low_latency
	apply 1 /proc/sys/net/ipv4/tcp_ecn
	apply 3 /proc/sys/net/ipv4/tcp_fastopen
	apply 1 /proc/sys/net/ipv4/tcp_sack
	apply 0 /proc/sys/net/ipv4/tcp_timestamps
}

# Touch panel game mode (OPPO / Realme / OnePlus touchpanel driver). flux_touch <on|off>
# Samsung touch firmware commands (sec_cmd): the driver lists what it supports
# in cmd_list, so "set_game_mode" is only sent when the panel has it.
SEC_TSP=/sys/class/sec/tsp
FLUX_SEC_GAME=/dev/.flux_sec_game

sec_touch_game_mode() {
	grep -qw set_game_mode "$SEC_TSP/cmd_list" 2>/dev/null
}

# flux_touch <on|off>: input threads on all devices, plus the panel's own game
# mode where the vendor driver has one (OPPO / realme / OnePlus, Samsung)
flux_touch() {
	if [ "$1" = on ] && [ -z "$FLUX_TOUCH_DISABLED" ]; then
		flux_input boost
	else
		flux_input restore
	fi
	flux_input_boost "$1"

	# Samsung sec_ts / stm_ts: only switched off again if Flux switched it on
	if sec_touch_game_mode; then
		if [ "$1" = on ] && [ -z "$FLUX_TOUCH_DISABLED" ]; then
			echo "set_game_mode,1" >"$SEC_TSP/cmd" 2>/dev/null && : >"$FLUX_SEC_GAME"
		elif [ -f "$FLUX_SEC_GAME" ]; then
			echo "set_game_mode,0" >"$SEC_TSP/cmd" 2>/dev/null
			rm -f "$FLUX_SEC_GAME"
		fi
	fi

	# OPPO / realme / OnePlus (oplus touchpanel driver)
	tp_path="/proc/touchpanel"
	[ -d "$tp_path" ] || return 0
	if [ "$1" = on ] && [ -z "$FLUX_TOUCH_DISABLED" ]; then
		apply 1 $tp_path/game_switch_enable
		apply 0 $tp_path/oplus_tp_limit_enable
		apply 0 $tp_path/oppo_tp_limit_enable
		apply 1 $tp_path/oplus_tp_direction
		apply 1 $tp_path/oppo_tp_direction
	else
		apply 0 $tp_path/game_switch_enable
		apply 1 $tp_path/oplus_tp_limit_enable
		apply 1 $tp_path/oppo_tp_limit_enable
		apply 0 $tp_path/oplus_tp_direction
		apply 0 $tp_path/oppo_tp_direction
	fi
}

# CPU input boost: a touch raises the CPU floor for a moment. msm cpu_boost
# (drivers/cpufreq/cpu-boost.c, Qualcomm non-GKI) takes "cpu:freq" pairs; Sultan's
# cpu_input_boost has a duration only. Kernels without either are left alone.
flux_input_boost() {
	cb=/sys/module/cpu_boost/parameters
	cib=/sys/module/cpu_input_boost/parameters
	flux_boost_save inputboost $cb/input_boost_ms $cb/input_boost_freq $cib/input_boost_duration
	flux_boost_restore inputboost
	[ "$1" = on ] && [ -z "$FLUX_TOUCH_DISABLED" ] || return 0

	if [ -f $cb/input_boost_freq ]; then
		freqs=""
		for policy in /sys/devices/system/cpu/cpufreq/policy*; do
			avail="$policy/scaling_available_frequencies"
			[ -f "$avail" ] || continue
			mid=$(which_midfreq "$avail")
			case "$mid" in '' | *[!0-9]*) continue ;; esac
			freqs="$freqs ${policy##*policy}:$mid"
		done
		[ -n "$freqs" ] && apply "${freqs# }" $cb/input_boost_freq
		raise_to 120 $cb/input_boost_ms
	fi
	raise_to 120 $cib/input_boost_duration
}

# Highest refresh rate while gaming (opt-in: FLUX_REFRESH_ENABLED). flux_refresh <boost|restore>
FLUX_REFRESH_BACKUP="/dev/.flux_refresh_orig"
flux_refresh() {
	if [ "$1" = boost ] && [ -n "$FLUX_REFRESH_ENABLED" ]; then
		max=$(dumpsys display 2>/dev/null | grep -oE 'fps=[0-9]+(\.[0-9]+)?' | cut -d= -f2 | sort -rn | head -n 1)
		max=${max%%.*}
		case "$max" in '' | *[!0-9]*) return 0 ;; esac
		[ "$max" -ge 60 ] || return 0
		[ -f "$FLUX_REFRESH_BACKUP" ] ||
			echo "$(settings get system peak_refresh_rate) $(settings get system min_refresh_rate)" >"$FLUX_REFRESH_BACKUP"
		settings put system peak_refresh_rate "$max"
		settings put system min_refresh_rate "$max"
		return 0
	fi
	[ -f "$FLUX_REFRESH_BACKUP" ] || return 0
	read -r peak min <"$FLUX_REFRESH_BACKUP"
	for pair in "peak_refresh_rate:$peak" "min_refresh_rate:$min"; do
		key=${pair%%:*}
		val=${pair#*:}
		if [ -z "$val" ] || [ "$val" = null ]; then
			settings delete system "$key" >/dev/null 2>&1
		else
			settings put system "$key" "$val"
		fi
	done
	rm -f "$FLUX_REFRESH_BACKUP"
}

# GPU devfreq governor node (Adreno kgsl, Mali / PowerVR / Xclipse devfreq)
gpu_governor_node() {
	for node in /sys/class/kgsl/kgsl-3d0/devfreq/governor /sys/class/devfreq/*gpu*/governor \
		/sys/class/devfreq/*mali*/governor /sys/class/devfreq/*g3d*/governor; do
		[ -f "$node" ] && {
			echo "$node"
			return 0
		}
	done
	return 1
}

# change_gpu_gov <governor>: empty or unavailable keeps the kernel's own governor.
change_gpu_gov() {
	node=$(gpu_governor_node) || return 0
	flux_boost_save gpugov "$node"
	if [ -n "$1" ] && grep -qw -- "$1" "${node%/governor}/available_governors" 2>/dev/null; then
		apply "$1" "$node"
	else
		flux_boost_restore gpugov
	fi
}

###################################
# Surface boost (all devices)
###################################

# Frames are composed by SurfaceFlinger and the hardware composer HAL (HWC).
# Android keeps them in the foreground/system cgroups; while a game runs their
# threads are moved into the top-app cgroups, so they get the same CPUs,
# schedtune boost (Non-GKI / Legacy) or uclamp floor (GKI, see Flux Sched) as
# the game. This is the same cgroup v1 "tasks" interface Android's own
# task profiles use (Documentation/admin-guide/cgroup-v1/cpusets.rst): writing
# a TID moves only that thread. Every thread's previous group is saved and
# restored when leaving the game.

FLUX_SURFACE_BACKUP=/dev/.flux_surface
FLUX_INPUT_BACKUP=/dev/.flux_input

# flux_cgroup_restore <backup>: every thread back to the group it came from
flux_cgroup_restore() {
	[ -f "$1" ] || return 0
	while read -r tid dir path; do
		[ -d "/proc/$tid" ] && echo "$tid" >"$dir${path%/}/tasks" 2>/dev/null
	done <"$1"
	rm -f "$1"
}

# flux_cgroup_top_app <backup> <tid>...: move threads into the top-app groups,
# recording "<tid> <cgroup root> <previous path>" for each controller
flux_cgroup_top_app() {
	backup=$1
	shift
	for tid in "$@"; do
		[ -f "/proc/$tid/cgroup" ] || continue
		# cgroup v1 lines: "<id>:<controllers>:<path>"
		while IFS=: read -r _ ctrl path; do
			case "$ctrl" in
			cpuset) dir=/dev/cpuset ;;
			schedtune) dir=/dev/stune ;;
			cpu | cpu,cpuacct) dir=/dev/cpuctl ;;
			*) continue ;;
			esac
			[ "$path" = /top-app ] && continue
			[ -f "$dir/top-app/tasks" ] || continue
			echo "$tid $dir $path" >>"$backup"
			echo "$tid" >"$dir/top-app/tasks" 2>/dev/null
		done <"/proc/$tid/cgroup"
	done
}

# All thread IDs of the given processes
process_tids() {
	for pid in "$@"; do
		for task in /proc/"$pid"/task/*; do
			[ -d "$task" ] && echo "${task##*/}"
		done
	done
}

# flux_surface <boost|restore>
flux_surface() {
	flux_cgroup_restore "$FLUX_SURFACE_BACKUP"
	[ "$1" = boost ] && [ -z "$FLUX_SURFACE_DISABLED" ] || return 0
	[ -f /dev/cpuset/top-app/tasks ] || return 0

	pids="$(pidof surfaceflinger) $(pgrep -f 'graphics\.composer|display\.composer' 2>/dev/null)"
	: >"$FLUX_SURFACE_BACKUP"
	# shellcheck disable=SC2046,SC2086
	flux_cgroup_top_app "$FLUX_SURFACE_BACKUP" $(process_tids $pids)
}

# Touch input on every device: Android reads and dispatches touch events on
# two system_server threads, InputReader and InputDispatcher
# (frameworks/native/services/inputflinger). They join the game's top-app
# groups like SurfaceFlinger above, so a touch reaches the game without
# waiting for a slow or parked core.
flux_input() {
	flux_cgroup_restore "$FLUX_INPUT_BACKUP"
	[ "$1" = boost ] && [ -z "$FLUX_TOUCH_DISABLED" ] || return 0
	[ -f /dev/cpuset/top-app/tasks ] || return 0

	: >"$FLUX_INPUT_BACKUP"
	for pid in $(pidof system_server); do
		for task in /proc/"$pid"/task/*; do
			case "$(cat "$task/comm" 2>/dev/null)" in
			InputReader | InputDispatcher) flux_cgroup_top_app "$FLUX_INPUT_BACKUP" "${task##*/}" ;;
			esac
		done
	done
}

###################################
# Chipset boost (vendor kernel interfaces)
###################################

# Vendor knobs that Qualcomm's and ARM's own performance services drive, set
# for the whole game session instead of per touch/launch. Older chipsets
# benefit most: their cores are parked aggressively and the GPU power-collapses
# between frames. Only in the full performance profile (not Lite); every node
# is saved first and restored when leaving the game.
#
#   core_ctl      Qualcomm core control (msm kernels, WALT module on GKI):
#                 min_cpus = max_cpus keeps every core of a cluster online
#   sched_boost   Qualcomm WALT/HMP boost, the knob the QTI PerfHAL uses:
#                 2 = conservative (top-app on big cores, background stays);
#                 HMP kernels only accept 0/1, so 1 is used there
#   kgsl          Adreno power control (kgsl_pwrctrl): keep bus, rail and
#                 clocks on and skip nap between frames
#   workqueue     per-CPU kernel workqueues instead of power-efficient ones
#   Mali kbase    ARM Mali power_policy: always_on instead of coarse_demand
#                 (Exynos is handled in exynos_performance already)

FLUX_MALI_BACKUP=/dev/.flux_mali_policy

sched_boost_node() {
	for node in /proc/sys/walt/sched_boost /proc/sys/kernel/sched_boost; do
		[ -f "$node" ] && {
			echo "$node"
			return 0
		}
	done
	return 1
}

mali_policy_nodes() {
	find /sys/devices/platform/ -maxdepth 3 -name power_policy -path "*mali*" 2>/dev/null
}

flux_chipset_restore() {
	flux_boost_restore chipset
	[ -f "$FLUX_MALI_BACKUP" ] || return 0
	while read -r node policy; do
		[ -f "$node" ] && echo "$policy" >"$node" 2>/dev/null
	done <"$FLUX_MALI_BACKUP"
	rm -f "$FLUX_MALI_BACKUP"
}

# flux_chipset <boost|restore>
flux_chipset() {
	flux_chipset_restore
	[ "$1" = boost ] && [ -z "$FLUX_CHIPSET_DISABLED" ] && [ "$LITE_MODE" -eq 0 ] || return 0

	# Qualcomm core_ctl
	for dir in /sys/devices/system/cpu/cpu*/core_ctl; do
		[ -f "$dir/min_cpus" ] && [ -f "$dir/max_cpus" ] || continue
		flux_boost_save chipset "$dir/min_cpus"
		apply "$(cat "$dir/max_cpus")" "$dir/min_cpus"
	done

	# Qualcomm sched_boost
	if node=$(sched_boost_node); then
		flux_boost_save chipset "$node"
		apply 2 "$node"
		[ "$(cat "$node" 2>/dev/null)" = 2 ] || apply 1 "$node"
	fi

	# Adreno kgsl power lock: clock, bus and rail held on, no nap between frames.
	# With max clocks, or with Stable clocks only when GPU power lock is switched
	# on (keeping the rails on costs heat all game). Device rules can forbid it.
	kgsl=/sys/class/kgsl/kgsl-3d0
	gpu_lock=$PIN_MAX
	[ -n "$FLUX_GPU_LOCK" ] && gpu_lock=1
	[ -n "$FLUX_NO_GPU_POWER_LOCK" ] && gpu_lock=0
	if [ -d "$kgsl" ] && [ "$gpu_lock" -eq 1 ]; then
		flux_boost_save chipset $kgsl/force_clk_on $kgsl/force_bus_on $kgsl/force_rail_on $kgsl/force_no_nap
		apply 1 $kgsl/force_clk_on
		apply 1 $kgsl/force_bus_on
		apply 1 $kgsl/force_rail_on
		apply 1 $kgsl/force_no_nap
	fi

	# Adreno Reflex (opt-in): kgsl dispatcher and DCVS keys, only where the kernel has them
	if [ -d "$kgsl" ] && [ -n "$FLUX_REFLEX" ]; then
		flux_boost_save chipset $kgsl/devfreq/adrenoboost $kgsl/preempt_level $kgsl/dispatch/context_burst_count \
			$kgsl/ctxt_aware_enable $kgsl/perfcounter
		# adrenoboost (custom kernels, msm_adreno_tz): 0-3, 2 = medium ramp-up bias
		apply 2 $kgsl/devfreq/adrenoboost
		# Ringbuffer-level preemption: the game's command stream is not cut mid-frame
		apply 0 $kgsl/preempt_level
		# More command batches per context per dispatcher round (default 5)
		raise_to 10 $kgsl/dispatch/context_burst_count
		# Context-aware DCVS: a busy context does not get clocked down
		apply 1 $kgsl/ctxt_aware_enable
		# GPU performance counters off (profilers such as Snapdragon Profiler stop reading them)
		apply 0 $kgsl/perfcounter
	fi

	# Per-CPU instead of "power efficient" unbound workqueues: kernel work
	# (e.g. display and input drivers' deferred work) runs on the CPU that
	# queued it instead of being moved to an idle little core
	# (kernel/workqueue.c, CONFIG_WQ_POWER_EFFICIENT_DEFAULT)
	wq=/sys/module/workqueue/parameters/power_efficient
	if [ -f "$wq" ]; then
		flux_boost_save chipset "$wq"
		apply N "$wq"
	fi

	# ARM Mali kbase power policy; the file lists all policies, the active one in brackets
	[ "$SOC" = 3 ] && return 0
	[ "$PIN_MAX" -eq 1 ] || return 0 # Sustained: the GPU may power down between frames
	: >"$FLUX_MALI_BACKUP"
	for node in $(mali_policy_nodes); do
		cur=$(sed -n 's/.*\[\([a-z_]*\)\].*/\1/p' "$node" 2>/dev/null)
		{ [ -n "$cur" ] && grep -qw always_on "$node"; } || continue
		echo "$node $cur" >>"$FLUX_MALI_BACKUP"
		echo always_on >"$node" 2>/dev/null
	done
}

###################################
# Main Performance scripts
###################################

perfcommon() {
	# Disable Kernel panic
	# Workaround for kernel panic on startup in S25U.
	# This is wrong, you know it and I know it.
	# Move on and call me an idiot later.
	apply 0 /proc/sys/kernel/panic
	apply 0 /proc/sys/kernel/panic_on_oops
	apply 0 /proc/sys/kernel/panic_on_warn
	apply 0 /proc/sys/kernel/softlockup_panic

	# Sync to data in the rare case a device crashes
	sync

	# I/O Tweaks
	for dir in /sys/block/*; do
		# Disable I/O statistics accounting
		apply 0 "$dir/queue/iostats"

		# Don't use I/O as random spice
		apply 0 "$dir/queue/add_random"
	done &

	# Networking tweaks (switchable: Game tweaks → Network)
	flux_net

	# Limit max perf event processing time to this much CPU usage
	apply 3 /proc/sys/kernel/perf_cpu_time_max_percent

	# Disable schedstats
	apply 0 /proc/sys/kernel/sched_schedstats

	# Disable Oppo/Realme cpustats
	apply 0 /proc/sys/kernel/task_cpustats_enable

	# Disable Sched auto group
	apply 0 /proc/sys/kernel/sched_autogroup_enabled

	# Enable CRF
	apply 1 /proc/sys/kernel/sched_child_runs_first

	# Improve real time latencies by reducing the scheduler migration time
	apply 32 /proc/sys/kernel/sched_nr_migrate

	# Tweaking scheduler to reduce latency
	apply 50000 /proc/sys/kernel/sched_migration_cost_ns
	apply 1000000 /proc/sys/kernel/sched_min_granularity_ns
	apply 1500000 /proc/sys/kernel/sched_wakeup_granularity_ns

	# Disable read-ahead for swap devices
	apply 0 /proc/sys/vm/page-cluster

	# Update /proc/stat less often to reduce jitter
	apply 15 /proc/sys/vm/stat_interval

	# Disable compaction_proactiveness
	apply 0 /proc/sys/vm/compaction_proactiveness

	# Disable SPI CRC
	apply 0 /sys/module/mmc_core/parameters/use_spi_crc

	# Disable OnePlus opchain
	apply 0 /sys/module/opchain/parameters/chain_on

	# Disable Oplus bloats
	apply 0 /sys/module/cpufreq_bouncing/parameters/enable
	apply 0 /proc/task_info/task_sched_info/task_sched_info_enable
	apply 0 /proc/oplus_scheduler/sched_assist/sched_assist_enabled

	# Report max CPU capabilities to these libraries
	apply "libunity.so, libil2cpp.so, libmain.so, libUE4.so, libgodot_android.so, libgdx.so, libgdx-box2d.so, libminecraftpe.so, libLive2DCubismCore.so, libyuzu-android.so, libryujinx.so, libcitra-android.so, libhdr_pro_engine.so, libandroidx.graphics.path.so, libeffect.so" /proc/sys/kernel/sched_lib_name
	apply 255 /proc/sys/kernel/sched_lib_mask_force

	# ── GKI-specific tweaks ─────────────────────────────────────────────────
	# GKI kernels (android<ver>- tagged) support standard eBPF, UFFD, and
	# io_uring interfaces but lack many vendor-private nodes.
	if [ "$IS_GKI" -eq 1 ]; then
		# io_uring stays as Android configures it (disabled for apps on purpose), and
		# sched_cfs_bandwidth_slice_us is left alone: 0 is below the kernel minimum.

		# Use standard eBPF-based network path if available
		apply 1 /proc/sys/net/core/bpf_jit_enable 2>/dev/null || true

		# Linux 5.13+ moved the CFS tunables set above from /proc/sys/kernel to
		# debugfs; same values. Kernels with EEVDF (6.6+) no longer have the
		# granularity knobs, so those writes are skipped there.
		sched_dbg=/sys/kernel/debug/sched
		apply 32 $sched_dbg/nr_migrate
		apply 50000 $sched_dbg/migration_cost_ns
		apply 1000000 $sched_dbg/min_granularity_ns
		apply 1500000 $sched_dbg/wakeup_granularity_ns
	fi

	# ── Non-GKI (OEM/vendor kernel) specific tweaks ──────────────────────────
	# These nodes are typically only present on vendor-patched kernels.
	if [ "$IS_GKI" -eq 0 ]; then
		# Qualcomm DCVS bus votes — skip on GKI (handled by standard cpufreq)
		for component in LLCC DDR L3; do
			apply 40 /sys/devices/system/cpu/bus_dcvs/$component/ipm_ceil 2>/dev/null || true
		done

		# Xiaomi/MIUI kernel extensions
		apply 0 /sys/module/migt/parameters/glk_disable 2>/dev/null || true
		apply 1 /sys/module/migt/parameters/migt_enable 2>/dev/null || true

		# Samsung/Exynos scheduler hints
		apply 0 /sys/kernel/ems/eas_disable 2>/dev/null || true
	fi

	# Set thermal governor to step_wise (works on both GKI and Non-GKI).
	# Skipped with HiCo: it switches and restores the zone governors itself.
	[ "$HICO_ACTIVE" -eq 0 ] && for dir in /sys/class/thermal/thermal_zone*; do
		apply "step_wise" "$dir/policy"
	done

	# Snapshot stock uclamp values before any profile changes them
	flux_sched_backup
}

performance_profile() {
	LITE_MODE=0
	[ "$1" = "lite" ] && LITE_MODE=1

	# PIN_MAX=1 pins CPU, GPU and memory bus at their highest clock (min = max) and uses the
	# performance governor: the most heat. Sustained mode (default, Settings -> Flux Boost) and
	# Lite keep the highest clocks reachable but let the governor move between a mid floor and
	# the top, so a game that does not need every MHz does not heat the phone into throttling.
	PIN_MAX=1
	if [ $LITE_MODE -eq 1 ] || [ -n "$FLUX_SUSTAINED" ]; then
		PIN_MAX=0
	fi

	# Disable battery saver module
	[ -f /sys/module/battery_saver/parameters/enabled ] && {
		if grep -qo '[0-9]\+' /sys/module/battery_saver/parameters/enabled; then
			apply 0 /sys/module/battery_saver/parameters/enabled
		else
			apply N /sys/module/battery_saver/parameters/enabled
		fi
	}

	# Disable split lock mitigation
	apply 0 /proc/sys/kernel/split_lock_mitigate

	# Consider scheduling tasks that are eager to run
	sched_feature NEXT_BUDDY

	# Some sources report large latency spikes during large migrations
	sched_feature NO_TTWU_QUEUE

	if [ -d "/dev/stune/" ]; then
		# Prefer to schedule top-app tasks on idle CPUs
		apply 1 /dev/stune/top-app/schedtune.prefer_idle

		# Mark top-app as boosted, find high-performing CPUs
		apply 1 /dev/stune/top-app/schedtune.boost
	fi

	# uclamp equivalent of the stune boost above for 5.x/GKI kernels
	if [ $LITE_MODE -eq 1 ]; then
		flux_sched lite
	else
		flux_sched performance
	fi

	# Memory, block queues and game thread priority (Flux Boost)
	flux_boost boost

	# Touch panel game mode (switchable: Game tweaks → Touch)
	flux_touch on

	# Highest refresh rate while gaming (opt-in)
	flux_refresh boost

	# The performance tweaks below drive the GPU; keep the kernel's governor under them.
	change_gpu_gov ""

	# SurfaceFlinger / composer on the game's cgroups (switchable: Game tweaks → Surface)
	flux_surface boost

	# core_ctl, sched_boost, GPU power rails (switchable: Game tweaks → Chipset; not in Lite)
	flux_chipset boost

	# Memory tweak
	apply 80 /proc/sys/vm/vfs_cache_pressure

	# Set CPU governor to performance.
	# If lite mode enabled, use the default governor instead.
	# device mitigation also will prevent performance gov to be
	# applied (some device hates performance governor).
	if [ $PIN_MAX -eq 1 ] && [ -z "$FLUX_NO_PERFORMANCE_CPUGOV" ]; then
		change_cpu_gov performance
	else
		change_cpu_gov "$DEFAULT_CPU_GOV"
	fi

	# CPU clocks: pinned at the maximum, or a mid floor with the maximum reachable (PIN_MAX=0).
	if [ -d /proc/ppm ]; then
		cpufreq_ppm_max_perf
	else
		cpufreq_max_perf
	fi

	# I/O Tweaks
	for dir in /sys/block/mmcblk0 /sys/block/mmcblk1 /sys/block/sd*; do
		# Reduce heuristic read-ahead in exchange for I/O latency
		apply 32 "$dir/queue/read_ahead_kb"

		# Reduce the maximum number of I/O requests in exchange for latency
		apply 32 "$dir/queue/nr_requests"
	done &

	case $SOC in
	1) mediatek_performance ;;
	2) snapdragon_performance ;;
	3) exynos_performance ;;
	4) unisoc_performance ;;
	5) tensor_performance ;;
	6) tegra_performance ;;
	esac

	# Free the page cache for the game (switchable: Game tweaks → Clear memory cache)
	[ -z "$FLUX_DROP_CACHES_DISABLED" ] && echo 3 >/proc/sys/vm/drop_caches
}

balance_profile() {
	# Disable battery saver module
	[ -f /sys/module/battery_saver/parameters/enabled ] && {
		if grep -qo '[0-9]\+' /sys/module/battery_saver/parameters/enabled; then
			apply 0 /sys/module/battery_saver/parameters/enabled
		else
			apply N /sys/module/battery_saver/parameters/enabled
		fi
	}

	# Enable split lock mitigation
	apply 1 /proc/sys/kernel/split_lock_mitigate

	# Consider scheduling tasks that are eager to run
	sched_feature NEXT_BUDDY

	# Schedule tasks on their origin CPU if possible
	sched_feature TTWU_QUEUE

	if [ -d "/dev/stune/" ]; then
		# We are not concerned with prioritizing latency
		apply 0 /dev/stune/top-app/schedtune.prefer_idle

		# Don't boost foreground tasks, let the governor handle it
		apply 0 /dev/stune/top-app/schedtune.boost
	fi

	# Back to stock uclamp values
	flux_sched balance

	# Flux Boost off: vendor memory / block queue values, game threads back to their priority
	flux_boost restore

	# Touch panel back to normal, refresh rate back to the user's setting
	flux_touch off
	flux_refresh restore

	# Composer threads and vendor chipset knobs back to where they were
	flux_surface restore
	flux_chipset restore

	# Re-evaluate the network switch (it may have changed since boot)
	flux_net

	# Memory Tweaks
	apply 120 /proc/sys/vm/vfs_cache_pressure

	# Restore min CPU frequency
	change_cpu_gov "$DEFAULT_CPU_GOV"

	if [ -d /proc/ppm ]; then
		cpufreq_ppm_unlock
	else
		cpufreq_unlock
	fi

	# I/O Tweaks
	for dir in /sys/block/mmcblk0 /sys/block/mmcblk1 /sys/block/sd*; do
		# Reduce heuristic read-ahead in exchange for I/O latency
		apply 128 "$dir/queue/read_ahead_kb"

		# Reduce the maximum number of I/O requests in exchange for latency
		apply 64 "$dir/queue/nr_requests"
	done &

	case $SOC in
	1) mediatek_normal ;;
	2) snapdragon_normal ;;
	3) exynos_normal ;;
	4) unisoc_normal ;;
	5) tensor_normal ;;
	6) tegra_normal ;;
	esac

	# GPU governor for daily use (empty = the kernel's own)
	change_gpu_gov "$FLUX_BALANCED_GPUGOV"
}

powersave_profile() {
	balance_profile

	# Allow cores to go idle, we are not concerned with prioritizing latency
    [ -d "/dev/stune/" ] && apply 1 /dev/stune/top-app/schedtune.prefer_idle

	# Cap background capacity to save power
	flux_sched powersave

	# Enable battery saver module
	[ -f /sys/module/battery_saver/parameters/enabled ] && {
		if grep -qo '[0-9]\+' /sys/module/battery_saver/parameters/enabled; then
			apply 1 /sys/module/battery_saver/parameters/enabled
		else
			apply Y /sys/module/battery_saver/parameters/enabled
		fi
	}

	# CPU governor
	change_cpu_gov "$FLUX_POWERSAVE_CPUGOV"

	case $SOC in
	1) mediatek_powersave ;;
	2) snapdragon_powersave ;;
	3) exynos_powersave ;;
	4) unisoc_powersave ;;
	5) tensor_powersave ;;
	6) tegra_powersave ;;
	esac

	# GPU governor for powersave (empty = the kernel's own)
	change_gpu_gov "$FLUX_POWERSAVE_GPUGOV"
}

###################################
# System tweaks: graphics props, adaptive refresh, zram
###################################

# Run by fluxd at start and whenever one of these switches changes
# ("flux_profiler system"). A switch that is off puts back the values saved
# before Flux first changed them, so every part reverts fully.

FLUX_PROPS_ORIG="$MODULE_CONFIG/props_orig"
FLUX_PROPS_STAGE="$MODULE_CONFIG/props_stage"
FLUX_SYSTEM_PROP=/data/adb/modules/flux/system.prop

flux_resetprop() {
	for bin in resetprop /data/adb/magisk/resetprop /data/adb/ksu/bin/resetprop /data/adb/ap/bin/resetprop; do
		command -v "$bin" >/dev/null 2>&1 && {
			echo "$bin"
			return 0
		}
	done
	return 1
}

# flux_prop_live <name> <value|"">: the live value (debug.* also through setprop)
flux_prop_live() {
	if rp=$(flux_resetprop); then
		if [ -n "$2" ]; then
			"$rp" -n "$1" "$2" >/dev/null 2>&1
		else
			"$rp" -d "$1" >/dev/null 2>&1
		fi
	else
		case "$1" in debug.*) setprop "$1" "$2" >/dev/null 2>&1 ;; esac
	fi
}

# flux_prop <name> <value>: the value now and from the next boot on (module
# system.prop, loaded before SurfaceFlinger starts); the ROM's value is kept once
flux_prop() {
	grep -q "^$1=" "$FLUX_PROPS_ORIG" 2>/dev/null || echo "$1=$(getprop "$1")" >>"$FLUX_PROPS_ORIG"
	echo "$1=$2" >>"$FLUX_PROPS_STAGE"
	flux_prop_live "$1" "$2"
}

# The ROM's value of a prop Flux may have changed
flux_prop_orig() {
	if grep -q "^$1=" "$FLUX_PROPS_ORIG" 2>/dev/null; then
		sed -n "s/^$1=//p" "$FLUX_PROPS_ORIG" | head -n 1
	else
		getprop "$1"
	fi
}

# flux_prop_lower <name> <value>: only when the ROM's value is unset, 0 or higher
flux_prop_lower() {
	cur=$(flux_prop_orig "$1")
	case "$cur" in '' | *[!0-9]*) cur=0 ;; esac
	if [ "$cur" -eq 0 ] || [ "$cur" -gt "$2" ]; then
		flux_prop "$1" "$2"
	fi
}

# flux_prop_default <name> <value>: only when the ROM leaves it unset
flux_prop_default() {
	[ -z "$(flux_prop_orig "$1")" ] && flux_prop "$1" "$2"
	return 0
}

# Props of every enabled group go to system.prop; all others get the ROM value back.
flux_props_commit() {
	if [ -f "$FLUX_PROPS_ORIG" ]; then
		: >"$FLUX_PROPS_ORIG.new"
		while IFS='=' read -r name value; do
			[ -n "$name" ] || continue
			if grep -q "^$name=" "$FLUX_PROPS_STAGE" 2>/dev/null; then
				echo "$name=$value" >>"$FLUX_PROPS_ORIG.new"
			else
				flux_prop_live "$name" "$value"
			fi
		done <"$FLUX_PROPS_ORIG"
		mv -f "$FLUX_PROPS_ORIG.new" "$FLUX_PROPS_ORIG"
		[ -s "$FLUX_PROPS_ORIG" ] || rm -f "$FLUX_PROPS_ORIG"
	fi

	if [ -s "$FLUX_PROPS_STAGE" ] && [ -d "${FLUX_SYSTEM_PROP%/*}" ]; then
		{
			echo "# Written by Flux (Settings > System tweaks); removed when those are off"
			cat "$FLUX_PROPS_STAGE"
		} >"$FLUX_SYSTEM_PROP"
	else
		rm -f "$FLUX_SYSTEM_PROP"
	fi
	rm -f "$FLUX_PROPS_STAGE"
}

# Adreno Reflex: SurfaceFlinger / EGL latency keys (the kgsl part runs while gaming).
flux_reflex_props() {
	[ -n "$FLUX_REFLEX" ] || return 0
	# Latch buffers whose GPU fence has not signalled yet: the frame is shown one
	# vsync earlier (auto_ on Android 13+, the older key before)
	flux_prop debug.sf.auto_latch_unsignaled 1
	flux_prop debug.sf.latch_unsignaled 1
	# GL backpressure: SurfaceFlinger skips a composition instead of queueing behind the GPU
	flux_prop debug.sf.enable_gl_backpressure 1
	# No frames rendered ahead by HWUI (some vendors raise it for smoother benchmarks)
	flux_prop debug.hwui.render_ahead 0
	# Adreno EGL swapchain: triple buffering, no extra queued frame
	flux_prop debug.egl.buffcount 3
}

# Graphics pipeline: threaded RenderEngine, HWUI performance hints, composition prediction.
flux_graphics_props() {
	[ -n "$FLUX_GRAPHICS" ] || return 0
	# SurfaceFlinger's RenderEngine on its own thread; a ROM that already picked a
	# (Vulkan or threaded) backend keeps it
	case "$(flux_prop_orig debug.renderengine.backend)" in
	'' | skiagl) flux_prop debug.renderengine.backend skiaglthreaded ;;
	esac
	# HWUI reports frame timing to the power HAL (ADPF hint sessions, Android 12+):
	# the CPU ramps up for UI frames before they are late
	flux_prop debug.hwui.use_hint_manager true
	# Predict the HWC composition strategy and start GPU composition early (Android 13+)
	flux_prop debug.sf.predict_hwc_composition_strategy 1
}

FLUX_ADAPTIVE_ORIG="$MODULE_CONFIG/refresh_adaptive_orig"

# Refresh rates the display supports, ascending, one per line
flux_refresh_rates() {
	dumpsys display 2>/dev/null | grep -oE 'fps=[0-9]+(\.[0-9]+)?' | cut -d= -f2 | cut -d. -f1 | sort -un
}

# Adaptive refresh: the panel may drop to its lowest rate (>= 60 Hz) when the
# content allows it; the peak stays at the highest. Vendors that pin
# min_refresh_rate to the peak keep the panel at 120/144 Hz all the time.
flux_adaptive_refresh() {
	if [ -n "$FLUX_ADAPTIVE_REFRESH" ]; then
		# Frame rate override: apps and games get their own rate (60 fps game on a 120 Hz panel)
		flux_prop ro.surface_flinger.enable_frame_rate_override true
		# Content-based rate selection; idle drop after 3 s instead of a longer vendor timer
		flux_prop_default ro.surface_flinger.use_content_detection_for_refresh_rate true
		flux_prop_lower ro.surface_flinger.set_idle_timer_ms 3000
		# A touch returns to the peak rate at once
		flux_prop_default ro.surface_flinger.set_touch_timer_ms 200

		# During a game the game's refresh handling owns these settings.
		[ -f "$FLUX_REFRESH_BACKUP" ] && return 0
		rates=$(flux_refresh_rates)
		low=""
		for rate in $rates; do
			if [ "$rate" -ge 60 ]; then
				low=$rate
				break
			fi
		done
		high=$(echo "$rates" | tail -n 1)
		case "$low$high" in '' | *[!0-9]*) return 0 ;; esac
		[ "$high" -gt "$low" ] || return 0
		[ -f "$FLUX_ADAPTIVE_ORIG" ] ||
			echo "$(settings get system peak_refresh_rate) $(settings get system min_refresh_rate)" >"$FLUX_ADAPTIVE_ORIG"
		settings put system min_refresh_rate "$low"
		settings put system peak_refresh_rate "$high"
		return 0
	fi

	[ -f "$FLUX_ADAPTIVE_ORIG" ] || return 0
	[ -f "$FLUX_REFRESH_BACKUP" ] && return 0
	read -r peak min <"$FLUX_ADAPTIVE_ORIG"
	for pair in "peak_refresh_rate:$peak" "min_refresh_rate:$min"; do
		key=${pair%%:*}
		val=${pair#*:}
		if [ -z "$val" ] || [ "$val" = null ]; then
			settings delete system "$key" >/dev/null 2>&1
		else
			settings put system "$key" "$val"
		fi
	done
	rm -f "$FLUX_ADAPTIVE_ORIG"
}

FLUX_ZRAM_ORIG="$MODULE_CONFIG/zram_orig"
FLUX_ZRAM_APPLIED="$MODULE_CONFIG/zram_applied"

# Active compressor: the bracketed entry of comp_algorithm
zram_algo() {
	sed -n 's/.*\[\([^]]*\)\].*/\1/p' "$1/comp_algorithm" 2>/dev/null
}

# zram_resize <dir> <size> <algo>: swapoff, reset, new size and compressor, swapon
zram_resize() {
	name=${1##*/}
	dev=/dev/block/$name
	[ -b "$dev" ] || dev=/dev/$name
	[ -b "$dev" ] || return 1
	# swapoff moves every swapped page back to RAM; it fails (and zram stays
	# as it is) when there is not enough free memory
	if grep -qE "^(/dev/block/|/dev/)$name " /proc/swaps; then
		swapoff "$dev" >/dev/null 2>&1 || return 1
	fi
	echo 1 >"$1/reset" 2>/dev/null
	[ -n "$3" ] && echo "$3" >"$1/comp_algorithm" 2>/dev/null
	echo "$2" >"$1/disksize" 2>/dev/null || return 1
	mkswap "$dev" >/dev/null 2>&1 || return 1
	swapon "$dev" >/dev/null 2>&1
}

# Zram sized to the RAM: 3/4 of it up to 4 GB, half above, at most 6 GB; lz4
# (fastest to decompress: pages a game touches again come back quickest).
# Sizes in MB, the shell's arithmetic may be 32-bit.
flux_zram() {
	zram=/sys/block/zram0
	[ -f $zram/disksize ] || return 0
	# Writeback to a backing file (Xiaomi / Samsung memory extension) is the vendor's setup
	case "$(cat $zram/backing_dev 2>/dev/null)" in '' | none) ;; *) return 0 ;; esac

	if [ -z "$FLUX_ZRAM" ]; then
		[ -f "$FLUX_ZRAM_ORIG" ] || return 0
		read -r size algo <"$FLUX_ZRAM_ORIG"
		# The ROM sets zram up again at every boot: only undo what this boot changed
		if [ "$(cat $zram/disksize)" = "$(cat "$FLUX_ZRAM_APPLIED" 2>/dev/null)" ]; then
			case "$size" in '' | *[!0-9]*) ;; *) zram_resize $zram "$size" "$algo" ;; esac
		fi
		rm -f "$FLUX_ZRAM_ORIG" "$FLUX_ZRAM_APPLIED"
		return 0
	fi

	mem_kb=0
	while read -r key value _; do
		if [ "$key" = MemTotal: ]; then
			mem_kb=$value
			break
		fi
	done </proc/meminfo
	case "$mem_kb" in '' | *[!0-9]*) return 0 ;; esac
	mem_mb=$((mem_kb / 1024))
	[ "$mem_mb" -gt 0 ] || return 0
	if [ "$mem_mb" -le 4096 ]; then
		size_mb=$((mem_mb * 3 / 4))
	else
		size_mb=$((mem_mb / 2))
	fi
	[ "$size_mb" -gt 6144 ] && size_mb=6144
	[ "$size_mb" -ge 256 ] || return 0

	algo=$(zram_algo $zram)
	for want in lz4 lzo-rle lzo; do
		if grep -qw "$want" $zram/comp_algorithm 2>/dev/null; then
			algo=$want
			break
		fi
	done

	cur=$(cat $zram/disksize)
	# Already set up by Flux this boot
	[ "$cur" = "$(cat "$FLUX_ZRAM_APPLIED" 2>/dev/null)" ] && [ "$(zram_algo $zram)" = "$algo" ] && return 0

	[ -f "$FLUX_ZRAM_ORIG" ] || echo "$cur $(zram_algo $zram)" >"$FLUX_ZRAM_ORIG"
	zram_resize $zram "${size_mb}M" "$algo" && cat $zram/disksize >"$FLUX_ZRAM_APPLIED"
}

flux_system() {
	rm -f "$FLUX_PROPS_STAGE"
	flux_reflex_props
	flux_graphics_props
	flux_adaptive_refresh
	flux_props_commit
	flux_zram
}

###################################
# Main Function
###################################

case "$1" in
"perfcommon") perfcommon ;;
"system") flux_system ;;
"performance") performance_profile ;;
"performance_lite") performance_profile lite ;;
"balance") balance_profile ;;
"powersave") powersave_profile ;;
esac

wait
exit 0
