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
 *
 * The binder observer approach (IProcessObserver / IDisplayManagerCallback
 * registered from native code through libbinder_ndk) follows Encore Tweaks'
 * BinderMonitor, Copyright (C) 2024-2026 Rem01Gaming, Apache License 2.0.
 */

#include "NativeMonitor.hpp"

#include <BinderNDK.hpp>
#include <Flux.hpp>
#include <FluxLog.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *TAG = "NativeMonitor";

/// Must match SynthesisCore's Protocol.VERSION for the fields written here.
constexpr int PROTOCOL_VERSION = 3;

// ── Transaction codes ─────────────────────────────────────────────────────────

enum class Tx {
    IsInteractive,
    IsPowerSaveMode,
    RegisterProcessObserver,
    OnForegroundActivitiesChanged,
    OnProcessDied,
    GetNameForUid,
    RegisterDisplayCallback,
    OnDisplayEvent,
    GetZenMode,
    GetThermalHeadroom,
    GetCurrentThermalStatus,
    IsMusicActive,
    GetMode,
};

struct TxQuery {
    Tx tx;
    const char *query;
    bool required;
};

// Keep in sync with the resolve list in module/service.sh.
constexpr TxQuery kQueries[] = {
    {Tx::IsInteractive, "android.os.IPowerManager.Stub::TRANSACTION_isInteractive", true},
    {Tx::IsPowerSaveMode, "android.os.IPowerManager.Stub::TRANSACTION_isPowerSaveMode", true},
    {Tx::RegisterProcessObserver, "android.app.IActivityManager.Stub::TRANSACTION_registerProcessObserver", true},
    {Tx::OnForegroundActivitiesChanged, "android.app.IProcessObserver.Stub::TRANSACTION_onForegroundActivitiesChanged", true},
    {Tx::OnProcessDied, "android.app.IProcessObserver.Stub::TRANSACTION_onProcessDied", true},
    {Tx::GetNameForUid, "android.content.pm.IPackageManager.Stub::TRANSACTION_getNameForUid", true},
    {Tx::RegisterDisplayCallback, "android.hardware.display.IDisplayManager.Stub::TRANSACTION_registerCallback", false},
    {Tx::OnDisplayEvent, "android.hardware.display.IDisplayManagerCallback.Stub::TRANSACTION_onDisplayEvent", false},
    {Tx::GetZenMode, "android.app.INotificationManager.Stub::TRANSACTION_getZenMode", false},
    {Tx::GetThermalHeadroom, "android.os.IThermalService.Stub::TRANSACTION_getThermalHeadroom", false},
    {Tx::GetCurrentThermalStatus, "android.os.IThermalService.Stub::TRANSACTION_getCurrentThermalStatus", false},
    {Tx::IsMusicActive, "android.media.IAudioService.Stub::TRANSACTION_isMusicActive", false},
    {Tx::GetMode, "android.media.IAudioService.Stub::TRANSACTION_getMode", false},
};

// ── State ─────────────────────────────────────────────────────────────────────

struct Service {
    const char *name;
    const char *descriptor;
    AIBinder *binder = nullptr;
    AIBinder_Class *clazz = nullptr;
};

struct Status {
    std::string focused_app = "none 0 0";
    int screen_awake = 1;
    int battery_saver = 0;
    int zen_mode = 0;
    int charging_state = 0;
    std::string thermal_status = "-1.00";
    int audio_active = 0;
    int thermal_api_available = 0;
    int kernel_is_gki = 0;
    int thermal_level = -1;
    int battery_level = -1;
    std::string battery_temp;
    int call_active = 0;
};

struct State {
    std::mutex mutex;
    std::condition_variable wake;
    std::unordered_map<Tx, uint32_t> codes;

    Service power{"power", "android.os.IPowerManager"};
    Service activity{"activity", "android.app.IActivityManager"};
    Service package{"package", "android.content.pm.IPackageManager"};
    Service display{"display", "android.hardware.display.IDisplayManager"};
    Service notification{"notification", "android.app.INotificationManager"};
    Service thermal{"thermalservice", "android.os.IThermalService"};
    Service audio{"audio", "android.media.IAudioService"};

    // Held for the process lifetime so the services keep our callbacks alive.
    AIBinder *process_observer = nullptr;
    AIBinder *display_callback = nullptr;

    // Set when system_server dies; the poll loop then reconnects.
    AIBinder_DeathRecipient *death_recipient = nullptr;
    std::atomic<bool> system_died = false;

    // Processes with foreground activities, most recent last.
    std::vector<std::pair<int32_t, int32_t>> foreground; // (pid, uid)
    std::unordered_map<int32_t, std::string> uid_names;

