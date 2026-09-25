#
# Copyright (C) 2024-2026 Rem01Gaming
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

MODULE_CONFIG="/data/adb/.config/flux"

# Stop the daemons if they are still running (uninstall from the manager
# while booted); on the next boot they are simply never started.
for pidfile in sysmon_watchdog.pid sysmon.pid; do
	pid=$(cat "$MODULE_CONFIG/$pidfile" 2>/dev/null)
	[ -n "$pid" ] && kill "$pid" 2>/dev/null
done
pkill -x fluxd 2>/dev/null

# HiCo Thermal follows fluxd's game state; without Flux it must not keep
# thermal throttling disabled. Put the stock thermal configuration back now
# (it also suspends itself as soon as it sees Flux gone).
HICOD=/data/adb/modules/hico/system/bin/hicod
[ -x "$HICOD" ] && "$HICOD" restore >/dev/null 2>&1

# Symlinks created by customize.sh on KernelSU / APatch
for dir in /data/adb/ksu/bin /data/adb/ap/bin; do
	for bin in fluxd flux_profiler flux_utility; do
		[ -L "$dir/$bin" ] && rm -f "$dir/$bin"
	done
done

# Configuration, logs and the boot cleanup hook
rm -rf "$MODULE_CONFIG"
rm -f /data/adb/service.d/.flux_cleanup.sh
# Refresh rate forced while gaming: give the user's setting back
if [ -f /dev/.flux_refresh_orig ]; then
	read -r peak min </dev/.flux_refresh_orig
	[ -n "$peak" ] && [ "$peak" != null ] && settings put system peak_refresh_rate "$peak"
	[ -n "$min" ] && [ "$min" != null ] && settings put system min_refresh_rate "$min"
fi
# Values Flux changed while gaming (Flux Boost, game tweaks, chipset boost):
# "<group> <node> <mode> <value>" per line
if [ -f /dev/.flux_boost_orig ]; then
	while read -r _ node mode value; do
		[ -f "$node" ] || continue
		chmod 644 "$node" 2>/dev/null
		echo "$value" >"$node" 2>/dev/null
		chmod "$mode" "$node" 2>/dev/null
	done </dev/.flux_boost_orig
fi
# SurfaceFlinger / composer and input threads back to their cgroups: "<tid> <root> <path>"
for backup in /dev/.flux_surface /dev/.flux_input; do
	[ -f "$backup" ] || continue
	while read -r tid dir path; do
		[ -d "/proc/$tid" ] && echo "$tid" >"$dir${path%/}/tasks" 2>/dev/null
	done <"$backup"
done
# Samsung touch game mode switched on by Flux
[ -f /dev/.flux_sec_game ] && echo "set_game_mode,0" >/sys/class/sec/tsp/cmd 2>/dev/null
# Mali power policy: "<node> <policy>"
if [ -f /dev/.flux_mali_policy ]; then
	while read -r node policy; do
		[ -f "$node" ] && echo "$policy" >"$node" 2>/dev/null
	done </dev/.flux_mali_policy
fi
rm -f /dev/.flux_sched_orig /dev/.flux_boost_orig /dev/.flux_game_prio /dev/.flux_refresh_orig \
	/dev/.flux_surface /dev/.flux_input /dev/.flux_mali_policy /dev/.flux_sec_game

# Leftovers from Encore Tweaks, which Flux replaces
rm -rf /data/adb/.config/encore
rm -f /data/adb/service.d/.encore_cleanup.sh
for dir in /data/adb/ksu/bin /data/adb/ap/bin; do
	for bin in encored encore_profiler encore_utility encore_log; do
		rm -f "$dir/$bin"
	done
done
