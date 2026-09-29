#!/system/bin/sh
# Flux Game Runtime — real-device validation (Phase 3).
#
#   su -c 'sh /data/adb/modules/flux/system/bin/flux_runtime_validate.sh <package>'   (or run it from any path)
#
# Read-mostly: it adds ONE entry (the package you name) to game_profiles.json, backing the file up
# first and putting it back at the end (or when interrupted). It never edits the runtime, never
# invokes GameRuntime by hand, and never deletes compat_journal. The game must already be in
# Flux's game list. You launch and close the game yourself; Flux's own detection does the rest.
#
# Optional environment:
#   WITH_TOUCH_STORAGE=1   also set touch=responsive and storage=gaming for the test entry
#   SKIP_CRASH=1           skip the game-process-death test
#   HICO_STATE_CMD='...'   extra command whose output is recorded as HiCo state
#
# Evidence lands in /data/adb/.config/flux/validation/<timestamp>/ and is summarised in report.txt.
# Results are PASS / FAIL / NOT TESTED per item; nothing is inferred from a profile file alone.

PKG="$1"
[ -n "$PKG" ] || { echo "usage: $0 <package>"; exit 2; }
case "$PKG" in *[!A-Za-z0-9._-]* | '') echo "invalid package"; exit 2 ;; esac

CFG=/data/adb/.config/flux
PROFILES=$CFG/game_profiles.json
JOURNAL=$CFG/compat_journal
LOG=$CFG/flux.log
D=$CFG/validation/$(date +%Y%m%d_%H%M%S)
mkdir -p "$D" || exit 1
REPORT=$D/report.txt
BACKUP=$D/game_profiles.json.orig
HAD_PROFILES=0

say() { echo "$*" | tee -a "$REPORT"; }
res() { printf '%-34s %s\n' "$1" "$2" | tee -a "$REPORT"; }

restore_profiles() {
	if [ "$HAD_PROFILES" = 1 ]; then cp "$BACKUP" "$PROFILES"; else rm -f "$PROFILES"; fi
}
trap 'restore_profiles' EXIT INT TERM

# ---- nodes: the exact ones the planners write (jni/compat/PerfPlanner.cpp) ------------------
MEM_NODES="/proc/sys/vm/swappiness /proc/sys/vm/vfs_cache_pressure /proc/sys/vm/page-cluster /proc/sys/vm/dirty_expire_centisecs /proc/sys/vm/watermark_boost_factor"
MEM_EXPECT="swappiness=60 vfs_cache_pressure=80 page-cluster=0 dirty_expire_centisecs=1500 watermark_boost_factor=0"
TOUCH_NODE=/sys/module/cpu_boost/parameters/input_boost_ms
BLOCK_QUEUES=$(for d in /sys/block/sd* /sys/block/nvme* /sys/block/mmcblk*; do
	case "$d" in *rpmb | *boot[0-9]) continue ;; esac
	[ "$(basename "$d" | cut -c1-6)" = mmcblk ] && [ "$(cat "$d/removable" 2>/dev/null)" = 1 ] && continue
	[ -d "$d/queue" ] && echo "$d/queue"
done)

val() { if [ -e "$1" ]; then tr -d '\n' <"$1" 2>/dev/null; else echo "ABSENT"; fi; }

panel_rates() {
	dumpsys display 2>/dev/null | grep -oE 'fps=[0-9]+(\.[0-9]+)?' | cut -d= -f2 | sort -rn | uniq | tr '\n' ' '
}

