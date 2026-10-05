#include "TelemetryStore.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace flux::observatory {

// -- POSIX file layer ---------------------------------------------------------------------------

namespace {

bool write_fd(int fd, const std::string &data) {
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

class PosixIo final : public FileIo {
  public:
    bool mkdirs(const std::string &dir) override {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) return false;
        std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ec);
        return std::filesystem::is_directory(dir, ec);
    }
    bool exists(const std::string &path) const override {
        struct stat st {};
        return ::stat(path.c_str(), &st) == 0;
    }
    std::optional<std::string> read_all(const std::string &path) const override {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream out;
        out << in.rdbuf();
        if (in.bad()) return std::nullopt;
        return out.str();
    }
    std::optional<char> last_byte(const std::string &path) const override {
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return std::nullopt;
        char c = 0;
        std::optional<char> r;
        if (::lseek(fd, -1, SEEK_END) >= 0 && ::read(fd, &c, 1) == 1) r = c;
        ::close(fd);
        return r;
    }
    std::optional<uint64_t> size(const std::string &path) const override {
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0) return std::nullopt;
        return static_cast<uint64_t>(st.st_size);
    }
    bool append(const std::string &path, const std::string &data) override {
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        const bool ok = write_fd(fd, data) && ::fdatasync(fd) == 0;
        return ::close(fd) == 0 && ok;
    }
    bool write_atomic(const std::string &path, const std::string &data) override {
        const std::string tmp = path + ".tmp";
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        const bool ok = write_fd(fd, data) && ::fsync(fd) == 0;
        if (::close(fd) != 0 || !ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
            ::unlink(tmp.c_str());
            return false;
        }
        const auto slash = path.rfind('/');
        if (slash != std::string::npos) {
            const int dfd = ::open(path.substr(0, slash).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dfd >= 0) {
                ::fsync(dfd);
                ::close(dfd);
            }
        }
        return true;
    }
    bool remove(const std::string &path) override { return ::unlink(path.c_str()) == 0 || errno == ENOENT; }
    std::vector<std::string> list(const std::string &dir) const override {
        std::vector<std::string> out;
        DIR *d = ::opendir(dir.c_str());
        if (!d) return out;
        while (const dirent *e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back(n);
        }
        ::closedir(d);
        std::sort(out.begin(), out.end());
        return out;
    }
};

// -- segments -----------------------------------------------------------------------------------

constexpr int64_t kHourMs = 3600LL * 1000;
constexpr const char *kFormatLine = "zairenkai-telemetry 1";

std::string segment_name(int64_t ts_ms) {
    const time_t secs = static_cast<time_t>(ts_ms / 1000);
    struct tm t {};
    gmtime_r(&secs, &t);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02d%02dT%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour);
    return buf;
}

/// Hour start (ms) of "YYYYMMDDTHH", nullopt for names that are not ours.
std::optional<int64_t> segment_start(const std::string &stem) {
    int y, mo, d, h;
    char tee;
    if (stem.size() != 11 || std::sscanf(stem.c_str(), "%4d%2d%2d%c%2d", &y, &mo, &d, &tee, &h) != 5 || tee != 'T')
        return std::nullopt;
    struct tm t {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    return static_cast<int64_t>(timegm(&t)) * 1000;
}

bool ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string join(const std::set<std::string> &s) {
    std::string out;
    for (const auto &x : s) out += (out.empty() ? "" : ",") + x;
    return out;
}

std::set<std::string> split_set(const std::string &s) {
    std::set<std::string> out;
    std::istringstream in(s);
    std::string x;
    while (std::getline(in, x, ','))
        if (!x.empty()) out.insert(x);
    return out;
}

/// Calls fn(event) for each valid line; returns the number of corrupted lines.
template <typename Fn> uint64_t for_each_line(const std::string &text, Fn fn) {
    uint64_t corrupted = 0;
    std::istringstream in(text);
    std::string line, err;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Event e;
        if (line.size() > kMaxRecordBytes || !from_json(line, e, err) || e.event_id.empty()) {
            ++corrupted;
            continue;
        }
        fn(e, line);
    }
    return corrupted;
}

} // namespace

std::shared_ptr<FileIo> make_posix_io() { return std::make_shared<PosixIo>(); }

// -- index ----------------------------------------------------------------------------------------

