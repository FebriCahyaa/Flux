# Flux Tweaks v1.1.0

#### 🚨 Breaking Changes

- **webui:** redesign monitor UI, add about section, GKI/Non-GKI support & Poppins font ([`656c736`](https://github.com/FebriCahyaa/Flux/commit/656c73665ec57206029afbf6481e8ea485585c37))

#### ✨ Features

- **module:** updates from the root manager, release workflow, fixes ([`21ef24d`](https://github.com/FebriCahyaa/Flux/commit/21ef24d4119dec982804fb0a68873b5d34562403))
- **security:** commit the SynthesisCore certificate pin ([`b4e148c`](https://github.com/FebriCahyaa/Flux/commit/b4e148ce5c1f283f53782cb7b91637fc4e1b90c7))
- **security:** verified SynthesisCore sync with pinned signing certificate ([`3b03e55`](https://github.com/FebriCahyaa/Flux/commit/3b03e55165a7307ff6d7a616d87ebf4e40d7f83e))
- **sched:** add Flux Sched, uclamp scheduler prioritisation for games ([`5214c71`](https://github.com/FebriCahyaa/Flux/commit/5214c716bff130979b2f765d66edd3423d078a4d))
- consume SynthesisCore protocol 3 and add thermal level fallback ([`01aea35`](https://github.com/FebriCahyaa/Flux/commit/01aea35dc5e309a139883969175869fe62c29ffc))
- SynthesisCore version handshake and fix missed status updates ([`e165002`](https://github.com/FebriCahyaa/Flux/commit/e165002fe3385a452ceb9ba1c5db01757c72684b))
- **service:** resolve binder transaction codes once at boot ([`3a5105d`](https://github.com/FebriCahyaa/Flux/commit/3a5105d9b2cdf7f2577e772332f0868b2fa8df5f))
- **webui:** redesign monitor UI, add about section, GKI/Non-GKI support & Poppins font ([`656c736`](https://github.com/FebriCahyaa/Flux/commit/656c73665ec57206029afbf6481e8ea485585c37))
- **webui:** add live monitor tab with SynthesisCore dashboard ([`eb8fb73`](https://github.com/FebriCahyaa/Flux/commit/eb8fb73f01f71bc495cecf57cdadc569e05c66f1))
- integrate SynthesisCore new fields (charging, thermal, audio) ([`d6a52a3`](https://github.com/FebriCahyaa/Flux/commit/d6a52a3729a9524f31200a1162e2d7103a8e42d3))

#### 🐛 Bug Fixes

- **sync:** accept "latest" as the tag input ([`a1ff40f`](https://github.com/FebriCahyaa/Flux/commit/a1ff40f03e7438f087c25b30f4b13a723404af59))
- **sync:** do not fail when Actions may not open pull requests ([`9808568`](https://github.com/FebriCahyaa/Flux/commit/98085685f99d0426b1fe20e327fadf3c49a0c18b))
- **sync:** parse the signer digest from apksigner 37 output ([`fad8a1e`](https://github.com/FebriCahyaa/Flux/commit/fad8a1e2a60e467f0b70ec2f629299a424eaff7c))
- **daemon:** resolve random freeze and reboot causes ([`f9c4eba`](https://github.com/FebriCahyaa/Flux/commit/f9c4eba4ccef6564ccfe4e168f92b178ba7fb3e0))
- **service:** exempt SynthesisCore from Powerkeeper and add watchdog ([`24ed5b9`](https://github.com/FebriCahyaa/Flux/commit/24ed5b96ad183b7aeca462d72dbbafc98a1c0824))
- **webui/store:** tighten thermalSupported gate using thermal_api_available from SynthesisCore - Add thermalApiAvailable and kernelIsGki refs to monitor store state - Update thermalSupported computed: now requires both thermalApiAvailable=true   AND thermalHeadroom >= 0, previously only checked thermalHeadroom >= 0 - Add case 'thermal_api_available' and case 'kernel_is_gki' to   parseSynthesisCore() switch to consume new SynthesisCore output fields - Expose thermalApiAvailable and kernelIsGki in store return object - Update parseSynthesisCore() JSDoc to document two new output fields ([`6c2b0d2`](https://github.com/FebriCahyaa/Flux/commit/6c2b0d2b14d30b112d1b6a431d0c0b0ad6aeae45))
- **service:** rotate sysmon.log instead of deleting on each boot ([`5025e41`](https://github.com/FebriCahyaa/Flux/commit/5025e416c330a500107a78f5c89c1a0276f245d6))
- **webui/store:** tighten thermalSupported gate using thermal_api_available from SynthesisCore ([`0687536`](https://github.com/FebriCahyaa/Flux/commit/0687536cea2fb245fbc83c75dadda62dbe35bfe5))
- **webui:** guard NaN thermal value in Monitor store ([`3eb58b5`](https://github.com/FebriCahyaa/Flux/commit/3eb58b5e8c656020ebc420ef5a387118a5dc595c))
- **webui:** sync profile map and i18n with PERFORMANCE_LITE_PROFILE ([`075f4f1`](https://github.com/FebriCahyaa/Flux/commit/075f4f11d786243a3273b8b17c379c805bbcf32d))
- **service:** pass synthesis_core.json path to SynthesisCore daemon ([`35744e7`](https://github.com/FebriCahyaa/Flux/commit/35744e710ef718a17da8a5eca65984df6bada671))
- **webui:** fix logo background, profile status, and expose CPU Governor menu ([`9bb04e8`](https://github.com/FebriCahyaa/Flux/commit/9bb04e8c3391628d602e0cdc624d54e4c365a55c))
- remove old Vuetify components, use views ([`ebe3947`](https://github.com/FebriCahyaa/Flux/commit/ebe394746f286637b56e538f91329f065293c2c6))
- **profiler:** restore system status cache integration ([`cdec9a3`](https://github.com/FebriCahyaa/Flux/commit/cdec9a3876ff910e4fd3c2ec338d05d385dfcfa4))
- **profiler:** restore system status cache integration ([`59e1c70`](https://github.com/FebriCahyaa/Flux/commit/59e1c7066a8f0b1c818f90c6e36051eed43f10e4))
- **synthesiscore:** add missing SYNTHESIS_CORE_FILE definition ([`45d9beb`](https://github.com/FebriCahyaa/Flux/commit/45d9bebad4dfb3c7d728fe590c7cd6fa8e05b868))

#### 📚 Documentation

- **prebuilt:** correct the FLUX_DISPATCH_TOKEN permission ([`a69dfaa`](https://github.com/FebriCahyaa/Flux/commit/a69dfaa54d9b71dc9d3a6fcf9c0fee751be87e79))
- write the Flux README ([`50269e9`](https://github.com/FebriCahyaa/Flux/commit/50269e91fe8b257b5efe61176afaff08e64add5f))

#### 📦 Build & Dependencies

- add external dependencies as submodules ([`79e1ec0`](https://github.com/FebriCahyaa/Flux/commit/79e1ec040360d716480160312b39f4c062ea4113))

#### 🔧 CI

- **telegram:** redesign build notifications and fix delivery ([`cf45417`](https://github.com/FebriCahyaa/Flux/commit/cf454177ec5be0dc72824c20eb4fd19439f70cda))

#### 🧹 Maintenance

- **prebuilt:** sync SynthesisCore v2.1.0 (#2) ([`01d3c08`](https://github.com/FebriCahyaa/Flux/commit/01d3c080e8db88e78a1efd2cd3d1128571e4243f))
- **prebuilt:** update synthesiscore.apk ([`c95669f`](https://github.com/FebriCahyaa/Flux/commit/c95669f70bc6269df408b55a6b9b6292145a65fd))

#### 📝 Other Changes

- SynthesisCore Update Sync new system ([`df1434c`](https://github.com/FebriCahyaa/Flux/commit/df1434c777ab92a92053525719fca1d1109a3937))
- SynthesisCore new ([`83cc55a`](https://github.com/FebriCahyaa/Flux/commit/83cc55a8ef4e06820ecdb17f7e8b1c6ae4fc2218))
- Fetch new synthesiscore ([`2991000`](https://github.com/FebriCahyaa/Flux/commit/299100090312ec52390d4f2ffe3affad2f39a1cb))
- fetch synthesiscore update. ([`4155c62`](https://github.com/FebriCahyaa/Flux/commit/4155c62dfb6f195811c61818df89353da74b4b8c))
- **webui:** re-export logo images with transparent background ([`a750723`](https://github.com/FebriCahyaa/Flux/commit/a750723630616b01548ed336543fa34bdf3fd82a))
- Fix Build. ([`6b939b9`](https://github.com/FebriCahyaa/Flux/commit/6b939b930744edae52494bf4312b6bcf0a275198))
- include 'SynthesisCore' ([`af7382e`](https://github.com/FebriCahyaa/Flux/commit/af7382e1713f10cde424f05ab99932feadb076f3))
- Fix Typo. ([`b036036`](https://github.com/FebriCahyaa/Flux/commit/b036036312604b57211a9a77db4fc9935eba2de0))
- Fix Error system_monitor --> synthesis_core ([`2e299c8`](https://github.com/FebriCahyaa/Flux/commit/2e299c8f00f5bdb110c594ec3be301cd9231d78e))
- initial Flux Tweaks ([`bd1595f`](https://github.com/FebriCahyaa/Flux/commit/bd1595f82216a1dca611dc97987db6cd173ba465))

