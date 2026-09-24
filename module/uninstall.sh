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

# Symlinks created by customize.sh on KernelSU / APatch
for dir in /data/adb/ksu/bin /data/adb/ap/bin; do
	for bin in fluxd flux_profiler flux_utility; do
		[ -L "$dir/$bin" ] && rm -f "$dir/$bin"
	done
done

# Configuration, logs and the boot cleanup hook
rm -rf "$MODULE_CONFIG"
rm -f /data/adb/service.d/.flux_cleanup.sh
rm -f /dev/.flux_sched_orig

# Leftovers from Encore Tweaks, which Flux replaces
rm -rf /data/adb/.config/encore
rm -f /data/adb/service.d/.encore_cleanup.sh
for dir in /data/adb/ksu/bin /data/adb/ap/bin; do
	for bin in encored encore_profiler encore_utility encore_log; do
		rm -f "$dir/$bin"
	done
done