struct PersistentEventStore::SegmentIndex {
    uint64_t bytes = 0, count = 0;
    int64_t min_ts = LLONG_MAX, max_ts = LLONG_MIN;
    int max_severity = -1;
    std::set<std::string> sessions, sources, types, transactions, packages;
    std::map<std::string, std::string> session_package;

    void add(const Event &e) {
        ++count;
        min_ts = std::min(min_ts, e.timestamp_ms);
        max_ts = std::max(max_ts, e.timestamp_ms);
        max_severity = std::max(max_severity, static_cast<int>(e.severity));
        if (!e.session_id.empty()) sessions.insert(e.session_id);
        sources.insert(e.source);
        types.insert(e.type);
        if (!e.transaction_id.empty()) transactions.insert(e.transaction_id);
        std::string package;
        if (auto a = e.after.find("package"); a != e.after.end()) package = a->second;
        else if (auto b = e.before.find("package"); b != e.before.end()) package = b->second;
        if (!package.empty()) {
            packages.insert(package);
            if (!e.session_id.empty()) session_package[e.session_id] = package;
        }
    }
    std::string serialize() const {
        std::string out = "format=1\nbytes=" + std::to_string(bytes) + "\ncount=" + std::to_string(count) +
                          "\nmin_ts=" + std::to_string(min_ts) + "\nmax_ts=" + std::to_string(max_ts) +
                          "\nmax_severity=" + std::to_string(max_severity) + "\nsessions=" + join(sessions) +
                          "\nsources=" + join(sources) + "\ntypes=" + join(types) + "\ntransactions=" +
                          join(transactions) + "\npackages=" + join(packages) + "\n";
        for (const auto &[s, p] : session_package) out += "session_package=" + s + ":" + p + "\n";
        return out;
    }
    static std::optional<SegmentIndex> parse(const std::string &text) {
        SegmentIndex x;
        bool format = false;
        std::istringstream in(text);
        std::string line;
        try {
            while (std::getline(in, line)) {
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const auto k = line.substr(0, eq), v = line.substr(eq + 1);
                if (k == "format") format = v == "1";
                else if (k == "bytes") x.bytes = std::stoull(v);
                else if (k == "count") x.count = std::stoull(v);
                else if (k == "min_ts") x.min_ts = std::stoll(v);
                else if (k == "max_ts") x.max_ts = std::stoll(v);
                else if (k == "max_severity") x.max_severity = std::stoi(v);
                else if (k == "sessions") x.sessions = split_set(v);
                else if (k == "sources") x.sources = split_set(v);
                else if (k == "types") x.types = split_set(v);
                else if (k == "transactions") x.transactions = split_set(v);
                else if (k == "packages") x.packages = split_set(v);
                else if (k == "session_package") {
                    auto c = v.find(':');
                    if (c != std::string::npos) x.session_package[v.substr(0, c)] = v.substr(c + 1);
                }
            }
        } catch (...) {
            return std::nullopt;
        }
        if (!format) return std::nullopt;
        return x;
    }
};

namespace {
std::string idx_path(const std::string &segment) { return segment.substr(0, segment.size() - 6) + ".idx"; }
} // namespace

std::optional<PersistentEventStore::SegmentIndex> PersistentEventStore::load_index(const std::string &segment) const {
    auto text = io_->read_all(idx_path(segment));
    if (!text) return std::nullopt;
    auto x = SegmentIndex::parse(*text);
    if (!x || x->bytes != io_->size(segment).value_or(UINT64_MAX)) return std::nullopt; // stale or broken
    return x;
}

std::optional<PersistentEventStore::SegmentIndex> PersistentEventStore::load_index_for_bytes(const std::string &segment,
                                                                                        uint64_t bytes) const {
    auto text = io_->read_all(idx_path(segment));
    if (!text) return std::nullopt;
    auto x = SegmentIndex::parse(*text);
    if (!x || x->bytes != bytes) return std::nullopt;
    return x;
}

PersistentEventStore::SegmentIndex PersistentEventStore::build_index(const std::string &segment) const {
    SegmentIndex x;
    auto text = io_->read_all(segment);
    if (!text) return x;
    for_each_line(*text, [&](const Event &e, const std::string &) { x.add(e); });
    x.bytes = text->size();
    return x;
}

// -- store ----------------------------------------------------------------------------------------

PersistentEventStore::PersistentEventStore(std::string root, TelemetryClock clock, std::shared_ptr<FileIo> io)
    : root_(std::move(root)), clock_(std::move(clock)), io_(std::move(io)) {}

