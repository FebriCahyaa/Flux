// Zairenkai Kernel Intelligence foundation (Step 7) — observation only.
//
// Answers: what kernel is running, which interfaces exist, what can be read, what the
// permissions suggest could be written, and which interfaces need a vendor adapter.
//
// Nothing in this module writes. The filesystem seam (`ReadOnlyFs`) has no write
// operation at all; "writable" is a permission hint and `verified` is always false
// until a later phase proves a write + read-back through the Transaction Engine.
// No policy, no tweaks, no thermal control, no device-specific cases: vendor
// knowledge lives only in adapters that name interfaces.
#pragma once

#include "CapabilityContext.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flux::kernel {

enum class Confidence { None, Low, Medium, High };
enum class Integration { Unknown, GKI, NonGKI };
enum class Generation { Unknown, Legacy, NonLegacy };

const char *to_string(Confidence c);
const char *to_string(Integration i);
const char *to_string(Generation g);

// ---------------------------------------------------------------------------
// Identity and classification
// ---------------------------------------------------------------------------

struct KernelVersion {
    int major = 0, minor = 0, patch = 0;
    bool valid = false;
    int code() const { return major * 100 + minor; } // 5.10 -> 510
};

/// Raw evidence, injected so classification is testable without a device.
struct KernelEvidence {
    std::string release;      // uname -r / /proc/sys/kernel/osrelease
    std::string proc_version; // /proc/version (fallback source of the release)
};

struct KernelIdentity {
    std::string release;
    KernelVersion version;
    int android_release = 0; // N from "-androidN-", 0 when absent
    std::string kmi;         // "android12-5.10" when both parts are known

    Integration integration = Integration::Unknown;
    Confidence integration_confidence = Confidence::None;
    std::string integration_reason;

    Generation generation = Generation::Unknown;
    Confidence generation_confidence = Confidence::None;
    std::string generation_reason;
};

KernelVersion parse_version(const std::string &release);

/// Two independent axes. GKI needs two agreeing signals (android tag >= 12 and kernel
/// >= 5.10); a single signal never yields a High GKI claim.
KernelIdentity classify(const KernelEvidence &evidence);

/// Legacy boundary: kernels older than 4.19 (the boundary the profiler already uses).
inline constexpr int kLegacyBelow = 419;
/// First kernel line that can be GKI 2.0.
inline constexpr int kGkiMinKernel = 510;
inline constexpr int kGkiMinAndroid = 12;

// ---------------------------------------------------------------------------
// Read-only filesystem seam
// ---------------------------------------------------------------------------

class ReadOnlyFs {
  public:
    virtual ~ReadOnlyFs() = default;
    enum class Kind { Missing, File, Directory, Other };
    virtual Kind kind(const std::string &path) const = 0;
    /// Content of a regular file, nullopt when unreadable.
    virtual std::optional<std::string> read(const std::string &path) const = 0;
    /// Entry names of a directory (sorted), empty when missing/unreadable.
    virtual std::vector<std::string> list(const std::string &dir) const = 0;
    /// Permission hint only (access(W_OK)); never opens for writing.
    virtual bool writable_hint(const std::string &path) const = 0;
};

/// Real filesystem below `root` ("" = "/"). Paths passed in are relative ("sys/...").
std::unique_ptr<ReadOnlyFs> make_readonly_fs(const std::string &root = "");

// ---------------------------------------------------------------------------
// Capability model
// ---------------------------------------------------------------------------

enum class Domain {
    CpuFreq, CpuPolicy, Governor, Uclamp, Scheduler, Cpuset, Cgroup, DevFreq,
    Gpu, Thermal, Zram, Swap, IoScheduler, InputBoost, DisplayRefresh,
};
const char *to_string(Domain d);
inline constexpr int kDomainCount = 15;

enum class Risk { Low, Medium, High };
const char *to_string(Risk r);

/// How a node's content is interpreted and validated.
enum class ValueKind {
    Integer,  // one integer
    Text,     // one line of printable text
    Selector, // "a [b] c" — the bracketed item is the value, the list is the range
    Info,     // multi-line informational content (never a write target)
};

