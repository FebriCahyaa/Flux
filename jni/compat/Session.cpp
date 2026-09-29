#include "Session.hpp"

#include <sstream>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::compat {

namespace {

constexpr const char *kJournalMagic = "#flux-compat-journal v1";

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

bool write_state(const Io &io, const std::string &path, const std::string &content) {
    if (path.empty()) return true;
    if (io.write_atomic) return io.write_atomic(path, content);
    return io.write && io.write(path, content);
}

} // namespace

const char *to_string(EndReason r) {
    switch (r) {
    case EndReason::Exit: return "exit";
    case EndReason::ProcessDeath: return "process_death";
    case EndReason::FocusLost: return "focus_lost";
    case EndReason::Switch: return "switch";
    case EndReason::Failure: return "failure";
    case EndReason::DaemonStop: return "daemon_stop";
    }
    return "exit";
}

RecoveryReport SessionRuntime::recover() {
    RecoveryReport rep;
    const Io &io = d_.runtime.io;
    if (d_.journal_path.empty() || !io.exists || !io.read || !io.exists(d_.journal_path)) return rep;
    auto text = io.read(d_.journal_path);
    if (!text) return rep;

    std::vector<std::string> lines;
    for (const auto &l : split_lines(*text)) {
        if (l.empty()) continue;
        if (l[0] == '#') {
            if (l.rfind("#package=", 0) == 0) rep.package = l.substr(9);
            continue;
        }
        lines.push_back(l);
    }
    if (lines.empty()) return rep; // an empty journal is a clean shutdown
    rep.journal_found = true;

    say(d_.info, "[GameRuntime] recovery transaction=" + (rep.package.empty() ? std::string("unknown") : rep.package) +
                     " entries=" + std::to_string(lines.size()));
    Watchdog::Report w = Watchdog::recover_verified(io, lines);
    rep.found = w.found;
    rep.restored = w.restored;
    rep.failed = w.failed.size();

    if (w.failed.empty()) {
        write_state(io, d_.journal_path, "");
        say(d_.info, "[GameRuntime] recovery=PASS restored=" + std::to_string(w.restored));
    } else {
        // Keep exactly the lines that are still wrong, so the next boot retries only those.
        std::string keep = std::string(kJournalMagic) + "\n#package=" + rep.package + "\n";
        for (const auto &l : w.failed) keep += l + "\n";
        write_state(io, d_.journal_path, keep);
        carry_ = w.failed;
        say(d_.warn, "[GameRuntime] recovery=FAILED unrecovered=" + std::to_string(w.failed.size()) +
                         " (journal kept for the next boot)");
    }
    return rep;
}

bool SessionRuntime::begin(const SessionKey &key) {
    if (key.package.empty()) return false;
    if (active_ && key_ == key) return false; // same session: nothing to do
    if (active_) end(EndReason::Switch);      // never two transactions at once

    ResolvedInputs in;
    std::string err;
    if (!d_.inputs || !d_.inputs(key, in, err)) {
        // Unreadable documents must not cost the game its Flux profile. Nothing was applied.
        say(d_.warn, "[GameRuntime] activate package=" + key.package + " inputs unavailable: " + err +
                         " performance_fallback=CONTINUE");
        key_ = key;
        active_ = true; // remember the session so the failure is not retried on every wake
        perf_started_ = true;
        last_ = Activation{};
        status_ = ContextState::Failed;
        persist_status();
        return true;
    }
    if (!in.warning.empty()) say(d_.warn, "[GameRuntime] profile warning: " + in.warning);

    say(d_.info, "[GameRuntime] activate package=" + key.package + " pid=" + std::to_string(key.pid) +
                     " uid=" + std::to_string(key.uid));
    say(d_.info, std::string("[GameRuntime] profile=") + (in.has_profile ? "game" : "none") +
                     " mode=" + to_string(in.profile.mode) + " memory=" + in.profile.memory +
                     " touch=" + in.profile.touch + " storage=" + in.profile.storage + " refresh=" + in.profile.refresh);

    last_ = rt_.activate_compat(in.profile, in.known, in.hw, key.pid, key.uid);
    key_ = key;
    active_ = true;
    perf_started_ = false;
    status_ = last_.context;

    if (d_.set_refresh_request) d_.set_refresh_request(static_cast<int>(last_.refresh_request_hz));
    if (last_.refresh_request_hz > 0)
        say(d_.info, "[GameRuntime] refresh request=" + std::to_string(static_cast<int>(last_.refresh_request_hz)) +
                         "Hz" + (last_.refresh_note.empty() ? "" : " (" + last_.refresh_note + ")"));
    else if (!last_.refresh_note.empty())
        say(d_.info, "[GameRuntime] refresh not requested: " + last_.refresh_note);

    const bool asked = last_.resolution.should_apply || last_.context != ContextState::Inactive;
    if (!last_.provider.state.empty() && last_.provider.state != "unavailable")
        say(d_.info, "[GameRuntime] provider=" + last_.provider.state +
                         (last_.provider.transaction_id.empty() ? "" : " transaction=" + last_.provider.transaction_id) +
                         (last_.provider.reason.empty() ? "" : " reason=" + last_.provider.reason));
    if (last_.context == ContextState::Active) {
        say(d_.info, "[GameRuntime] compatibility=ACTIVE backend=" + last_.backend + " verification=PASS");
    } else if (last_.context == ContextState::Failed) {
        std::string reason = last_.log.empty() ? "unknown" : last_.log.front();
        say(d_.warn, "[GameRuntime] compatibility=FAILED backend=" + last_.backend + " reason=" + reason);
        say(d_.warn, "[GameRuntime] performance_fallback=CONTINUE");
    } else if (asked) {
        say(d_.info, "[GameRuntime] compatibility=NOT_APPLIED");
    } else {
        say(d_.debug, std::string("[GameRuntime] compatibility=NONE (") +
                          (last_.resolution.recommendation.empty() ? "nothing required" : "no reliable profile") + ")");
    }
    for (const auto &l : last_.log) say(d_.debug, l);

    persist_journal();
    persist_status();
    return true;
}