void PersistentEventStore::fail(const std::string &why) {
    ++health_.failed;
    health_.failing = true;
    health_.last_error = why;
}

bool PersistentEventStore::open(bool read_only) {
    health_ = StorageHealth{};
    health_.read_only = read_only;
    const std::string tel = root_ + "/telemetry";
    try {
        if (read_only) {
            if (!io_->exists(tel)) {
                health_.last_error = "telemetry directory absent";
                return false;
            }
        } else if (!io_->mkdirs(events_dir())) {
            health_.last_error = "cannot create " + events_dir();
            return false;
        }
        const std::string format_path = tel + "/FORMAT";
        if (auto f = io_->read_all(format_path)) {
            std::string line = *f;
            line.erase(line.find_last_not_of(" \r\n") + 1);
            if (line != kFormatLine) {
                health_.last_error = "unsupported telemetry format '" + line + "' (not written)";
                return false;
            }
        } else if (read_only || !io_->write_atomic(format_path, std::string(kFormatLine) + "\n")) {
            health_.last_error = "telemetry FORMAT missing or not writable";
            return false;
        }
        health_.format_ok = true;
        if (!read_only)
            for (const auto &n : io_->list(events_dir()))
                if (ends_with(n, ".tmp")) io_->remove(events_dir() + "/" + n); // interrupted atomic writes

        // Recent ids (newest segments first) for duplicate detection across restarts.
        auto names = io_->list(events_dir());
        for (auto it = names.rbegin(); it != names.rend() && recent_ids_.size() < kRecentIds; ++it) {
            if (!ends_with(*it, ".jsonl")) continue;
            if (auto text = io_->read_all(events_dir() + "/" + *it))
                for_each_line(*text, [&](const Event &e, const std::string &) {
                    if (recent_ids_.size() < kRecentIds && recent_ids_.insert(e.event_id).second)
                        recent_order_.push_back(e.event_id);
                });
        }
        health_.open = true;
        return true;
    } catch (const std::exception &e) {
        health_.last_error = e.what();
        return false;
    }
}

bool PersistentEventStore::append(const Event &e) {
    try {
        if (!health_.open || health_.read_only || !health_.format_ok) {
            fail("telemetry store not open for writing");
            return false;
        }
        const int64_t wall = clock_.wall_ms ? clock_.wall_ms() : 0;
        if (e.event_id.empty() || (wall >= kMinValidWallMs && e.timestamp_ms < wall - kRetentionMs)) {
            ++health_.rejected; // no id, or already past retention
            return false;
        }
        if (recent_ids_.count(e.event_id)) {
            ++health_.duplicates;
            return false;
        }
        const std::string line = to_json(e);
        if (line.size() + 1 > kMaxRecordBytes) {
            ++health_.rejected;
            return false;
        }
        const std::string seg = events_dir() + "/" + segment_name(e.timestamp_ms) + ".jsonl";
        const uint64_t before = io_->size(seg).value_or(0);
        const auto last = before ? io_->last_byte(seg) : std::nullopt;
        const std::string data = (last && *last != '\n' ? "\n" : "") + line + "\n"; // isolate a torn line
        if (!io_->append(seg, data)) {
            fail("append failed: " + seg);
            return false;
        }
        ++health_.written;
        health_.failing = false;
        recent_ids_.insert(e.event_id);
        recent_order_.push_back(e.event_id);
        while (recent_order_.size() > kRecentIds) {
            recent_ids_.erase(recent_order_.front());
            recent_order_.pop_front();
        }
        // Index: extend when current, otherwise rebuild. Failure leaves it stale (rebuilt on demand).
        SegmentIndex x;
        if (auto prev = load_index_for_bytes(seg, before)) {
            x = *prev;
            x.add(e);
            x.bytes = before + data.size();
        } else {
            x = build_index(seg);
        }
        if (!io_->write_atomic(idx_path(seg), x.serialize())) health_.last_error = "index write failed: " + seg;
        return true;
    } catch (const std::exception &ex) {
        fail(std::string("append threw: ") + ex.what());
        return false;
    }
}

