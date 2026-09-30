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

#include "Transaction.hpp"

#include <algorithm>

namespace flux::runtime {

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

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

constexpr const char *kHeaderTag = "flux-journal ";
/// Header written by the old Game Runtime branch (compat_journal); its entries use the same format.
constexpr const char *kLegacyCompatHeader = "#flux-compat-journal v1";

} // namespace

const char *to_string(TxState s) {
    switch (s) {
    case TxState::Inactive: return "inactive";
    case TxState::Preparing: return "preparing";
    case TxState::Active: return "active";
    case TxState::Restoring: return "restoring";
    case TxState::Restored: return "restored";
    case TxState::Failed: return "failed";
    }
    return "unknown";
}

std::string make_transaction_id(int64_t now_ms, uint64_t seq) {
    return "tx-" + std::to_string(now_ms) + "-" + std::to_string(seq);
}

// -- NodeWriteOperation ---------------------------------------------------------

NodeWriteOperation::NodeWriteOperation(Io io, std::string category, std::string path, std::string value,
                                       std::function<std::string(const std::string &)> snapshot_view)
    : io_(std::move(io)), category_(std::move(category)), path_(std::move(path)), value_(std::move(value)),
      view_(std::move(snapshot_view)) {}

bool NodeWriteOperation::snapshot() {
    if (!io_.exists || !io_.read || !io_.exists(path_)) return false;
    auto cur = io_.read(path_);
    if (!cur) return false;
    original_ = trim(*cur);
    if (view_) original_ = view_(original_);
    have_original_ = true;
    return true;
}

bool NodeWriteOperation::apply() {
    if (!have_original_ || !io_.write) return false;
    applied_ = true; // a failed write may still have partially landed; restore retries
    return io_.write(path_, value_);
}

bool NodeWriteOperation::verify() {
    auto cur = io_.read(path_);
    return cur && trim(*cur) == value_;
}

bool NodeWriteOperation::restore() {
    if (!applied_ || !have_original_) return true;
    // The write result alone decides nothing: a failed write may leave the node untouched (already
    // at its snapshot), and a "successful" one may not stick. The read-back is the verdict.
    if (io_.write) io_.write(path_, original_);
    const auto cur = io_.read ? io_.read(path_) : std::nullopt;
    if (!cur) return false; // cannot verify: keep it journaled
    std::string now = trim(*cur);
    if (view_) now = view_(now);
    if (now != original_) return false;
    applied_ = false;
    return true;
}

bool NodeWriteOperation::verify_restore() {
    if (!have_original_) return true;
    auto cur = io_.read(path_);
    if (!cur) return false;
    std::string now = trim(*cur);
    if (view_) now = view_(now);
    return now == original_;
}

void NodeWriteOperation::evidence(std::map<std::string, std::string> &before,
                                  std::map<std::string, std::string> &after) const {
    if (!have_original_) return;
    before[path_] = original_;
    after[path_] = value_;
}

std::string NodeWriteOperation::journal_entry() const {
    // Write-ahead: the entry exists from snapshot on; restoring an unchanged node is harmless.
    return have_original_ ? journal::encode_entry(path_, original_) : std::string{};
}

// -- Transaction ----------------------------------------------------------------

Transaction::Transaction(std::string id, RuntimePlan plan, JournalSink sink, TxObserver observer)
    : id_(std::move(id)), plan_(std::move(plan)), sink_(std::move(sink)), observer_(std::move(observer)) {}

void Transaction::notify(TxNotice::Kind kind, bool ok, const std::string &detail) const {
    if (!observer_) return;
    try {
        TxNotice n;
        n.kind = kind;
        n.ok = ok;
        n.tx_id = id_;
        n.domain = plan_.domain;
        n.subject = plan_.subject;
        n.detail = detail;
        n.restored = last_restored_;
        n.failed = last_failed_;
        for (const TransactionOperation *op : touched_) op->evidence(n.before, n.after);
        observer_(n);
    } catch (...) {
        // Observability must never change what the transaction does.
    }
}

bool Transaction::persist() {
    if (!sink_) return true;
    return sink_(journal::serialize(id_, plan_.domain, plan_.subject, journal_entries()));
}

bool Transaction::start() {
    if (state_ != TxState::Inactive) return state_ == TxState::Active;
    state_ = TxState::Preparing;
    notify(TxNotice::Kind::Begin, true, std::to_string(plan_.operations.size()) + " operations planned");
    for (auto &op : plan_.operations) {
        if (!op->snapshot()) {
            skipped_.push_back(op->category() + ": " + op->describe());
            log_.push_back("skip " + op->describe());
            continue;
        }
        touched_.push_back(op.get());
        if (!persist()) {
            log_.push_back("journal write failed before " + op->describe());
            touched_.pop_back(); // not applied, nothing to undo
            notify(TxNotice::Kind::Apply, false, "journal write failed before " + op->describe());
            const bool clean = rollback();
            state_ = TxState::Failed;
            notify(TxNotice::Kind::Rollback, clean, clean ? "rolled back; every node read back at its snapshot" : std::to_string(last_failed_) + " node(s) not at snapshot after rollback; journal kept");
            return false;
        }
        const bool applied = op->apply();
        const bool verified = applied && op->verify();
        if (!verified) {
            log_.push_back("apply/verify failed: " + op->describe());
            notify(applied ? TxNotice::Kind::Verify : TxNotice::Kind::Apply, false,
                   (applied ? "verify failed: " : "apply failed: ") + op->describe());
            const bool clean = rollback();
            state_ = TxState::Failed;
            notify(TxNotice::Kind::Rollback, clean, clean ? "rolled back; every node read back at its snapshot" : std::to_string(last_failed_) + " node(s) not at snapshot after rollback; journal kept");
            return false;
        }
        log_.push_back("applied " + op->describe());
    }
    state_ = touched_.empty() ? TxState::Inactive : TxState::Active;
    if (state_ == TxState::Active) {
        const std::string n = std::to_string(touched_.size()) + " operations";
        notify(TxNotice::Kind::Apply, true, n + " applied");
        notify(TxNotice::Kind::Verify, true, n + " read back");
    }
    return true;
}