snap() { # <label>
	f=$D/$1.txt
	{
		echo "== $1  $(date '+%F %T')"
		echo "-- fluxd:            $(pidof fluxd 2>/dev/null || echo NOT-RUNNING)"
		echo "-- current_profile:  $(cat $CFG/current_profile 2>/dev/null)"
		echo "-- gameinfo:         $(cat $CFG/gameinfo 2>/dev/null)"
		echo "-- compat_journal:   $(wc -c <$JOURNAL 2>/dev/null || echo ABSENT) bytes"
		sed 's/^/     /' "$JOURNAL" 2>/dev/null
		echo "-- compat_status:    $(cat $CFG/compat_status.json 2>/dev/null)"
		for n in $MEM_NODES; do echo "mem $(basename $n)=$(val $n)"; done
		echo "touch input_boost_ms=$(val $TOUCH_NODE)"
		for q in $BLOCK_QUEUES; do
			echo "storage $q read_ahead_kb=$(val $q/read_ahead_kb) rq_affinity=$(val $q/rq_affinity)"
		done
		echo "refresh peak=$(settings get system peak_refresh_rate) min=$(settings get system min_refresh_rate)"
		echo "refresh marker=$([ -f /dev/.flux_refresh_orig ] && cat /dev/.flux_refresh_orig || echo none)"
		echo "panel rates: $(panel_rates)"
		echo "-- hico"
		echo "module.prop: $([ -f /data/adb/modules/hico/module.prop ] && echo yes || echo no)  disable: $([ -f /data/adb/modules/hico/disable ] && echo yes || echo no)"
		echo "hico.conf: $(cat /data/adb/.config/hico/hico.conf 2>/dev/null | tr '\n' ' ')"
		echo "hico procs: $(ps -A 2>/dev/null | grep -i hico | tr -s ' ' | cut -d' ' -f2,9- | tr '\n' ';')"
		[ -n "$HICO_STATE_CMD" ] && echo "hico cmd: $(sh -c "$HICO_STATE_CMD" 2>&1 | tr '\n' ' ')"
	} >"$f" 2>&1
}

get() { grep "^$2" "$D/$1.txt" | head -n 1 | sed "s/^$2//"; }
line() { grep -F "$2" "$D/$1.txt" | head -n 1; }
wait_for() { # <seconds> <cmd...> : poll every second
	t=$1; shift
	while [ "$t" -gt 0 ]; do "$@" && return 0; sleep 1; t=$((t - 1)); done
	return 1
}
in_game() { grep -q "^$PKG " $CFG/gameinfo 2>/dev/null; }
out_game() { grep -q '^NULL' $CFG/gameinfo 2>/dev/null; }
ask() { printf '%s ' "$1"; read -r _; }
log_mark() { wc -l <"$LOG" 2>/dev/null || echo 0; }
log_since() { tail -n +"$(($1 + 1))" "$LOG" 2>/dev/null; }

# ---- 0. health -----------------------------------------------------------------------------
say "Flux Game Runtime device validation  package=$PKG  evidence=$D"
say "device: $(getprop ro.product.model) / Android $(getprop ro.build.version.release) (SDK $(getprop ro.build.version.sdk)) / kernel $(uname -r) / root: $(su -v 2>/dev/null || echo unknown)"
snap baseline
DAEMON=FAIL; [ -n "$(pidof fluxd 2>/dev/null)" ] && DAEMON=PASS
if [ "$DAEMON" = FAIL ]; then say "fluxd is not running — stopping (daemon must be healthy)."; res "Daemon activation" FAIL; exit 1; fi
grep -q "\"$PKG\"" $CFG/gamelist.json 2>/dev/null || { say "$PKG is not in gamelist.json — add it in Flux first."; exit 1; }
[ -f "$D/../../capabilities.json" ] || [ -f $CFG/capabilities.json ] || say "note: capabilities.json missing (hardware treated as unknown)"

# ---- 1. test profile (exactly one entry) -----------------------------------------------------
[ -f "$PROFILES" ] && { cp "$PROFILES" "$BACKUP"; HAD_PROFILES=1; }
EXTRA=''
[ -n "$WITH_TOUCH_STORAGE" ] && EXTRA=', "touch": "responsive", "storage": "gaming"'
cat >"$PROFILES" <<EOF
{ "$PKG": { "package": "$PKG",
  "performance": { "memory": "gaming", "refresh": "hz120"$EXTRA },
  "compatibility": { "mode": "real" } } }
EOF
say "test profile written for $PKG only (compatibility.mode=real: no identity layer). Original saved: $([ $HAD_PROFILES = 1 ] && echo yes || echo 'none existed')"

