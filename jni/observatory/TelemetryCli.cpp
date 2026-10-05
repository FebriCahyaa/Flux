#include "TelemetryCli.hpp"

#include "InstallationEpoch.hpp"
#include "ObservatoryAnalyzer.hpp"
#include "TelemetryStore.hpp"

#include <chrono>

namespace flux::observatory {

int run_telemetry_command(const std::vector<std::string> &args, const std::string &root, std::ostream &out,
                          std::ostream &err) {
    if (args.empty()) {
        err << "usage: telemetry status | retention | query [k=v]... | session <id> | analyze <id> | history <package> "
               "[from=ms] [to=ms]\n";
        return 1;
    }
    // The wall clock is used only by the `retention` dry run; analysis never reads it.
    const TelemetryClock clock{
        [] {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());
        },
        [] {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count());
        }};
    PersistentEventStore store(root, clock, make_posix_io());
    if (!store.open(true)) { // read-only: never creates, writes or repairs anything
        err << "telemetry unavailable: " << store.health().last_error << "\n";
        return 1;
    }
    const std::string &sub = args[0];
    try {
        if (sub == "status") {
            QueryStats st;
            auto all = store.query({}, &st);
            auto epoch_text = make_posix_io()->read_all(root + "/installation.json");
            auto epoch = epoch_text ? epoch_from_json(*epoch_text) : std::nullopt;
            out << "format: ok\nsegments: " << st.segments_total << "\nevents: " << all.size()
                << "\ncorrupted_lines: " << st.corrupted_lines << "\noldest_ms: " << (all.empty() ? 0 : all.front().timestamp_ms)
                << "\nnewest_ms: " << (all.empty() ? 0 : all.back().timestamp_ms)
                << "\ninstallation_id: " << (epoch ? epoch->installation_id : "unavailable") << "\n";
            return 0;
        }
        if (sub == "retention") {
            auto r = store.maintain(true);
            out << "dry_run: true\nran: " << (r.ran ? "true" : "false") << "\nskipped: " << r.skipped_reason
                << "\ncutoff_ms: " << r.cutoff_ms << "\nsegments_to_delete: " << r.segments_deleted
                << "\nsegments_to_trim: " << r.segments_trimmed << "\nrecords_to_remove: " << r.records_removed << "\n";
            return 0;
        }
        if (sub == "query") {
            TelemetryQuery q;
            for (size_t i = 1; i < args.size(); ++i) {
                const auto eq = args[i].find('=');
                if (eq == std::string::npos) {
                    err << "expected key=value, got " << args[i] << "\n";
                    return 1;
                }
                const std::string k = args[i].substr(0, eq), v = args[i].substr(eq + 1);
                if (k == "session") q.session_id = v;
                else if (k == "package") q.package = v;
                else if (k == "source") q.source = v;
                else if (k == "type") q.type = v;
                else if (k == "tx") q.transaction_id = v;
                else if (k == "severity") q.min_severity = parse_severity(v);
                else if (k == "from") q.from_ms = std::stoll(v);
                else if (k == "to") q.to_ms = std::stoll(v);
                else if (k == "limit") q.limit = std::stoul(v);
                else {
                    err << "unknown filter " << k << "\n";
                    return 1;
                }
            }
            for (const auto &e : store.query(q)) out << to_json(e) << "\n";
            return 0;
        }
        const ObservatoryAnalyzer analyzer(store);
        if ((sub == "session" || sub == "analyze") && args.size() == 2) {
            if (sub == "session") out << to_text(analyzer.timeline(args[1]));
            else out << to_text(analyzer.analyze(args[1]));
            return 0;
        }
        if (sub == "history" && args.size() >= 2) {
            std::optional<int64_t> from, to;
            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i].rfind("from=", 0) == 0) from = std::stoll(args[i].substr(5));
                else if (args[i].rfind("to=", 0) == 0) to = std::stoll(args[i].substr(3));
                else {
                    err << "unknown history option " << args[i] << "\n";
                    return 1;
                }
            }
            if (!to) to = store.newest_timestamp(); // deterministic: the newest stored event, not "now"
            if (!to) {
                out << "package: " << args[1] << "\nsessions: 0\npatterns: none\nlimitation: no events stored\n";
                return 0;
            }
            out << to_text(analyzer.history(args[1], from.value_or(*to - kRetentionMs), *to));
            return 0;
        }
    } catch (const std::exception &e) {
        err << "invalid arguments: " << e.what() << "\n";
        return 1;
    }
    err << "unknown or incomplete telemetry command: " << sub << "\n";
    return 1;
}

} // namespace flux::observatory