    Status status;
    std::string last_written;
    bool dirty = true;
    std::string error;
} g;

uint32_t code(Tx tx) {
    auto it = g.codes.find(tx);
    return it == g.codes.end() ? 0 : it->second;
}

// ── Binder helpers ────────────────────────────────────────────────────────────

void *noop_create(void *args) { return args; }
void noop_destroy(void *) {}
binder_status_t noop_transact(AIBinder *, uint32_t, const AParcel *, AParcel *) { return STATUS_UNKNOWN_TRANSACTION; }

bool string_allocator(void *data, int32_t size, char **out) {
    if (size < 0) return false;
    auto *str = static_cast<std::string *>(data);
    str->resize(static_cast<size_t>(size));
    *out = str->data();
    return true;
}

/**
 * Runs a transaction on a remote service. The interface token is written by
 * AIBinder_prepareTransaction for the associated class; @p write_args appends
 * the arguments. Returns the reply positioned after the status header, or null.
 */
template <typename WriteArgs>
AParcel *transact(const Service &svc, Tx tx, WriteArgs write_args) {
    const uint32_t tx_code = code(tx);
    if (!svc.binder || tx_code == 0) return nullptr;

    AParcel *in = nullptr, *out = nullptr;
    if (AIBinder_prepareTransaction(svc.binder, &in) != STATUS_OK) return nullptr;
    write_args(in);

    if (AIBinder_transact(svc.binder, tx_code, &in, &out, 0) != STATUS_OK) {
        if (out) AParcel_delete(out);
        return nullptr;
    }

    AStatus *status = nullptr;
    const bool ok = AParcel_readStatusHeader(out, &status) == STATUS_OK && status && AStatus_isOk(status);
    if (status) AStatus_delete(status);
    if (!ok) {
        AParcel_delete(out);
        return nullptr;
    }
    return out;
}

AParcel *transact(const Service &svc, Tx tx) {
    return transact(svc, tx, [](AParcel *) {});
}

std::optional<int32_t> read_int(AParcel *reply) {
    if (!reply) return std::nullopt;
    int32_t value = 0;
    const bool ok = AParcel_readInt32(reply, &value) == STATUS_OK;
    AParcel_delete(reply);
    return ok ? std::optional<int32_t>(value) : std::nullopt;
}

std::optional<bool> read_bool(AParcel *reply) {
    if (!reply) return std::nullopt;
    bool value = false;
    const bool ok = AParcel_readBool(reply, &value) == STATUS_OK;
    AParcel_delete(reply);
    return ok ? std::optional<bool>(value) : std::nullopt;
}

std::optional<float> read_float(AParcel *reply) {
    if (!reply) return std::nullopt;
    float value = NAN;
    const bool ok = AParcel_readFloat(reply, &value) == STATUS_OK;
    AParcel_delete(reply);
    return ok ? std::optional<float>(value) : std::nullopt;
}

AIBinder *acquire_service(Service &svc, bool required) {
    if (svc.binder) {
        AIBinder_decStrong(svc.binder);
        svc.binder = nullptr;
    }
    svc.binder = AServiceManager_getService(svc.name);
    for (int i = 0; !svc.binder && required && i < 100; ++i) { // up to 10 s during boot
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        svc.binder = AServiceManager_getService(svc.name);
    }
    if (svc.binder) {
        if (!svc.clazz) svc.clazz = AIBinder_Class_define(svc.descriptor, noop_create, noop_destroy, noop_transact);
        if (!svc.clazz || !AIBinder_associateClass(svc.binder, svc.clazz)) {
            LOGW_TAG(TAG, "Cannot associate interface {} with service '{}'", svc.descriptor, svc.name);
            AIBinder_decStrong(svc.binder);
            svc.binder = nullptr;
        }
    }
    return svc.binder;
}

// ── Status file ───────────────────────────────────────────────────────────────

std::string render(const Status &s) {
    std::ostringstream o;
    o << "synthesis_version " << PROTOCOL_VERSION << '\n'
      << "focused_app " << s.focused_app << '\n'
      << "screen_awake " << s.screen_awake << '\n'
      << "battery_saver " << s.battery_saver << '\n'
      << "zen_mode " << s.zen_mode << '\n'
      << "charging_state " << s.charging_state << '\n'
      << "thermal_status " << s.thermal_status << '\n'
      << "audio_active " << s.audio_active << '\n'
      << "thermal_api_available " << s.thermal_api_available << '\n'
      << "kernel_is_gki " << s.kernel_is_gki << '\n';
    if (s.thermal_level >= 0) o << "thermal_level " << s.thermal_level << '\n';
    if (s.battery_level >= 0) o << "battery_level " << s.battery_level << '\n';
    if (!s.battery_temp.empty()) o << "battery_temp " << s.battery_temp << '\n';
    o << "call_active " << s.call_active << '\n';
    return o.str();
}