# ---- 2. launch --------------------------------------------------------------------------------
M0=$(log_mark)
ask "Launch $PKG now, let it reach gameplay, then press Enter:"
GD=FAIL; wait_for 30 in_game && GD=PASS
snap during
sleep 5; snap during_settled
log_since "$M0" >"$D/log_start.txt"

# ---- 3. classify the running state --------------------------------------------------------------
mem_ok=PASS; mem_seen=0
for kv in $MEM_EXPECT; do
	k=${kv%%=*}; want=${kv#*=}
	got=$(get during_settled "mem $k=")
	[ "$got" = ABSENT ] && continue          # unsupported node: must simply be untouched
	mem_seen=$((mem_seen + 1))
	[ "$got" = "$want" ] || { mem_ok=FAIL; echo "memory $k: want $want got $got" >>"$D/mismatch.txt"; }
done
[ "$mem_seen" -gt 0 ] || mem_ok=FAIL
for kv in $MEM_EXPECT; do
	k=${kv%%=*}
	[ "$(get baseline "mem $k=")" = ABSENT ] && [ "$(get during_settled "mem $k=")" != ABSENT ] && mem_ok=FAIL
done

RT=FAIL; grep -q "activate package=$PKG" "$D/log_start.txt" && RT=PASS
PERF=FAIL
cp_now=$(cat $CFG/current_profile 2>/dev/null)
gi=$(cat $CFG/gameinfo 2>/dev/null)
case "$gi" in "$PKG "[0-9]*" "[0-9]*) [ "$cp_now" != "" ] && PERF=PASS ;; esac
grep -q "compatibility=FAILED" "$D/log_start.txt" && say "note: compatibility failed; performance must (and is checked to) continue: $PERF"

REF=NOT-TESTED
rates=$(get during_settled "panel rates: ")
case "$rates" in *119.9* | *120*)
	peak=$(get during_settled "refresh peak=" | cut -d' ' -f1); min=$(get during_settled "refresh peak=" | sed 's/.*min=//')
	[ "$peak" = 120 ] && [ "$min" = 120 ] && REF=PASS || REF=FAIL ;;
*) say "panel offers no ~120 Hz mode ($rates): a hz120 request must change NOTHING"
	[ "$(get during_settled 'refresh peak=')" = "$(get baseline 'refresh peak=')" ] && REF=PASS || REF=FAIL ;;
esac

TOUCH=NOT-TESTED; STORAGE=NOT-TESTED
if [ -n "$WITH_TOUCH_STORAGE" ]; then
	if [ "$(get baseline 'touch input_boost_ms=')" = ABSENT ]; then
		[ "$(get during_settled 'touch input_boost_ms=')" = ABSENT ] && TOUCH="PASS(node absent, untouched)" || TOUCH=FAIL
	else
		[ "$(get during_settled 'touch input_boost_ms=')" = 80 ] && TOUCH=PASS || TOUCH=FAIL
	fi
	if [ -n "$BLOCK_QUEUES" ]; then
		grep -q 'read_ahead_kb=512' "$D/during_settled.txt" && STORAGE=PASS || STORAGE=FAIL
	fi
else
	# not requested: GameRuntime must not have touched touch/storage; Flux Boost may, so report only
	TOUCH="NOT-TESTED(profile default)"; STORAGE="NOT-TESTED(profile default)"
fi

# ---- 4. exit ---------------------------------------------------------------------------------------
M1=$(log_mark)
ask "Close $PKG normally (leave the app), then press Enter:"
DEACT=FAIL; wait_for 30 out_game && DEACT=PASS
sleep 3; snap after
log_since "$M1" >"$D/log_exit.txt"
grep -q "deactivate package=$PKG" "$D/log_exit.txt" || DEACT=FAIL

RESTORE=PASS
for n in $MEM_NODES; do
	k=$(basename "$n")
	[ "$(get after "mem $k=")" = "$(get baseline "mem $k=")" ] || { RESTORE=FAIL; echo "restore $k: baseline $(get baseline "mem $k=") after $(get after "mem $k=")" >>"$D/mismatch.txt"; }
