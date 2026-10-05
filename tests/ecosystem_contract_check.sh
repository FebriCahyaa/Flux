#!/bin/sh
# Phase 4.5D cross-repository contract check (read-only, deterministic).
# Verifies that Zairenkai (Flux), Synrei Thermal Intelligence (HiCo) and Zairenkai Intelligence
# (SynthesisCore) still agree on every technical contract after the public brand migrations.
# Usage: ecosystem_contract_check.sh <flux root> <hico root> <synthesiscore root>
F=${1:?flux root}; H=${2:?hico root}; S=${3:?synthesiscore root}
fail=0; n=0
bad() { echo "FAIL: $*"; fail=1; }
has() { n=$((n + 1)); grep -qF -- "$2" "$1" || bad "$1 lacks: $2"; }
hasnt() { n=$((n + 1)); if grep -qF -- "$2" "$1"; then bad "$1 exposes: $2"; fi; }
SC_SRC=$S/app/src/main/java/com/febricahyaa/synthesiscore

# -- Zairenkai -> Synrei: state interface, module, config, commands --------------------------------
has "$F/jni/thermal/SynreiThermalAdapter.hpp" 'kSynreiStatePath = "dev/hico/state"'
has "$H/jni/include/HiCo.hpp" '#define HICO_RUNTIME_DIR "/dev/hico"'
has "$H/jni/include/HiCo.hpp" '#define HICO_STATE_FILE HICO_RUNTIME_DIR "/state"'
has "$H/jni/include/HiCo.hpp" '#define HICO_CONFIG_DIR "/data/adb/.config/hico"'
has "$H/jni/include/HiCo.hpp" '#define HICO_MODULE_DIR "/data/adb/modules/hico"'
has "$H/module/module.prop" 'id=hico'
for key in $(grep -oE 'kv\.find\("[a-z_]+"\)|temp\(kv, "[a-z_]+"\)' "$F/jni/thermal/SynreiThermalAdapter.cpp" | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u); do
	has "$H/jni/src/Daemon.cpp" "kv(\"$key\""
done
for st in idle boost relaxed safety suspended disabled; do has "$H/jni/src/Daemon.cpp" "return \"$st\";"; done
for m in module.prop disable remove; do has "$F/scripts/flux_profiler.sh" "/data/adb/modules/hico/$m"; done
has "$F/scripts/flux_profiler.sh" '/data/adb/.config/hico'
has "$F/module/uninstall.sh" 'HICOD=/data/adb/modules/hico/system/bin/hicod'
has "$F/module/uninstall.sh" '"$HICOD" restore'
for c in status zones device; do
	n=$((n + 1)); grep -qE "hicod\"? $c" "$F/scripts/flux_utility.sh" || bad "flux_utility.sh does not call hicod $c"
done
has "$H/jni/src/main.cpp" 'if (cmd == "restore" || cmd == "stop") return cmd_restore();'
has "$H/jni/src/main.cpp" 'if (cmd == "status") return cmd_status('
has "$H/jni/src/main.cpp" 'if (cmd == "zones") return cmd_zones();'
has "$H/jni/src/main.cpp" 'if (cmd == "device") return cmd_device('
n=$((n + 1)); [ "$(grep -c 'add_executable(hicod' "$H/CMakeLists.txt")" = 1 ] || bad "hicod is not the single thermal daemon"

# -- Synrei -> Zairenkai: what hicod reads from fluxd ---------------------------------------------
has "$H/jni/include/HiCo.hpp" '#define FLUX_MODULE_DIR "/data/adb/modules/flux"'
has "$H/jni/include/HiCo.hpp" '#define FLUX_BINARY FLUX_MODULE_DIR "/system/bin/fluxd"'
has "$H/jni/include/HiCo.hpp" '#define FLUX_CONFIG_DIR "/data/adb/.config/flux"'
has "$F/jni/include/Flux.hpp" '#define CONFIG_DIR "/data/adb/.config/flux"'
has "$F/jni/include/Flux.hpp" '#define MODPATH "/data/adb/modules/flux"'
has "$F/jni/include/Flux.hpp" '#define LOCK_FILE CONFIG_DIR "/.lock"'
has "$F/jni/include/Flux.hpp" '#define PROFILE_MODE CONFIG_DIR "/current_profile"'
has "$F/jni/include/Flux.hpp" '#define GAME_INFO CONFIG_DIR "/gameinfo"'
has "$F/jni/include/Flux.hpp" '#define SYNTHESIS_CORE_FILE CONFIG_DIR "/synthesis_core.json"'
has "$H/jni/include/HiCo.hpp" '#define FLUX_STATUS_FILE FLUX_CONFIG_DIR "/synthesis_core.json"'
has "$F/module/module.prop" 'id=flux'
has "$F/jni/Android.mk" 'LOCAL_MODULE := fluxd'

