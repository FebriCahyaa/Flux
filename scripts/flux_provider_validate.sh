#!/system/bin/sh
# Flux Zygisk provider — real-device validation with the dedicated test app (Phase 4).
#
#   su -c 'sh flux_provider_validate.sh'
#
# Needs: the three test-app flavors installed (tools/compat-testapp: dev.flux.compattest.{a,b,plain}),
# Zygisk enabled in your root manager, the Flux zip installed and the device rebooted since.
#
# What it does, in order (every result is read back from the app's own output, not from Flux):
#   1. records the real identity of each app with no profile
#   2. writes ONE test library + profiles for apps A and B only (backing up your files, restoring on exit)
#   3. arms the plans (fluxd compat_arm) and launches A, B and the unconfigured app
#   4. checks A shows profile A, B shows profile B, the unconfigured app still shows the real device
#   5. checks the global `getprop` values did not change
#   6. removes the profiles, re-arms, relaunches A: it must be real again
#
# It does not touch a real game, changes no Flux setting other than the provider opt-in file and the
# profile documents above, and never claims a graphics unlock: that needs a real game.

CFG=/data/adb/.config/flux
PA=dev.flux.compattest.a
PB=dev.flux.compattest.b
PP=dev.flux.compattest.plain
D=$CFG/validation/provider_$(date +%Y%m%d_%H%M%S)
mkdir -p "$D" || exit 1
REPORT=$D/report.txt
say() { echo "$*" | tee -a "$REPORT"; }
res() { printf '%-42s %s\n' "$1" "$2" | tee -a "$REPORT"; }

for f in game_profiles.json compat_library.json compat_zygisk_optin; do
	[ -e "$CFG/$f" ] && cp "$CFG/$f" "$D/$f.orig" && echo yes >"$D/$f.had"
done
restore() {
	for f in game_profiles.json compat_library.json compat_zygisk_optin; do
		if [ -f "$D/$f.had" ]; then cp "$D/$f.orig" "$CFG/$f"; else rm -f "$CFG/$f"; fi
	done
	fluxd compat_arm >/dev/null 2>&1
}
trap restore EXIT INT TERM

for p in $PA $PB $PP; do
	pm list packages 2>/dev/null | grep -q "package:$p\$" || { say "test app $p is not installed"; exit 1; }
done
[ -n "$(pidof fluxd)" ] || { say "fluxd is not running"; exit 1; }
[ -f /data/adb/modules/flux/zygisk/arm64-v8a.so ] || [ -f /data/adb/modules/flux/zygisk/armeabi-v7a.so ] ||
	{ say "the Zygisk provider library is not in the installed module"; exit 1; }

grab() { # <json> <key> : first "key":"value" string value
	echo "$1" | grep -o "\"$2\":\"[^\"]*\"" | head -n 1 | cut -d'"' -f4
}
launch() { # <package> : start it fresh, wait for its FLUXTEST line, print the JSON
	am force-stop "$1" >/dev/null 2>&1
	logcat -c
	am start -n "$1/dev.flux.compattest.MainActivity" >/dev/null 2>&1
	t=25
	while [ $t -gt 0 ]; do
		line=$(logcat -d -s FLUXTEST:I 2>/dev/null | grep "\"package\":\"$1\"" | tail -n 1)
		case "$line" in *'"gl_native":{"vendor"'*'"vk_direct"'*) echo "${line#*FLUXTEST: }"; return 0 ;; esac
		# GL arrives from the GL thread a moment after the first report: wait for the line that has it
		sleep 1
		t=$((t - 1))
	done
	echo "${line#*FLUXTEST: }"
}
pid_of() { pidof "$1" | awk '{print $1}'; }

# ---- 1. real identity ------------------------------------------------------------------------------------
REAL_PROP_MODEL=$(getprop ro.product.model)
rm -f "$CFG/game_profiles.json" "$CFG/compat_library.json"
fluxd compat_arm >/dev/null 2>&1
say "1. real identity (no profiles)"
RA=$(launch $PA); echo "$RA" >"$D/real_a.json"
RB=$(launch $PB); echo "$RB" >"$D/real_b.json"
REAL_MODEL=$(grab "$RA" MODEL)
REAL_RENDERER=$(echo "$RA" | grep -o '"gl_native":{[^}]*}' | grep -o '"renderer":"[^"]*"' | cut -d'"' -f4)
say "   Build.MODEL=$REAL_MODEL  gl renderer=$REAL_RENDERER"
[ -n "$REAL_MODEL" ] || { say "the test app produced no output; is it installed and launchable?"; exit 1; }

# ---- 2. test profiles for A and B only ---------------------------------------------------------------------
cat >"$CFG/compat_library.json" <<'EOF'
{ "identities": {
  "fx_dev_a": {"layer":"device","fields":{"MODEL":"FluxTest-A","BRAND":"FluxBrandA","MANUFACTURER":"FluxMakerA"}},
  "fx_cpu_a": {"layer":"cpu","fields":{"SOC_MODEL":"fluxsoc-a","SOC_MANUFACTURER":"FluxSocMakerA"}},
  "fx_gpu_a": {"layer":"gpu","fields":{"gl_vendor":"FluxVendorA","gl_renderer":"FluxTest GPU A","vk_device_name":"FluxTest GPU A","vk_vendor_id":"0xA001","vk_device_id":"0x0A"}},
  "fx_dev_b": {"layer":"device","fields":{"MODEL":"FluxTest-B","BRAND":"FluxBrandB"}},
  "fx_gpu_b": {"layer":"gpu","fields":{"gl_renderer":"FluxTest GPU B","vk_device_name":"FluxTest GPU B"}} } }
