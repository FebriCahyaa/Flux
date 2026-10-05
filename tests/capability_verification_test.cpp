// Capability verification foundation (Step 8.14): gating, explicit adapters, transactional
// write / read-back / restore / read-back, failure semantics, thermal protection, path safety,
// metadata preservation, Transaction Engine semantics, determinism, isolation.
#include "flux_test.hpp"
#include "CapabilityVerification.hpp"
#include "DecisionEngine.hpp"
#include "Event.hpp"

#include <set>

namespace ctx = flux::context;
namespace k = flux::kernel;
namespace rt = flux::runtime;

namespace {

/// In-memory nodes with failure injection; counts every write.
struct FakeNodes {
    std::map<std::string, std::string> nodes;
    std::set<std::string> reject_writes;      // every write to these paths fails, nothing stored
    std::set<std::string> sticky;             // writes "succeed" but the value does not change
    std::set<std::string> reject_second;      // first write ok, later writes fail (restore rejected)
    std::set<std::string> corrupt_second;     // later writes store a different value (restore mismatch)
    std::map<std::string, int> writes;
    std::vector<std::string> written;
    rt::Io io() {
        rt::Io io;
        io.exists = [this](const std::string &p) { return nodes.count(p) > 0; };
        io.read = [this](const std::string &p) -> std::optional<std::string> {
            auto it = nodes.find(p);
            if (it == nodes.end()) return std::nullopt;
            return it->second + "\n";
        };
        io.write = [this](const std::string &p, const std::string &v) {
            const int n = ++writes[p];
            written.push_back(p);
            if (!nodes.count(p) || reject_writes.count(p)) return false;
            if (n > 1 && reject_second.count(p)) return false;
            if (sticky.count(p)) return true;
            nodes[p] = (n > 1 && corrupt_second.count(p)) ? "999" : v;
            return true;
        };
        return io;
    }
    int total_writes() const {
        int t = 0;
        for (auto &[_, n] : writes) t += n;
        return t;
    }
};

const std::string P = "sys/devices/system/cpu/cpufreq/policy4/";
const std::string MAX = "/" + P + "scaling_max_freq";

FakeNodes cpu_nodes() {
    FakeNodes f;
    f.nodes[MAX] = "2419200";
    f.nodes["/" + P + "scaling_min_freq"] = "710400";
    f.nodes["/" + P + "scaling_available_frequencies"] = "710400 1056000 1804800 2419200";
    f.nodes["/sys/class/kgsl/kgsl-3d0/max_gpuclk"] = "587000000";
    f.nodes["/sys/class/kgsl/kgsl-3d0/gpu_available_frequencies"] = "587000000 525000000 441600000 305000000";
    f.nodes["/sys/block/sda/queue/read_ahead_kb"] = "128";
    f.nodes["/proc/sys/vm/swappiness"] = "60";
    f.nodes["/sys/class/devfreq/soc:qcom,gpubw/max_freq"] = "7980";
    f.nodes["/sys/class/devfreq/soc:qcom,gpubw/min_freq"] = "762";
    f.nodes["/sys/class/devfreq/soc:qcom,gpubw/available_frequencies"] = "762 1720 2086 7980";
    f.nodes["/sys/class/thermal/thermal_zone0/mode"] = "enabled";
    f.nodes["/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"] = "schedutil";
    return f;
}

ctx::CapabilityFact fact(const std::string &id, const std::string &domain, const std::string &interface,
                         ctx::Risk risk = ctx::Risk::Medium) {
    ctx::CapabilityFact f;
    f.id = id;
    f.domain = domain;
    f.source = "generic";
    f.publisher = "kernel";
    f.support = ctx::Support::Yes;
    f.readable = true;
    f.writable = true;
    f.verified = false;
    f.rollback = true;
    f.risk = risk;
    f.confidence = ctx::Confidence::High;
    f.interface = interface;
    f.value = "x";
    f.range = "r";
    f.requires_adapter = false;
    f.note = "n";
    return f;
}

ctx::CapabilityFact cpu_max() { return fact("cpufreq.policy4.scaling_max_freq", "cpufreq", P + "scaling_max_freq"); }

k::CapabilityVerifier verifier(FakeNodes &n, std::vector<std::string> *journal = nullptr,
                               std::function<std::optional<std::string>()> pre = nullptr) {
    rt::Transaction::JournalSink sink = nullptr;
    if (journal) sink = [journal](const std::string &text) {
        journal->push_back(text);
        return true;
    };
    return k::CapabilityVerifier(n.io(), k::builtin_verifiers(), sink, nullptr, std::move(pre));
}

void refused(k::CapabilityVerifier &v, const ctx::CapabilityFact &f, const std::string &reason, FakeNodes &n) {
    const int before = n.total_writes();
    auto r = v.verify(f);
    CHECK(!r.attempted && !r.verified);
    CHECK_EQ(r.reason, reason);
    CHECK_EQ(n.total_writes(), before);
}

void test_gating() {
    auto n = cpu_nodes();
    auto v = verifier(n);
    auto f = cpu_max();
    f.support = ctx::Support::No;
    refused(v, f, "unsupported", n); // 1
    f = cpu_max();
    f.id += ".b";
    f.readable = false;
    refused(v, f, "unreadable", n); // 2
    f = cpu_max();
    f.id += ".c";
    f.writable = false;
    refused(v, f, "not_writable", n); // 3
    f = cpu_max();
    f.id += ".d";
    f.risk = ctx::Risk::Unknown;
    refused(v, f, "unknown_risk", n); // 4
    f = cpu_max();
    f.id += ".e";
    f.risk = ctx::Risk::High;
    refused(v, f, "high_risk", n);
    f = cpu_max();
    f.id += ".f";
    f.rollback = false;
    refused(v, f, "no_rollback", n);
    // 5. writable, safe, but no explicit verifier (governor switch is not exactly reversible).
    refused(v, fact("governor.policy0", "governor", "sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
            "no_safe_verifier", n);
    // Unreadable at verification time (node vanished).
    auto gone = fact("cpufreq.policy9.scaling_max_freq", "cpufreq", "sys/devices/system/cpu/cpufreq/policy9/scaling_max_freq");
    refused(v, gone, "unreadable", n);
}

void test_success_and_preservation() {
    auto n = cpu_nodes();
    std::vector<std::string> journal;
    auto v = verifier(n, &journal);
    auto r = v.verify(cpu_max()); // 6.
    CHECK(r.attempted && r.verified && r.restored);
    CHECK_EQ(r.reason, std::string("verified"));
    CHECK_EQ(r.verifier, std::string("cpufreq_scaling_max_freq"));
    CHECK_EQ(r.before_value, std::string("2419200"));
    CHECK_EQ(r.test_value, std::string("1804800")); // next lower available frequency (safe direction)
    CHECK_EQ(r.readback_value, std::string("1804800"));
    CHECK_EQ(r.final_value, std::string("2419200"));
    CHECK(r.confidence == ctx::Confidence::High && r.risk == ctx::Risk::Medium);
    CHECK_EQ(n.nodes[MAX], std::string("2419200")); // 12. original preserved
    CHECK_EQ(n.writes[MAX], 2);                    // test write + restore write, nothing else
    for (auto &p : n.written) CHECK_EQ(p, MAX);

    // 19. Transaction Engine semantics: write-ahead journal with the original, empty after restore.
    CHECK(journal.size() >= 2);
    auto first = rt::journal::parse(journal.front());
    CHECK(first.version == 1 && first.domain == "capability_verification" &&
          first.subject == "cpufreq.policy4.scaling_max_freq");
    CHECK(first.entries.size() == 1 && first.entries[0] == rt::journal::encode_entry(MAX, "2419200"));
    CHECK(rt::journal::parse(journal.back()).entries.empty());

    // Other adapters: KGSL, devfreq, read-ahead, swappiness.
    auto g = v.verify(fact("gpu.kgsl.max_gpuclk", "gpu", "sys/class/kgsl/kgsl-3d0/max_gpuclk"));
    CHECK(g.verified && g.test_value == "525000000");
    auto d = v.verify(fact("gpu.devfreq.soc:qcom,gpubw.max_freq", "gpu", "sys/class/devfreq/soc:qcom,gpubw/max_freq"));
    CHECK(d.verified && d.test_value == "2086");
    auto ra = v.verify(fact("io.sda.read_ahead_kb", "io_scheduler", "sys/block/sda/queue/read_ahead_kb", ctx::Risk::Low));
    CHECK(ra.verified && ra.test_value == "64" && n.nodes["/sys/block/sda/queue/read_ahead_kb"] == "128");
    auto sw = v.verify(fact("swap.swappiness", "swap", "proc/sys/vm/swappiness", ctx::Risk::Low));
    CHECK(sw.verified && sw.test_value == "59" && n.nodes["/proc/sys/vm/swappiness"] == "60");

    // 18. metadata preserved; 13. only the verified id changes.
    ctx::CapabilityContext c;
    auto other = fact("governor.policy0", "governor", "sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    c.publish("kernel", {cpu_max(), other});
    c.publish("graphics", {fact("graphics.x", "graphics", "")});
    CHECK_EQ(k::apply_verification(c, {r}), size_t(1));
    auto *after = c.resolve("cpufreq.policy4.scaling_max_freq").fact;
    CHECK(after && after->verified && after->writable && after->readable && after->support == ctx::Support::Yes);
    CHECK(after && after->interface == P + "scaling_max_freq" && after->source == "generic" && after->value == "x" &&
          after->range == "r" && after->risk == ctx::Risk::Medium && after->rollback &&
          after->confidence == ctx::Confidence::High && after->domain == "cpufreq");
    CHECK(!c.resolve("governor.policy0").fact->verified);
    CHECK(c.resolve("graphics.x").fact != nullptr); // other publishers untouched
    // Decision Engine compatibility: the verified control is now actionable, nothing else changed.
    CHECK(flux::policy::gate(&c, "cpu").verdict == flux::policy::CapabilityGate::Verdict::Actionable);
}

void test_failures() {
    { // 8. write rejected
        auto n = cpu_nodes();
        n.reject_writes.insert(MAX);
        auto v = verifier(n);
        auto r = v.verify(cpu_max());
        CHECK(r.attempted && !r.verified);
        CHECK_EQ(r.reason, std::string("write_rejected"));
        CHECK(r.restored && r.final_value == "2419200");
    }
    { // 7. write accepted, read-back differs
        auto n = cpu_nodes();
        n.sticky.insert(MAX);
        auto v = verifier(n);
        auto r = v.verify(cpu_max());
        CHECK(!r.verified && r.reason == "readback_mismatch");
        CHECK_EQ(r.readback_value, std::string("2419200"));
        CHECK(r.restored);
    }
    { // 9. restore write rejected
        auto n = cpu_nodes();
        n.reject_second.insert(MAX);
        std::vector<std::string> journal;
        auto v = verifier(n, &journal);
        auto r = v.verify(cpu_max());
        CHECK(!r.verified && !r.restored);
        CHECK_EQ(r.reason, std::string("restore_write_rejected"));
        CHECK_EQ(r.final_value, std::string("1804800"));
        CHECK(!rt::journal::parse(journal.back()).entries.empty()); // kept for recovery
    }
    { // 10. restore read-back differs
        auto n = cpu_nodes();
        n.corrupt_second.insert(MAX);
        auto v = verifier(n);
        auto r = v.verify(cpu_max());
        CHECK(!r.verified && !r.restored);
        CHECK_EQ(r.reason, std::string("restore_readback_mismatch"));
        CHECK_EQ(r.final_value, std::string("999"));
    }
    { // 11. no safe test value: already at the lowest available frequency
        auto n = cpu_nodes();
        n.nodes[MAX] = "710400";
        auto v = verifier(n);
        refused(v, cpu_max(), "no_safe_test_value", n);
        auto n2 = cpu_nodes();
        n2.nodes.erase("/" + P + "scaling_available_frequencies"); // no list: no guessing
        auto v2 = verifier(n2);
        refused(v2, cpu_max(), "no_safe_test_value", n2);
        auto n3 = cpu_nodes();
        n3.nodes[MAX] = "fast";
        auto v3 = verifier(n3);
        refused(v3, cpu_max(), "invalid_current_value", n3);
    }
    { // journal cannot be written: nothing applied
        auto n = cpu_nodes();
        auto v = k::CapabilityVerifier(n.io(), k::builtin_verifiers(), [](const std::string &) { return false; });
        auto r = v.verify(cpu_max());
        CHECK(!r.verified && r.reason == "journal_failed" && n.writes[MAX] == 0);
    }
    { // precondition (Synrei active): refused before any I/O
        auto n = cpu_nodes();
        auto v = verifier(n, nullptr, [] { return std::optional<std::string>("Synrei is managing thermal state (boost)"); });
        auto r = v.verify(cpu_max());
        CHECK(!r.attempted && r.reason.find("precondition") == 0 && n.total_writes() == 0);
    }
}

void test_repeat_thermal_paths() {
    auto n = cpu_nodes();
    n.reject_writes.insert(MAX);
    auto v = verifier(n);
    auto first = v.verify(cpu_max());
    const int writes = n.total_writes();
    for (int i = 0; i < 5; ++i) { // 15. no hammering of a failed interface
        auto again = v.verify(cpu_max());
        CHECK(!again.verified && again.reason == first.reason);
    }
    CHECK_EQ(n.total_writes(), writes);
    CHECK_EQ(v.results().size(), size_t(1));

    // 16. thermal capabilities are never written, whatever their metadata says.
    auto n2 = cpu_nodes();
    auto v2 = verifier(n2);
    refused(v2, fact("thermal.thermal_zone0.mode", "thermal", "sys/class/thermal/thermal_zone0/mode", ctx::Risk::Low),
            "thermal_protected", n2);
    refused(v2, fact("x.cooling", "cpufreq", "sys/class/thermal/cooling_device0/cur_state"), "thermal_protected", n2);
    refused(v2, fact("y.cap", "devfreq", "sys/devices/virtual/thermal/thermal_message/cpu_limits"), "thermal_protected", n2);

    // 17. arbitrary / unsafe interfaces are never accepted, even with a matching-looking id.
    for (auto bad : {std::string("sys/devices/system/cpu/cpufreq/policy4/../../../../proc/sys/kernel/x"),
                     std::string("/sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq"), // absolute input
                     std::string("data/local/tmp/scaling_max_freq"), std::string(""),
                     std::string("sys/devices/system/cpu/cpufreq/policy4//scaling_max_freq")}) {
        auto f = cpu_max();
        f.id = "bad." + std::to_string(bad.size());
        f.interface = bad;
        auto r = v2.verify(f);
        CHECK(!r.attempted && !r.verified);
        CHECK(r.reason == "unsafe_interface" || r.reason == "no_safe_verifier");
    }
    CHECK_EQ(n2.total_writes(), 0);
}

void test_determinism_and_isolation() {
    auto run = [] {
        auto n = cpu_nodes();
        auto v = verifier(n);
        ctx::CapabilityContext c;
        c.publish("kernel", {fact("swap.swappiness", "swap", "proc/sys/vm/swappiness", ctx::Risk::Low), cpu_max(),
                             fact("thermal.thermal_zone0.mode", "thermal", "sys/class/thermal/thermal_zone0/mode", ctx::Risk::Low),
                             fact("governor.policy0", "governor", "sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")});
        const auto generation = c.generation();
        auto results = v.verify_all(c);
        CHECK(c.generation() == generation); // 21. verifying alone changes nothing; no decision is made
        std::string text;
        for (auto &r : results) text += r.capability_id + ":" + r.reason + ":" + r.test_value + ";";
        // 22/23. Writes only to the verified targets (no GameRuntime / planner nodes touched).
        for (auto &p : n.written) CHECK(p == MAX || p == "/proc/sys/vm/swappiness");
        CHECK(n.nodes["/sys/class/thermal/thermal_zone0/mode"] == "enabled");
        return text;
    };
    const auto a = run(), b = run();
    CHECK_EQ(a, b); // 20. deterministic (id order)
    CHECK(a.find("cpufreq.policy4.scaling_max_freq:verified") != std::string::npos);
    CHECK(a.find("governor.policy0:no_safe_verifier") != std::string::npos);
    CHECK(a.find("thermal.thermal_zone0.mode:thermal_protected") != std::string::npos);
    // 24. Observatory schema unchanged.
    CHECK_EQ(flux::observatory::kSchemaVersion, 1);
    CHECK_EQ(flux::observatory::EventRegistry::builtin().types().size(), size_t(19));
    // 14. unsuccessful verification leaves the fact unverified when applied.
    ctx::CapabilityContext c;
    c.publish("kernel", {cpu_max()});
    k::VerificationResult failed;
    failed.capability_id = "cpufreq.policy4.scaling_max_freq";
    failed.reason = "readback_mismatch";
    CHECK_EQ(k::apply_verification(c, {failed}), size_t(0));
    CHECK(!c.resolve("cpufreq.policy4.scaling_max_freq").fact->verified);
}

} // namespace

int main() {
    test_gating();
    test_success_and_preservation();
    test_failures();
    test_repeat_thermal_paths();
    test_determinism_and_isolation();
    return flux_test::report("capability_verification_test");
}