struct Capability {
    std::string id;
    Domain domain = Domain::CpuFreq;
    bool supported = false; // interface exists
    bool readable = false;  // content read and valid for its kind
    bool writable = false;  // permission hint only
    bool verified = false;  // always false in Step 7 (no write + read-back yet)
    std::string interface;  // path relative to the root
    std::string source;     // adapter that declared it: generic / qualcomm / mediatek / ...
    std::string value;
    std::string range;
    Confidence confidence = Confidence::None;
    Risk risk = Risk::Low;
    bool rollback = false; // a write could be restored by writing the read value back
    bool requires_adapter = false;
    std::string note; // why unsupported / unreadable / invalid
};

/// One interface an adapter knows about. `path` may contain '*' inside a segment;
/// every match becomes its own capability and `{}` in `id` is replaced by the
/// matched segment(s) joined with '.'.
struct ProbeSpec {
    std::string id;
    Domain domain;
    std::string path;
    ValueKind kind;
    Risk risk;
    bool rollback;
    std::vector<std::string> range_siblings = {}; // 1 = list file, 2 = min..max
    std::vector<std::string> exclude = {};        // prefixes of matched segments to skip
    bool observe_only = false;                    // writable never reported
};

// ---------------------------------------------------------------------------
// Adapters
// ---------------------------------------------------------------------------

struct PlatformHint {
    std::string board_platform; // ro.board.platform
    std::string hardware;       // ro.hardware
    std::string soc_manufacturer; // ro.soc.manufacturer
};

class Adapter {
  public:
    virtual ~Adapter() = default;
    virtual std::string name() const = 0;
    /// How strongly the device evidence says this adapter applies. None = not at all.
    virtual Confidence match(const PlatformHint &hint, const ReadOnlyFs &fs) const = 0;
    virtual std::vector<ProbeSpec> specs() const = 0;
};

std::unique_ptr<Adapter> make_generic_adapter();
std::unique_ptr<Adapter> make_qualcomm_adapter();
std::unique_ptr<Adapter> make_mediatek_adapter();

/// Generic is always applied; the best-matching vendor adapter (if any) adds its specs.
/// Further vendors register here — no device-specific code paths anywhere else.
class AdapterRegistry {
  public:
    static AdapterRegistry with_builtin();
    void add(std::unique_ptr<Adapter> adapter);
    const Adapter &generic() const { return *generic_; }
    struct Selection {
        const Adapter *vendor = nullptr; // nullptr = generic only
        Confidence confidence = Confidence::None;
    };
    Selection select(const PlatformHint &hint, const ReadOnlyFs &fs) const;

  private:
    std::unique_ptr<Adapter> generic_;
    std::vector<std::unique_ptr<Adapter>> vendors_;
};

// ---------------------------------------------------------------------------
// Probing
// ---------------------------------------------------------------------------

/// Probes every spec. Missing interfaces become supported=false records (a missing
/// glob becomes one record with '*' in its id), so absence is reported, not hidden.
std::vector<Capability> probe(const std::vector<ProbeSpec> &specs, const std::string &source,
                              bool requires_adapter, const ReadOnlyFs &fs);

struct KernelReport {
    KernelIdentity identity;
    std::string adapter = "generic";
    Confidence adapter_confidence = Confidence::None;
    std::vector<Capability> capabilities;

    const Capability *find(const std::string &id) const;
    std::vector<const Capability *> in(Domain d) const;
};

/// Reads the kernel release from the fs (proc/sys/kernel/osrelease, proc/version),
/// classifies, selects adapters and probes. Read-only end to end.
KernelReport observe(const ReadOnlyFs &fs, const PlatformHint &hint,
                     const AdapterRegistry &registry);

// ---------------------------------------------------------------------------
// Capability context export (Step 7.5)
// ---------------------------------------------------------------------------

/// Publisher name used for kernel facts in the shared capability context.
inline constexpr const char *kContextPublisher = "kernel";

/// Kernel report -> context facts, one per capability plus kernel.integration,
/// kernel.generation and kernel.adapter. Mapping is 1:1: supported=false becomes No (an
/// observed absence), an Unknown axis stays Unknown, and readable/writable/verified/risk/
/// confidence/source are copied, never upgraded.
std::vector<flux::context::CapabilityFact> export_facts(const KernelReport &report);

/// Convenience: export_facts + publish under kContextPublisher.
void publish(const KernelReport &report, flux::context::CapabilityContext &context);

} // namespace flux::kernel
