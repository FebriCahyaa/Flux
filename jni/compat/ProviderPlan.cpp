#include "ProviderPlan.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::compat::provider {

namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

const char *layer_name(Layer l) { return to_string(l); }

std::optional<Layer> layer_from(const std::string &s) {
    if (s == "device") return Layer::Device;
    if (s == "cpu") return Layer::Cpu;
    if (s == "gpu") return Layer::Gpu;
    return std::nullopt;
}

bool printable_value(const std::string &v) {
    if (v.empty() || v.size() > 128) return false;
    for (unsigned char c : v)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool parse_u32(const std::string &s, uint32_t &out) {
    if (s.empty() || s.size() > 12) return false;
    char *end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 0); // 0x.. hex or decimal
    if (end == s.c_str() || *end != '\0' || v > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

uint64_t fnv1a(const std::string &s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

void put_str(Writer &w, const char *k, const std::string &v) {
    w.Key(k);
    w.String(v.c_str());
}

void put_map(Writer &w, const char *k, const std::map<std::string, std::string> &m) {
    w.Key(k);
    w.StartObject();
    for (const auto &[a, b] : m) put_str(w, a.c_str(), b);
    w.EndObject();
}

std::string get_str(const rapidjson::Value &o, const char *k) {
    return (o.IsObject() && o.HasMember(k) && o[k].IsString()) ? o[k].GetString() : "";
}
int64_t get_int(const rapidjson::Value &o, const char *k) {
    return (o.IsObject() && o.HasMember(k) && o[k].IsNumber()) ? o[k].GetInt64() : 0;
}

std::map<std::string, std::string> get_map(const rapidjson::Value &o, const char *k) {
    std::map<std::string, std::string> m;
    if (o.IsObject() && o.HasMember(k) && o[k].IsObject())
        for (auto it = o[k].MemberBegin(); it != o[k].MemberEnd(); ++it)
            if (it->value.IsString()) m[it->name.GetString()] = it->value.GetString();
    return m;
}

const std::map<std::string, std::string> &props_for_build() {
    static const std::map<std::string, std::string> m = {
        {"MODEL", "ro.product.model"},           {"BRAND", "ro.product.brand"},
        {"MANUFACTURER", "ro.product.manufacturer"}, {"DEVICE", "ro.product.device"},
        {"PRODUCT", "ro.product.name"},          {"FINGERPRINT", "ro.build.fingerprint"},
        {"SOC_MODEL", "ro.soc.model"},           {"SOC_MANUFACTURER", "ro.soc.manufacturer"},
        {"HARDWARE", "ro.hardware"},             {"BOARD", "ro.product.board"}};
    return m;
}

} // namespace

const std::vector<std::string> &supported_fields(Layer layer) {
    static const std::vector<std::string> device = {"BRAND", "MANUFACTURER", "MODEL", "DEVICE", "PRODUCT", "FINGERPRINT"};
    static const std::vector<std::string> cpu = {"SOC_MODEL", "SOC_MANUFACTURER", "HARDWARE", "BOARD"};
    static const std::vector<std::string> gpu = {"gl_vendor",      "gl_renderer",    "gl_version",
                                                 "egl_vendor",     "vk_device_name", "vk_vendor_id",
                                                 "vk_device_id",   "vk_api_version", "vk_driver_version"};
    static const std::vector<std::string> none;
    switch (layer) {
    case Layer::Device: return device;
    case Layer::Cpu: return cpu;
    case Layer::Gpu: return gpu;
    case Layer::Display: return none;
    }
    return none;
}

std::optional<Layer> layer_of_field(const std::string &field) {
    for (Layer l : {Layer::Device, Layer::Cpu, Layer::Gpu}) {
        const auto &f = supported_fields(l);
        if (std::find(f.begin(), f.end(), field) != f.end()) return l;
    }
    return std::nullopt;
}

const char *to_string(Reject r) {
    switch (r) {
    case Reject::None: return "none";
    case Reject::InvalidJson: return "invalid_json";
    case Reject::WrongVersion: return "wrong_version";
    case Reject::Inactive: return "inactive";
    case Reject::NoTransaction: return "no_transaction";
    case Reject::TransactionMismatch: return "transaction_mismatch";
    case Reject::BootMismatch: return "boot_mismatch";
    case Reject::DaemonGone: return "daemon_gone";
    case Reject::Expired: return "expired";
    case Reject::PackageMismatch: return "package_mismatch";
    case Reject::ProcessNotInScope: return "process_not_in_scope";
    case Reject::NoLayers: return "no_layers";
    case Reject::UnsupportedField: return "unsupported_field";
    case Reject::InvalidValue: return "invalid_value";
    case Reject::CapabilityClaim: return "capability_claim";
    }
    return "none";
}

const char *to_string(ProcState s) {
    switch (s) {
    case ProcState::Matched: return "matched";
    case ProcState::Applied: return "applied";
    case ProcState::Verified: return "verified";
    case ProcState::Failed: return "failed";
    case ProcState::Ended: return "ended";
    }
    return "failed";
}

// -- plan ----------------------------------------------------------------------------------------

Plan make_plan(const Resolution &res, const ProfileLibrary &lib, const std::string &boot_id, int64_t daemon_pid,
               int64_t now_ms, int64_t lease_ms, const ProcessScope &scope, const std::string &profile_name) {
    Plan p;
    p.version = kPlanVersion;
    p.package = res.package;
    p.boot_id = boot_id;
    p.daemon_pid = daemon_pid;
    p.armed_at_ms = now_ms;
    p.expires_at_ms = lease_ms > 0 ? now_ms + lease_ms : 0;
    p.profile = profile_name;
    p.mode = to_string(res.mode);
    p.scope = scope;
    for (const auto &d : res.layers) {
        if (!d.required || d.layer == Layer::Display) continue;
        auto it = lib.identities.find(d.identity);
        if (it == lib.identities.end()) continue;
        p.layers.push_back(d.layer);
        p.identities[layer_name(d.layer)] = it->second.fields;
    }
    p.active = !p.layers.empty();
    if (p.active) p.transaction_id = compute_transaction_id(p);
    return p;
}

std::string compute_transaction_id(const Plan &p) {
    std::string canon = p.package + "|" + p.boot_id + "|" + p.mode + "|" + std::to_string(static_cast<int>(p.scope.kind));
    for (const auto &s : p.scope.processes) canon += "|p:" + s;
    for (Layer l : p.layers) {
        canon += std::string("|L:") + layer_name(l);
        auto it = p.identities.find(layer_name(l));
        if (it != p.identities.end())
            for (const auto &[k, v] : it->second) canon += "|" + k + "=" + v;
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.6s-%016llx", p.boot_id.c_str(), static_cast<unsigned long long>(fnv1a(canon)));
    return buf;
}

std::string plan_to_json(const Plan &p) {
    rapidjson::StringBuffer sb;
    Writer w(sb);
    w.StartObject();
    w.Key("version"); w.Int(p.version);
    w.Key("active"); w.Bool(p.active);
    put_str(w, "package", p.package);
    put_str(w, "transaction_id", p.transaction_id);
    put_str(w, "boot_id", p.boot_id);
    w.Key("daemon_pid"); w.Int64(p.daemon_pid);
    w.Key("armed_at_ms"); w.Int64(p.armed_at_ms);
    w.Key("expires_at_ms"); w.Int64(p.expires_at_ms);
    put_str(w, "profile", p.profile);
    put_str(w, "mode", p.mode);
    w.Key("process_scope");
    w.StartObject();
    put_str(w, "mode", p.scope.kind == ProcessScope::Kind::Main ? "main" : p.scope.kind == ProcessScope::Kind::All ? "all" : "listed");
    w.Key("processes");
    w.StartArray();
    for (const auto &s : p.scope.processes) w.String(s.c_str());
    w.EndArray();
    w.EndObject();
    w.Key("layers");
    w.StartArray();
    for (Layer l : p.layers) w.String(layer_name(l));
    w.EndArray();
    w.Key("identities");
    w.StartObject();
    for (const auto &[layer, fields] : p.identities) put_map(w, layer.c_str(), fields);
    w.EndObject();
    w.EndObject();
    return sb.GetString();
}

bool plan_from_json(const std::string &json, Plan &out, std::string &error) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError() || !d.IsObject()) {
        error = "invalid JSON";
        return false;
    }
    Plan p;
    p.version = static_cast<int>(get_int(d, "version"));
    p.active = d.HasMember("active") && d["active"].IsBool() && d["active"].GetBool();
    p.package = get_str(d, "package");
    p.transaction_id = get_str(d, "transaction_id");
    p.boot_id = get_str(d, "boot_id");
    p.daemon_pid = get_int(d, "daemon_pid");
    p.armed_at_ms = get_int(d, "armed_at_ms");
    p.expires_at_ms = get_int(d, "expires_at_ms");
    p.profile = get_str(d, "profile");
    p.mode = get_str(d, "mode");
    if (d.HasMember("process_scope") && d["process_scope"].IsObject()) {
        const auto &s = d["process_scope"];
        std::string m = get_str(s, "mode");
        p.scope.kind = m == "all" ? ProcessScope::Kind::All : m == "listed" ? ProcessScope::Kind::Listed : ProcessScope::Kind::Main;
        if (s.HasMember("processes") && s["processes"].IsArray())
            for (const auto &v : s["processes"].GetArray())
                if (v.IsString()) p.scope.processes.push_back(v.GetString());
    }
    if (d.HasMember("layers") && d["layers"].IsArray())
        for (const auto &v : d["layers"].GetArray())
            if (v.IsString())
                if (auto l = layer_from(v.GetString())) p.layers.push_back(*l);
    if (d.HasMember("identities") && d["identities"].IsObject())
        for (auto it = d["identities"].MemberBegin(); it != d["identities"].MemberEnd(); ++it) {
            std::map<std::string, std::string> f;
            if (it->value.IsObject())
                for (auto m = it->value.MemberBegin(); m != it->value.MemberEnd(); ++m)
                    if (m->value.IsString()) f[m->name.GetString()] = m->value.GetString();
            p.identities[it->name.GetString()] = std::move(f);
        }
    out = std::move(p);
    return true;
}

// -- validation ------------------------------------------------------------------------------------

Validation validate_plan(const Plan &p, const Env &env) {
    Validation v;
    auto fail = [&](Reject r, std::string d) {
        v.reject = r;
        v.detail = std::move(d);
        return v;
    };
    if (p.version != kPlanVersion) return fail(Reject::WrongVersion, "plan version " + std::to_string(p.version));
    if (!p.active) return fail(Reject::Inactive, "plan is not active");
    if (p.transaction_id.empty()) return fail(Reject::NoTransaction, "no transaction id");
    if (p.package.empty() || p.package.find('/') != std::string::npos || p.package.find("..") != std::string::npos)
        return fail(Reject::PackageMismatch, "invalid package name");
    if (p.transaction_id != compute_transaction_id(p))
        return fail(Reject::TransactionMismatch, "transaction id does not match the plan contents");
    if (p.boot_id.empty() || p.boot_id != env.boot_id) return fail(Reject::BootMismatch, "plan was armed in another boot");
    if (!env.daemon_alive) return fail(Reject::DaemonGone, "the daemon that armed this plan is not running");
    if (p.expires_at_ms > 0 && env.now_ms > p.expires_at_ms) return fail(Reject::Expired, "lease expired");
    if (p.layers.empty()) return fail(Reject::NoLayers, "plan lists no layers");
    for (const auto &s : p.scope.processes)
        if (!printable_value(s)) return fail(Reject::InvalidValue, "invalid process name in scope");
    if (p.scope.kind == ProcessScope::Kind::Listed && p.scope.processes.empty())
        return fail(Reject::ProcessNotInScope, "scope is 'listed' but no process is listed");

    for (Layer l : p.layers) {
        LayerVerdict lv{l, false, ""};
        auto it = p.identities.find(layer_name(l));
        if (l == Layer::Display) {
            lv.reason = "display is not an identity layer";
        } else if (it == p.identities.end() || it->second.empty()) {
            lv.reason = "no identity fields";
        } else {
            IdentityProfile id;
            id.name = "plan";
            id.layer = l;
            id.fields = it->second;
            std::string err;
            const auto &ok_fields = supported_fields(l);
            bool bad = false;
            if (!validate_identity(id, err)) {
                lv.reason = "capability claim: " + err; // ISA features, Vulkan extensions/features, ...
                bad = true;
            }
            for (const auto &[k, val] : it->second) {
                if (bad) break;
                if (std::find(ok_fields.begin(), ok_fields.end(), k) == ok_fields.end()) {
                    lv.reason = "unsupported field '" + k + "'";
                    bad = true;
                } else if (!printable_value(val)) {
                    lv.reason = "invalid value for '" + k + "'";
                    bad = true;
                } else if ((k == "vk_vendor_id" || k == "vk_device_id" || k == "vk_driver_version")) {
                    uint32_t tmp;
                    if (!parse_u32(val, tmp)) { lv.reason = "'" + k + "' is not a 32-bit number"; bad = true; }
                } else if (k == "vk_api_version" && !parse_vk_api_version(val)) {
                    lv.reason = "'vk_api_version' must look like 1.1 or 1.3.0";
                    bad = true;
                }
            }
            lv.usable = !bad;
        }
        v.layers.push_back(lv);
    }
    bool any = false;
    for (const auto &lv : v.layers) any = any || lv.usable;
    if (!any) {
        // Every requested layer was refused: name the first precise reason.
        v.reject = Reject::UnsupportedField;
        v.detail = v.layers.empty() ? "no layers" : v.layers.front().reason;
        if (v.detail.rfind("capability claim", 0) == 0) v.reject = Reject::CapabilityClaim;
    }
    return v;
}

// -- matching ---------------------------------------------------------------------------------------

Match match_process(const Plan &p, const ProcessInfo &proc) {
    Match m;
    if (proc.package.empty() || proc.package != p.package) {
        m.reject = Reject::PackageMismatch;
        m.detail = proc.package.empty() ? "uid owner unknown" : "uid belongs to " + proc.package;
        return m;
    }
    const std::string &n = proc.nice_name;
    bool in = false;
    switch (p.scope.kind) {
    case ProcessScope::Kind::Main: in = n == p.package; break;
    case ProcessScope::Kind::All: in = n == p.package || n.rfind(p.package + ":", 0) == 0; break;
    case ProcessScope::Kind::Listed: in = std::find(p.scope.processes.begin(), p.scope.processes.end(), n) != p.scope.processes.end(); break;
    }
    if (!in) {
        m.reject = Reject::ProcessNotInScope;
        m.detail = "process '" + n + "' is not in the plan's scope";
        return m;
    }
    m.matched = true;
    return m;
}

// -- items ------------------------------------------------------------------------------------------------

std::optional<uint32_t> parse_vk_api_version(const std::string &s) {
    unsigned a = 0, b = 0, c = 0;
    char tail = 0;
    int n = std::sscanf(s.c_str(), "%u.%u.%u%c", &a, &b, &c, &tail);
    if (n == 3) { /* major.minor.patch */ }
    else if (std::sscanf(s.c_str(), "%u.%u%c", &a, &b, &tail) == 2) { c = 0; }
    else return std::nullopt;
    if (a > 7 || b > 1023 || c > 4095) return std::nullopt;
    return (a << 22) | (b << 12) | c;
}

Items items_from_plan(const Plan &p, const Validation &v) {
    Items it;
    for (const auto &lv : v.layers) {
        if (!lv.usable) continue;
        const auto &fields = p.identities.at(layer_name(lv.layer));
        for (const auto &[k, val] : fields) {
            if (lv.layer == Layer::Device || lv.layer == Layer::Cpu) {
                it.build[k] = val;
                auto pk = props_for_build().find(k);
                if (pk != props_for_build().end()) it.props[pk->second] = val;
            } else if (lv.layer == Layer::Gpu) {
                uint32_t n = 0;
                if (k == "gl_vendor") it.gl["vendor"] = val;
                else if (k == "gl_renderer") it.gl["renderer"] = val;
                else if (k == "gl_version") it.gl["version"] = val;
                else if (k == "egl_vendor") it.gl["egl_vendor"] = val;
                else if (k == "vk_device_name") it.vk.device_name = val;
                else if (k == "vk_vendor_id" && parse_u32(val, n)) it.vk.vendor_id = n;
                else if (k == "vk_device_id" && parse_u32(val, n)) it.vk.device_id = n;
                else if (k == "vk_driver_version" && parse_u32(val, n)) it.vk.driver_version = n;
                else if (k == "vk_api_version") it.vk.api_version = parse_vk_api_version(val);
            }
        }
    }
    return it;
}

// -- wire ----------------------------------------------------------------------------------------------------

static void write_items(Writer &w, const Items &i) {
    w.StartObject();
    put_map(w, "build", i.build);
    put_map(w, "props", i.props);
    put_map(w, "gl", i.gl);
    w.Key("vk");
    w.StartObject();
    put_str(w, "device_name", i.vk.device_name);
    if (i.vk.vendor_id) { w.Key("vendor_id"); w.Uint64(*i.vk.vendor_id); }
    if (i.vk.device_id) { w.Key("device_id"); w.Uint64(*i.vk.device_id); }
    if (i.vk.api_version) { w.Key("api_version"); w.Uint64(*i.vk.api_version); }
    if (i.vk.driver_version) { w.Key("driver_version"); w.Uint64(*i.vk.driver_version); }
    w.EndObject();
    w.EndObject();
}

std::string decision_to_json(const Decision &d) {
    rapidjson::StringBuffer sb;
    Writer w(sb);
    w.StartObject();
    put_str(w, "result", d.result == Decision::Result::Target ? "target" : d.result == Decision::Result::Rejected ? "rejected" : "not_target");
    put_str(w, "reject", to_string(d.reject));
    put_str(w, "detail", d.detail);
    put_str(w, "package", d.package);
    put_str(w, "transaction_id", d.transaction_id);
    put_str(w, "profile", d.profile);
    w.Key("layers");
    w.StartArray();
    for (const auto &l : d.layers) {
        w.StartObject();
        put_str(w, "layer", layer_name(l.layer));
        w.Key("usable"); w.Bool(l.usable);
        put_str(w, "reason", l.reason);
        w.EndObject();
    }
    w.EndArray();
    w.Key("items");
    write_items(w, d.items);
    w.EndObject();
    return sb.GetString();
}

bool decision_from_json(const std::string &json, Decision &out, std::string &error) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        error = "invalid JSON";
        return false;
    }
    Decision d;
    std::string r = get_str(doc, "result");
    d.result = r == "target" ? Decision::Result::Target : r == "rejected" ? Decision::Result::Rejected : Decision::Result::NotTarget;
    std::string rj = get_str(doc, "reject");
    for (Reject x : {Reject::None, Reject::InvalidJson, Reject::WrongVersion, Reject::Inactive, Reject::NoTransaction,
                     Reject::TransactionMismatch, Reject::BootMismatch, Reject::DaemonGone, Reject::Expired,
                     Reject::PackageMismatch, Reject::ProcessNotInScope, Reject::NoLayers, Reject::UnsupportedField,
                     Reject::InvalidValue, Reject::CapabilityClaim})
        if (rj == to_string(x)) d.reject = x;
    d.detail = get_str(doc, "detail");
    d.package = get_str(doc, "package");
    d.transaction_id = get_str(doc, "transaction_id");
    d.profile = get_str(doc, "profile");
    if (doc.HasMember("layers") && doc["layers"].IsArray())
        for (const auto &l : doc["layers"].GetArray())
            if (auto ly = layer_from(get_str(l, "layer")))
                d.layers.push_back({*ly, l.HasMember("usable") && l["usable"].IsBool() && l["usable"].GetBool(), get_str(l, "reason")});
    if (doc.HasMember("items") && doc["items"].IsObject()) {
        const auto &it = doc["items"];
        d.items.build = get_map(it, "build");
        d.items.props = get_map(it, "props");
        d.items.gl = get_map(it, "gl");
        if (it.HasMember("vk") && it["vk"].IsObject()) {
            const auto &vk = it["vk"];
            d.items.vk.device_name = get_str(vk, "device_name");
            auto u = [&](const char *k) -> std::optional<uint32_t> {
                if (vk.HasMember(k) && vk[k].IsUint64()) return static_cast<uint32_t>(vk[k].GetUint64());
                return std::nullopt;
            };
            d.items.vk.vendor_id = u("vendor_id");
            d.items.vk.device_id = u("device_id");
            d.items.vk.api_version = u("api_version");
            d.items.vk.driver_version = u("driver_version");
        }
    }
    out = std::move(d);
    return true;
}

