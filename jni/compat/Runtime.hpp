#pragma once

// Runtime transactions.
//
//   begin -> snapshot -> prepare -> apply -> verify -> ACTIVE
//   end   -> restore  -> verify restore -> COMPLETE
//   any failure while applying -> roll back what was applied
//   daemon restart / process death -> journal-driven recovery
//
// All device access goes through Io so the host tests can drive every path.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CompatTypes.hpp"
#include "Resolver.hpp"

namespace flux::compat {

/// Filesystem seam. Production wires this to the real /sys and /proc; tests use a map.
struct Io {
    std::function<bool(const std::string &)> exists;
    std::function<std::optional<std::string>(const std::string &)> read;
    std::function<bool(const std::string &, const std::string &)> write;
    /// Crash-safe replace (temp file + rename) for the journal and status files.
    /// Falls back to `write` when unset.
    std::function<bool(const std::string &, const std::string &)> write_atomic;
};

/// One reversible change. `category` groups them for diagnostics
/// ("memory", "touch", "storage", "launch_boost", "compat.device", ...).
class Action {
public:
    virtual ~Action() = default;
    virtual std::string category() const = 0;
    virtual std::string describe() const = 0;
    virtual bool snapshot() = 0;  ///< capture the state to restore; false = cannot be undone, do not apply
    virtual bool apply() = 0;
    virtual bool verify() = 0;
    virtual bool restore() = 0;
    virtual bool verify_restore() = 0;
    /// Serialised form for the recovery journal; empty when nothing needs recovery.
    virtual std::string journal() const { return {}; }
};

/// Write @p value to a node, remembering the previous content. Unavailable nodes
/// are never written: snapshot() fails, so the whole action is skipped.
class NodeWrite final : public Action {
public:
    /// @param snapshot_view  maps the raw node text to what must be written back
    ///        (e.g. "[mq-deadline] none" -> "mq-deadline"); identity when null.
    NodeWrite(Io io, std::string category, std::string path, std::string value,
              std::function<std::string(const std::string &)> snapshot_view = nullptr)
        : io_(std::move(io)), category_(std::move(category)), path_(std::move(path)), value_(std::move(value)),
          view_(std::move(snapshot_view)) {}

    std::string category() const override { return category_; }
    std::string describe() const override { return path_ + " = " + value_; }
    bool snapshot() override;
    bool apply() override;
    bool verify() override;
    bool restore() override;
    bool verify_restore() override;
    std::string journal() const override;

