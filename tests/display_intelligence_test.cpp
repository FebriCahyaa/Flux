// Display & rendering capability (Step 8.5): detection, missing interfaces, unsupported
// features, conflicting sources, publishing, and refresh-capability != FPS.
#include "flux_test.hpp"
#include "DisplayIntelligence.hpp"

#include <map>

namespace ctx = flux::context;
namespace d = flux::display;

namespace {

const char *kDumpsys =
    "DISPLAY MANAGER (dumpsys display)\n"
    "  mDisplayDevices=\n"
    "    DisplayDeviceInfo{\"Built-in Screen\": uniqueId=\"local:4619827259835644672\", 1080 x 2400, modeId 2, "
    "renderFrameRate 120.0, defaultModeId 1, supportedModes [{id=1, width=1080, height=2400, fps=60.0, vsync=60.0, "
    "alternativeRefreshRates=[90.0, 120.0], supportedHdrTypes=[2, 3]}, {id=2, width=1080, height=2400, fps=120.0, "
    "vsync=120.0, alternativeRefreshRates=[60.0, 90.0], supportedHdrTypes=[2, 3]}, {id=3, width=1080, height=2400, "
    "fps=90.0, vsync=90.0, alternativeRefreshRates=[60.0, 120.0], supportedHdrTypes=[2, 3]}], colorMode 0, "
    "hdrCapabilities HdrCapabilities{mSupportedHdrTypes=[2, 3, 4], mMaxLuminance=1000.0}, density 440}\n"
    "    DisplayDeviceInfo{\"Overlay #1\": uniqueId=\"overlay:1\", 720 x 480, modeId 9, supportedModes [{id=9, "
    "width=720, height=480, fps=30.0}]}\n";

const char *kSingleMode =
    "DisplayDeviceInfo{\"Built-in Screen\": 720 x 1600, modeId 1, supportedModes [{id=1, width=720, height=1600, "
    "fps=60.0, vsync=60.0}], hdrCapabilities HdrCapabilities{mSupportedHdrTypes=[], mMaxLuminance=500.0}}\n";

const char *kServices =
    "Found 3 services:\n"
    "0\tSurfaceFlinger: [android.ui.ISurfaceComposer]\n"
    "1\tandroid.hardware.graphics.composer3.IComposer/default: [android.hardware.graphics.composer3.IComposer]\n"
    "2\tdisplay: [android.hardware.display.IDisplayManager]\n";

struct Props {
    std::map<std::string, std::string> v;
    std::function<std::string(const std::string &)> fn() {
        return [this](const std::string &k) { auto it = v.find(k); return it == v.end() ? std::string() : it->second; };
    }
};

using Facts = std::vector<ctx::CapabilityFact>;
const ctx::CapabilityFact *find(const Facts &f, const std::string &id) {
    for (auto &x : f)
        if (x.id == id) return &x;
    return nullptr;
}

ctx::CapabilityFact kfact(const std::string &id, const std::string &domain, const std::string &value) {
    ctx::CapabilityFact f;
    f.id = id;
    f.domain = domain;
    f.source = "generic";
    f.support = ctx::Support::Yes;
    f.readable = true;
    f.confidence = ctx::Confidence::High;
    f.value = value;
    return f;
}

void test_parse() {
    auto p = d::parse_dumpsys_display(kDumpsys);
    CHECK_EQ(p.modes.size(), size_t(3)); // overlay display ignored
    CHECK_EQ(p.active_mode_id, 2);
    CHECK(p.hdr_types.has_value());
    if (p.hdr_types) CHECK_EQ(p.hdr_types->size(), size_t(3));
    CHECK(d::parse_dumpsys_display("").modes.empty());
    CHECK(!d::parse_dumpsys_display("garbage").hdr_types.has_value());
}

void test_display_detection() {
    Props p;
    p.v = {{"ro.surface_flinger.set_idle_timer_ms", "4000"}};
    auto facts = d::observe_display({kDumpsys, "Physical size: 1080x2400\n", kServices, p.fn(), nullptr});
    auto *modes = find(facts, "display.refresh.modes");
    CHECK(modes && modes->value == "60 90 120" && modes->confidence == ctx::Confidence::High);
    auto *cur = find(facts, "display.refresh.current_hz");
    CHECK(cur && cur->value == "120" && cur->support == ctx::Support::Yes);
    CHECK(find(facts, "display.refresh.min_hz")->value == "60");
    CHECK(find(facts, "display.refresh.max_hz")->value == "120");
    auto *adaptive = find(facts, "display.refresh.adaptive");
    CHECK(adaptive && adaptive->support == ctx::Support::Yes && adaptive->confidence == ctx::Confidence::High);
    auto *res = find(facts, "display.resolution");
    CHECK(res && res->value == "1080x2400" && res->support == ctx::Support::Yes);
    CHECK(res && res->note.find("conflict") == std::string::npos);
    auto *hdr = find(facts, "display.hdr");
    CHECK(hdr && hdr->support == ctx::Support::Yes && hdr->value == "hdr10 hlg hdr10_plus");

    for (auto &f : facts) {
        CHECK_EQ(f.domain, std::string("display"));
        CHECK(!f.writable);
        CHECK(!f.verified);
        // Refresh capability is never presented as FPS.
        CHECK(f.id.find("fps") == std::string::npos);
    }
    CHECK(cur && cur->note.find("not frames") != std::string::npos);
}

void test_rendering_detection() {
    Props p;
    p.v = {{"debug.renderengine.backend", "skiaglthreaded"}, {"ro.hardware.hwcomposer", "qcom"}};
    ctx::CapabilityContext c;
    c.publish("graphics", {kfact("graphics.interfaces", "graphics", "vulkan opengles egl")});
    auto facts = d::observe_rendering({kDumpsys, std::nullopt, kServices, p.fn(), &c});
    auto *sf = find(facts, "rendering.surfaceflinger");
    CHECK(sf && sf->support == ctx::Support::Yes && sf->confidence == ctx::Confidence::High);
    auto *comp = find(facts, "rendering.composer");
    CHECK(comp && comp->value == "aidl:composer3" && comp->confidence == ctx::Confidence::High);
    auto *re = find(facts, "rendering.renderengine");
    CHECK(re && re->value == "skiaglthreaded" && re->confidence == ctx::Confidence::Medium);
    auto *ft = find(facts, "rendering.frame_timing");
    CHECK(ft && ft->support == ctx::Support::Yes && ft->value == "surfaceflinger_latency");
    auto *pipe = find(facts, "rendering.pipeline");
    CHECK(pipe && pipe->value == "vulkan opengles egl" && pipe->support == ctx::Support::Yes);
    for (auto &f : facts) {
        CHECK_EQ(f.domain, std::string("rendering"));
        CHECK(!f.writable);
    }
}

void test_missing_interfaces() {
    auto facts = d::observe_display({});
    auto rfacts = d::observe_rendering({});
    for (auto id : {"display.refresh.modes", "display.refresh.current_hz", "display.refresh.min_hz",
                    "display.refresh.max_hz", "display.refresh.adaptive", "display.resolution", "display.hdr"}) {
        auto *f = find(facts, id);
        CHECK(f != nullptr);
        if (f) CHECK(f->support == ctx::Support::Unknown && f->confidence == ctx::Confidence::None && !f->note.empty());
    }
    for (auto id : {"rendering.surfaceflinger", "rendering.composer", "rendering.renderengine",
                    "rendering.frame_timing", "rendering.pipeline"}) {
        auto *f = find(rfacts, id);
        CHECK(f != nullptr);
        if (f) CHECK(f->support == ctx::Support::Unknown && f->confidence == ctx::Confidence::None);
    }
    // Collected but unparseable: still Unknown (low), never No.
    auto junk = d::observe_display({std::string("Can't find service: display"), std::nullopt, std::nullopt, nullptr, nullptr});
    auto *m = find(junk, "display.refresh.modes");
    CHECK(m && m->support == ctx::Support::Unknown && m->note.find("unparseable") != std::string::npos);
    // Services collected without SurfaceFlinger: an observed absence.
    auto noservice = d::observe_rendering({std::nullopt, std::nullopt, std::string("Found 0 services:\n"), nullptr, nullptr});
    CHECK(find(noservice, "rendering.surfaceflinger")->support == ctx::Support::No);
    CHECK(find(noservice, "rendering.frame_timing")->support == ctx::Support::No);
}

void test_unsupported_features() {
    auto facts = d::observe_display({kSingleMode, std::nullopt, std::nullopt, nullptr, nullptr});
    auto *adaptive = find(facts, "display.refresh.adaptive");
    CHECK(adaptive && adaptive->support == ctx::Support::No);
    CHECK(adaptive && adaptive->confidence == ctx::Confidence::Medium);
    auto *hdr = find(facts, "display.hdr");
    CHECK(hdr && hdr->support == ctx::Support::No && hdr->confidence == ctx::Confidence::High);
    CHECK(find(facts, "display.refresh.min_hz")->value == find(facts, "display.refresh.max_hz")->value);
    // Adaptive declared by the compositor even with one listed mode -> Yes (Medium).
    Props p;
    p.v = {{"ro.surface_flinger.use_content_detection_for_refresh_rate", "true"}};
    auto f2 = d::observe_display({kSingleMode, std::nullopt, std::nullopt, p.fn(), nullptr});
    CHECK(find(f2, "display.refresh.adaptive")->support == ctx::Support::Yes);
}

void test_conflicting_sources() {
    // wm size (High) disagrees with dumpsys (High): resolution Unknown, both claims kept.
    auto facts = d::observe_display({kDumpsys, "Physical size: 1440x3200\n", std::nullopt, nullptr, nullptr});
    auto *res = find(facts, "display.resolution");
    CHECK(res && res->support == ctx::Support::Unknown && res->note.find("conflict") != std::string::npos);
    CHECK(find(facts, "display.resolution.dumpsys") && find(facts, "display.resolution.dumpsys")->value == "1080x2400");
    CHECK(find(facts, "display.resolution.wm") && find(facts, "display.resolution.wm")->value == "1440x3200");

    // Kernel DRM modes (Medium) disagree: dumpsys answers, conflict noted.
    ctx::CapabilityContext c;
    c.publish("kernel", {kfact("display.card0-DSI-1.modes", "display_refresh", "720x1600\n1080x2400")});
    auto f2 = d::observe_display({kDumpsys, std::nullopt, std::nullopt, nullptr, &c});
    auto *r2 = find(f2, "display.resolution");
    CHECK(r2 && r2->value == "1080x2400" && r2->support == ctx::Support::Yes);
    CHECK(r2 && r2->note.find("conflict") != std::string::npos);

    // HDR: property says no, dumpsys (stronger) says yes.
    Props p;
    p.v = {{"ro.surface_flinger.has_HDR_display", "false"}};
    auto f3 = d::observe_display({kDumpsys, std::nullopt, std::nullopt, p.fn(), nullptr});
    auto *hdr = find(f3, "display.hdr");
    CHECK(hdr && hdr->support == ctx::Support::Yes && hdr->note.find("conflict") != std::string::npos);
    // Property alone is Medium evidence.
    auto f4 = d::observe_display({std::nullopt, std::nullopt, std::nullopt, p.fn(), nullptr});
    auto *h4 = find(f4, "display.hdr");
    CHECK(h4 && h4->support == ctx::Support::No && h4->confidence == ctx::Confidence::Medium);
}

void test_publishing() {
    ctx::CapabilityContext c;
    c.publish("kernel", {kfact("cpufreq.policy0.scaling_max_freq", "cpufreq", "1804800")});
    d::publish({kDumpsys, std::nullopt, kServices, nullptr, &c}, c);
    CHECK_EQ(c.publishers().size(), size_t(3)); // display, kernel, rendering
    CHECK(!c.ids("display").empty());
    CHECK(!c.ids("rendering").empty());
    auto r = c.resolve("display.refresh.max_hz");
    CHECK(r.fact && r.fact->publisher == "display" && r.fact->value == "120");
    CHECK(c.resolve("rendering.surfaceflinger").fact->publisher == "rendering");
    CHECK(c.supports("cpufreq.policy0.scaling_max_freq") == ctx::Support::Yes);
    auto n = c.ids().size();
    d::publish({kDumpsys, std::nullopt, kServices, nullptr, &c}, c);
    CHECK_EQ(c.ids().size(), n);
    CHECK_EQ(c.facts("display.refresh.modes").size(), size_t(1));
}

} // namespace

int main() {
    test_parse();
    test_display_detection();
    test_rendering_detection();
    test_missing_interfaces();
    test_unsupported_features();
    test_conflicting_sources();
    test_publishing();
    return flux_test::report("display_intelligence_test");
}