/// tmp + fsync + rename; O_EXCL|O_NOFOLLOW so a planted symlink cannot redirect the write.
bool write_atomically(const std::string &path, const std::string &content) {
    const std::string tmp = path + ".tmp";
    unlink(tmp.c_str());
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    const bool written = write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size());
    const bool synced = fsync(fd) == 0;
    close(fd);
    if (!written || !synced || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

/// Caller holds g.mutex.
void publish_locked() {
    const std::string text = render(g.status);
    if (text == g.last_written) return;
    if (write_atomically(SYNTHESIS_CORE_FILE, text)) {
        g.last_written = text;
    } else {
        LOGW_TAG(TAG, "Failed to write {}: {}", SYNTHESIS_CORE_FILE, strerror(errno));
    }
}

// ── Sources ───────────────────────────────────────────────────────────────────

std::string package_for_uid(int32_t uid) {
    if (auto it = g.uid_names.find(uid); it != g.uid_names.end()) return it->second;
    std::string name;
    if (AParcel *reply = transact(g.package, Tx::GetNameForUid, [uid](AParcel *in) { AParcel_writeInt32(in, uid); })) {
        AParcel_readString(reply, &name, string_allocator);
        AParcel_delete(reply);
    }
    // Strip a trailing NUL some builds include, keep only a sane package token.
    name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
    for (char &c : name) {
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == ':' || c == '-')) c = '_';
    }
    if (name.size() > 127) name.resize(127);
    if (!name.empty()) g.uid_names[uid] = name;
    return name;
}

/// Caller holds g.mutex.
void update_focused_locked() {
    std::string focused = "none 0 0";
    if (!g.foreground.empty()) {
        const auto [pid, uid] = g.foreground.back();
        const std::string pkg = package_for_uid(uid);
        if (!pkg.empty()) focused = pkg + " " + std::to_string(pid) + " " + std::to_string(uid);
    }
    g.status.focused_app = focused;
}

void on_foreground_changed(int32_t pid, int32_t uid, bool foreground) {
    std::lock_guard lock(g.mutex);
    std::erase_if(g.foreground, [pid](const auto &entry) { return entry.first == pid; });
    if (foreground) g.foreground.emplace_back(pid, uid);
    update_focused_locked();
    publish_locked();
}

void on_process_died(int32_t pid, int32_t uid) {
    std::lock_guard lock(g.mutex);
    const auto before = g.foreground.size();
    std::erase_if(g.foreground, [pid](const auto &entry) { return entry.first == pid; });
    if (g.foreground.size() != before) {
        update_focused_locked();
        publish_locked();
    }
    (void)uid;
}

binder_status_t process_observer_transact(AIBinder *, uint32_t tx, const AParcel *in, AParcel *) {
    if (tx == code(Tx::OnForegroundActivitiesChanged)) {
        int32_t pid = -1, uid = -1;
        bool foreground = false;
        AParcel_readInt32(in, &pid);
        AParcel_readInt32(in, &uid);
        AParcel_readBool(in, &foreground);
        if (pid > 0 && uid >= 0) on_foreground_changed(pid, uid, foreground);
        return STATUS_OK;
    }
    if (tx == code(Tx::OnProcessDied)) {
        int32_t pid = -1, uid = -1;
        AParcel_readInt32(in, &pid);
        AParcel_readInt32(in, &uid);
        if (pid > 0) on_process_died(pid, uid);
        return STATUS_OK;
    }
    return STATUS_OK; // other IProcessObserver callbacks are not needed
}

void sample_interactive_locked() {
    if (auto v = read_bool(transact(g.power, Tx::IsInteractive))) g.status.screen_awake = *v ? 1 : 0;
}

binder_status_t display_callback_transact(AIBinder *, uint32_t tx, const AParcel *, AParcel *) {
    if (tx != code(Tx::OnDisplayEvent)) return STATUS_OK;
    std::lock_guard lock(g.mutex);
    sample_interactive_locked();
    publish_locked();
    g.wake.notify_all(); // poll interval depends on the screen state
    return STATUS_OK;
}

std::string read_file(const char *path) {
    std::ifstream f(path);
    std::string value;
    std::getline(f, value);
    return value;
}