    const std::string &path() const { return path_; }

private:
    Io io_;
    std::string category_, path_, value_;
    std::function<std::string(const std::string &)> view_;
    std::string original_;
    bool have_original_ = false;
    bool applied_ = false;
};

/// Execution backend for the process-scoped part (Device/CPU/GPU identity).
/// The engine decides; the backend only carries the decision out.
struct EffectiveProfile;

/// What a backend can tell the UI about the process-level context (only the Zygisk backend has one).
struct ProviderReport {
    std::string state = "unavailable"; ///< unavailable | not_configured | unsupported | installed | loaded | matched | applied | verified | failed
    std::string transaction_id;
    std::string reason;
    std::vector<std::pair<std::string, std::string>> layers; ///< "device" -> "verified", ...
};

class Backend {
public:
    virtual ~Backend() = default;
    /// Called before prepare(): the profile being activated and the process it is for.
    virtual void configure(const EffectiveProfile &, int /*pid*/, int /*uid*/) {}
    virtual std::string last_error() const { return {}; }
    virtual ProviderReport provider_report() const { return {}; }
    virtual const char *name() const = 0;
    virtual BackendState available() const = 0;
    virtual bool supports(const std::string &package) const = 0;
    virtual bool prepare(const Resolution &plan, const ProfileLibrary &lib) = 0;
    virtual bool apply(const Resolution &plan) = 0;
    virtual bool verify(const Resolution &plan) = 0;
    virtual bool restore(const Resolution &plan) = 0;
    /// Journal line that undoes this backend's state after a crash; empty when nothing is staged.
    virtual std::string recovery_line(const Resolution &) const { return {}; }
};

/// Native backend: it can only satisfy the display layer (refresh compatibility
/// is handled by the existing RefreshHold path). Identity needs a process-resident
/// component, which is what the Zygisk backend exists for.
class NativeBackend final : public Backend {
public:
    const char *name() const override { return "native"; }
    BackendState available() const override { return BackendState::Available; }
    bool supports(const std::string &) const override { return true; }
    bool prepare(const Resolution &plan, const ProfileLibrary &) override;
    bool apply(const Resolution &) override { return true; }
    bool verify(const Resolution &) override { return true; }
    bool restore(const Resolution &) override { return true; }
    /// Layers this backend cannot carry out for @p plan.
    static std::vector<Layer> unsupported_layers(const Resolution &plan);
};

/// Adapts a Backend to the Action interface so identity work is part of the same
/// transaction as the node writes.
class BackendAction final : public Action {
public:
    BackendAction(Backend &b, const Resolution &plan, const ProfileLibrary &lib) : b_(b), plan_(plan), lib_(lib) {}
    std::string category() const override { return std::string("compat.") + b_.name(); }
    std::string describe() const override { return std::string(b_.name()) + " context for " + plan_.package; }
    bool snapshot() override { return b_.prepare(plan_, lib_); }
    bool apply() override { return b_.apply(plan_); }
    bool verify() override { return b_.verify(plan_); }
    bool restore() override { return b_.restore(plan_); }
    bool verify_restore() override { return true; }
    std::string journal() const override { return b_.recovery_line(plan_); }

private:
    Backend &b_;
    const Resolution &plan_;
    const ProfileLibrary &lib_;
};

class Transaction {
public:
    explicit Transaction(std::string package) : package_(std::move(package)) {}

    void add(std::unique_ptr<Action> a) { actions_.push_back(std::move(a)); }

    /// snapshot -> apply -> verify for every action. Any failure rolls back what
    /// was applied and leaves the transaction Failed. Actions whose snapshot fails
    /// are skipped and recorded, not treated as a failure of the whole.
    bool start();
    /// restore in reverse order and verify each; returns true when nothing is left behind.
    bool finish();
    /// Write the applied values again (a profile script may have overwritten them) and
    /// verify. Never re-snapshots: the originals captured at start() stay the originals.
    bool reapply();

    ContextState state() const { return state_; }
    const std::string &package() const { return package_; }
    const std::vector<std::string> &skipped() const { return skipped_; }
    const std::vector<std::string> &log() const { return log_; }
    /// One line per still-applied action, for the recovery journal.
    std::vector<std::string> journal() const;

private:
    bool rollback();
    std::string package_;
    std::vector<std::unique_ptr<Action>> actions_;
    std::vector<Action *> applied_;
    std::vector<std::string> skipped_, log_;
    ContextState state_ = ContextState::Inactive;
};

/// Crash recovery. `journal_line` values come from NodeWrite::journal(); on daemon
/// start any leftover journal is replayed so a crashed daemon cannot leave a game's
/// tweaks (or its compatibility context) behind.
struct Watchdog {
    static std::string encode(const std::string &path, const std::string &original);
    /// Restore one journal line; returns false when the line is malformed or the write fails.
    static bool recover_line(const Io &io, const std::string &line);
    static size_t recover(const Io &io, const std::vector<std::string> &lines);
    static bool decode(const std::string &line, std::string &path, std::string &original);

    struct Report {
        size_t found = 0, restored = 0;
        std::vector<std::string> failed; ///< lines that could not be restored or verified
    };
    /// Restore every line newest-first, then read each node back. A line only counts as
    /// restored when the node really holds the journalled value.
    static Report recover_verified(const Io &io, const std::vector<std::string> &lines);
    /// Decision helper: an Active transaction whose process is gone must be restored.
    static bool must_restore(ContextState s, bool process_alive) { return s == ContextState::Active && !process_alive; }
};

} // namespace flux::compat
