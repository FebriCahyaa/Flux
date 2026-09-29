#!/bin/sh
# Exercises flux_refresh (scripts/flux_profiler.sh) against fake `dumpsys` and `settings`
# commands. Only the refresh functions are extracted and run; nothing else in the profiler
# is sourced, and no real settings or /dev path is touched.

SCRIPT="$1"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin"
fail=0
check() { # <description> <expected> <actual>
	if [ "$2" != "$3" ]; then
		echo "FAIL: $1: expected '$2' got '$3'"
		fail=1
	fi
}

# The panel: 60 / 90 / 119.99 (a "120 Hz" mode as Android reports it)
cat >"$T/bin/dumpsys" <<EOF
#!/bin/sh
echo "  DisplayModeRecord{id=1, width=1080, height=2400, fps=60.0}"
echo "  DisplayModeRecord{id=2, width=1080, height=2400, fps=90.000015}"
echo "  DisplayModeRecord{id=3, width=1080, height=2400, fps=119.99}"
EOF
cat >"$T/bin/settings" <<EOF
#!/bin/sh
store="$T/settings.db"
case "\$1" in
get) grep "^\$3=" "\$store" 2>/dev/null | cut -d= -f2- || true ;;
put) grep -v "^\$3=" "\$store" >"\$store.n" 2>/dev/null; echo "\$3=\$4" >>"\$store.n"; mv "\$store.n" "\$store" ;;
delete) grep -v "^\$3=" "\$store" >"\$store.n" 2>/dev/null; mv "\$store.n" "\$store" ;;
esac
EOF
chmod +x "$T/bin/dumpsys" "$T/bin/settings"

# Extract just the refresh block, redirecting the ownership marker into the sandbox.
sed -n '/^FLUX_REFRESH_BACKUP=/,/^# GPU devfreq governor node/p' "$SCRIPT" | sed '$d' |
	sed "s|/dev/.flux_refresh_orig|$T/backup|" >"$T/refresh.sh"

run() { # <env assignments...> -- <action>; prints nothing, effects land in settings.db
	(
		PATH="$T/bin:$PATH"
		# shellcheck disable=SC2163
		while [ "$1" != -- ]; do export "$1"; shift; done
		shift
		. "$T/refresh.sh"
		flux_refresh "$1"
	)
}
get() { grep "^$1=" "$T/settings.db" 2>/dev/null | cut -d= -f2-; }
reset() { printf 'peak_refresh_rate=90\nmin_refresh_rate=60\n' >"$T/settings.db"; rm -f "$T/backup"; }

# 1. panel rates are normalised: 119.99 -> 120
check "rates" "120 90 60" "$(PATH="$T/bin:$PATH"; . "$T/refresh.sh"; flux_panel_rates | tr '\n' ' ' | sed 's/ $//')"

# 2. game's own target is honoured and pinned
reset
run FLUX_REFRESH_ENABLED=1 FLUX_REFRESH_TARGET_HZ=90 -- boost
check "target peak" 90 "$(get peak_refresh_rate)"
check "target min" 90 "$(get min_refresh_rate)"
check "marker written" yes "$([ -f "$T/backup" ] && echo yes || echo no)"

# 3. restore puts the user's values back and drops the marker
run -- restore
check "restore peak" 90 "$(get peak_refresh_rate)"
check "restore min" 60 "$(get min_refresh_rate)"
check "marker removed" no "$([ -f "$T/backup" ] && echo yes || echo no)"

# 4. a target the panel does not offer is ignored, never forced
reset
run FLUX_REFRESH_ENABLED=1 FLUX_REFRESH_TARGET_HZ=144 -- boost
check "unsupported peak untouched" 90 "$(get peak_refresh_rate)"
check "unsupported min untouched" 60 "$(get min_refresh_rate)"
check "no marker for a no-op" no "$([ -f "$T/backup" ] && echo yes || echo no)"

# 5. 120 matches the 119.99 mode
reset
run FLUX_REFRESH_ENABLED=1 FLUX_REFRESH_TARGET_HZ=120 -- boost
check "120 target" 120 "$(get peak_refresh_rate)"
run -- restore

# 6. no target: the old "highest rate" behaviour
reset
run FLUX_REFRESH_ENABLED=1 -- boost
check "highest peak" 120 "$(get peak_refresh_rate)"
run -- restore
check "highest restore" 60 "$(get min_refresh_rate)"

# 7. neither option set: nothing happens
reset
run FLUX_REFRESH_TARGET_HZ= -- boost
check "disabled peak" 90 "$(get peak_refresh_rate)"
check "disabled no marker" no "$([ -f "$T/backup" ] && echo yes || echo no)"

# 8. garbage target is ignored
reset
run FLUX_REFRESH_ENABLED=1 'FLUX_REFRESH_TARGET_HZ=90;rm' -- boost
check "garbage ignored" 90 "$(get peak_refresh_rate)"

# 9. a second boost does not overwrite the saved originals
reset
run FLUX_REFRESH_ENABLED=1 FLUX_REFRESH_TARGET_HZ=60 -- boost
run FLUX_REFRESH_ENABLED=1 FLUX_REFRESH_TARGET_HZ=90 -- boost
run -- restore
check "double boost restore peak" 90 "$(get peak_refresh_rate)"
check "double boost restore min" 60 "$(get min_refresh_rate)"

[ "$fail" = 0 ] && echo "refresh_script_test: all passed"
exit "$fail"