EOF
cat >"$CFG/game_profiles.json" <<EOF
{ "$PA": {"package":"$PA","compatibility":{"mode":"advanced","device_profile":"fx_dev_a","cpu_profile":"fx_cpu_a","gpu_profile":"fx_gpu_a"}},
  "$PB": {"package":"$PB","compatibility":{"mode":"advanced","device_profile":"fx_dev_b","gpu_profile":"fx_gpu_b"}} }
EOF
touch "$CFG/compat_zygisk_optin"
ARMED=$(fluxd compat_arm 2>&1 | tail -n 1)
say "2. armed packages: $ARMED  (armed.list: $(tr '\t\n' ' ' </data/adb/modules/flux/armed.list 2>/dev/null))"

# ---- 3. launch A, B and the unconfigured app --------------------------------------------------------------------
JA=$(launch $PA); echo "$JA" >"$D/a.json"; PIDA=$(pid_of $PA)
JB=$(launch $PB); echo "$JB" >"$D/b.json"; PIDB=$(pid_of $PB)
JP=$(launch $PP); echo "$JP" >"$D/plain.json"; PIDP=$(pid_of $PP)

has() { echo "$1" | grep -q "$2"; }
r() { if "$@"; then echo PASS; else echo FAIL; fi; }

# provider loaded in this boot
BOOT=$(cat /proc/sys/kernel/random/boot_id)
LOADED=FAIL
grep -q "\"boot_id\":\"$BOOT\"" $CFG/compat_provider/provider.json 2>/dev/null && grep -q '"loaded":true' $CFG/compat_provider/provider.json && LOADED=PASS

say ""
say "================ RESULTS (test app, not a real game) ================"
res "Provider loaded (this boot)"            "$LOADED"
res "A: Build.MODEL/BRAND/MANUFACTURER"     "$(r has "$JA" '"MODEL":"FluxTest-A"')/$(r has "$JA" '"BRAND":"FluxBrandA"')/$(r has "$JA" '"MANUFACTURER":"FluxMakerA"')"
res "A: native ro.product.model"            "$(r has "$JA" '"ro.product.model":"FluxTest-A"')"
res "A: CPU identity (SOC_MODEL, API 31+)"  "$(if has "$JA" SOC_MODEL; then r has "$JA" '"SOC_MODEL":"fluxsoc-a"'; else echo 'N/A (API<31)'; fi)"
res "A: GL renderer (Java and native)"      "$(r has "$JA" '"gl_java":{[^}]*"renderer":"FluxTest GPU A"')/$(r has "$JA" '"gl_native":{[^}]*"renderer":"FluxTest GPU A"')"
res "A: GL vendor"                           "$(r has "$JA" '"vendor":"FluxVendorA"')"
res "A: Vulkan (direct import route)"       "$(r has "$JA" '"vk_direct":{"deviceName":"FluxTest GPU A","vendorID":40961')"
res "A: Vulkan (vkGetInstanceProcAddr)"     "$(r has "$JA" '"vk_proc_addr":{"deviceName":"FluxTest GPU A"')"
res "B: shows profile B, not A"             "$(r has "$JB" '"MODEL":"FluxTest-B"')/$(if has "$JB" FluxTest-A; then echo FAIL; else echo PASS; fi)"
res "B: GL/Vulkan of profile B"             "$(r has "$JB" '"renderer":"FluxTest GPU B"')/$(r has "$JB" '"vk_direct":{"deviceName":"FluxTest GPU B"')"
res "B: CPU layer off (real SOC)"           "$(if has "$JB" 'fluxsoc'; then echo FAIL; else echo PASS; fi)"
res "Unconfigured app: real Build.MODEL"    "$(r has "$JP" "\"MODEL\":\"$REAL_MODEL\"")"
res "Unconfigured app: no override anywhere" "$(if has "$JP" 'FluxTest'; then echo FAIL; else echo PASS; fi)"
res "Global getprop unchanged"               "$(if [ "$(getprop ro.product.model)" = "$REAL_PROP_MODEL" ]; then echo PASS; else echo FAIL; fi)"
STA=$(cat $CFG/compat_provider/proc/$PIDA.json 2>/dev/null)
STB=$(cat $CFG/compat_provider/proc/$PIDB.json 2>/dev/null)
res "Provider status for A (state)"          "$(grab "$STA" state)"
res "Provider status for B (state)"          "$(grab "$STB" state)"
res "No status file for the unconfigured app" "$([ -f $CFG/compat_provider/proc/$PIDP.json ] && echo FAIL || echo PASS)"
res "A and B carry different transactions"   "$([ -n "$(grab "$STA" transaction_id)" ] && [ "$(grab "$STA" transaction_id)" != "$(grab "$STB" transaction_id)" ] && echo PASS || echo FAIL)"

# ---- 6. rollback: profiles removed -> real again ------------------------------------------------------------------------
rm -f "$CFG/game_profiles.json"
fluxd compat_arm >/dev/null 2>&1
JA2=$(launch $PA); echo "$JA2" >"$D/a_after.json"
res "After removing the profile: A real again" "$(r has "$JA2" "\"MODEL\":\"$REAL_MODEL\"")/$(if has "$JA2" FluxTest; then echo FAIL; else echo PASS; fi)"
res "Armed list empty after removal"           "$([ -s /data/adb/modules/flux/armed.list ] && echo FAIL || echo PASS)"
say ""
say "Not tested here: real-game behaviour, graphics options, FPS. Evidence: $D"
