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
