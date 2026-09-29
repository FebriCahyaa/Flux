# Flux compat test app

A tiny app that prints what an app process can read about the device, so the Zygisk provider can be
checked with something other than a real game.

**Status: source only. It has not been built or run** (no Android SDK/NDK in the environment that wrote it).
Build it with Android Studio or `gradle :app:assembleDebug`; nothing in Flux's CI builds it.

It reports, once at start-up and on every "Refresh" tap:

* `android.os.Build`: BRAND, MANUFACTURER, MODEL, DEVICE, PRODUCT, FINGERPRINT, and (API 31+) SOC_MODEL, SOC_MANUFACTURER, HARDWARE, BOARD
* native property reads: `__system_property_get("ro.product.model")`, `ro.soc.model`, ...
* OpenGL ES (`GLES20.glGetString` from Java and `glGetString` from native): vendor, renderer, version; EGL vendor
* Vulkan: `deviceName`, `vendorID`, `deviceID`, `apiVersion`, `driverVersion`, read both through the
  directly imported `vkGetPhysicalDeviceProperties` and through `vkGetInstanceProcAddr`
* CPU: `/proc/cpuinfo` "Hardware" line and `Build.SOC_MODEL` (informational; the provider does not touch `/proc/cpuinfo`)

The same JSON is written to logcat (tag `FLUXTEST`, one line) and to `files/identity.json`.

Three flavors give three packages with separate processes, for the isolation test:

| flavor | package | role |
| --- | --- | --- |
| `appA` | `dev.flux.compattest.a` | gets profile A |
| `appB` | `dev.flux.compattest.b` | gets profile B |
| `plain` | `dev.flux.compattest.plain` | no profile: must show the real device |

`scripts/flux_provider_validate.sh` drives the whole comparison on a rooted device.
