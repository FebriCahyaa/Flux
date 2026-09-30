#include "KernelIntelligence.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <regex>
#include <sstream>
#include <unistd.h>

namespace flux::kernel {

const char *to_string(Confidence c) {
    switch (c) {
    case Confidence::None: return "none";
    case Confidence::Low: return "low";
    case Confidence::Medium: return "medium";
    case Confidence::High: return "high";
    }
    return "none";
}
const char *to_string(Integration i) {
    switch (i) {
    case Integration::GKI: return "gki";
    case Integration::NonGKI: return "non_gki";
    case Integration::Unknown: break;
    }
    return "unknown";
}
const char *to_string(Generation g) {
    switch (g) {
    case Generation::Legacy: return "legacy";
    case Generation::NonLegacy: return "non_legacy";
    case Generation::Unknown: break;
    }
    return "unknown";
}
const char *to_string(Risk r) {
    switch (r) {
    case Risk::Low: return "low";
    case Risk::Medium: return "medium";
    case Risk::High: return "high";
    }
    return "high";
}
const char *to_string(Domain d) {
    static const char *const names[kDomainCount] = {
        "cpufreq", "cpu_policy", "governor", "uclamp", "scheduler", "cpuset", "cgroup", "devfreq",
        "gpu", "thermal", "zram", "swap", "io_scheduler", "input_boost", "display_refresh"};
    auto i = static_cast<int>(d);
    return i >= 0 && i < kDomainCount ? names[i] : "unknown";
}

namespace {

std::string trim(const std::string &s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string join(const std::vector<std::string> &v, const char *sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? sep : "") + v[i];
    return out;
}

Confidence weaker(Confidence c) {
    return c == Confidence::None ? c : static_cast<Confidence>(static_cast<int>(c) - 1);
}

} // namespace

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

KernelVersion parse_version(const std::string &release) {
    static const std::regex re(R"(^(\d+)\.(\d+)(?:\.(\d+))?)");
    std::smatch m;
    KernelVersion v;
    if (!std::regex_search(release, m, re)) return v;
    v.major = std::stoi(m[1]);
    v.minor = std::stoi(m[2]);
    v.patch = m[3].matched ? std::stoi(m[3]) : 0;
    v.valid = true;
    return v;
}

KernelIdentity classify(const KernelEvidence &evidence) {
    KernelIdentity k;
    bool from_banner = false;
    k.release = trim(evidence.release);
    if (k.release.empty()) {
        static const std::regex banner(R"(Linux version (\S+))");
        std::smatch m;
        if (std::regex_search(evidence.proc_version, m, banner)) {
            k.release = m[1];
            from_banner = true;
        }
    }
    k.version = parse_version(k.release);
    if (!k.version.valid) {
        k.integration_reason = k.generation_reason = "kernel release missing or unparseable";
        return k;
    }

    // Generation: version is a direct fact, so one source is enough for High.
    const int code = k.version.code();
    k.generation = code < kLegacyBelow ? Generation::Legacy : Generation::NonLegacy;
    k.generation_confidence = Confidence::High;
    k.generation_reason = std::to_string(k.version.major) + "." + std::to_string(k.version.minor) +
                          (code < kLegacyBelow ? " < 4.19" : " >= 4.19");

    static const std::regex tag(R"(-android(\d+)-)");
    std::smatch m;
    const bool tagged = std::regex_search(k.release, m, tag);
    if (tagged) {
        k.android_release = std::stoi(m[1]);
        k.kmi = "android" + std::to_string(k.android_release) + "-" + std::to_string(k.version.major) +
                "." + std::to_string(k.version.minor);
    }

    if (tagged && code >= kGkiMinKernel && k.android_release >= kGkiMinAndroid) {
        k.integration = Integration::GKI;
        k.integration_confidence = Confidence::High;
        k.integration_reason = "KMI tag " + k.kmi + " and kernel >= 5.10 agree";
    } else if (tagged) {
        k.integration = Integration::Unknown;
        k.integration_confidence = Confidence::Low;
        k.integration_reason = "android tag " + k.kmi + " predates the GKI 2.0 KMI; signals disagree";
    } else if (code >= kGkiMinKernel) {
        k.integration = Integration::NonGKI;
        k.integration_confidence = Confidence::Medium;
        k.integration_reason = "no KMI tag on a GKI-capable kernel (custom or vendor build, tag may be stripped)";
    } else {
        k.integration = Integration::NonGKI;
        k.integration_confidence = Confidence::High;
        k.integration_reason = "kernel older than 5.10 cannot be GKI 2.0";
    }
    if (from_banner) {
        k.integration_confidence = weaker(k.integration_confidence);
        k.integration_reason += " (release read from /proc/version banner only)";
    }
    return k;
}

// ---------------------------------------------------------------------------
// Real read-only filesystem
// ---------------------------------------------------------------------------

namespace {

class RealFs : public ReadOnlyFs {
  public:
    explicit RealFs(std::string root) : root_(root.empty() ? "/" : std::move(root)) {}

