// Runtime metrics collector (Step 8.7): success, missing interface, malformed value, permission
// denied, conflicting metric sources, bottleneck input, read-only.
#include "flux_test.hpp"
#include "RuntimeMetrics.hpp"

#include <set>

namespace ctx = flux::context;
namespace k = flux::kernel;
namespace m = flux::metrics;

namespace {

struct FakeFs : k::ReadOnlyFs {
    std::map<std::string, std::string> files;
    std::set<std::string> denied;
    Kind kind(const std::string &p) const override {
        if (files.count(p)) return Kind::File;
        auto it = files.lower_bound(p + "/");
        return it != files.end() && it->first.rfind(p + "/", 0) == 0 ? Kind::Directory : Kind::Missing;
    }
    std::optional<std::string> read(const std::string &p) const override {
        if (denied.count(p) || !files.count(p)) return std::nullopt;
        return files.at(p);
    }
    std::vector<std::string> list(const std::string &d) const override {
        std::set<std::string> out;
        for (auto &[p, _] : files)
            if (p.rfind(d + "/", 0) == 0) {
                auto rest = p.substr(d.size() + 1);
                out.insert(rest.substr(0, rest.find('/')));
            }
        return {out.begin(), out.end()};
    }
    bool writable_hint(const std::string &) const override { return true; }
};

const std::string P0 = "sys/devices/system/cpu/cpufreq/policy0/";
const std::string P4 = "sys/devices/system/cpu/cpufreq/policy4/";
const std::string KG = "sys/class/kgsl/kgsl-3d0/";

// user nice system idle iowait irq softirq
std::string stat(int c0_busy, int c0_idle, int c4_busy, int c4_idle, int iowait) {
    auto line = [](const std::string &n, int busy, int idle, int io) {
        return n + " " + std::to_string(busy) + " 0 0 " + std::to_string(idle) + " " + std::to_string(io) + " 0 0 0 0 0\n";
    };
    return line("cpu ", c0_busy + c4_busy, c0_idle + c4_idle, iowait) + line("cpu0", c0_busy, c0_idle, iowait) +
           line("cpu4", c4_busy, c4_idle, 0) + "intr 1 2 3\n";
}

FakeFs device() {
    FakeFs f;
    f.files["proc/stat"] = stat(100, 900, 100, 900, 10);
    f.files[P0 + "scaling_cur_freq"] = "1804800\n";
    f.files[P0 + "cpuinfo_max_freq"] = "1804800\n";
    f.files[P0 + "cpuinfo_min_freq"] = "300000\n";
    f.files[P0 + "related_cpus"] = "0 1 2 3\n";
    f.files[P4 + "scaling_cur_freq"] = "2419200\n";
    f.files[P4 + "cpuinfo_max_freq"] = "2419200\n";
    f.files[P4 + "cpuinfo_min_freq"] = "710400\n";
    f.files[P4 + "related_cpus"] = "4 5 6 7\n";
    f.files[KG + "gpu_busy_percentage"] = "93 %\n";
    f.files[KG + "gpubusy"] = "  930000   1000000\n";
    f.files[KG + "gpuclk"] = "587000000\n";
    f.files[KG + "max_gpuclk"] = "587000000\n";
    f.files["proc/meminfo"] = "MemTotal:        7812345 kB\nMemFree:          200000 kB\nMemAvailable:    2048000 kB\n"
                              "SwapTotal:       4194300 kB\nSwapFree:        4000000 kB\n";
    f.files["proc/pressure/memory"] = "some avg10=12.50 avg60=3.00 avg300=1.00 total=123\nfull avg10=2.00 avg60=0.50 avg300=0.10 total=45\n";
    f.files["proc/pressure/io"] = "some avg10=4.25 avg60=1.00 avg300=0.50 total=9\nfull avg10=1.00 avg60=0.20 avg300=0.10 total=3\n";
    f.files["sys/block/zram0/mm_stat"] = "1048576000 262144000 270000000 0 300000000 10 20 0 0\n";
    f.files["sys/block/sda/stat"] = "1000 0 8000 2000 500 0 4000 1000 0 0 0 0 0 0 0\n";
    f.files["sys/block/sda/queue/scheduler"] = "[mq-deadline] none\n";
    f.files["sys/block/loop0/stat"] = "1 0 0 0 0 0 0 0 0 0 0\n";
    return f;
}

void second_tick(FakeFs &f) {
    // cpu4 fully busy over the interval, cpu0 25 %, iowait +5 of 400 total jiffies.
    f.files["proc/stat"] = stat(150, 1045, 300, 900, 15);
    f.files["sys/block/sda/stat"] = "1100 0 8800 2400 600 0 4800 1300 0 0 0 0 0 0 0\n";
}

void test_collector_success() {
    auto f = device();
    m::RuntimeMetricsCollector c(f);
    auto first = c.sample(1000);
    auto *util0 = first.get("cpu.utilization.busiest_core");
    CHECK(util0 && !util0->readable && util0->note.find("previous sample") != std::string::npos);

    second_tick(f);
    auto s = c.sample(2000);
    CHECK_EQ(s.timestamp_ms, int64_t(2000));
    auto *busiest = s.get("cpu.utilization.busiest_core");
    CHECK(busiest && busiest->readable && busiest->value && *busiest->value > 0.99);
    CHECK(busiest && busiest->text == "cpu4");
    CHECK(busiest && busiest->confidence == ctx::Confidence::High && busiest->source == "proc/stat");
    auto *total = s.get("cpu.utilization.total");
    CHECK(total && total->value && *total->value > 0.5 && *total->value < 0.7);
    auto *ratio = s.get("cpu.freq_ratio.busiest_cluster");
    CHECK(ratio && ratio->value && *ratio->value > 0.99 && ratio->text == "policy4");
    auto *cl = s.get("cpu.cluster.policy4.cur_mhz");
    CHECK(cl && cl->value && *cl->value == 2419.2 && cl->verified); // inside cpuinfo min..max
    CHECK(s.get("cpu.cluster.policy4.cpus")->text == "4 5 6 7");

    auto *gpu = s.get("gpu.utilization");
    CHECK(gpu && gpu->value && *gpu->value == 93 && gpu->verified); // two sources agree
    CHECK(s.get("gpu.freq_ratio") && *s.get("gpu.freq_ratio")->value == 1.0);
    CHECK(s.get("gpu.interfaces")->text.find("kgsl") != std::string::npos);

    CHECK(*s.get("mem.total_mb")->value > 7600 && *s.get("mem.available_mb")->value == 2000);
    CHECK(*s.get("mem.psi_some_avg10")->value == 12.5);
    CHECK(*s.get("swap.total_mb")->value > 4000);
    CHECK(*s.get("zram.zram0.orig_mb")->value == 1000 && *s.get("zram.zram0.compr_mb")->value == 250);

    CHECK(*s.get("io.psi_some_avg10")->value == 4.25);
    auto *iow = s.get("io.iowait_ratio");
    CHECK(iow && iow->value && *iow->value > 0.01 && *iow->value < 0.02);
    CHECK(s.get("io.sda.scheduler")->text == "mq-deadline");
    auto *lat = s.get("io.sda.latency_ms");
    CHECK(lat && lat->value && *lat->value == 3.5); // (400+300) ticks / (100+100) ios
    CHECK(s.get("io.loop0.latency_ms") == nullptr);  // pseudo devices skipped

    for (auto &x : s.metrics) {
        CHECK_EQ(x.timestamp_ms, int64_t(2000));
        if (x.readable) {
            CHECK(!x.source.empty());
            CHECK(x.confidence != ctx::Confidence::None);
        } else {
            CHECK(x.confidence == ctx::Confidence::None);
            CHECK(!x.note.empty());
        }
    }
}

void test_missing_interface() {
    FakeFs empty;
    m::RuntimeMetricsCollector c(empty);
    c.sample(1);
    auto s = c.sample(2);
    for (auto id : {"cpu.utilization.busiest_core", "cpu.utilization.total", "cpu.freq_ratio.busiest_cluster",
                    "gpu.utilization", "gpu.freq_ratio", "gpu.interfaces", "mem.total_mb", "mem.available_mb",
                    "mem.psi_some_avg10", "swap.total_mb", "io.psi_some_avg10", "io.iowait_ratio"}) {
        auto *x = s.get(id);
        CHECK(x != nullptr);
        if (!x) continue;
        CHECK(!x->readable);
        CHECK(!x->value);
        CHECK(x->confidence == ctx::Confidence::None);
        CHECK(!x->note.empty());
    }
    // Old kernel: MemAvailable absent -> Unknown, never derived from MemFree.
    FakeFs old;
    old.files["proc/meminfo"] = "MemTotal: 2000000 kB\nMemFree: 500000 kB\n";
    m::RuntimeMetricsCollector c2(old);
    auto s2 = c2.sample(1);
    CHECK(s2.get("mem.total_mb")->readable);
    CHECK(!s2.get("mem.available_mb")->readable);
    CHECK(s2.get("mem.available_mb")->note.find("MemAvailable") != std::string::npos);
}

void test_malformed_values() {
    auto f = device();
    f.files[P4 + "scaling_cur_freq"] = "abc\n";
    f.files[KG + "gpu_busy_percentage"] = "lots\n";
    f.files[KG + "gpubusy"] = "1 2 3 x\n";
    f.files["proc/meminfo"] = "MemTotal: many kB\nMemAvailable: -5 kB\n";
    f.files["proc/pressure/memory"] = "some avg10=high\n";
    f.files["proc/stat"] = "garbage\n";
    f.files["sys/block/zram0/mm_stat"] = "1 2\n";
    m::RuntimeMetricsCollector c(f);
    c.sample(1);
    auto s = c.sample(2);
    for (auto id : {"cpu.cluster.policy4.cur_mhz", "gpu.utilization", "mem.total_mb", "mem.available_mb",
                    "mem.psi_some_avg10", "cpu.utilization.busiest_core", "zram.zram0.orig_mb"}) {
        auto *x = s.get(id);
        CHECK(x != nullptr);
        if (!x) continue;
        CHECK(!x->readable);
        CHECK(!x->value);
        CHECK(x->note.find("malformed") != std::string::npos || x->note.find("previous") != std::string::npos);
    }
    CHECK(s.get("cpu.cluster.policy0.cur_mhz")->readable); // one bad node doesn't spoil others
}

void test_permission_denied() {
    auto f = device();
    f.denied = {"proc/pressure/memory", KG + "gpu_busy_percentage", KG + "gpubusy", P0 + "scaling_cur_freq"};
    m::RuntimeMetricsCollector c(f);
    auto s = c.sample(1);
    for (auto id : {"mem.psi_some_avg10", "gpu.utilization", "cpu.cluster.policy0.cur_mhz"}) {
        auto *x = s.get(id);
        CHECK(x && !x->readable && x->note.find("unreadable") != std::string::npos);
    }
    CHECK(s.get("mem.available_mb")->readable);
}

void test_conflicting_sources() {
    auto f = device();
    // Hardware-reported frequency disagrees with the governor's view by >10 %.
    f.files[P4 + "cpuinfo_cur_freq"] = "1400000\n";
    // Two GPU busy sources disagree by >15 points.
    f.files[KG + "gpubusy"] = "400000 1000000\n";
    m::RuntimeMetricsCollector c(f);
    auto s = c.sample(1);
    auto *freq = s.get("cpu.cluster.policy4.cur_mhz");
    CHECK(freq && freq->readable && *freq->value == 1400.0); // hardware reading preferred
    CHECK(freq && !freq->verified && freq->confidence == ctx::Confidence::Medium);
    CHECK(freq && freq->note.find("conflict") != std::string::npos);
    auto *gpu = s.get("gpu.utilization");
    CHECK(gpu && *gpu->value == 93 && !gpu->verified && gpu->confidence == ctx::Confidence::Medium);
    CHECK(gpu && gpu->note.find("conflict") != std::string::npos);
    // Agreeing sources are verified.
    f.files[P4 + "cpuinfo_cur_freq"] = "2400000\n";
    auto s2 = c.sample(2);
    CHECK(s2.get("cpu.cluster.policy4.cur_mhz")->verified);
}

void test_bottleneck_input() {
    auto f = device();
    m::RuntimeMetricsCollector c(f);
    auto first = m::to_runtime_sample(c.sample(1000), 40, 60);
    CHECK(!first.cpu_busiest_core); // unknown stays unknown
    CHECK(first.gpu_busy && *first.gpu_busy == 0.93);
    CHECK(first.mem_psi_some && *first.mem_psi_some == 12.5);
    CHECK(first.fps && *first.fps == 40);

    std::vector<flux::bottleneck::RuntimeSample> samples;
    for (int i = 0; i < 6; ++i) {
        // cpu4 saturated each interval, GPU mostly idle.
        f.files["proc/stat"] = stat(100 + i * 25, 900 + i * 75, 100 + (i + 1) * 100, 900, 10);
        f.files[KG + "gpu_busy_percentage"] = "40 %\n";
        f.files[KG + "gpubusy"] = "400000 1000000\n";
        f.files["proc/pressure/memory"] = "some avg10=0.50 avg60=0.10 avg300=0.00 total=1\n";
        samples.push_back(m::to_runtime_sample(c.sample(2000 + i * 1000), 40, 60));
    }
    flux::bottleneck::BottleneckInputs in;
    in.session = {"g", samples};
    in.now_ms = 9000;
    auto a = flux::bottleneck::assess(in);
    CHECK(a.primary == flux::bottleneck::Kind::Cpu);
    CHECK(a.get(flux::bottleneck::Kind::Cpu)->state >= flux::bottleneck::State::Likely);
}

void test_read_only() {
    // The collector only has a ReadOnlyFs (no write API); the tree is unchanged after sampling.
    auto f = device();
    auto before = f.files;
    m::RuntimeMetricsCollector c(f);
    c.sample(1);
    c.sample(2);
    CHECK(f.files == before);
}

} // namespace

int main() {
    test_collector_success();
    test_missing_interface();
    test_malformed_values();
    test_permission_denied();
    test_conflicting_sources();
    test_bottleneck_input();
    test_read_only();
    return flux_test::report("runtime_metrics_test");
}