std::vector<Event> PersistentEventStore::query(const TelemetryQuery &q, QueryStats *stats) const {
    QueryStats st;
    std::vector<Event> out;
    try {
        std::vector<std::string> segs;
        for (const auto &n : io_->list(events_dir()))
            if (ends_with(n, ".jsonl") && segment_start(n.substr(0, n.size() - 6))) segs.push_back(events_dir() + "/" + n);
        st.segments_total = static_cast<int>(segs.size());

        // Package -> sessions, from indexes (or a scan where an index is missing).
        std::set<std::string> pkg_sessions;
        if (q.package) {
            for (const auto &s : segs) {
                auto x = load_index(s);
                const SegmentIndex idx = x ? *x : build_index(s);
                for (const auto &[sid, pkg] : idx.session_package)
                    if (pkg == *q.package) pkg_sessions.insert(sid);
            }
        }
        std::set<std::string> seen;
        for (const auto &s : segs) {
            const auto name = s.substr(s.rfind('/') + 1);
            const int64_t start = *segment_start(name.substr(0, name.size() - 6));
            if ((q.to_ms && start > *q.to_ms) || (q.from_ms && start + kHourMs <= *q.from_ms)) continue;
            if (auto x = load_index(s)) {
                bool skip = (q.session_id && !x->sessions.count(*q.session_id)) || (q.source && !x->sources.count(*q.source)) ||
                            (q.type && !x->types.count(*q.type)) ||
                            (q.transaction_id && !x->transactions.count(*q.transaction_id)) ||
                            (q.min_severity && x->max_severity < static_cast<int>(*q.min_severity));
                if (q.package && !x->packages.count(*q.package)) {
                    bool any = false;
                    for (const auto &sid : pkg_sessions) any = any || x->sessions.count(sid);
                    skip = skip || !any;
                }
                if (skip) {
                    ++st.segments_skipped_by_index;
                    continue;
                }
            }
            ++st.segments_scanned;
            auto text = io_->read_all(s);
            if (!text) continue;
            st.corrupted_lines += for_each_line(*text, [&](const Event &e, const std::string &) {
                if (!seen.insert(e.event_id).second) {
                    ++st.duplicates_skipped;
                    return;
                }
                if (q.from_ms && e.timestamp_ms < *q.from_ms) return;
                if (q.to_ms && e.timestamp_ms > *q.to_ms) return;
                if (q.session_id && e.session_id != *q.session_id) return;
                if (q.source && e.source != *q.source) return;
                if (q.type && e.type != *q.type) return;
                if (q.transaction_id && e.transaction_id != *q.transaction_id) return;
                if (q.min_severity && static_cast<int>(e.severity) < static_cast<int>(*q.min_severity)) return;
                if (q.package) {
                    auto p = e.after.find("package");
                    const bool direct = p != e.after.end() && p->second == *q.package;
                    if (!direct && !pkg_sessions.count(e.session_id)) return;
                }
                out.push_back(e);
            });
        }
        std::stable_sort(out.begin(), out.end(), [](const Event &a, const Event &b) {
            return a.timestamp_ms != b.timestamp_ms ? a.timestamp_ms < b.timestamp_ms : a.sequence < b.sequence;
        });
        if (q.limit && out.size() > q.limit) out.resize(q.limit);
    } catch (...) {
        // A broken store yields what was read so far; queries never throw into callers.
    }
    if (stats) *stats = st;
    return out;
}

std::optional<int64_t> PersistentEventStore::newest_timestamp() const {
    try {
        auto names = io_->list(events_dir());
        for (auto it = names.rbegin(); it != names.rend(); ++it) {
            if (!ends_with(*it, ".jsonl") || !segment_start(it->substr(0, it->size() - 6))) continue;
            std::optional<int64_t> newest;
            if (auto text = io_->read_all(events_dir() + "/" + *it))
                for_each_line(*text, [&](const Event &e, const std::string &) {
                    if (!newest || e.timestamp_ms > *newest) newest = e.timestamp_ms;
                });
            if (newest) return newest;
        }
    } catch (...) {
    }
    return std::nullopt;
}

bool PersistentEventStore::maintenance_due() const {
    if (!last_maint_steady_ || !clock_.steady_ms) return true;
    return clock_.steady_ms() - *last_maint_steady_ >= kMaintenanceIntervalMs;
}