std::string proc_status_to_json(const ProcStatus &s) {
    rapidjson::StringBuffer sb;
    Writer w(sb);
    w.StartObject();
    put_str(w, "package", s.package);
    put_str(w, "process", s.process);
    put_str(w, "transaction_id", s.transaction_id);
    w.Key("pid"); w.Int64(s.pid);
    w.Key("uid"); w.Int(s.uid);
    put_str(w, "state", to_string(s.state));
    w.Key("updated_ms"); w.Int64(s.updated_ms);
    w.Key("layers");
    w.StartArray();
    for (const auto &l : s.layers) {
        w.StartObject();
        put_str(w, "layer", layer_name(l.layer));
        put_str(w, "state", l.state);
        put_str(w, "detail", l.detail);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    return sb.GetString();
}

bool proc_status_from_json(const std::string &json, ProcStatus &out, std::string &error) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError() || !d.IsObject()) {
        error = "invalid JSON";
        return false;
    }
    ProcStatus s;
    s.package = get_str(d, "package");
    s.process = get_str(d, "process");
    s.transaction_id = get_str(d, "transaction_id");
    s.pid = get_int(d, "pid");
    s.uid = static_cast<int>(get_int(d, "uid"));
    s.updated_ms = get_int(d, "updated_ms");
    std::string st = get_str(d, "state");
    s.state = st == "applied" ? ProcState::Applied : st == "verified" ? ProcState::Verified : st == "failed" ? ProcState::Failed : st == "ended" ? ProcState::Ended : ProcState::Matched;
    if (d.HasMember("layers") && d["layers"].IsArray())
        for (const auto &l : d["layers"].GetArray())
            if (auto ly = layer_from(get_str(l, "layer"))) s.layers.push_back({*ly, get_str(l, "state"), get_str(l, "detail")});
    out = std::move(s);
    return true;
}