done
[ "$(get after 'refresh peak=')" = "$(get baseline 'refresh peak=')" ] || { RESTORE=FAIL; echo "refresh not restored" >>"$D/mismatch.txt"; }
[ -f /dev/.flux_refresh_orig ] && { RESTORE=FAIL; echo "refresh marker still present" >>"$D/mismatch.txt"; }
[ "$(get after 'touch input_boost_ms=')" = "$(get baseline 'touch input_boost_ms=')" ] || RESTORE=FAIL
for q in $BLOCK_QUEUES; do
	[ "$(line after "storage $q")" = "$(line baseline "storage $q")" ] || { RESTORE=FAIL; echo "storage $q differs" >>"$D/mismatch.txt"; }
done
grep -q 'restore=PASS' "$D/log_exit.txt" || RESTORE=FAIL

JOUR=PASS
[ -s "$JOURNAL" ] && JOUR=FAIL   # non-empty after a clean exit = stale incomplete transaction

# ---- 5. crash / process death -------------------------------------------------------------------------
CRASH=NOT-TESTED
if [ -z "$SKIP_CRASH" ]; then
	M2=$(log_mark)
	ask "Crash test: launch $PKG again, wait for gameplay, press Enter (the script will kill ONLY the game process):"
	if wait_for 30 in_game; then
		sleep 5; snap crash_during
		gpid=$(awk '{print $2}' $CFG/gameinfo)
		if [ -n "$gpid" ] && [ "$gpid" -gt 1 ] 2>/dev/null; then
			kill -9 "$gpid"
			CRASH=FAIL; wait_for 30 out_game && CRASH=PASS
			sleep 3; snap crash_after
			log_since "$M2" >"$D/log_crash.txt"
			grep -q 'reason=process_death' "$D/log_crash.txt" || CRASH=FAIL
			[ "$(get crash_after 'mem swappiness=')" = "$(get baseline 'mem swappiness=')" ] || CRASH=FAIL
			[ -s "$JOURNAL" ] && CRASH=FAIL
		fi
	fi
fi

# ---- 6. HiCo (ownership unchanged) ---------------------------------------------------------------------------
HICO=NOT-TESTED
if [ -f /data/adb/modules/hico/module.prop ]; then
	HICO=PASS
	# GameRuntime must not touch anything of HiCo's: its config and module flags identical before/after.
	[ "$(get baseline 'hico.conf: ')" = "$(get after 'hico.conf: ')" ] || HICO=FAIL
	[ "$(line baseline 'module.prop:')" = "$(line after 'module.prop:')" ] || HICO=FAIL
	grep -qi 'hico' "$D/log_start.txt" "$D/log_exit.txt" 2>/dev/null && say "note: hico mentioned in Flux log slice; review it"
	say "HiCo before/during/after recorded in baseline.txt / during_settled.txt / after.txt (compare 'hico' sections)."
fi

# ---- report ------------------------------------------------------------------------------------------------------------
say ""
say "================ RESULTS (this device, this game; not a graphics-unlock test) ================"
res "Daemon activation"       "$DAEMON"
res "Game detection"          "$GD"
res "GameRuntime activation"  "$RT"
res "Performance integration" "$PERF"
res "Memory override"         "$mem_ok"
res "Refresh"                 "$REF"
res "Touch"                   "$TOUCH"
res "Storage"                 "$STORAGE"
res "Deactivation"            "$DEACT"
res "Restore"                 "$RESTORE"
res "Journal"                 "$JOUR"
res "Crash recovery"          "$CRASH"
res "HiCo integration"        "$HICO"
res "Device/CPU/GPU identity" "NOT TESTED"
res "Zygisk provider"         "NOT IMPLEMENTED"
res "Graphics unlock"         "NOT TESTED"
[ -f "$D/mismatch.txt" ] && { say "-- mismatches:"; cat "$D/mismatch.txt" | tee -a "$REPORT"; }
say "Evidence: $D  (baseline/during/after snapshots, log_start/exit/crash slices)"
say "Profiles file restored on exit."
