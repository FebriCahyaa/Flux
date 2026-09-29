#pragma once

// Filesystem contract shared by the daemon (writer of plans) and the provider (reader).
// Kept as plain constants so the Zygisk module needs nothing from the daemon's headers.

#define FLUX_PROVIDER_STATE_DIR "/data/adb/.config/flux/compat_provider"
#define FLUX_PROVIDER_PLANS_DIR FLUX_PROVIDER_STATE_DIR "/plans"       /* <package>.json, written by fluxd */
#define FLUX_PROVIDER_PROC_DIR FLUX_PROVIDER_STATE_DIR "/proc"         /* <pid>.json, written by the companion */
#define FLUX_PROVIDER_INFO_FILE FLUX_PROVIDER_STATE_DIR "/provider.json" /* written when the companion starts */
/* Read by the module in preAppSpecialize through Api::getModuleDir(): "<package>\t<app_id>\t<tx>" lines. */
#define FLUX_PROVIDER_ARMED_NAME "armed.list"
/* While this file exists in the module dir the provider does nothing at all (manual recovery switch). */
#define FLUX_PROVIDER_KILL_SWITCH "no_provider"
#define FLUX_PROVIDER_ARMED_FILE "/data/adb/modules/flux/" FLUX_PROVIDER_ARMED_NAME
#define FLUX_PACKAGES_LIST "/data/system/packages.list"