RetentionReport PersistentEventStore::maintain(bool dry_run) {
    RetentionReport r;
    r.dry_run = dry_run;
    try {
        if (!health_.format_ok) {
            r.skipped_reason = "store not open";
            return r;
        }
        const int64_t wall = clock_.wall_ms ? clock_.wall_ms() : 0;
        const int64_t steady = clock_.steady_ms ? clock_.steady_ms() : 0;
        if (wall < kMinValidWallMs) {
            r.skipped_reason = "wall clock not set; retention skipped";
            return r;
        }
        if (last_maint_wall_ && last_maint_steady_ &&
            (wall - *last_maint_wall_) - (steady - *last_maint_steady_) > kMaxClockJumpMs) {
            r.skipped_reason = "wall clock jump detected (wall advanced far more than elapsed time); retention deferred";
            last_maint_wall_ = wall; // the next run measures from here
            last_maint_steady_ = steady;
            return r;
        }
        last_maint_wall_ = wall;
        last_maint_steady_ = steady;
        r.ran = true;
        r.cutoff_ms = wall - kRetentionMs; // records with timestamp < cutoff are older than 7 x 24 h

        const auto names = io_->list(events_dir());
        std::set<std::string> present(names.begin(), names.end());
        for (const auto &n : names) {
            const std::string path = events_dir() + "/" + n;
            if (ends_with(n, ".tmp")) {
                if (!dry_run && !health_.read_only) io_->remove(path);
                continue;
            }
            if (ends_with(n, ".idx")) {
                if (!present.count(n.substr(0, n.size() - 4) + ".jsonl")) {
                    ++r.orphans_removed;
                    if (!dry_run && !health_.read_only) io_->remove(path);
                }
                continue;
            }
            if (!ends_with(n, ".jsonl")) continue;
            const auto start = segment_start(n.substr(0, n.size() - 6));
            if (!start || *start >= r.cutoff_ms) continue; // not ours, or entirely fresh
            auto text = io_->read_all(path);
            if (!text) continue;
            std::string kept;
            uint64_t removed = 0;
            const uint64_t corrupted = for_each_line(*text, [&](const Event &e, const std::string &line) {
                if (e.timestamp_ms < r.cutoff_ms) ++removed;
                else kept += line + "\n";
            });
            r.records_removed += removed;
            if (removed == 0 && corrupted == 0) continue;
            if (dry_run || health_.read_only) {
                kept.empty() ? ++r.segments_deleted : ++r.segments_trimmed;
                continue;
            }
            if (kept.empty()) {
                io_->remove(idx_path(path)); // index first: a segment without index is always valid
                if (io_->remove(path)) ++r.segments_deleted;
            } else if (io_->write_atomic(path, kept)) {
                ++r.segments_trimmed;
                io_->write_atomic(idx_path(path), build_index(path).serialize());
            }
        }
    } catch (const std::exception &e) {
        r.skipped_reason = std::string("retention failed: ") + e.what();
    }
    return r;
}

// -- tee sink -------------------------------------------------------------------------------------

TeeEventSink::TeeEventSink(EventSink &memory, PersistentEventStore *disk, std::function<int64_t()> wall_ms)
    : memory_(memory), disk_(disk), wall_ms_(std::move(wall_ms)) {}

WriteResult TeeEventSink::write(Event e) {
    const std::string type = e.type;
    WriteResult r = memory_.write(e);
    if (!r.accepted || !disk_) return r; // rejected events are never persisted
    e.event_id = r.event_id;
    e.sequence = r.sequence;
    bool ok = false;
    try {
        ok = disk_->append(e);
    } catch (...) {
        ok = false;
    }
    if (ok) {
        reported_failure_ = false;
    } else if (disk_->health().failing && !reported_failure_) {
        reported_failure_ = true; // once per failure streak, memory only
        try {
            Event f;
            f.timestamp_ms = wall_ms_ ? wall_ms_() : e.timestamp_ms;
            f.source = "observatory";
            f.type = "OBSERVATORY_STORAGE_FAILED";
            f.severity = Severity::Warning;
            f.reason = "telemetry persistence failed: " + disk_->health().last_error;
            if (f.reason.size() > 512) f.reason.resize(512);
            f.confidence = Confidence::High;
            f.result = Result::Failed;
            f.after = {{"failed", std::to_string(disk_->health().failed)}};
            memory_.write(f);
        } catch (...) {
        }
    }
    if (type == "SESSION_END") {
        try {
            if (disk_->maintenance_due()) disk_->maintain();
        } catch (...) {
        }
    }
    return r;
}

} // namespace flux::observatory