    Kind kind(const std::string &p) const override {
        std::error_code ec;
        auto path = full(p);
        auto link = std::filesystem::symlink_status(path, ec);
        if (ec || !std::filesystem::exists(link)) return Kind::Missing;
        auto st = std::filesystem::status(path, ec);
        if (ec || !std::filesystem::exists(st)) return Kind::Other; // dangling link
        if (std::filesystem::is_regular_file(st)) return Kind::File;
        if (std::filesystem::is_directory(st)) return Kind::Directory;
        return Kind::Other;
    }
    std::optional<std::string> read(const std::string &p) const override {
        if (kind(p) != Kind::File) return std::nullopt;
        std::ifstream in(full(p), std::ios::binary);
        if (!in) return std::nullopt;
        std::string out(kReadCap, '\0');
        in.read(out.data(), static_cast<std::streamsize>(out.size()));
        if (in.bad()) return std::nullopt;
        out.resize(static_cast<size_t>(in.gcount()));
        return out;
    }
    std::vector<std::string> list(const std::string &d) const override {
        std::vector<std::string> out;
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(full(d), ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            out.push_back(it->path().filename().string());
        std::sort(out.begin(), out.end());
        return out;
    }
    bool writable_hint(const std::string &p) const override {
        return ::access(full(p).c_str(), W_OK) == 0;
    }

  private:
    static constexpr size_t kReadCap = 64 * 1024;
    std::string full(const std::string &p) const {
        return (std::filesystem::path(root_) / p).string();
    }
    std::string root_;
};

} // namespace

std::unique_ptr<ReadOnlyFs> make_readonly_fs(const std::string &root) {
    return std::make_unique<RealFs>(root);
}

// ---------------------------------------------------------------------------
// Probing
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kMaxValue = 4096;

struct Parsed {
    bool ok = false;
    std::string value, range, note;
};

Parsed interpret(const std::string &raw, ValueKind kind) {
    Parsed p;
    if (raw.size() > kMaxValue) {
        p.note = "invalid: oversized content";
        return p;
    }
    auto t = trim(raw);
    if (t.empty()) {
        p.note = "invalid: empty";
        return p;
    }
    for (unsigned char c : t) {
        if (c < 0x20 && !(kind == ValueKind::Info && (c == '\n' || c == '\t'))) {
            p.note = "invalid: control byte";
            return p;
        }
    }
    switch (kind) {
    case ValueKind::Integer: {
        size_t i = t[0] == '-' ? 1 : 0;
        if (i == t.size() || !std::all_of(t.begin() + static_cast<long>(i), t.end(),
                                          [](unsigned char c) { return std::isdigit(c); })) {
            p.note = "invalid: not an integer";
            return p;
        }
        break;
    }
    case ValueKind::Text:
        if (t.find('\n') != std::string::npos) {
            p.note = "invalid: multi-line";
            return p;
        }
        break;
    case ValueKind::Selector: {
        std::istringstream in(t);
        std::vector<std::string> items;
        std::string tok, sel;
        int selected = 0;
        while (in >> tok) {
            if (tok.size() > 2 && tok.front() == '[' && tok.back() == ']') {
                tok = tok.substr(1, tok.size() - 2);
                sel = tok;
                ++selected;
            }
            items.push_back(tok);
        }
        if (selected != 1) {
            p.note = "invalid: selector has no single [selected] entry";
            return p;
        }
        p.ok = true;
        p.value = sel;
        p.range = join(items, " ");
        return p;
    }
    case ValueKind::Info: break;
    }
    p.ok = true;
    p.value = t;
    return p;
}

std::vector<std::string> split(const std::string &path) {
    std::vector<std::string> out;
    std::string seg;
    std::istringstream in(path);
    while (std::getline(in, seg, '/'))
        if (!seg.empty()) out.push_back(seg);
    return out;
}

struct Match {
    std::string path;
    std::vector<std::string> captured;
};

void expand(const ReadOnlyFs &fs, const ProbeSpec &spec, const std::vector<std::string> &segs, size_t i,
            const std::string &prefix, std::vector<std::string> &captured, std::vector<Match> &out) {
    if (i == segs.size()) {
        out.push_back({prefix, captured});
        return;
    }
    const auto &seg = segs[i];
    auto next = [&](const std::string &name) { return prefix.empty() ? name : prefix + "/" + name; };
    if (seg.find('*') == std::string::npos) {
        expand(fs, spec, segs, i + 1, next(seg), captured, out);
        return;
    }
    for (const auto &name : fs.list(prefix)) {
        if (::fnmatch(seg.c_str(), name.c_str(), 0) != 0) continue;
        if (std::any_of(spec.exclude.begin(), spec.exclude.end(),
                        [&](const std::string &x) { return name.rfind(x, 0) == 0; }))
            continue;
        captured.push_back(name);
        expand(fs, spec, segs, i + 1, next(name), captured, out);
        captured.pop_back();
    }
}

std::string make_id(const std::string &templ, const std::string &capture) {
    auto pos = templ.find("{}");
    return pos == std::string::npos ? templ : templ.substr(0, pos) + capture + templ.substr(pos + 2);
}

std::string range_of(const ReadOnlyFs &fs, const std::string &node, const ProbeSpec &spec) {
    if (spec.range_siblings.empty()) return "";
    auto dir = node.substr(0, node.rfind('/'));
    std::vector<std::string> parts;
    for (const auto &sib : spec.range_siblings) {
        auto raw = fs.read(dir + "/" + sib);
        if (!raw) return "";
        auto t = trim(*raw);
        if (t.empty() || t.size() > kMaxValue) return "";
        parts.push_back(t);
    }
    return join(parts, spec.range_siblings.size() == 2 ? ".." : " ");
}

Capability probe_node(const ReadOnlyFs &fs, const ProbeSpec &spec, const std::string &id,
                      const std::string &path, const std::string &source, bool requires_adapter) {
    Capability c;
    c.id = id;
    c.domain = spec.domain;
    c.interface = path;
    c.source = source;
    c.risk = spec.risk;
    c.requires_adapter = requires_adapter;
    const bool observe_only = spec.observe_only || spec.kind == ValueKind::Info;

    const auto kind = fs.kind(path);
    if (kind == ReadOnlyFs::Kind::Missing) {
        c.confidence = Confidence::Medium;
        c.note = "interface absent";
        return c;
    }
    c.supported = true;
    if (kind != ReadOnlyFs::Kind::File) {
        c.confidence = Confidence::Low;
        c.note = "invalid: not a regular file";
        return c;
    }
    c.writable = !observe_only && fs.writable_hint(path);
    auto raw = fs.read(path);
    if (!raw) {
        c.confidence = Confidence::Medium;
        c.note = "unreadable (permission or I/O error)";
        return c;
    }
    auto parsed = interpret(*raw, spec.kind);
    if (!parsed.ok) {
        c.confidence = Confidence::Low;
        c.note = parsed.note;
        return c;
    }
    c.readable = true;
    c.value = parsed.value;
    c.range = spec.kind == ValueKind::Selector ? parsed.range : range_of(fs, path, spec);
    c.rollback = spec.rollback && !observe_only;
    c.confidence = Confidence::High;
    c.note = c.writable ? "writable by permission only; not verified" : "";
    return c;
}

} // namespace

std::vector<Capability> probe(const std::vector<ProbeSpec> &specs, const std::string &source,
                              bool requires_adapter, const ReadOnlyFs &fs) {
    std::vector<Capability> out;
    for (const auto &spec : specs) {
        if (spec.path.find('*') == std::string::npos) {
            out.push_back(probe_node(fs, spec, spec.id, spec.path, source, requires_adapter));
            continue;
        }
        std::vector<Match> matches;
        std::vector<std::string> captured;
        const auto segs = split(spec.path);
        // Expand up to the leaf's parent; the leaf itself may be absent per match.
        std::vector<std::string> parents(segs.begin(), segs.end() - 1);
        expand(fs, spec, parents, 0, "", captured, matches);
        if (matches.empty()) {
            auto c = probe_node(fs, spec, make_id(spec.id, "*"), spec.path, source, requires_adapter);
            c.supported = false;
            c.note = "no interface matches " + spec.path;
            out.push_back(std::move(c));
            continue;
        }
        for (const auto &m : matches)
            out.push_back(probe_node(fs, spec, make_id(spec.id, join(m.captured, ".")),
                                     m.path + "/" + segs.back(), source, requires_adapter));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Adapters (interface names only — no values, no policy)
// ---------------------------------------------------------------------------

namespace {

using D = Domain;
using V = ValueKind;
using R = Risk;

const std::string kPolicy = "sys/devices/system/cpu/cpufreq/policy*/";
const std::vector<std::string> kCpuRange = {"cpuinfo_min_freq", "cpuinfo_max_freq"};

class GenericAdapter : public Adapter {
  public:
    std::string name() const override { return "generic"; }
    Confidence match(const PlatformHint &, const ReadOnlyFs &) const override { return Confidence::Low; }
    std::vector<ProbeSpec> specs() const override {
        const std::vector<std::string> pseudo_blk = {"loop", "ram", "zram", "dm-"};
        return {
            {"cpufreq.{}.scaling_max_freq", D::CpuFreq, kPolicy + "scaling_max_freq", V::Integer, R::Medium, true, kCpuRange},
            {"cpufreq.{}.scaling_min_freq", D::CpuFreq, kPolicy + "scaling_min_freq", V::Integer, R::Medium, true, kCpuRange},
            {"cpufreq.{}.scaling_cur_freq", D::CpuFreq, kPolicy + "scaling_cur_freq", V::Integer, R::Low, false, {}, {}, true},
            {"policy.{}.related_cpus", D::CpuPolicy, kPolicy + "related_cpus", V::Text, R::Low, false, {}, {}, true},
            {"policy.{}.affected_cpus", D::CpuPolicy, kPolicy + "affected_cpus", V::Text, R::Low, false, {}, {}, true},
            {"governor.{}", D::Governor, kPolicy + "scaling_governor", V::Text, R::Medium, true, {"scaling_available_governors"}},
            {"uclamp.top-app.min", D::Uclamp, "dev/cpuctl/top-app/cpu.uclamp.min", V::Text, R::Medium, true},
            {"uclamp.top-app.max", D::Uclamp, "dev/cpuctl/top-app/cpu.uclamp.max", V::Text, R::Medium, true},
            {"uclamp.foreground.min", D::Uclamp, "dev/cpuctl/foreground/cpu.uclamp.min", V::Text, R::Medium, true},
            {"uclamp.sched_util_clamp_min", D::Uclamp, "proc/sys/kernel/sched_util_clamp_min", V::Integer, R::Medium, true},
            {"sched.sched_latency_ns", D::Scheduler, "proc/sys/kernel/sched_latency_ns", V::Integer, R::Medium, true},
            {"sched.sched_energy_aware", D::Scheduler, "proc/sys/kernel/sched_energy_aware", V::Integer, R::Medium, true},
            {"sched.sched_schedstats", D::Scheduler, "proc/sys/kernel/sched_schedstats", V::Integer, R::Low, true},
            {"cpuset.top-app.cpus", D::Cpuset, "dev/cpuset/top-app/cpus", V::Text, R::Medium, true},
            {"cpuset.foreground.cpus", D::Cpuset, "dev/cpuset/foreground/cpus", V::Text, R::Medium, true},
            {"cpuset.background.cpus", D::Cpuset, "dev/cpuset/background/cpus", V::Text, R::Medium, true},
            {"cpuset.system-background.cpus", D::Cpuset, "dev/cpuset/system-background/cpus", V::Text, R::Medium, true},
            {"cgroup.v1_controllers", D::Cgroup, "proc/cgroups", V::Info, R::Low, false},
            {"cgroup.v2_controllers", D::Cgroup, "sys/fs/cgroup/cgroup.controllers", V::Text, R::Low, false, {}, {}, true},
            {"devfreq.{}.governor", D::DevFreq, "sys/class/devfreq/*/governor", V::Text, R::Medium, true, {"available_governors"}},
            {"devfreq.{}.max_freq", D::DevFreq, "sys/class/devfreq/*/max_freq", V::Integer, R::Medium, true, {"available_frequencies"}},
            {"devfreq.{}.cur_freq", D::DevFreq, "sys/class/devfreq/*/cur_freq", V::Integer, R::Low, false, {}, {}, true},
            {"gpu.devfreq.{}.cur_freq", D::Gpu, "sys/class/devfreq/*gpu*/cur_freq", V::Integer, R::Low, false, {}, {}, true},
            {"gpu.devfreq.{}.max_freq", D::Gpu, "sys/class/devfreq/*gpu*/max_freq", V::Integer, R::Medium, true, {"available_frequencies"}},
            {"gpu.mali.{}.cur_freq", D::Gpu, "sys/class/devfreq/*mali*/cur_freq", V::Integer, R::Low, false, {}, {}, true},
            {"thermal.{}.type", D::Thermal, "sys/class/thermal/thermal_zone*/type", V::Text, R::High, false, {}, {}, true},
            {"thermal.{}.temp", D::Thermal, "sys/class/thermal/thermal_zone*/temp", V::Integer, R::High, false, {}, {}, true},
            {"thermal.{}.mode", D::Thermal, "sys/class/thermal/thermal_zone*/mode", V::Text, R::High, false, {}, {}, true},
            {"zram.{}.disksize", D::Zram, "sys/block/zram*/disksize", V::Integer, R::Medium, false},
            {"zram.{}.comp_algorithm", D::Zram, "sys/block/zram*/comp_algorithm", V::Selector, R::Medium, false},
            {"swap.swaps", D::Swap, "proc/swaps", V::Info, R::Low, false},
            {"swap.swappiness", D::Swap, "proc/sys/vm/swappiness", V::Integer, R::Low, true},
            {"io.{}.scheduler", D::IoScheduler, "sys/block/*/queue/scheduler", V::Selector, R::Low, true, {}, pseudo_blk},
            {"io.{}.read_ahead_kb", D::IoScheduler, "sys/block/*/queue/read_ahead_kb", V::Integer, R::Low, true, {}, pseudo_blk},
            {"input_boost.cpu_input_boost.freq", D::InputBoost, "sys/module/cpu_input_boost/parameters/input_boost_freq", V::Text, R::Medium, true},
            {"display.{}.modes", D::DisplayRefresh, "sys/class/drm/card*-*/modes", V::Info, R::Low, false},
            {"display.fb0.modes", D::DisplayRefresh, "sys/class/graphics/fb0/modes", V::Info, R::Low, false},
        };
    }
};

bool full_match(const std::string &s, const char *pattern) {
    return !s.empty() && std::regex_match(lower(s), std::regex(pattern));
}

Confidence combine(bool by_property, bool by_interface) {
    if (by_property && by_interface) return Confidence::High;
    if (by_property || by_interface) return Confidence::Medium;
    return Confidence::None;
}

class QualcommAdapter : public Adapter {
  public:
    std::string name() const override { return "qualcomm"; }
    Confidence match(const PlatformHint &h, const ReadOnlyFs &fs) const override {
        const bool prop = lower(h.hardware) == "qcom" || full_match(h.soc_manufacturer, "qti|qualcomm") ||
                          full_match(h.board_platform, "(msm|sdm|sm|qcs|sdm)\\d{3,4}[a-z]?");
        const bool iface = fs.kind("sys/class/kgsl/kgsl-3d0") != ReadOnlyFs::Kind::Missing;
        return combine(prop, iface);
    }
    std::vector<ProbeSpec> specs() const override {
        const std::string k = "sys/class/kgsl/kgsl-3d0/";
        return {
            {"gpu.kgsl.gpu_model", D::Gpu, k + "gpu_model", V::Text, R::Low, false, {}, {}, true},
            {"gpu.kgsl.max_gpuclk", D::Gpu, k + "max_gpuclk", V::Integer, R::Medium, true, {"gpu_available_frequencies"}},
            {"gpu.kgsl.devfreq_governor", D::Gpu, k + "devfreq/governor", V::Text, R::Medium, true, {"available_governors"}},
            {"gpu.kgsl.gpubusy", D::Gpu, k + "gpubusy", V::Text, R::Low, false, {}, {}, true},
            {"input_boost.cpu_boost.freq", D::InputBoost, "sys/module/cpu_boost/parameters/input_boost_freq", V::Text, R::Medium, true},
            {"input_boost.cpu_boost.ms", D::InputBoost, "sys/module/cpu_boost/parameters/input_boost_ms", V::Integer, R::Medium, true},
        };
    }
};

class MediaTekAdapter : public Adapter {
  public:
    std::string name() const override { return "mediatek"; }
    Confidence match(const PlatformHint &h, const ReadOnlyFs &fs) const override {
        const bool prop = full_match(h.board_platform, "mt\\d{4}[a-z]?") || full_match(h.hardware, "mt\\d{4}[a-z]?") ||
                          full_match(h.soc_manufacturer, "mediatek|mtk");
        const bool iface = fs.kind("proc/gpufreqv2") != ReadOnlyFs::Kind::Missing ||
                           fs.kind("proc/gpufreq") != ReadOnlyFs::Kind::Missing;
        return combine(prop, iface);
    }
    std::vector<ProbeSpec> specs() const override {
        return {
            {"gpu.gpufreqv2.opp_table", D::Gpu, "proc/gpufreqv2/gpu_working_opp_table", V::Info, R::Low, false},
            {"gpu.gpufreq.opp_dump", D::Gpu, "proc/gpufreq/gpufreq_opp_dump", V::Info, R::Low, false},
            {"gpu.ged.gpu_utilization", D::Gpu, "sys/kernel/ged/hal/gpu_utilization", V::Text, R::Low, false, {}, {}, true},
            {"gpu.ged.current_freq", D::Gpu, "sys/kernel/ged/hal/current_freqency", V::Info, R::Low, false},
        };
    }
};

} // namespace

std::unique_ptr<Adapter> make_generic_adapter() { return std::make_unique<GenericAdapter>(); }
std::unique_ptr<Adapter> make_qualcomm_adapter() { return std::make_unique<QualcommAdapter>(); }
std::unique_ptr<Adapter> make_mediatek_adapter() { return std::make_unique<MediaTekAdapter>(); }

AdapterRegistry AdapterRegistry::with_builtin() {
    AdapterRegistry r;
    r.generic_ = make_generic_adapter();
    r.add(make_qualcomm_adapter());
    r.add(make_mediatek_adapter());
    return r;
}

void AdapterRegistry::add(std::unique_ptr<Adapter> adapter) {
    if (adapter) vendors_.push_back(std::move(adapter));
}

AdapterRegistry::Selection AdapterRegistry::select(const PlatformHint &hint, const ReadOnlyFs &fs) const {
    Selection best;
    for (const auto &v : vendors_) {
        auto c = v->match(hint, fs);
        if (c > best.confidence) best = {v.get(), c};
    }
    return best;
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

const Capability *KernelReport::find(const std::string &id) const {
    for (const auto &c : capabilities)
        if (c.id == id) return &c;
    return nullptr;
}

std::vector<const Capability *> KernelReport::in(Domain d) const {
    std::vector<const Capability *> out;
    for (const auto &c : capabilities)
        if (c.domain == d) out.push_back(&c);
    return out;
}

KernelReport observe(const ReadOnlyFs &fs, const PlatformHint &hint, const AdapterRegistry &registry) {
    KernelReport r;
    r.identity = classify({fs.read("proc/sys/kernel/osrelease").value_or(""),
                           fs.read("proc/version").value_or("")});
    r.capabilities = probe(registry.generic().specs(), "generic", false, fs);
    auto sel = registry.select(hint, fs);
    if (sel.vendor) {
        r.adapter = sel.vendor->name();
        r.adapter_confidence = sel.confidence;
        auto extra = probe(sel.vendor->specs(), r.adapter, true, fs);
        r.capabilities.insert(r.capabilities.end(), extra.begin(), extra.end());
    }
    return r;
}

} // namespace flux::kernel

// ---------------------------------------------------------------------------
// Capability context export (Step 7.5)
// ---------------------------------------------------------------------------

namespace flux::kernel {

namespace {

namespace cx = flux::context;

cx::Confidence map(Confidence c) { return static_cast<cx::Confidence>(static_cast<int>(c)); }

cx::Risk map(Risk r) {
    switch (r) {
    case Risk::Low: return cx::Risk::Low;
    case Risk::Medium: return cx::Risk::Medium;
    case Risk::High: return cx::Risk::High;
    }
    return cx::Risk::Unknown;
}

cx::CapabilityFact identity_fact(const std::string &id, const std::string &value, bool known, Confidence c,
                                 const std::string &note) {
    cx::CapabilityFact f;
    f.id = id;
    f.domain = "kernel";
    f.source = "classifier";
    f.support = known ? cx::Support::Yes : cx::Support::Unknown;
    f.readable = known;
    f.confidence = map(c);
    f.risk = cx::Risk::Unknown;
    f.value = value;
    f.note = note;
    return f;
}

} // namespace

std::vector<cx::CapabilityFact> export_facts(const KernelReport &report) {
    std::vector<cx::CapabilityFact> out;
    out.reserve(report.capabilities.size() + 3);
    for (const auto &c : report.capabilities) {
        cx::CapabilityFact f;
        f.id = c.id;
        f.domain = to_string(c.domain);
        f.source = c.source;
        f.support = c.supported ? cx::Support::Yes : cx::Support::No;
        f.readable = c.readable;
        f.writable = c.writable;
        f.verified = c.verified;
        f.confidence = map(c.confidence);
        f.risk = map(c.risk);
        f.rollback = c.rollback;
        f.requires_adapter = c.requires_adapter;
        f.interface = c.interface;
        f.value = c.value;
        f.range = c.range;
        f.note = c.note;
        out.push_back(std::move(f));
    }
    const auto &id = report.identity;
    out.push_back(identity_fact("kernel.integration", to_string(id.integration),
                                id.integration != Integration::Unknown, id.integration_confidence,
                                id.integration_reason));
    out.push_back(identity_fact("kernel.generation", to_string(id.generation), id.generation != Generation::Unknown,
                                id.generation_confidence, id.generation_reason));
    auto adapter = identity_fact("kernel.adapter", report.adapter, true,
                                 report.adapter == "generic" ? Confidence::High : report.adapter_confidence,
                                 "vendor adapter selection");
    adapter.source = "adapter_registry";
    out.push_back(std::move(adapter));
    return out;
}

void publish(const KernelReport &report, cx::CapabilityContext &context) {
    context.publish(kContextPublisher, export_facts(report));
}

} // namespace flux::kernel