# -- Zairenkai -> Zairenkai Intelligence: package, entry, protocol fields ------------------------
has "$S/app/build.gradle.kts" 'applicationId = "com.febricahyaa.synthesiscore"'
has "$F/module/service.sh" 'com.febricahyaa.synthesiscore.MainKt'
has "$SC_SRC/core/Protocol.kt" 'const val VERSION = 3'
for mode in --version --resolve --once --capabilities; do has "$SC_SRC/MainKt.kt" "\"$mode\""; done
for key in $(grep -oE 'sscanf\(line, "[a-z_]+' "$F/jni/base/SynthesisCore/SynthesisCore.cpp" | sed 's/.*"//' | sort -u); do
	has "$SC_SRC/core/Protocol.kt" "= \"$key\""
done

# -- Zairenkai Intelligence -> Zairenkai: release assets, sync event, signing pin ---------------
has "$S/.github/workflows/release.yml" 'echo "APK_NAME=SynthesisCore-${TAG}.apk"'
has "$F/.github/scripts/sync_synthesiscore.sh" 'asset="SynthesisCore-$tag.apk"'
has "$S/.github/workflows/release.yml" '-f event_type=synthesiscore-release'
has "$F/.github/workflows/sync_synthesiscore.yml" 'types: [synthesiscore-release]'
has "$S/.github/workflows/release.yml" "FLUX_REPOSITORY: \${{ vars.FLUX_REPOSITORY || 'FebriCahyaa/Flux' }}"
pin=$(tr -d ' \n' <"$F/prebuilt/synthesiscore.cert.sha256")
has "$F/prebuilt/synthesiscore.json" "\"certificate_sha256\": \"$pin\""
n=$((n + 1)); sum=$(sha256sum "$F/prebuilt/synthesiscore.apk" | cut -c1-64)
grep -qF "$sum" "$F/prebuilt/synthesiscore.apk.sha256" || bad "prebuilt APK checksum mismatch"
has "$F/prebuilt/synthesiscore.json" "\"sha256\": \"$sum\""

# -- Update channels ------------------------------------------------------------------------------
has "$F/module/module.prop" 'updateJson=https://raw.githubusercontent.com/FebriCahyaa/Flux/main/update.json'
has "$H/module/module.prop" 'updateJson=https://raw.githubusercontent.com/FebriCahyaa/HiCo-Release/main/update.json'

# -- Public identity and Aeyrin --------------------------------------------------------------------
has "$F/module/module.prop" 'name=Zairenkai'
has "$H/module/module.prop" 'name=Synrei Thermal Intelligence'
has "$S/app/src/main/res/values/strings.xml" '>Zairenkai Intelligence<'
has "$F/README.md" '# Zairenkai'
has "$S/README.md" '# Zairenkai Intelligence'
for f in "$F/README.md" "$F/module/module.prop" "$F/jni/FluxCLI.cpp" "$F/jni/brand/Brand.cpp" "$F/webui/index.html" \
	"$H/module/module.prop" "$H/jni/src/main.cpp" "$H/webui/index.html" "$H/webui/src/locales/en.json" "$H/.github/workflows/release.yml" \
	"$S/README.md" "$S/app/src/main/res/values/strings.xml" "$SC_SRC/MainKt.kt" "$S/.github/workflows/release.yml"; do
	hasnt "$f" Aeyrin
done

echo "ecosystem_contract_check: $n checks, $([ $fail -eq 0 ] && echo passed || echo FAILED)"
exit $fail