void SessionRuntime::after_profile_applied() {
    if (!active_) return;
    if (!perf_started_) {
        perf_started_ = true;
        rt_.activate_perf(last_);
        for (const auto &l : last_.log) say(d_.debug, l);
        if (last_.perf_context == ContextState::Active)
            say(d_.info, "[GameRuntime] performance overrides=ACTIVE");
        else if (last_.perf_context == ContextState::Failed)
            say(d_.warn, "[GameRuntime] performance overrides=FAILED (rolled back)");
        persist_journal();
        persist_status();
        return;
    }
    if (last_.perf_context == ContextState::Active && !rt_.reassert_perf())
        say(d_.warn, "[GameRuntime] performance overrides could not be re-asserted");
}

bool SessionRuntime::end(EndReason why) {
    if (!active_) return false; // second deactivate is a no-op
    say(d_.info, "[GameRuntime] deactivate package=" + key_.package + " reason=" + to_string(why));
    active_ = false;
    status_ = ContextState::Restoring;

    const bool clean = rt_.deactivate();
    if (d_.set_refresh_request) d_.set_refresh_request(0);

    if (clean) {
        say(d_.info, "[GameRuntime] restore=PASS");
        persist_journal(); // empty unless an earlier recovery left lines behind
        status_ = ContextState::Restored;
    } else {
        // Leave the journal exactly as it was: the next boot will retry the restore.
        say(d_.warn, "[GameRuntime] restore=FAILED (journal kept for recovery)");
        status_ = ContextState::Failed;
    }
    perf_started_ = false;
    last_ = Activation{};
    persist_status();
    return true;
}

void SessionRuntime::persist_journal() {
    if (d_.journal_path.empty()) return;
    auto lines = carry_;
    if (active_) {
        auto live = rt_.journal();
        lines.insert(lines.end(), live.begin(), live.end());
    }
    std::string body;
    if (!lines.empty()) {
        body = std::string(kJournalMagic) + "\n#package=" + key_.package + "\n";
        for (const auto &l : lines) body += l + "\n";
    }
    if (!write_state(d_.runtime.io, d_.journal_path, body))
        say(d_.warn, "[GameRuntime] could not persist the recovery journal");
}

void SessionRuntime::persist_status() {
    if (d_.status_path.empty()) return;
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    w.StartObject();
    w.Key("active"); w.Bool(active_);
    w.Key("package"); w.String(active_ ? key_.package.c_str() : "");
    w.Key("context"); w.String(to_string(status_));
    w.Key("perf_context"); w.String(to_string(last_.perf_context));
    w.Key("backend"); w.String(last_.backend.c_str());
    w.Key("backend_state"); w.String(to_string(last_.backend_state));
    w.Key("refresh_request_hz"); w.Int(static_cast<int>(last_.refresh_request_hz));
    w.Key("provider");
    w.StartObject();
    w.Key("state"); w.String(last_.provider.state.c_str());
    w.Key("transaction_id"); w.String(last_.provider.transaction_id.c_str());
    w.Key("reason"); w.String(last_.provider.reason.c_str());
    w.Key("layers");
    w.StartObject();
    for (const auto &[k, v] : last_.provider.layers) { w.Key(k.c_str()); w.String(v.c_str()); }
    w.EndObject();
    w.EndObject();
    w.Key("sustained"); w.String("unknown"); // only the FPS monitor can establish this
    w.EndObject();
    write_state(d_.runtime.io, d_.status_path, sb.GetString());
}

} // namespace flux::compat
