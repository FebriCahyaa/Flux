#include "GameRuntime.hpp"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::compat {

namespace {

double refresh_for(const std::string &r) {
    if (r == "hz60") return 60;
    if (r == "hz90") return 90;
    if (r == "hz120") return 120;
    return 0;
}

} // namespace

Activation GameRuntime::activate(const EffectiveProfile &p, const std::optional<GameRequirement> &known,
                                 const RealHardware &hw) {
    Activation a = activate_compat(p, known, hw);
    activate_perf(a);
    return a;
}

Activation GameRuntime::activate_compat(const EffectiveProfile &p, const std::optional<GameRequirement> &known,
                                        const RealHardware &hw) {
    // A second activation without a deactivate would strand the first snapshot.
    if (active()) deactivate();

    Activation a;
    package_ = p.package;
    profile_ = p;
    const ProfileLibrary empty;
    const ProfileLibrary &lib = d_.library ? *d_.library : empty;
    a.resolution = resolve_compatibility(p, known, hw, lib);
    plan_ = std::make_unique<Resolution>(a.resolution);

    // -- compatibility context ----------------------------------------------
    std::vector<Layer> identity_layers = NativeBackend::unsupported_layers(*plan_);
    if (plan_->should_apply && !identity_layers.empty()) {
        Backend *zb = d_.zygisk;
        a.backend_state = zb ? zb->available() : BackendState::Unavailable;
        if (zb && a.backend_state == BackendState::Available && zb->supports(p.package)) {
            backend_ = zb;
            a.backend = zb->name();
        } else {
            a.log.push_back("[FCE] apply skipped: identity layers need the zygisk backend (" +
                            std::string(to_string(a.backend_state)) + ")");
            for (Layer l : identity_layers) {
                auto &d = a.resolution.layers[static_cast<size_t>(l)];
                d.state = LayerState::Failed;
                d.reason = "no backend can scope this to the game process";
            }
            a.resolution.should_apply = false;
            a.context = ContextState::Failed;
        }
    } else if (plan_->should_apply && d_.native) {
        backend_ = d_.native;
        a.backend = d_.native->name();
        a.backend_state = BackendState::Available;
    }

    if (backend_) {
        compat_ = std::make_unique<Transaction>(p.package);
        compat_->add(std::make_unique<BackendAction>(*backend_, *plan_, lib));
        if (compat_->start()) {
            a.context = compat_->state();
            for (Layer l : {Layer::Device, Layer::Cpu, Layer::Gpu}) {
                auto &d = a.resolution.layers[static_cast<size_t>(l)];
                if (d.required && compat_->state() == ContextState::Active) d.state = LayerState::Verified;
            }
        } else {
            a.context = ContextState::Failed;
            a.log.push_back("[FCE] apply failed backend=" + a.backend + " rollback=PASS");
            for (auto &d : a.resolution.layers)
                if (d.required) d.state = LayerState::Failed;
            a.resolution.should_apply = false;
        }
        for (const auto &s : compat_->skipped()) a.skipped.push_back(s);
        a.log.insert(a.log.end(), compat_->log().begin(), compat_->log().end());
        if (a.context != ContextState::Active) compat_.reset();
    }

    // -- refresh: hand a request to the existing path, never write it here --------
    double want = refresh_for(p.refresh);
    if (a.resolution.decision(Layer::Display).required && known && known->target_fps > 0)
        want = std::max(want, known->target_fps);
    if (want > 0) {
        Tri sup = hw.panel_supports(want);
        if (sup == Tri::No) a.refresh_note = "panel has no mode at the requested refresh";
        else {
            a.refresh_request_hz = want;
            if (sup == Tri::Unknown) a.refresh_note = "panel modes unknown; request passed on unverified";
        }
    }

    // -- the two identity views ---------------------------------------------------
    a.effective_identity["device"] = hw.brand + " " + hw.model;
    a.effective_identity["cpu"] = hw.soc;
    a.effective_identity["gpu"] = hw.gpu_vendor + " " + hw.gpu_model;
    if (a.context == ContextState::Active)
        for (const auto &d : a.resolution.layers) {
            if (!d.required || d.identity.empty() || d.layer == Layer::Display) continue;
            auto it = lib.identities.find(d.identity);
            if (it != lib.identities.end()) a.effective_identity[to_string(d.layer)] = d.identity;
        }

    a.log.push_back("[FCE] package=" + p.package);
    a.log.push_back(std::string("[FCE] mode=") + to_string(p.mode));
    return a;
}


