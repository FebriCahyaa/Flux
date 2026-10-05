// Observatory persistent telemetry store and 7-day retention (Step 8.11).
//
// Layout under <root> (fluxd: /data/adb/.config/zairenkai):
//   installation.json                     installation epoch (InstallationEpoch.hpp) — never retained/deleted here
//   telemetry/FORMAT                      "zairenkai-telemetry <version>"
//   telemetry/events/YYYYMMDDTHH.jsonl    one Observatory event per line (to_json), UTC hour of timestamp_ms
//   telemetry/events/YYYYMMDDTHH.idx      derived index of that segment (rebuildable, may be absent)
//
// Producers never see this: TeeEventSink writes to the bounded memory store first and then,
// best effort, to this store. Disk failures are counted and reported, never thrown.
// Retention: records older than 7 x 24 h (wall clock) are removed; see TELEMETRY_RETENTION.md for
// the clock-safety rules.
#pragma once

#include "Event.hpp"
#include "EventStore.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace flux::observatory {

inline constexpr const char *kTelemetryRoot = "/data/adb/.config/zairenkai";
inline constexpr int kTelemetryFormat = 1;
inline constexpr int64_t kRetentionMs = 7LL * 24 * 3600 * 1000;
inline constexpr int64_t kMaintenanceIntervalMs = 3600LL * 1000;
inline constexpr int64_t kMinValidWallMs = 1577836800000LL; // 2020-01-01: earlier = clock unset
inline constexpr int64_t kMaxClockJumpMs = 24LL * 3600 * 1000; // wall vs steady divergence in one process
inline constexpr size_t kMaxRecordBytes = 48 * 1024;
inline constexpr size_t kRecentIds = 4096;

/// File operations used by the store; a seam for failure injection in tests.
class FileIo {
  public:
    virtual ~FileIo() = default;
    virtual bool mkdirs(const std::string &dir) = 0;
    virtual bool exists(const std::string &path) const = 0;
    virtual std::optional<std::string> read_all(const std::string &path) const = 0;
    virtual std::optional<char> last_byte(const std::string &path) const = 0;
    virtual std::optional<uint64_t> size(const std::string &path) const = 0;
    /// One write(2) with O_APPEND, then fdatasync.
    virtual bool append(const std::string &path, const std::string &data) = 0;
    /// temp file + fsync + rename (+ directory fsync).
    virtual bool write_atomic(const std::string &path, const std::string &data) = 0;
    virtual bool remove(const std::string &path) = 0; // true when gone (also if it never existed)
    virtual std::vector<std::string> list(const std::string &dir) const = 0;
};
std::shared_ptr<FileIo> make_posix_io();

struct TelemetryClock {
    std::function<int64_t()> wall_ms;   // epoch ms (may be unset / jump)
    std::function<int64_t()> steady_ms; // monotonic, this process
};

struct StorageHealth {
    bool open = false;
    bool format_ok = false;
    bool read_only = false;
    uint64_t written = 0, duplicates = 0, rejected = 0, failed = 0;
    std::string last_error;
    bool failing = false; // the most recent append failed
};

struct RetentionReport {
    bool ran = false;
    bool dry_run = false;
    std::string skipped_reason;
    int64_t cutoff_ms = 0;
    int segments_deleted = 0, segments_trimmed = 0, orphans_removed = 0;
    uint64_t records_removed = 0;
};

struct TelemetryQuery {
    std::optional<int64_t> from_ms, to_ms; // inclusive, event timestamp
    std::optional<std::string> session_id, package, source, type, transaction_id;
    std::optional<Severity> min_severity;
    size_t limit = 0; // 0 = no limit (oldest first)
};

struct QueryStats {
    int segments_total = 0, segments_scanned = 0, segments_skipped_by_index = 0;
    uint64_t corrupted_lines = 0, duplicates_skipped = 0;
};

class PersistentEventStore {
  public:
    PersistentEventStore(std::string root, TelemetryClock clock, std::shared_ptr<FileIo> io);

    /// Creates the directories and FORMAT (unless read_only), removes leftover *.tmp files,
    /// loads recent event ids for duplicate detection. False when unusable (health says why).
    bool open(bool read_only = false);

    /// Appends one already-validated event (with event_id). Never throws.
    /// False on failure, duplicate, oversize, expired or closed store (health counts each).
    bool append(const Event &e);

    std::vector<Event> query(const TelemetryQuery &q, QueryStats *stats = nullptr) const;
    /// Newest stored event timestamp (reads only the newest segment); nullopt when empty. Read-only.
    std::optional<int64_t> newest_timestamp() const;

    /// Deletes / trims expired segments. Skips (and says why) when the wall clock is unusable.
    RetentionReport maintain(bool dry_run = false);
    bool maintenance_due() const;

    const StorageHealth &health() const { return health_; }
    std::string events_dir() const { return root_ + "/telemetry/events"; }

  private:
    struct SegmentIndex;
    std::optional<SegmentIndex> load_index(const std::string &segment) const;
    std::optional<SegmentIndex> load_index_for_bytes(const std::string &segment, uint64_t bytes) const;
    SegmentIndex build_index(const std::string &segment) const;
    void fail(const std::string &why);

    std::string root_;
    TelemetryClock clock_;
    std::shared_ptr<FileIo> io_;
    StorageHealth health_;
    std::set<std::string> recent_ids_;
    std::deque<std::string> recent_order_;
    std::optional<int64_t> last_maint_wall_, last_maint_steady_;
};

/// EventSink for producers: memory store first (its result is returned), then the persistent store.
/// Persistent failures are isolated; the first failure of a streak is recorded in memory as
/// OBSERVATORY_STORAGE_FAILED (never on disk, never as success). SESSION_END triggers due maintenance.
class TeeEventSink final : public EventSink {
  public:
    TeeEventSink(EventSink &memory, PersistentEventStore *disk, std::function<int64_t()> wall_ms);
    WriteResult write(Event e) override;

  private:
    EventSink &memory_;
    PersistentEventStore *disk_;
    std::function<int64_t()> wall_ms_;
    bool reported_failure_ = false;
};

} // namespace flux::observatory