bool Transaction::rollback() {
    bool clean = true;
    last_restored_ = last_failed_ = 0;
    std::vector<TransactionOperation *> left;
    for (auto it = touched_.rbegin(); it != touched_.rend(); ++it) {
        const bool ok = (*it)->restore() && (*it)->verify_restore();
        log_.push_back(std::string(ok ? "restored " : "RESTORE FAILED ") + (*it)->describe());
        if (ok) ++last_restored_;
        else ++last_failed_;
        if (!ok) left.insert(left.begin(), *it);
        clean = clean && ok;
    }
    touched_ = std::move(left); // what could not be restored stays journaled for recovery
    persist();
    return clean;
}

bool Transaction::reapply() {
    if (state_ != TxState::Active) return false;
    bool ok = true;
    for (TransactionOperation *op : touched_) ok = (op->apply() && op->verify()) && ok;
    if (!ok) log_.push_back("reapply failed");
    return ok;
}

bool Transaction::finish() {
    if (state_ != TxState::Active) return state_ == TxState::Inactive || state_ == TxState::Restored;
    state_ = TxState::Restoring;
    std::map<std::string, std::string> before, after; // evidence of what is being restored
    for (const TransactionOperation *op : touched_) op->evidence(after, before);
    const bool clean = rollback();
    state_ = clean ? TxState::Restored : TxState::Failed;
    if (observer_) {
        try {
            TxNotice n;
            n.kind = TxNotice::Kind::Restore;
            n.ok = clean;
            n.tx_id = id_;
            n.domain = plan_.domain;
            n.subject = plan_.subject;
            n.detail = clean ? "restored and read back"
                             : std::to_string(last_failed_) + " of " + std::to_string(last_restored_ + last_failed_) +
                                   " not at snapshot after restore; journal kept";
            n.restored = last_restored_;
            n.failed = last_failed_;
            n.before = std::move(before);
            n.after = std::move(after);
            observer_(n);
        } catch (...) {
        }
    }
    return clean;
}

std::vector<std::string> Transaction::journal_entries() const {
    std::vector<std::string> out;
    for (const TransactionOperation *op : touched_) {
        std::string e = op->journal_entry();
        if (!e.empty()) out.push_back(std::move(e));
    }
    return out;
}

// -- journal --------------------------------------------------------------------

namespace journal {

std::string encode_entry(const std::string &path, const std::string &original) {
    return escape(path) + "\t" + escape(original);
}

bool decode_entry(const std::string &line, std::string &path, std::string &original) {
    const size_t tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) return false;
    path = unescape(line.substr(0, tab));
    original = unescape(line.substr(tab + 1));
    // A journal is untrusted input after a crash: absolute paths only, no traversal.
    return !path.empty() && path[0] == '/' && path.find("..") == std::string::npos;
}

std::string serialize(const std::string &tx_id, const std::string &domain, const std::string &subject,
                      const std::vector<std::string> &entries) {
    std::string out = kHeaderTag + std::to_string(kVersion) + "\t" + escape(tx_id) + "\t" + escape(domain) + "\t" +
                      escape(subject) + "\n";
    for (const auto &e : entries) out += e + "\n";
    return out;
}

Parsed parse(const std::string &text) {
    Parsed p;
    bool first = true;
    for (const std::string &raw : split(text, '\n')) {
        std::string line = raw;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (first && line == kLegacyCompatHeader) {
            first = false; // legacy journal: version 0, entries follow
            continue;
        }
        if (first && line.rfind(kHeaderTag, 0) == 0) {
            first = false;
            auto f = split(line.substr(std::string(kHeaderTag).size()), '\t');
            int v = 0;
            try {
                v = std::stoi(f[0]);
            } catch (...) {
                v = -1;
            }
            p.version = v;
            if (f.size() > 1) p.tx_id = unescape(f[1]);
            if (f.size() > 2) p.domain = unescape(f[2]);
            if (f.size() > 3) p.subject = unescape(f[3]);
            continue;
        }
        first = false;
        std::string path, original;
        if (p.version != 0 && p.version != kVersion) p.corrupted.push_back(line);
        else if (decode_entry(line, path, original)) p.entries.push_back(line);
        else p.corrupted.push_back(line);
    }
    return p;
}

} // namespace journal

// -- recovery -------------------------------------------------------------------

RecoveryReport recover(const Io &io, const std::string &journal_text) {
    const journal::Parsed p = journal::parse(journal_text);
    RecoveryReport r;
    r.tx_id = p.tx_id;
    r.corrupted = p.corrupted;
    for (auto it = p.entries.rbegin(); it != p.entries.rend(); ++it) {
        ++r.found;
        std::string path, original;
        bool ok = journal::decode_entry(*it, path, original);
        if (ok) {
            // Attempt the write, then let the read-back decide (see NodeWriteOperation::restore).
            if (io.write) io.write(path, original);
            auto now = io.read ? io.read(path) : std::nullopt;
            ok = now && trim(*now) == original;
        }
        if (ok) ++r.restored;
        else r.failed.push_back(*it);
    }
    std::reverse(r.failed.begin(), r.failed.end());
    return r;
}

} // namespace flux::runtime
