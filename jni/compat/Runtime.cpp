#include "Runtime.hpp"

#include <algorithm>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace flux::compat {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

std::string escape(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}

std::string unescape(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            o += s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i];
        } else {
            o += s[i];
        }
    }
    return o;
}

} // namespace

// -- NodeWrite ---------------------------------------------------------------

bool NodeWrite::snapshot() {
    if (!io_.exists || !io_.read || !io_.exists(path_)) return false; // never write a node that isn't there
    auto cur = io_.read(path_);
    if (!cur) return false;
    original_ = trim(*cur);
    if (view_) original_ = view_(original_);
    have_original_ = true;
    return true;
}

bool NodeWrite::apply() {
    if (!have_original_) return false;
    applied_ = true; // even a failed write may have partially landed; rollback will retry the restore
    return io_.write(path_, value_);
}

bool NodeWrite::verify() {
    auto cur = io_.read(path_);
    return cur && trim(*cur) == value_;
}

bool NodeWrite::restore() {
    if (!applied_ || !have_original_) return true;
    if (!io_.write(path_, original_)) return false;
    applied_ = false;
    return true;
}

bool NodeWrite::verify_restore() {
    if (!have_original_) return true;
    auto cur = io_.read(path_);
    if (!cur) return false;
    std::string now = trim(*cur);
    if (view_) now = view_(now);
    return now == original_;
}

std::string NodeWrite::journal() const {
    return (applied_ && have_original_) ? Watchdog::encode(path_, original_) : std::string{};
}

// -- backends ---------------------------------------------------------------

std::vector<Layer> NativeBackend::unsupported_layers(const Resolution &plan) {
    std::vector<Layer> out;
    for (const auto &d : plan.layers)
        if (d.required && d.layer != Layer::Display) out.push_back(d.layer);
    return out;
}

bool NativeBackend::prepare(const Resolution &plan, const ProfileLibrary &) {
    // Identity cannot be scoped to one process from the daemon: refuse rather than
    // pretend, so nothing is ever rewritten system-wide.
    return unsupported_layers(plan).empty();
}

// -- Transaction --------------------------------------------------------------

bool Transaction::start() {
    state_ = ContextState::Preparing;
    for (auto &a : actions_) {
        if (!a->snapshot()) {
            skipped_.push_back(a->category() + ": " + a->describe());
            log_.push_back("skip " + a->describe());
            continue;
        }
        applied_.push_back(a.get());
        if (!a->apply() || !a->verify()) {
            log_.push_back("apply/verify failed: " + a->describe());
            rollback();
            state_ = ContextState::Failed;
            return false;
        }
        log_.push_back("applied " + a->describe());
    }
    state_ = applied_.empty() ? ContextState::Inactive : ContextState::Active;
    return true;
}

bool Transaction::rollback() {
    bool clean = true;
    for (auto it = applied_.rbegin(); it != applied_.rend(); ++it) {
        bool ok = (*it)->restore() && (*it)->verify_restore();
        log_.push_back(std::string(ok ? "rolled back " : "ROLLBACK FAILED ") + (*it)->describe());
        clean = clean && ok;
    }
    applied_.clear();
    return clean;
}

bool Transaction::finish() {
    if (state_ != ContextState::Active) return state_ == ContextState::Inactive || state_ == ContextState::Restored;
    state_ = ContextState::Restoring;
    bool clean = rollback();
    state_ = clean ? ContextState::Restored : ContextState::Failed;
    return clean;
}

bool Transaction::reapply() {
    if (state_ != ContextState::Active) return false;
    bool ok = true;
    for (Action *a : applied_) ok = (a->apply() && a->verify()) && ok;
    if (!ok) log_.push_back("reapply failed");
    return ok;
}

std::vector<std::string> Transaction::journal() const {
    std::vector<std::string> out;
    for (Action *a : applied_) {
        std::string j = a->journal();
        if (!j.empty()) out.push_back(std::move(j));
    }
    return out;
}

// -- Watchdog ----------------------------------------------------------------

std::string Watchdog::encode(const std::string &path, const std::string &original) {
    return escape(path) + "\t" + escape(original);
}

bool Watchdog::decode(const std::string &line, std::string &path, std::string &original) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) return false;
    path = unescape(line.substr(0, tab));
    original = unescape(line.substr(tab + 1));
    return !path.empty() && path[0] == '/' && path.find("..") == std::string::npos;
}

Watchdog::Report Watchdog::recover_verified(const Io &io, const std::vector<std::string> &lines) {
    Report r;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        ++r.found;
        std::string path, original;
        bool ok = decode(*it, path, original) && recover_line(io, *it);
        if (ok) {
            auto now = io.read ? io.read(path) : std::nullopt;
            ok = now && trim(*now) == original;
        }
        if (ok) ++r.restored;
        else r.failed.push_back(*it);
    }
    // failed lines keep their original journal order
    std::reverse(r.failed.begin(), r.failed.end());
    return r;
}

bool Watchdog::recover_line(const Io &io, const std::string &line) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) return false;
    std::string path = unescape(line.substr(0, tab));
    // A journal is untrusted input after a crash: only ever write to absolute paths.
    if (path.empty() || path[0] != '/' || path.find("..") != std::string::npos) return false;
    return io.write && io.write(path, unescape(line.substr(tab + 1)));
}

size_t Watchdog::recover(const Io &io, const std::vector<std::string> &lines) {
    size_t restored = 0;
    // Newest first, mirroring the rollback order.
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
        if (recover_line(io, *it)) ++restored;
    return restored;
}

} // namespace flux::compat