std::string format_decimal(float value, int digits) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", digits, static_cast<double>(value)); // "C" locale: '.'
    return buf;
}

void sample_battery_locked() {
    constexpr const char *base = "/sys/class/power_supply/battery/";
    const std::string status = read_file((std::string(base) + "status").c_str());
    g.status.charging_state = (status == "Charging" || status == "Full") ? 1 : 0;

    const std::string capacity = read_file((std::string(base) + "capacity").c_str());
    int level = -1;
    if (std::sscanf(capacity.c_str(), "%d", &level) == 1 && level >= 0 && level <= 100) g.status.battery_level = level;

    const std::string temp = read_file((std::string(base) + "temp").c_str());
    int tenths = 0;
    if (std::sscanf(temp.c_str(), "%d", &tenths) == 1 && tenths >= -400 && tenths <= 1200) {
        g.status.battery_temp = format_decimal(static_cast<float>(tenths) / 10.0f, 1);
    }
}

void sample_all_locked() {
    sample_interactive_locked();
    if (auto v = read_bool(transact(g.power, Tx::IsPowerSaveMode))) g.status.battery_saver = *v ? 1 : 0;

    if (auto v = read_int(transact(g.notification, Tx::GetZenMode))) {
        g.status.zen_mode = (*v >= 0 && *v <= 3) ? *v : 0;
    }

    if (auto v = read_float(transact(g.thermal, Tx::GetThermalHeadroom, [](AParcel *in) { AParcel_writeInt32(in, 1); }))) {
        g.status.thermal_status = std::isnan(*v) ? "-1.00" : format_decimal(std::clamp(*v, 0.0f, 1.0f), 2);
    }
    if (auto v = read_int(transact(g.thermal, Tx::GetCurrentThermalStatus))) {
        if (*v >= 0 && *v <= 6) g.status.thermal_level = *v;
    }

    if (auto v = read_bool(transact(g.audio, Tx::IsMusicActive, [](AParcel *in) { AParcel_writeBool(in, false); }))) {
        g.status.audio_active = *v ? 1 : 0;
    }
    if (auto v = read_int(transact(g.audio, Tx::GetMode))) {
        // MODE_IN_CALL (2), MODE_IN_COMMUNICATION (3), MODE_CALL_SCREENING (4)
        g.status.call_active = (*v == 2 || *v == 3 || *v == 4) ? 1 : 0;
    }

    sample_battery_locked();
}

bool connect_locked();

/// Caller holds g.mutex. True when system_server (and with it every service
/// proxy and registered callback) is gone.
bool system_dead_locked() {
    return g.system_died.load() || (g.activity.binder && !AIBinder_isAlive(g.activity.binder)) ||
           (g.power.binder && !AIBinder_isAlive(g.power.binder));
}

/// Caller holds g.mutex. After a system_server restart (crash or soft reboot)
/// our observers are no longer registered, so the foreground app would stay
/// stale forever: drop the old state and register again with the new instance.
void reconnect_locked() {
    LOGW_TAG(TAG, "system_server died, reconnecting");
    g.foreground.clear();
    g.uid_names.clear();
    g.status.focused_app = "none 0 0";
    publish_locked();

    g.system_died = false;
    if (connect_locked()) {
        LOGI_TAG(TAG, "Reconnected to system services");
    } else {
        LOGW_TAG(TAG, "Reconnect failed ({}), retrying", g.error);
        g.system_died = true;
    }
}

