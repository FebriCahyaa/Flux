#include "DisplayIntelligence.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <set>
#include <sstream>

namespace flux::display {

namespace {

namespace cx = flux::context;
using Facts = std::vector<cx::CapabilityFact>;

cx::CapabilityFact fact(const char *domain, const std::string &id, cx::Support support, cx::Confidence confidence,
                        const std::string &value, const std::string &source, const std::string &note) {
    cx::CapabilityFact f;
    f.id = id;
    f.domain = domain;
    f.source = source;
    f.support = support;
    f.readable = support == cx::Support::Yes && !value.empty();
    f.confidence = confidence;
    f.risk = cx::Risk::Low;
    f.value = value;
    f.note = note;
    return f;
}

cx::CapabilityFact unknown(const char *domain, const std::string &id, const std::string &why,
                           cx::Confidence c = cx::Confidence::None) {
    return fact(domain, id, cx::Support::Unknown, c, "", "", why);
}

std::string prop(const DisplayEvidence &e, const char *key) {
    if (!e.property) return "";
    auto v = e.property(key);
    v.erase(v.find_last_not_of(" \t\r\n") + 1);
    return v;
}

int hz(double v) { return static_cast<int>(std::lround(v)); }

std::string join_hz(const std::set<int> &s) {
    std::string out;
    for (int v : s) out += (out.empty() ? "" : " ") + std::to_string(v);
    return out;
}

/// One source's claim; `key` is what must agree (the value, or the support for yes/no facts).
struct Claim {
    std::string source, value, key;
    cx::Support support;
    cx::Confidence confidence;
};

/// Highest confidence answers; equal-confidence disagreement -> Unknown; per-source facts kept.
void resolve_claims(const char *domain, const std::string &id, const std::vector<Claim> &claims, Facts &out,
                    const std::string &none_note, bool per_source) {
    if (per_source)
        for (const auto &c : claims)
            out.push_back(fact(domain, id + "." + c.source, c.support, c.confidence, c.value, c.source, "per-source evidence"));
    if (claims.empty()) {
        out.push_back(unknown(domain, id, none_note));
        return;
    }
    const Claim *best = &claims[0];
    for (const auto &c : claims)
        if (c.confidence > best->confidence) best = &c;
    std::string summary;
    bool tie = false, any = false;
    for (const auto &c : claims) {
        summary += (summary.empty() ? "" : " ") + c.source + "=" + (c.value.empty() ? cx::to_string(c.support) : c.value);
        if (c.key == best->key) continue;
        any = true;
        if (c.confidence == best->confidence) tie = true;
    }
    if (tie) {
        out.push_back(unknown(domain, id, "conflict between equally confident sources: " + summary));
        return;
    }
    out.push_back(fact(domain, id, best->support, best->confidence, best->value, best->source,
                       any ? "conflict (lower-confidence sources disagree): " + summary : "sources: " + summary));
}

std::string hdr_name(int t) {
    switch (t) {
    case 1: return "dolby_vision";
    case 2: return "hdr10";
    case 3: return "hlg";
    case 4: return "hdr10_plus";
    default: return "type" + std::to_string(t);
    }
}

std::vector<int> int_list(const std::string &s) {
    std::vector<int> out;
    std::istringstream in(s);
    std::string tok;
    while (std::getline(in, tok, ',')) {
        tok.erase(0, tok.find_first_not_of(' '));
        if (!tok.empty() && std::all_of(tok.begin(), tok.end(), ::isdigit)) out.push_back(std::stoi(tok));
    }
    return out;
}

std::optional<std::string> first_resolution(const std::string &text) {
    static const std::regex re(R"((\d{3,5})x(\d{3,5}))");
    std::smatch m;
    if (!std::regex_search(text, m, re)) return std::nullopt;
    return m[1].str() + "x" + m[2].str();
}

} // namespace

ParsedDisplay parse_dumpsys_display(const std::string &text) {
    ParsedDisplay p;
    std::istringstream in(text);
    std::string line, chosen;
    while (std::getline(in, line)) {
        if (line.find("DisplayDeviceInfo{") == std::string::npos) continue;
        if (line.find("Built-in") != std::string::npos || line.find("local:") != std::string::npos) {
            chosen = line;
            break;
        }
        if (chosen.empty()) chosen = line;
    }
    if (chosen.empty()) return p;

    static const std::regex active(R"([ ,]modeId (\d+))");
    static const std::regex mode(R"(\{id=(\d+), width=(\d+), height=(\d+), fps=([\d.]+)([^}]*)\})");
    static const std::regex alt(R"(alternativeRefreshRates=\[([^\]]*)\])");
    static const std::regex hdr(R"(mSupportedHdrTypes=\[([^\]]*)\])");
    std::smatch m;
    if (std::regex_search(chosen, m, active)) p.active_mode_id = std::stoi(m[1]);
    for (auto it = std::sregex_iterator(chosen.begin(), chosen.end(), mode); it != std::sregex_iterator(); ++it) {
        DisplayMode dm{std::stoi((*it)[1]), std::stoi((*it)[2]), std::stoi((*it)[3]), std::stod((*it)[4]), {}};
        const std::string rest = (*it)[5];
        std::smatch a;
        if (std::regex_search(rest, a, alt)) {
            std::istringstream ai(a[1].str());
            std::string tok;
            while (std::getline(ai, tok, ','))
                if (!tok.empty()) dm.alternative_hz.push_back(std::atof(tok.c_str()));
        }
        if (dm.refresh_hz >= 1 && dm.refresh_hz <= 1000) p.modes.push_back(dm);
    }
    if (std::regex_search(chosen, m, hdr)) p.hdr_types = int_list(m[1]);
    return p;
}

std::vector<cx::CapabilityFact> observe_display(const DisplayEvidence &e) {
    constexpr const char *D = kDisplayDomain;
    Facts out;
    const ParsedDisplay parsed = e.dumpsys_display ? parse_dumpsys_display(*e.dumpsys_display) : ParsedDisplay{};
    const bool have = !parsed.modes.empty();
    const std::string missing = !e.dumpsys_display ? "dumpsys display not collected" : "dumpsys display unparseable";
    const auto miss_conf = e.dumpsys_display ? cx::Confidence::Low : cx::Confidence::None;
    const std::string not_fps = "panel mode refresh capability, not frames rendered (FPS)";

    const DisplayMode *active = nullptr;
    for (const auto &m : parsed.modes)
        if (m.id == parsed.active_mode_id) active = &m;

    std::set<int> all, at_res;
    for (const auto &m : parsed.modes) {
        all.insert(hz(m.refresh_hz));
        if (active && m.width == active->width && m.height == active->height) at_res.insert(hz(m.refresh_hz));
    }
    if (have) {
        out.push_back(fact(D, "display.refresh.modes", cx::Support::Yes, cx::Confidence::High, join_hz(all), "dumpsys", not_fps));
        out.push_back(fact(D, "display.refresh.min_hz", cx::Support::Yes, cx::Confidence::High, std::to_string(*all.begin()), "dumpsys", not_fps));
        out.push_back(fact(D, "display.refresh.max_hz", cx::Support::Yes, cx::Confidence::High, std::to_string(*all.rbegin()), "dumpsys", not_fps));
    } else {
        for (auto id : {"display.refresh.modes", "display.refresh.min_hz", "display.refresh.max_hz"})
            out.push_back(unknown(D, id, missing, miss_conf));
    }
    if (active)
        out.push_back(fact(D, "display.refresh.current_hz", cx::Support::Yes, cx::Confidence::High,
                           std::to_string(hz(active->refresh_hz)), "dumpsys", "active " + not_fps));
    else
        out.push_back(unknown(D, "display.refresh.current_hz", have ? "active mode not reported" : missing, miss_conf));

    // Adaptive refresh: several refresh modes at the active resolution, or compositor switching declared.
    const auto idle = prop(e, "ro.surface_flinger.set_idle_timer_ms");
    const bool declared = (!idle.empty() && idle != "0") ||
                          prop(e, "ro.surface_flinger.use_content_detection_for_refresh_rate") == "true";
    if (at_res.size() > 1)
        out.push_back(fact(D, "display.refresh.adaptive", cx::Support::Yes, cx::Confidence::High, "yes", "dumpsys",
                           "multiple refresh modes at the active resolution"));
    else if (declared)
        out.push_back(fact(D, "display.refresh.adaptive", cx::Support::Yes, cx::Confidence::Medium, "yes", "property",
                           "SurfaceFlinger idle timer / content detection declared"));
    else if (have)
        out.push_back(fact(D, "display.refresh.adaptive", cx::Support::No, cx::Confidence::Medium, "", "dumpsys",
                           "single refresh mode and no switching declared"));
    else
        out.push_back(unknown(D, "display.refresh.adaptive", missing, miss_conf));

    // Resolution.
    std::vector<Claim> res;
    if (active) {
        auto v = std::to_string(active->width) + "x" + std::to_string(active->height);
        res.push_back({"dumpsys", v, v, cx::Support::Yes, cx::Confidence::High});
    }
    if (e.wm_size) {
        static const std::regex phys(R"(Physical size: (\d+)x(\d+))");
        std::smatch m;
        if (std::regex_search(*e.wm_size, m, phys)) {
            auto v = m[1].str() + "x" + m[2].str();
            res.push_back({"wm", v, v, cx::Support::Yes, cx::Confidence::High});
        }
    }
    if (e.context) {
        for (const auto &kid : e.context->ids("display_refresh")) {
            if (kid.find(".modes") == std::string::npos) continue;
            auto r = e.context->resolve(kid);
            if (r.support != cx::Support::Yes || !r.fact) continue;
            if (auto v = first_resolution(r.fact->value)) {
                res.push_back({"kernel_drm", *v, *v, cx::Support::Yes, cx::Confidence::Medium});
                break;
            }
        }
    }
    resolve_claims(D, "display.resolution", res, out, "no resolution source", res.size() > 1);

    // HDR.
    std::vector<Claim> hdr;
    if (parsed.hdr_types) {
        std::string names;
        for (int t : *parsed.hdr_types) names += (names.empty() ? "" : " ") + hdr_name(t);
        const auto s = names.empty() ? cx::Support::No : cx::Support::Yes;
        hdr.push_back({"dumpsys", names, cx::to_string(s), s, cx::Confidence::High});
    }
    const auto has_hdr = prop(e, "ro.surface_flinger.has_HDR_display");
    if (has_hdr == "true" || has_hdr == "false") {
        const auto s = has_hdr == "true" ? cx::Support::Yes : cx::Support::No;
        hdr.push_back({"property", "", cx::to_string(s), s, cx::Confidence::Medium});
    }
    resolve_claims(D, "display.hdr", hdr, out, "no HDR capability source", hdr.size() > 1);
    return out;
}

std::vector<cx::CapabilityFact> observe_rendering(const DisplayEvidence &e) {
    constexpr const char *R = kRenderingDomain;
    Facts out;
    cx::Support sf = cx::Support::Unknown;
    if (!e.service_list) {
        out.push_back(unknown(R, "rendering.surfaceflinger", "service list not collected"));
    } else {
        sf = e.service_list->find("SurfaceFlinger: [") != std::string::npos ? cx::Support::Yes : cx::Support::No;
        out.push_back(fact(R, "rendering.surfaceflinger", sf, cx::Confidence::High, sf == cx::Support::Yes ? "running" : "",
                           "service_list", "registered binder service"));
    }

    const auto hwc = prop(e, "ro.hardware.hwcomposer");
    if (e.service_list && e.service_list->find("android.hardware.graphics.composer3.IComposer") != std::string::npos)
        out.push_back(fact(R, "rendering.composer", cx::Support::Yes, cx::Confidence::High, "aidl:composer3", "service_list",
                           hwc.empty() ? "" : "HAL name " + hwc));
    else if (!hwc.empty())
        out.push_back(fact(R, "rendering.composer", cx::Support::Yes, cx::Confidence::Medium, "hal:" + hwc, "property",
                           "ro.hardware.hwcomposer"));
    else
        out.push_back(unknown(R, "rendering.composer", "no composer evidence"));

    const auto backend = prop(e, "debug.renderengine.backend");
    out.push_back(backend.empty() ? unknown(R, "rendering.renderengine", "backend not declared; default depends on Android version")
                                  : fact(R, "rendering.renderengine", cx::Support::Yes, cx::Confidence::Medium, backend,
                                         "property", "debug.renderengine.backend"));

    if (sf == cx::Support::Yes)
        out.push_back(fact(R, "rendering.frame_timing", cx::Support::Yes, cx::Confidence::Medium, "surfaceflinger_latency",
                           "derived", "availability only; frames are measured by SessionRecorder, not here"));
    else if (sf == cx::Support::No)
        out.push_back(fact(R, "rendering.frame_timing", cx::Support::No, cx::Confidence::High, "", "derived",
                           "SurfaceFlinger not registered"));
    else
        out.push_back(unknown(R, "rendering.frame_timing", "SurfaceFlinger availability unknown"));

    auto apis = e.context ? e.context->resolve("graphics.interfaces") : cx::Resolution{};
    if (apis.support == cx::Support::Yes && apis.fact)
        out.push_back(fact(R, "rendering.pipeline", cx::Support::Yes, cx::Confidence::Medium, apis.fact->value, "graphics",
                           "graphics APIs available to apps" + (backend.empty() ? std::string() : "; compositor " + backend)));
    else
        out.push_back(unknown(R, "rendering.pipeline", "no graphics interface facts"));
    return out;
}

void publish(const DisplayEvidence &e, cx::CapabilityContext &context) {
    auto display = observe_display(e); // both read the context before either publish
    auto rendering = observe_rendering(e);
    context.publish(kDisplayDomain, std::move(display));
    context.publish(kRenderingDomain, std::move(rendering));
}

} // namespace flux::display
