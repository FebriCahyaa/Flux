# Kernel Capability Model (Step 7)

One `flux::kernel::Capability` per concrete interface. This is the kernel-side record; the graphics
capability schema v4 remains owned by SynthesisCore (D-11) and is not changed.

| Field | Meaning |
|---|---|
| `id` | stable dotted id, glob captures substituted (`cpufreq.policy4.scaling_max_freq`, `io.sda.scheduler`) |
| `domain` | one of 15 domains (`to_string`: cpufreq, cpu_policy, governor, uclamp, scheduler, cpuset, cgroup, devfreq, gpu, thermal, zram, swap, io_scheduler, input_boost, display_refresh) |
| `supported` | the interface exists (any kind) |
| `readable` | content read **and** valid for its value kind |
| `writable` | permission hint (`access(W_OK)`); always false for observe-only and `Info` nodes |
| `verified` | always false in Step 7 — needs a transactional write + read-back |
| `interface` | path relative to the root |
| `source` | adapter that declared it (`generic`, `qualcomm`, `mediatek`, registered vendor) |
| `value` | trimmed value; for selectors the bracketed item |
| `range` | selector list, one list sibling (`scaling_available_governors`), or `min..max` from two siblings |
| `confidence` | High = read & valid; Medium = absent or unreadable; Low = present but invalid |
| `risk` | Low / Medium / High (thermal = High) |
| `rollback` | a later write could be undone by writing `value` back (false for observe-only, zram size/algorithm) |
| `requires_adapter` | declared by a vendor adapter rather than generic |
| `note` | reason for absence/invalidity, or "writable by permission only; not verified" |

Value kinds: `Integer`, `Text` (single line), `Selector` (`a [b] c`), `Info` (multi-line, never a
write target). Reads are capped at 64 KiB; values over 4 KiB are invalid.

States a consumer must distinguish:

| supported | readable | Meaning |
|---|---|---|
| false | false | interface absent → adapter or kernel lacks it |
| true | false | present but unreadable or invalid → see `note`; never guess a value |
| true | true | observed value; `writable` is still only a hint |

## Shared capability context (Step 7.5)

`jni/context/CapabilityContext.*` (`flux::context`, NDK `FluxContext`, host `flux_context`) is where
capability facts are published and read. Producers publish; consumers resolve. The context itself does
not probe, write, or infer anything.

| Role | Who |
|---|---|
| Publisher | Kernel Intelligence (`kernel`), today. Future graphics and Synrei thermal producers use the same API |
| Consumer | Performance Planner (`PerfCapabilities::context`, carried only — no decision reads it yet). Future Graphics Engine and Synrei Thermal Engine |

`CapabilityFact` carries these fields unchanged: `source` (what determined it, e.g. the adapter),
`publisher` (set by the context), `support` (Yes / No / **Unknown**), `readable`, `writable`,
`verified`, `confidence`, `risk` (including Unknown), `rollback`, `requires_adapter`, `interface`,
`value`, `range`, `note`.

Rules:
- **Unknown stays Unknown.** An id nobody published, or a fact that states Unknown, resolves to
  Unknown with no deciding fact. Nothing is inferred from a neighbour, a domain, or a vendor.
- **Publishing is a snapshot.** Publishing replaces every fact that publisher gave before.
- **Conflicts.** The answer comes from the highest-confidence fact that states Yes or No. If equally
  confident publishers disagree, the result is Unknown and `conflict=true`. If a weaker publisher
  disagrees, the stronger fact answers and `conflict=true` stays visible. Fields are never merged, so a
  `writable=true` from a weaker source cannot leak into the answer.
- **Kernel mapping** (`flux::kernel::export_facts`):
  - `supported=false` becomes No, keeping the prober's confidence (Medium for an observed absence).
  - A present but invalid node becomes Yes with `readable=false` and Low confidence.
  - `kernel.integration` and `kernel.generation` are Unknown when the classifier says Unknown.
  - `kernel.adapter` names the selected adapter.
- **Observatory:** interface only. `set_observer(ContextNotice)` reports generation, publisher, and
  counts for supported, unsupported, unknown and conflicts after each publish. Observer exceptions are
  swallowed. Nothing is wired to the Observatory bridge, and no kernel event type or storage exists yet.