std::string provider_info_to_json(const ProviderInfo &p) {
    rapidjson::StringBuffer sb;
    Writer w(sb);
    w.StartObject();
    w.Key("loaded"); w.Bool(p.loaded);
    w.Key("api_version"); w.Int(p.api_version);
    put_str(w, "boot_id", p.boot_id);
    w.Key("companion_pid"); w.Int64(p.companion_pid);
    w.Key("started_ms"); w.Int64(p.started_ms);
    w.EndObject();
    return sb.GetString();
}

bool provider_info_from_json(const std::string &json, ProviderInfo &out, std::string &error) {
    rapidjson::Document d;
    d.Parse(json.c_str());
    if (d.HasParseError() || !d.IsObject()) {
        error = "invalid JSON";
        return false;
    }
    ProviderInfo p;
    p.loaded = d.HasMember("loaded") && d["loaded"].IsBool() && d["loaded"].GetBool();
    p.api_version = static_cast<int>(get_int(d, "api_version"));
    p.boot_id = get_str(d, "boot_id");
    p.companion_pid = get_int(d, "companion_pid");
    p.started_ms = get_int(d, "started_ms");
    out = std::move(p);
    return true;
}

std::string armed_list_to_text(const std::vector<ArmedEntry> &entries) {
    std::string s;
    for (const auto &e : entries) s += e.package + "\t" + std::to_string(e.app_id) + "\t" + e.transaction_id + "\n";
    return s;
}

std::vector<ArmedEntry> armed_list_from_text(const std::string &text) {
    std::vector<ArmedEntry> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find('\t');
        size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (a == std::string::npos || b == std::string::npos || a == 0) continue;
        ArmedEntry e;
        e.package = line.substr(0, a);
        e.app_id = std::atoi(line.substr(a + 1, b - a - 1).c_str());
        e.transaction_id = line.substr(b + 1);
        if (!e.transaction_id.empty()) out.push_back(std::move(e));
    }
    return out;
}

bool armed_candidate(const std::vector<ArmedEntry> &entries, const std::string &nice_name, int uid) {
    const std::string base = nice_name.substr(0, nice_name.find(':'));
    for (const auto &e : entries)
        if ((e.app_id > 0 && e.app_id == uid % 100000) || e.package == base) return true;
    return false;
}

} // namespace flux::compat::provider
