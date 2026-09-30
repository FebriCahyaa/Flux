// Kernel Intelligence (Step 7): classification, GKI confidence, capability probing,
// missing and invalid interfaces, adapter selection, and the read-only guarantee.
#include "flux_test.hpp"
#include "kernel/KernelIntelligence.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <unistd.h>

using namespace flux::kernel;
namespace fs = std::filesystem;

namespace {

/// In-memory tree. Directories are implied by file paths; `dirs` adds empty ones.
struct FakeFs : ReadOnlyFs {
    std::map<std::string, std::string> files;
    std::set<std::string> unreadable, writable, dirs;
    mutable int reads = 0;

    bool is_dir(const std::string &p) const {
        if (dirs.count(p)) return true;
        auto it = files.lower_bound(p + "/");
        return it != files.end() && it->first.rfind(p + "/", 0) == 0;
    }
    Kind kind(const std::string &p) const override {
        if (files.count(p)) return Kind::File;
        return is_dir(p) ? Kind::Directory : Kind::Missing;
    }
    std::optional<std::string> read(const std::string &p) const override {
        ++reads;
        if (!files.count(p) || unreadable.count(p)) return std::nullopt;
        return files.at(p);
    }
    std::vector<std::string> list(const std::string &d) const override {
        std::set<std::string> out;
        auto add = [&](const std::string &p) {
            if (p.rfind(d + "/", 0) != 0) return;
            auto rest = p.substr(d.size() + 1);
            out.insert(rest.substr(0, rest.find('/')));
        };
        for (auto &[p, _] : files) add(p);
        for (auto &p : dirs) add(p);
        return {out.begin(), out.end()};
    }
    bool writable_hint(const std::string &p) const override { return writable.count(p) > 0; }
};

KernelIdentity cls(const std::string &release) { return classify({release, ""}); }

void test_version_parsing() {
    auto v = parse_version("5.10.198-android12-9-00085-gabc");
    CHECK(v.valid);
    CHECK_EQ(v.major, 5);
    CHECK_EQ(v.minor, 10);
    CHECK_EQ(v.patch, 198);
    CHECK_EQ(parse_version("4.14").code(), 414);
    CHECK(!parse_version("").valid);
    CHECK(!parse_version("garbage").valid);
    CHECK(!parse_version("5").valid);
}

void test_classification_gki() {
    auto k = cls("5.10.198-android12-9-00085-gabc");
    CHECK(k.integration == Integration::GKI);
    CHECK(k.integration_confidence == Confidence::High);
    CHECK_EQ(k.android_release, 12);
    CHECK_EQ(k.kmi, std::string("android12-5.10"));
    CHECK(k.generation == Generation::NonLegacy);
    CHECK(k.generation_confidence == Confidence::High);

    auto k6 = cls("6.1.75-android14-11-g1234");
    CHECK(k6.integration == Integration::GKI);
    CHECK(k6.integration_confidence == Confidence::High);
    CHECK_EQ(k6.kmi, std::string("android14-6.1"));
}

void test_gki_confidence_single_signal() {
    // Android tag on a pre-5.10 kernel (GKI 1.0 era): tag alone is not GKI 2.0.
    auto k = cls("5.4.210-android11-2-g00");
    CHECK(k.integration != Integration::GKI);
    CHECK(k.integration_confidence <= Confidence::Low);
    CHECK(!k.integration_reason.empty());

    // 5.10 with an android11 tag: two signals disagree -> never High GKI.
    auto k2 = cls("5.10.43-android11-0-g00");
    CHECK(!(k2.integration == Integration::GKI && k2.integration_confidence == Confidence::High));

    // New kernel, tag stripped (custom kernel): probably not GKI, but not certain.
    auto custom = cls("5.10.200-perf-sultan");
    CHECK(custom.integration == Integration::NonGKI);
    CHECK(custom.integration_confidence == Confidence::Medium);

    // Old kernel without tag: GKI 2.0 impossible -> High NonGKI.
    auto old = cls("4.19.157-perf+");
    CHECK(old.integration == Integration::NonGKI);
    CHECK(old.integration_confidence == Confidence::High);
    CHECK(old.generation == Generation::NonLegacy);
}

void test_generation_independent() {
    auto legacy = cls("4.14.190-perf-g1");
    CHECK(legacy.generation == Generation::Legacy);
    CHECK(legacy.generation_confidence == Confidence::High);
    CHECK(legacy.integration == Integration::NonGKI);

    auto l2 = cls("3.18.140");
    CHECK(l2.generation == Generation::Legacy);

    auto unknown = cls("");
    CHECK(unknown.integration == Integration::Unknown);
    CHECK(unknown.integration_confidence == Confidence::None);
    CHECK(unknown.generation == Generation::Unknown);
    CHECK(unknown.generation_confidence == Confidence::None);
}

void test_proc_version_fallback() {
    auto k = classify({"", "Linux version 5.15.123-android13-4-gdead (build@host) (clang) #1 SMP"});
    CHECK_EQ(k.release, std::string("5.15.123-android13-4-gdead"));
    CHECK(k.integration == Integration::GKI);
    // A release taken only from /proc/version is one step less certain.
    CHECK(k.integration_confidence == Confidence::Medium);
}

FakeFs sample_device() {
    FakeFs f;
    f.files["proc/sys/kernel/osrelease"] = "5.10.198-android12-9-g1\n";
    const std::string p0 = "sys/devices/system/cpu/cpufreq/policy0/";
    f.files[p0 + "scaling_max_freq"] = "1804800\n";
    f.files[p0 + "scaling_min_freq"] = "300000\n";
    f.files[p0 + "scaling_cur_freq"] = "998400\n";
    f.files[p0 + "cpuinfo_min_freq"] = "300000\n";
    f.files[p0 + "cpuinfo_max_freq"] = "1804800\n";
    f.files[p0 + "scaling_governor"] = "schedutil\n";
    f.files[p0 + "scaling_available_governors"] = "schedutil performance powersave\n";
    f.files[p0 + "related_cpus"] = "0 1 2 3\n";
    const std::string p4 = "sys/devices/system/cpu/cpufreq/policy4/";
    f.files[p4 + "scaling_max_freq"] = "2419200\n";
    f.files[p4 + "scaling_governor"] = "schedutil\n";
    f.files["sys/block/sda/queue/scheduler"] = "[mq-deadline] kyber none\n";
    f.files["sys/block/loop0/queue/scheduler"] = "none\n";
    f.files["sys/block/zram0/disksize"] = "4294967296\n";
    f.files["sys/block/zram0/comp_algorithm"] = "lzo [lz4] zstd\n";
    f.files["proc/swaps"] = "Filename Type Size Used Priority\n/dev/block/zram0 partition 4194300 0 -2\n";
    f.files["sys/class/thermal/thermal_zone0/type"] = "cpu-0-0\n";
    f.files["sys/class/thermal/thermal_zone0/temp"] = "41000\n";
    f.writable = {p0 + "scaling_max_freq", p0 + "scaling_governor", "sys/block/sda/queue/scheduler",
                  "sys/class/thermal/thermal_zone0/temp"};
    return f;
}

std::string p0_path(const std::string &leaf) { return "sys/devices/system/cpu/cpufreq/policy0/" + leaf; }

void test_capability_probing() {
    auto dev = sample_device();
    auto reg = AdapterRegistry::with_builtin();
    auto r = observe(dev, {}, reg);
    CHECK(r.identity.integration == Integration::GKI);
    CHECK_EQ(r.adapter, std::string("generic"));

    auto *max0 = r.find("cpufreq.policy0.scaling_max_freq");
    CHECK(max0 != nullptr);
    if (max0) {
        CHECK(max0->supported);
        CHECK(max0->readable);
        CHECK(max0->writable);
        CHECK(!max0->verified);
        CHECK_EQ(max0->value, std::string("1804800"));
        CHECK_EQ(max0->range, std::string("300000..1804800"));
        CHECK_EQ(max0->interface, p0_path("scaling_max_freq"));
        CHECK_EQ(max0->source, std::string("generic"));
        CHECK(max0->domain == Domain::CpuFreq);
        CHECK(max0->confidence == Confidence::High);
        CHECK(max0->rollback);
        CHECK(!max0->requires_adapter);
    }
    auto *max4 = r.find("cpufreq.policy4.scaling_max_freq");
    CHECK(max4 && max4->readable && !max4->writable);

    auto *gov = r.find("governor.policy0");
    CHECK(gov != nullptr);
    if (gov) {
        CHECK_EQ(gov->value, std::string("schedutil"));
        CHECK_EQ(gov->range, std::string("schedutil performance powersave"));
    }

    auto *io = r.find("io.sda.scheduler");
    CHECK(io != nullptr);
    if (io) {
        CHECK_EQ(io->value, std::string("mq-deadline"));
        CHECK_EQ(io->range, std::string("mq-deadline kyber none"));
        CHECK(io->domain == Domain::IoScheduler);
    }
    CHECK(r.find("io.loop0.scheduler") == nullptr); // excluded pseudo device

    auto *comp = r.find("zram.zram0.comp_algorithm");
    CHECK(comp && comp->value == "lz4");

    // Thermal is observe-only: permission bits never turn into "writable".
    auto *temp = r.find("thermal.thermal_zone0.temp");
    CHECK(temp && temp->readable && !temp->writable && !temp->rollback);
    if (temp) CHECK(temp->risk == Risk::High);

    auto *swaps = r.find("swap.swaps");
    CHECK(swaps && swaps->readable && !swaps->writable);

    // Every domain is represented, supported or not.
    for (int d = 0; d < kDomainCount; ++d) CHECK(!r.in(static_cast<Domain>(d)).empty());
}

void test_missing_interfaces() {
    FakeFs empty;
    auto r = observe(empty, {}, AdapterRegistry::with_builtin());
    CHECK(r.identity.integration == Integration::Unknown);
    CHECK(!r.capabilities.empty());
    for (auto &c : r.capabilities) {
        CHECK(!c.supported);
        CHECK(!c.readable);
        CHECK(!c.writable);
        CHECK(!c.verified);
        CHECK(!c.note.empty());
    }
    auto *glob = r.find("cpufreq.*.scaling_max_freq");
    CHECK(glob != nullptr);
    CHECK(r.find("uclamp.top-app.min") != nullptr);
}

void test_invalid_nodes() {
    FakeFs f;
    const std::string p0 = "sys/devices/system/cpu/cpufreq/policy0/";
    f.files[p0 + "scaling_max_freq"] = "abc\n";          // not an integer
    f.files[p0 + "scaling_min_freq"] = "";               // empty
    f.files[p0 + "scaling_governor"] = "sched\x01util\n"; // control byte
    f.files[p0 + "scaling_cur_freq"] = "1";
    f.unreadable.insert(p0 + "scaling_cur_freq");        // permission denied
    f.files["sys/block/sda/queue/scheduler"] = "mq-deadline none\n"; // no selection
    f.files[p0 + "related_cpus"] = std::string(8192, '1'); // oversized
    auto caps = probe(make_generic_adapter()->specs(), "generic", false, f);
    auto find = [&](const std::string &id) -> const Capability * {
        for (auto &c : caps)
            if (c.id == id) return &c;
        return nullptr;
    };
    for (auto id : {"cpufreq.policy0.scaling_max_freq", "cpufreq.policy0.scaling_min_freq",
                    "governor.policy0", "cpufreq.policy0.scaling_cur_freq", "io.sda.scheduler",
                    "policy.policy0.related_cpus"}) {
        auto *c = find(id);
        CHECK(c != nullptr);
        if (!c) continue;
        CHECK(c->supported);
        CHECK(!c->readable);
        CHECK(c->value.empty());
        CHECK(c->confidence <= Confidence::Low || std::string(id) == "cpufreq.policy0.scaling_cur_freq");
        CHECK(!c->note.empty());
    }
}

void test_real_fs_invalid_and_readonly() {
    auto root = fs::temp_directory_path() / ("flux_kernel_" + std::to_string(::getpid()));
    fs::remove_all(root);
    auto pol = root / "sys/devices/system/cpu/cpufreq/policy0";
    fs::create_directories(pol / "scaling_max_freq"); // directory where a node belongs
    { std::ofstream(pol / "scaling_governor") << "schedutil\n"; }
    { std::ofstream(root / "proc_placeholder") << ""; }
    fs::create_symlink(root / "nowhere", pol / "scaling_min_freq"); // dangling

    auto before = fs::last_write_time(pol / "scaling_governor");
    auto rofs = make_readonly_fs(root.string());
    auto caps = probe(make_generic_adapter()->specs(), "generic", false, *rofs);
    const Capability *max = nullptr, *min = nullptr, *gov = nullptr;
    for (auto &c : caps) {
        if (c.id == "cpufreq.policy0.scaling_max_freq") max = &c;
        if (c.id == "cpufreq.policy0.scaling_min_freq") min = &c;
        if (c.id == "governor.policy0") gov = &c;
    }
    CHECK(max && max->supported && !max->readable && !max->note.empty());
    CHECK(min && !min->readable);
    CHECK(gov && gov->readable && gov->value == "schedutil");
    CHECK(fs::last_write_time(pol / "scaling_governor") == before);
    { std::ifstream in(pol / "scaling_governor"); std::string s; std::getline(in, s); CHECK_EQ(s, std::string("schedutil")); }
    CHECK(fs::is_directory(pol / "scaling_max_freq"));
    fs::remove_all(root);
}

void test_adapter_selection() {
    auto reg = AdapterRegistry::with_builtin();
    FakeFs qc;
    qc.files["sys/class/kgsl/kgsl-3d0/gpu_model"] = "Adreno650v2\n";
    qc.files["sys/class/kgsl/kgsl-3d0/max_gpuclk"] = "587000000\n";
    auto s = reg.select({"kona", "qcom", "QTI"}, qc);
    CHECK(s.vendor && s.vendor->name() == "qualcomm");
    CHECK(s.confidence == Confidence::High);

    // Property alone (interface absent) -> weaker.
    FakeFs none;
    auto s2 = reg.select({"kona", "qcom", ""}, none);
    CHECK(s2.vendor && s2.vendor->name() == "qualcomm");
    CHECK(s2.confidence == Confidence::Medium);

    FakeFs mtk;
    mtk.files["proc/gpufreqv2/gpu_working_opp_table"] = "[0] freq: 886000\n";
    auto s3 = reg.select({"mt6893", "mt6893", "Mediatek"}, mtk);
    CHECK(s3.vendor && s3.vendor->name() == "mediatek");
    CHECK(s3.confidence == Confidence::High);

    // Substrings inside unrelated names must not match (B-07: "*mt*", "*sm*").
    auto s4 = reg.select({"smartchip", "gsmt", ""}, none);
    CHECK(s4.vendor == nullptr);

    auto r = observe(qc, {"kona", "qcom", "QTI"}, reg);
    CHECK_EQ(r.adapter, std::string("qualcomm"));
    auto *model = r.find("gpu.kgsl.gpu_model");
    CHECK(model && model->readable && model->requires_adapter && model->source == "qualcomm");
    CHECK(model && model->value == "Adreno650v2");
    // The MediaTek specs are not probed on a Qualcomm device.
    CHECK(r.find("gpu.gpufreqv2.opp_table") == nullptr);
}

struct TestVendor : Adapter {
    std::string name() const override { return "testvendor"; }
    Confidence match(const PlatformHint &h, const ReadOnlyFs &) const override {
        return h.hardware == "tv1" ? Confidence::Medium : Confidence::None;
    }
    std::vector<ProbeSpec> specs() const override {
        return {{"gpu.tv.load", Domain::Gpu, "sys/tv/gpu/load", ValueKind::Integer, Risk::Low, false}};
    }
};

void test_vendor_framework() {
    auto reg = AdapterRegistry::with_builtin();
    reg.add(std::make_unique<TestVendor>());
    FakeFs f;
    f.files["sys/tv/gpu/load"] = "37\n";
    auto r = observe(f, {"", "tv1", ""}, reg);
    CHECK_EQ(r.adapter, std::string("testvendor"));
    auto *c = r.find("gpu.tv.load");
    CHECK(c && c->value == "37" && c->requires_adapter);
}

} // namespace

int main() {
    test_version_parsing();
    test_classification_gki();
    test_gki_confidence_single_signal();
    test_generation_independent();
    test_proc_version_fallback();
    test_capability_probing();
    test_missing_interfaces();
    test_invalid_nodes();
    test_real_fs_invalid_and_readonly();
    test_adapter_selection();
    test_vendor_framework();
    return flux_test::report("kernel_intelligence_test");
}
