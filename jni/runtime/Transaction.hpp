/*
 * Copyright (C) 2024-2026 FebriCahyaa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// Runtime Transaction Engine (docs/architecture/TRANSACTION_ENGINE.md).
//
// Executes an already-approved RuntimePlan: snapshot -> write-ahead journal -> apply -> verify,
// rolls back on any failure, restores at the end of the lifecycle, and replays a journal left by
// a crashed daemon. It never decides which operations are valid; the planner that builds the plan
// does. All device access goes through Io so every path runs in host tests.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flux::runtime {

struct Io {
    std::function<bool(const std::string &)> exists;
    std::function<std::optional<std::string>(const std::string &)> read;
    std::function<bool(const std::string &, const std::string &)> write;
};

enum class TxState { Inactive, Preparing, Active, Restoring, Restored, Failed };
const char *to_string(TxState s);

/// One reversible change inside a transaction.
class TransactionOperation {
public:
    virtual ~TransactionOperation() = default;
    virtual std::string category() const = 0;
    virtual std::string describe() const = 0;
    /// Capture what must be restored; false = cannot be undone, so the operation is skipped.
    virtual bool snapshot() = 0;
    virtual bool apply() = 0;
    virtual bool verify() = 0;
    virtual bool restore() = 0;
    virtual bool verify_restore() = 0;
    /// Journal entry that undoes this operation after a crash; empty when there is nothing to undo.
    virtual std::string journal_entry() const { return {}; }
};

/// Write a value to a node, remembering its previous content. Missing or unreadable nodes are never written.
class NodeWriteOperation final : public TransactionOperation {
public:
    /// @param snapshot_view maps raw node text to what must be written back ("[mq-deadline] none" -> "mq-deadline").
    NodeWriteOperation(Io io, std::string category, std::string path, std::string value,
                       std::function<std::string(const std::string &)> snapshot_view = nullptr);

    std::string category() const override { return category_; }
    std::string describe() const override { return path_ + " = " + value_; }
    bool snapshot() override;
    bool apply() override;
    bool verify() override;
    bool restore() override;
    bool verify_restore() override;
    std::string journal_entry() const override;

private:
    Io io_;
    std::string category_, path_, value_;
    std::function<std::string(const std::string &)> view_;
    std::string original_;
    bool have_original_ = false;
    bool applied_ = false;
};

/// Approved operations for one subject (a game session, a launch boost, ...). `domain` names the
/// owner that approved them ("performance", "runtime").
struct RuntimePlan {
    std::string domain;
    std::string subject;
    std::vector<std::unique_ptr<TransactionOperation>> operations;
};

/// "tx-<ms>-<seq>": unique per daemon run given a monotonically increasing seq.
std::string make_transaction_id(int64_t now_ms, uint64_t seq);

class Transaction {
public:
    /// Receives the serialised journal before every apply (write-ahead) and after restore.
    /// Returning false stops the transaction before anything unjournaled is applied.
    using JournalSink = std::function<bool(const std::string &journal_text)>;

    Transaction(std::string id, RuntimePlan plan, JournalSink sink = nullptr);

    /// snapshot -> journal -> apply -> verify for each operation; any failure rolls back.
    bool start();
    /// Re-write and verify applied values (a profile script may have overwritten them). Never re-snapshots.
    bool reapply();
    /// Restore in reverse order and verify each; true when nothing is left behind.
    bool finish();

    TxState state() const { return state_; }
    const std::string &id() const { return id_; }
    const std::string &domain() const { return plan_.domain; }
    const std::string &subject() const { return plan_.subject; }
    const std::vector<std::string> &skipped() const { return skipped_; }
    const std::vector<std::string> &log() const { return log_; }
    /// Journal entries of every operation that may have changed state.
    std::vector<std::string> journal_entries() const;

private:
    bool rollback();
    bool persist();

    std::string id_;
    RuntimePlan plan_;
    JournalSink sink_;
    std::vector<TransactionOperation *> touched_;
    std::vector<std::string> skipped_, log_;
    TxState state_ = TxState::Inactive;
};

namespace journal {

inline constexpr int kVersion = 1;

std::string encode_entry(const std::string &path, const std::string &original);
/// False for malformed lines and for paths that are not absolute or contain "..".
bool decode_entry(const std::string &line, std::string &path, std::string &original);

/// "flux-journal 1\t<tx>\t<domain>\t<subject>" header, then one entry per line.
std::string serialize(const std::string &tx_id, const std::string &domain, const std::string &subject,
                      const std::vector<std::string> &entries);

struct Parsed {
    int version = 0;          ///< 0 = legacy journal without header (entries only)
    std::string tx_id, domain, subject;
    std::vector<std::string> entries;
    std::vector<std::string> corrupted; ///< lines that are not valid entries; never replayed
};
Parsed parse(const std::string &text);

} // namespace journal

struct RecoveryReport {
    std::string tx_id;
    size_t found = 0, restored = 0;
    std::vector<std::string> failed;    ///< valid entries whose restore or read-back failed
    std::vector<std::string> corrupted; ///< lines rejected without writing anything
    /// Keep the journal unless everything was restored and nothing was corrupted.
    bool clean() const { return failed.empty() && corrupted.empty(); }
};

/// Replay a journal left by a crashed daemon, newest entry first, verifying each restore by read-back.
RecoveryReport recover(const Io &io, const std::string &journal_text);

} // namespace flux::runtime
