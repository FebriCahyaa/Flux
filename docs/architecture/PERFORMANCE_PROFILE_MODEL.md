# Performance Profile Model

Step 3 of `GAME_RUNTIME_MIGRATION_PLAN.md` (branch `integration/game-runtime-clean`).
Code: `jni/perf/ProfileModel.{hpp,cpp}` (`flux::perf`) · Tests: `tests/profile_model_test.cpp`.
Status: **Step 3 IN PROGRESS** — implemented and host-tested; linked into `fluxd` (`FluxPerf`);
not read by the daemon yet (no file loading or call path); not device-tested.

| IMPLEMENTED | NOT_IMPLEMENTED |
|---|---|
| layer model, resolver, partial override | loading a profile file on the device |
| validation (unknown profile, missing parent, cycle, invalid field/value) | daemon call path (Session, Step 5) |
| explanation output | WebUI editing of profiles |
| legacy format readers | device validation |

## Scope

Performance fields only: `profile`, `memory`, `touch`, `storage`, `refresh`, `refresh_custom_hz`,
`launch_boost`. No compatibility, device/CPU/GPU identity, Resolver or Zygisk data is modelled.

## Inheritance

```
BUILTIN defaults   profile=performance, memory/touch/storage=default, refresh=real, launch_boost=false
   |
GLOBAL             document "global" (cannot extend)
   |
PRESET chain       game.extends -> preset -> its extends ... applied root first
   |
GAME               document "games"["<package>"]
   |
RUNTIME            transient override (cannot extend)
```

Every field is optional in every layer; a layer replaces only what it sets (partial override).
`default` / `real` mean "leave the existing Flux profile alone" (see `PERFORMANCE_PLANNER.md`).

## Document format (current, v1)

```json
{
  "version": 1,
  "global":  { "memory": "balanced" },
  "presets": {
    "gaming":      { "memory": "gaming", "storage": "gaming", "launch_boost": true },
    "competitive": { "extends": "gaming", "touch": "competitive", "refresh": "hz120" }
  },
  "games": { "com.example.game": { "extends": "competitive", "touch": "responsive" } }
}
```

| Field | Values |
|---|---|
| profile | performance, performance_lite, balance, powersave |
| memory | default, balanced, gaming, gaming_plus |
| touch | default, balanced, responsive, responsive_plus, competitive |
| storage | default, balanced, gaming |
| refresh | real, adaptive, hz60, hz90, hz120, hz144, custom (needs `refresh_custom_hz` 30–240) |
| launch_boost | boolean |
| extends | preset name, 1–64 chars (presets and games only) |

## Validation

| Case | Result |
|---|---|
| Invalid JSON, non-object, `version` ≠ 1 | document rejected |
| Unknown key | `invalid field 'x' in <entry>`; entry rejected, others load |
| Bad value / type | `invalid value 'v' for <field>` / `<field> must be a boolean`; entry rejected |
| `refresh: custom` without a valid Hz | entry rejected |
| Game extends an undefined preset | `unknown profile 'x'` (resolution continues without it) |
| Preset extends an undefined preset | `missing parent 'x' of preset 'y'` |
| Cycle (incl. self) | `inheritance cycle: a -> b -> a` |

Resolution never fails hard: the chain stops at the break, lower layers and the game layer still
apply, and `errors` reports why.

## Explanation output

`ResolvedProfile::explain()`:
```
memory:
  source=preset gaming
touch:
  source=game override
refresh:
  source=preset competitive
profile:
  source=global
```
Sources: `builtin default`, `global`, `preset <name>`, `game override`, `runtime override`.
`chain` lists applied layers lowest first.

## Backward compatibility

| Existing format | Detection | Handling |
|---|---|---|
| Old Game Runtime `game_profiles.json` `{ "<pkg>": { "extends", "performance": {...}, "compatibility": {...} } }` | no `version`, no `presets`/`identities` | `performance` fields read; `compatibility` → warning *ignored (identity data is not supported)*; `package` ignored |
| Old `compat_library.json` `{ "presets": {...}, "identities": {...} }` | `presets` or `identities`, no `version` | presets read (nested `performance` accepted); `identities` → warning, ignored |
| Legacy `touch: "custom"` | value | warning, field ignored |
| `main` `gamelist.json` `lite_mode` | `layer_from_gamelist()` | `lite_mode=true` → `profile=performance_lite`; `false` → nothing |

`merge_document()` combines a library of presets with a games document without overwriting.

## Tests (written first; red → green)

inheritance order (grandparent → parent → game, global, builtin, unknown package), explanation
text, override priority (runtime > game > preset > global), missing parent, unknown profile, cycle
and self-cycle, invalid field/value/type/custom Hz, rejected entries not loaded, invalid JSON and
version, legacy game_profiles (compatibility ignored), legacy library (identities ignored),
gamelist lite_mode.