[[noreturn]] void poll_loop() {
    pthread_setname_np(pthread_self(), "NativeMonitor");
    std::unique_lock lock(g.mutex);
    while (true) {
        if (system_dead_locked()) reconnect_locked();
        if (!g.system_died) {
            sample_all_locked();
            publish_locked();
        }
        // 1 s while the screen is on (thermal headroom has no callback), 10 s when off.
        auto interval = g.status.screen_awake ? std::chrono::seconds(1) : std::chrono::seconds(10);
        if (g.system_died) interval = std::chrono::seconds(2);
        g.wake.wait_for(lock, interval);
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────

bool load_codes() {
    std::ifstream file(BINDER_CODES_FILE);
    if (!file) {
        g.error = std::string(BINDER_CODES_FILE) + " missing";
        return false;
    }
    std::unordered_map<std::string, uint32_t> resolved;
    std::string line;
    while (std::getline(file, line)) {
        const auto space = line.rfind(' ');
        if (space == std::string::npos) continue;
        unsigned long value = 0;
        if (std::sscanf(line.c_str() + space + 1, "%lu", &value) == 1 && value > 0 && value <= 0x00ffffff) {
            resolved[line.substr(0, space)] = static_cast<uint32_t>(value);
        }
    }
    for (const auto &q : kQueries) {
        if (auto it = resolved.find(q.query); it != resolved.end()) {
            g.codes[q.tx] = it->second;
        } else if (q.required) {
            g.error = std::string("required transaction code missing: ") + q.query;
            return false;
        }
    }
    return true;
}

bool register_observer(Service &svc, Tx tx, AIBinder *callback, const char *what) {
    AParcel *reply = transact(svc, tx, [callback](AParcel *in) { AParcel_writeStrongBinder(in, callback); });
    if (!reply) {
        LOGW_TAG(TAG, "Registering {} failed", what);
        return false;
    }
    AParcel_delete(reply);
    LOGI_TAG(TAG, "{} registered", what);
    return true;
}

void on_system_died(void *) {
    g.system_died = true;
    g.wake.notify_all();
}

/// Caller holds g.mutex (or runs before the poll thread exists). Acquires the
/// services and registers the observers; reused after a system_server restart.
bool connect_locked() {
    for (Service *svc : {&g.power, &g.activity, &g.package}) {
        if (!acquire_service(*svc, true)) {
            g.error = std::string("service unavailable: ") + svc->name;
            return false;
        }
    }
    for (Service *svc : {&g.display, &g.notification, &g.thermal, &g.audio}) {
        if (!acquire_service(*svc, false)) LOGW_TAG(TAG, "Optional service '{}' unavailable", svc->name);
    }

    if (!g.process_observer) {
        AIBinder_Class *observer_class =
            AIBinder_Class_define("android.app.IProcessObserver", noop_create, noop_destroy, process_observer_transact);
        g.process_observer = observer_class ? AIBinder_new(observer_class, nullptr) : nullptr;
    }
    if (!g.process_observer ||
        !register_observer(g.activity, Tx::RegisterProcessObserver, g.process_observer, "IProcessObserver")) {
        g.error = "registerProcessObserver failed";
        return false;
    }

    if (g.display.binder && code(Tx::RegisterDisplayCallback) && code(Tx::OnDisplayEvent)) {
        if (!g.display_callback) {
            AIBinder_Class *display_class = AIBinder_Class_define("android.hardware.display.IDisplayManagerCallback",
                                                                  noop_create, noop_destroy, display_callback_transact);
            g.display_callback = display_class ? AIBinder_new(display_class, nullptr) : nullptr;
        }
        if (g.display_callback) {
            register_observer(g.display, Tx::RegisterDisplayCallback, g.display_callback, "IDisplayManagerCallback");
        }
    }

    // activity lives in system_server: its death means every proxy is stale.
    if (!g.death_recipient) g.death_recipient = AIBinder_DeathRecipient_new(on_system_died);
    if (!g.death_recipient || AIBinder_linkToDeath(g.activity.binder, g.death_recipient, nullptr) != STATUS_OK) {
        LOGW_TAG(TAG, "linkToDeath failed; relying on liveness checks");
    }

    g.status.thermal_api_available = (g.thermal.binder && code(Tx::GetThermalHeadroom)) ? 1 : 0;
    return true;
}

bool is_gki_kernel() {
    struct utsname u {};
    if (uname(&u) != 0) return false;
    const std::string release = u.release;
    const auto pos = release.find("-android");
    return pos != std::string::npos && pos + 8 < release.size() && isdigit(static_cast<unsigned char>(release[pos + 8]));
}

} // namespace

namespace NativeMonitor {

bool start() {
    if (access(FORCE_JAVA_MONITOR_FILE, F_OK) == 0) {
        g.error = std::string(FORCE_JAVA_MONITOR_FILE) + " present";
        return false;
    }
    if (!BinderNDK_hasSymbol("AIBinder_transact")) {
        g.error = "libbinder_ndk unavailable";
        return false;
    }
    if (!load_codes()) return false;

    {
        std::lock_guard lock(g.mutex);
        if (!connect_locked()) return false;
        g.status.kernel_is_gki = is_gki_kernel() ? 1 : 0;
        sample_all_locked();
        publish_locked();
    }

    ABinderProcess_startThreadPool();
    std::thread(poll_loop).detach();

    LOGI_TAG(TAG, "Native monitor started (thermal={}, audio={}, display callback={})",
             g.thermal.binder != nullptr, g.audio.binder != nullptr, g.display_callback != nullptr);
    return true;
}

std::string last_error() {
    std::lock_guard lock(g.mutex);
    return g.error;
}

} // namespace NativeMonitor