void GameRuntime::activate_perf(Activation &a) {
    if (package_.empty() || perf_) return; // no session, or already applied
    const EffectiveProfile &p = profile_;
    PerfPlanInput in;
    in.memory = p.memory;
    in.touch = p.touch;
    in.storage = p.storage;
    in.block_queues = d_.block_queues;
    in.mitigation_allows = d_.mitigation_allows;
    auto actions = build_actions(d_.io, in, a.perf);
    if (!actions.empty()) {
        perf_ = std::make_unique<Transaction>(p.package);
        for (auto &act : actions) perf_->add(std::move(act));
        if (perf_->start()) a.perf_context = perf_->state();
        else a.perf_context = ContextState::Failed;
        for (const auto &s : perf_->skipped()) a.skipped.push_back(s);
        a.log.insert(a.log.end(), perf_->log().begin(), perf_->log().end());
        if (a.perf_context != ContextState::Active) perf_.reset();
    }
    for (const auto &c : a.perf)
        if (c.blocked_by_mitigation) a.log.push_back("[FCE] " + c.category + " skipped by device mitigation");

}

bool GameRuntime::reassert_perf() { return !perf_ || perf_->reapply(); }

bool GameRuntime::deactivate() {
    bool ok = true;
    if (perf_) ok = perf_->finish() && ok;
    if (compat_) ok = compat_->finish() && ok;
    perf_.reset();
    compat_.reset();
    plan_.reset();
    backend_ = nullptr;
    package_.clear();
    return ok;
}

std::vector<std::string> GameRuntime::journal() const {
    std::vector<std::string> j;
    if (perf_) j = perf_->journal();
    if (compat_) {
        auto c = compat_->journal();
        j.insert(j.end(), c.begin(), c.end());
    }
    return j;
}

std::string Activation::to_json() const {
    // The resolution already renders itself; wrap it with the runtime state.
    rapidjson::Document res;
    res.Parse(resolution.to_json().c_str());
    rapidjson::Document d;
    d.SetObject();
    auto &al = d.GetAllocator();
    d.AddMember("resolution", res, al);
    auto S = [&](const std::string &s) { return rapidjson::Value(s.c_str(), al); };
    d.AddMember("backend", S(backend), al);
    d.AddMember("backend_state", S(to_string(backend_state)), al);
    d.AddMember("context", S(to_string(context)), al);
    d.AddMember("perf_context", S(to_string(perf_context)), al);
    d.AddMember("refresh_request_hz", refresh_request_hz, al);
    d.AddMember("refresh_note", S(refresh_note), al);
    rapidjson::Value perf_arr(rapidjson::kArrayType);
    for (const auto &c : perf) {
        rapidjson::Value o(rapidjson::kObjectType);
        o.AddMember("category", S(c.category), al);
        o.AddMember("support", S(to_string(c.support)), al);
        o.AddMember("planned", c.planned, al);
        o.AddMember("available", c.available, al);
        o.AddMember("blocked_by_mitigation", c.blocked_by_mitigation, al);
        perf_arr.PushBack(o, al);
    }
    d.AddMember("perf", perf_arr, al);
    rapidjson::Value ident(rapidjson::kObjectType);
    for (const auto &[k, v] : effective_identity) ident.AddMember(S(k), S(v), al);
    d.AddMember("effective_identity", ident, al);
    rapidjson::Value sk(rapidjson::kArrayType);
    for (const auto &s : skipped) sk.PushBack(S(s), al);
    d.AddMember("skipped", sk, al);
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    d.Accept(w);
    return sb.GetString();
}

} // namespace flux::compat
