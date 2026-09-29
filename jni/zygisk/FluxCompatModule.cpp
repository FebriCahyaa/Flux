// Flux Zygisk compatibility provider.
//
// Built only against the public Zygisk module API (zygisk.hpp, unmodified). This module is an
// execution backend for Flux's Compatibility Engine: it never decides what to override. It reads
// the plan fluxd armed for a package and applies exactly the layers that plan lists, inside that
// package's own process only.
//
//   preAppSpecialize   (zygote privilege) cheap filter on armed.list via the module dir fd; only a
//                      candidate asks the root companion. Everything else unloads this library.
//   companion          (root) resolves the package from the UID, validates the plan, decides.
//   postAppSpecialize  (app sandbox) applies the decision: Java Build fields through JNI, native
//                      hooks through the GOT patcher, then reports the outcome back.
//
// Not a system_server module, not an anti-cheat bypass, and nothing here hides itself.

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include <rapidjson/document.h>

#include "zygisk.hpp"

#include "GotHook.hpp"
#include "Interpose.hpp"
#include "ProviderPaths.hpp"
#include "ProviderPlan.hpp"

using namespace flux::compat;
using namespace flux::compat::provider;
namespace ip = flux::zygisk::interpose;

namespace {

// Paths. Fixed on device; the host test points them at a temp directory through the environment.
#ifdef FLUX_HOST_TEST
std::string path_of(const char *env, const char *def) {
    const char *e = std::getenv(env);
    return e ? e : def;
}
#else
std::string path_of(const char *, const char *def) { return def; }
#endif
std::string state_dir() { return path_of("FLUX_TEST_STATE_DIR", FLUX_PROVIDER_STATE_DIR); }
std::string plans_dir() { return state_dir() + "/plans"; }
std::string proc_dir() { return state_dir() + "/proc"; }
std::string info_file() { return state_dir() + "/provider.json"; }
std::string packages_list() { return path_of("FLUX_TEST_PACKAGES_LIST", FLUX_PACKAGES_LIST); }

// -- tiny framed IPC: uint32 length + JSON --------------------------------------------------------

bool write_all(int fd, const void *buf, size_t n) {
    auto *p = static_cast<const char *>(buf);
    while (n) {
        ssize_t w = ::write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}
bool read_all(int fd, void *buf, size_t n) {
    auto *p = static_cast<char *>(buf);
    while (n) {
        ssize_t r = ::read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}
bool send_msg(int fd, const std::string &s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    return write_all(fd, &len, sizeof len) && write_all(fd, s.data(), s.size());
}
bool recv_msg(int fd, std::string &out) {
    uint32_t len = 0;
    if (!read_all(fd, &len, sizeof len) || len > (1u << 20)) return false;
    out.resize(len);
    return len == 0 || read_all(fd, out.data(), len);
}

int64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

std::string read_file(const std::string &path, size_t cap = 1u << 20) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    std::string s;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0 && s.size() < cap) s.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return s;
}

bool write_file_atomic(const std::string &path, const std::string &content) {
    const std::string tmp = path + ".tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = write_all(fd, content.data(), content.size());
    ::close(fd);
    return ok && ::rename(tmp.c_str(), path.c_str()) == 0;
}

// =================================================================================================
// Root companion
// =================================================================================================

std::string boot_id() {
    std::string s = read_file("/proc/sys/kernel/random/boot_id", 128);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

bool daemon_alive(int64_t pid) {
    if (pid <= 1) return false;
    // A recycled PID must not vouch for a dead daemon: check the process really is fluxd.
    std::string cmd = read_file("/proc/" + std::to_string(pid) + "/cmdline", 256);
    return cmd.find("fluxd") != std::string::npos;
}

/// Packages that own this app id, from the package manager's own list.
std::vector<std::string> packages_for_app_id(int app_id) {
    std::vector<std::string> out;
    std::string txt = read_file(packages_list(), 4u << 20);
    size_t pos = 0;
    while (pos < txt.size()) {
        size_t eol = txt.find('\n', pos);
        if (eol == std::string::npos) eol = txt.size();
        std::string line = txt.substr(pos, eol - pos);
        pos = eol + 1;
        size_t a = line.find(' ');
        if (a == std::string::npos) continue;
        size_t b = line.find(' ', a + 1);
        int uid = std::atoi(line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1).c_str());
        if (uid == app_id) out.push_back(line.substr(0, a));
    }
    return out;
}

void ensure_provider_info() {
    static bool done = false; // racy by design: writing the same file twice is harmless
    if (done) return;
    done = true;
    ::mkdir(state_dir().c_str(), 0755);
    ::mkdir(proc_dir().c_str(), 0755);
    ProviderInfo pi;
    pi.loaded = true;
    pi.api_version = ZYGISK_API_VERSION;
    pi.boot_id = boot_id();
    pi.companion_pid = getpid();
    pi.started_ms = now_ms();
    write_file_atomic(info_file(), provider_info_to_json(pi));
}

void write_status(const ProcStatus &s) {
    ::mkdir(proc_dir().c_str(), 0755);
    ProcStatus copy = s;
    copy.updated_ms = now_ms();
    write_file_atomic(proc_dir() + "/" + std::to_string(s.pid) + ".json", proc_status_to_json(copy));
}

Decision decide(int64_t pid, int uid, const std::string &process, ProcStatus &status_out, bool &write_it) {
    Decision d;
    write_it = false;
    const std::vector<std::string> candidates = packages_for_app_id(uid % 100000);
    if (candidates.empty()) return d; // not an installed app (or list unreadable): not a target

    Env env{now_ms(), boot_id(), false};
    for (const auto &pkg : candidates) {
        std::string text = read_file(plans_dir() + "/" + pkg + ".json", 64u << 10);
        if (text.empty()) continue;
        Plan plan;
        std::string err;
        if (!plan_from_json(text, plan, err) || plan.package != pkg) continue;

        Match m = match_process(plan, ProcessInfo{process, pkg, uid});
        if (!m.matched) {
            // A process of a targeted package that is simply outside the scope is not an error.
            if (m.reject == Reject::ProcessNotInScope) return d;
            continue;
        }
        env.daemon_alive = daemon_alive(plan.daemon_pid);
        Validation v = validate_plan(plan, env);

        d.package = pkg;
        d.transaction_id = plan.transaction_id;
        d.profile = plan.profile;
        d.layers = v.layers;
        status_out.package = pkg;
        status_out.process = process;
        status_out.transaction_id = plan.transaction_id;
        status_out.pid = pid;
        status_out.uid = uid;
        write_it = true;
        if (!v.ok()) {
            d.result = Decision::Result::Rejected;
            d.reject = v.reject;
            d.detail = v.detail;
            status_out.state = ProcState::Failed;
            for (const auto &lv : v.layers) status_out.layers.push_back({lv.layer, "failed", lv.reason.empty() ? v.detail : lv.reason});
            if (status_out.layers.empty()) status_out.layers.push_back({Layer::Device, "failed", std::string(to_string(v.reject)) + ": " + v.detail});
            return d;
        }
        d.result = Decision::Result::Target;
        d.items = items_from_plan(plan, v);
        status_out.state = ProcState::Matched;
        for (const auto &lv : v.layers)
            status_out.layers.push_back({lv.layer, lv.usable ? "matched" : "unsupported", lv.reason});
        return d;
    }
    return d;
}

bool pid_alive(int64_t pid) { return pid > 1 && ::kill(static_cast<pid_t>(pid), 0) == 0; }

void companion_handler(int fd) {
    ensure_provider_info();
    // A wedged client must not pin a companion thread forever while it is still at the handshake.
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    std::string req;
    if (!recv_msg(fd, req)) return;
    rapidjson::Document rd;
    rd.Parse(req.c_str());
    if (rd.HasParseError() || !rd.IsObject()) return;
    int64_t pid = rd.HasMember("pid") && rd["pid"].IsInt64() ? rd["pid"].GetInt64() : 0;
    int uid = rd.HasMember("uid") && rd["uid"].IsInt() ? rd["uid"].GetInt() : -1;
    std::string proc = rd.HasMember("process") && rd["process"].IsString() ? rd["process"].GetString() : "";
    if (pid <= 1 || uid < 0 || proc.empty()) return;

    ProcStatus st;
    bool write_it = false;
    Decision d = decide(pid, uid, proc, st, write_it);
    if (write_it) write_status(st);
    send_msg(fd, decision_to_json(d));
    if (d.result != Decision::Result::Target) return; // nothing further to track

    // Track the process: the module reports what it did, EOF means the process (or its fd) is gone.
    timeval none{0, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof none);
    for (;;) {
        std::string msg;
        if (!recv_msg(fd, msg)) break;
        rapidjson::Document md;
        md.Parse(msg.c_str());
        if (md.HasParseError() || !md.IsObject()) continue;
        if (md.HasMember("observed") && md["observed"].IsString()) {
            const std::string what = md["observed"].GetString();
            for (auto &l : st.layers)
                if ((what == "gl" || what == "egl" || what == "vk") && l.layer == Layer::Gpu && l.state != "failed") l.state = "observed";
            // One kind of GPU query answered is evidence; record which one.
            for (auto &l : st.layers)
                if (l.layer == Layer::Gpu && l.state == "observed" && l.detail.find(what) == std::string::npos)
                    l.detail += (l.detail.empty() ? "" : ",") + what;
        } else if (md.HasMember("layers") && md["layers"].IsArray()) {
            st.layers.clear();
            for (const auto &l : md["layers"].GetArray()) {
                std::string ln = l.HasMember("layer") && l["layer"].IsString() ? l["layer"].GetString() : "";
                Layer ly = ln == "cpu" ? Layer::Cpu : ln == "gpu" ? Layer::Gpu : Layer::Device;
                st.layers.push_back({ly, l.HasMember("state") && l["state"].IsString() ? l["state"].GetString() : "failed",
                                     l.HasMember("detail") && l["detail"].IsString() ? l["detail"].GetString() : ""});
            }
        }
        // Overall state from the layers: verified only when every layer is verified or observed.
        bool all_ok = !st.layers.empty(), any_ok = false;
        for (const auto &l : st.layers) {
            const bool ok = l.state == "verified" || l.state == "observed";
            const bool live = ok || l.state == "installed" || l.state == "armed";
            all_ok = all_ok && ok;
            any_ok = any_ok || live;
        }
        st.state = all_ok ? ProcState::Verified : any_ok ? ProcState::Applied : ProcState::Failed;
        write_status(st);
    }
    // EOF: either the process died, or the fd was closed by zygote's cleanup. Only the former ends the status.
    if (!pid_alive(pid)) {
        st.state = ProcState::Ended;
        write_status(st);
    }
}

// =================================================================================================
// Module (runs in every app process until it unloads itself)
// =================================================================================================

class FluxCompatModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        std::string process;
        if (args->nice_name) {
            const char *n = env_->GetStringUTFChars(args->nice_name, nullptr);
            if (n) {
                process = n;
                env_->ReleaseStringUTFChars(args->nice_name, n);
            }
        }
        const int uid = args->uid;

        if (process.empty() || !candidate(process, uid)) return leave();

        int fd = api_->connectCompanion();
        if (fd < 0) return leave();
        api_->exemptFd(fd); // keep the socket across specialization so the outcome can be reported
        std::string req = "{\"pid\":" + std::to_string(getpid()) + ",\"uid\":" + std::to_string(uid) +
                          ",\"process\":\"" + json_escape(process) + "\"}";
        std::string reply;
        std::string err;
        if (!send_msg(fd, req) || !recv_msg(fd, reply) || !decision_from_json(reply, decision_, err) ||
            decision_.result != Decision::Result::Target) {
            ::close(fd);
            return leave();
        }
        fd_ = fd;
        target_ = true;
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!target_) return;
        apply();
    }

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    bool target_ = false;
    int fd_ = -1;
    Decision decision_;
    std::vector<LayerResult> results_;
    flux::zygisk::GotHooker *hooker_ = nullptr; // process-lifetime object (hooks call into it)

    static std::string json_escape(const std::string &s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            if (static_cast<unsigned char>(c) >= 0x20) o += c;
        }
        return o;
    }

    /// Not a target: give the library back so nothing of us stays in the process.
    void leave() { api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY); }

    /// The cheap filter. One small read through the module dir; no IPC for ordinary apps.
    bool candidate(const std::string &process, int uid) {
        int dir = api_->getModuleDir();
        if (dir < 0) return true; // cannot tell: let the companion decide rather than silently do nothing
        int fd = openat(dir, FLUX_PROVIDER_ARMED_NAME, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return errno != ENOENT; // no list = nothing armed; any other failure = unreadable, ask the companion
        std::string text;
        char buf[2048];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof buf)) > 0 && text.size() < (64u << 10)) text.append(buf, static_cast<size_t>(n));
        ::close(fd);
        return armed_candidate(armed_list_from_text(text), process, uid);
    }

    void report() {
        std::string msg = "{\"layers\":[";
        for (size_t i = 0; i < results_.size(); ++i) {
            if (i) msg += ",";
            msg += std::string("{\"layer\":\"") + to_string(results_[i].layer) + "\",\"state\":\"" + json_escape(results_[i].state) +
                   "\",\"detail\":\"" + json_escape(results_[i].detail) + "\"}";
        }
        msg += "]}";
        if (fd_ >= 0 && !send_msg(fd_, msg)) fd_ = -1; // fd was reclaimed: the outcome stays with the companion's last view
    }

    LayerResult &result_for(Layer l) {
        for (auto &r : results_)
            if (r.layer == l) return r;
        results_.push_back({l, "applied", ""});
        return results_.back();
    }

    void apply() {
        const Items &items = decision_.items;
        for (const auto &lv : decision_.layers)
            if (lv.usable) result_for(lv.layer);
            else results_.push_back({lv.layer, "unsupported", lv.reason});

        // ---- Java Build fields (device and cpu layers) ----------------------------------------------
        if (!items.build.empty()) apply_build_fields(items);

        // ---- native hooks: properties, GL/EGL, Vulkan ---------------------------------------------------
        const bool need_hooks = !items.props.empty() || !items.gl.empty() || items.vk.any();
        if (need_hooks) install_hooks(items);

        report();
    }

    void apply_build_fields(const Items &items) {
        JNIEnv *env = env_;
        jclass build = env->FindClass("android/os/Build");
        if (!build) {
            env->ExceptionClear();
            for (auto &r : results_)
                if (r.layer != Layer::Gpu && r.state != "unsupported") { r.state = "failed"; r.detail = "android.os.Build not found"; }
            return;
        }
        std::vector<std::pair<Layer, std::string>> failed;
        std::vector<Layer> touched;
        for (const auto &[key, value] : items.build) {
            auto layer = layer_of_field(key);
            if (!layer) continue;
            jfieldID f = env->GetStaticFieldID(build, key.c_str(), "Ljava/lang/String;");
            if (!f) { // e.g. SOC_MODEL before API 31: not fabricated, reported
                env->ExceptionClear();
                failed.emplace_back(*layer, key + " does not exist on this Android version");
                continue;
            }
            jstring js = env->NewStringUTF(value.c_str());
            env->SetStaticObjectField(build, f, js);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                failed.emplace_back(*layer, key + ": write failed");
                continue;
            }
            // Verify by reading back what an app would read.
            jstring back = static_cast<jstring>(env->GetStaticObjectField(build, f));
            bool same = false;
            if (back) {
                const char *c = env->GetStringUTFChars(back, nullptr);
                same = c && value == c;
                if (c) env->ReleaseStringUTFChars(back, c);
                env->DeleteLocalRef(back);
            }
            env->DeleteLocalRef(js);
            if (!same) failed.emplace_back(*layer, key + ": read-back differs");
            else touched.push_back(*layer);
        }
        for (Layer l : {Layer::Device, Layer::Cpu}) {
            LayerResult *r = nullptr;
            for (auto &x : results_)
                if (x.layer == l && x.state != "unsupported") r = &x;
            if (!r) continue;
            bool had = false;
            for (auto &x : touched) had = had || x == l;
            std::string why;
            for (auto &f : failed)
                if (f.first == l) why += (why.empty() ? "" : "; ") + f.second;
            if (!why.empty()) { r->state = "failed"; r->detail = why; }
            else if (had) { r->state = "verified"; r->detail = "Build fields read back"; }
        }
        env->DeleteLocalRef(build);
    }

    void install_hooks(const Items &items) {
        // Report the first time each mechanism actually answers a query.
        int fd = fd_;
        ip::set_items(items, [fd](const char *what) {
            // Async-safe enough: a fixed short message, one write(2).
            static const char gl[] = "{\"observed\":\"gl\"}", egl[] = "{\"observed\":\"egl\"}", vk[] = "{\"observed\":\"vk\"}",
                              prop[] = "{\"observed\":\"prop\"}";
            const char *m = !strcmp(what, "gl") ? gl : !strcmp(what, "egl") ? egl : !strcmp(what, "vk") ? vk : prop;
            uint32_t len = static_cast<uint32_t>(strlen(m));
            if (fd >= 0) {
                char frame[64];
                memcpy(frame, &len, 4);
                memcpy(frame + 4, m, len);
                (void)!::write(fd, frame, 4 + len);
            }
        });
        hooker_ = new flux::zygisk::GotHooker(ip::specs_for(items, /*watch_dlopen=*/true), &ip::accept_object);
        ip::set_hooker(hooker_);
        hooker_->scan_new_objects();

        const size_t watch = hooker_->patched_for("dlopen") + hooker_->patched_for("android_dlopen_ext");
        const auto stats = hooker_->stats();
        const size_t props = hooker_->patched_for("__system_property_get");
        const size_t gl = hooker_->patched_for("glGetString") + hooker_->patched_for("eglQueryString");
        const size_t vk = hooker_->patched_for("vkGetPhysicalDeviceProperties") + hooker_->patched_for("vkGetPhysicalDeviceProperties2") +
                          hooker_->patched_for("vkGetInstanceProcAddr");

        auto note = [&](Layer l, const std::string &s) {
            LayerResult &r = result_for(l);
            r.detail += (r.detail.empty() ? "" : "; ") + s;
        };
        if (!items.props.empty()) {
            note(Layer::Device, "props slots=" + std::to_string(props));
        }
        if (!items.gl.empty() || items.vk.any()) {
            LayerResult &r = result_for(Layer::Gpu);
            if (r.state != "unsupported") {
                // "installed": slots patched now. "armed": nothing imports the symbols yet but new
                // libraries are watched. "failed": neither, so no query can ever be intercepted.
                if (gl + vk > 0) { r.state = "installed"; }
                else if (watch > 0) { r.state = "armed"; }
                else { r.state = "failed"; r.detail = "no import slot to patch and no library watcher installed"; }
                r.detail += (r.detail.empty() ? "" : "; ") + std::string("gl/egl slots=") + std::to_string(gl) + " vk slots=" + std::to_string(vk) +
                            " watchers=" + std::to_string(watch);
            }
        }
        if (stats.unsupported_objects)
            note(Layer::Gpu, std::to_string(stats.unsupported_objects) + " object(s) use packed relocations (not patched)");
    }
};

} // namespace

#ifdef FLUX_HOST_TEST
void flux_host_test_companion(int fd) { companion_handler(fd); }
#endif

REGISTER_ZYGISK_MODULE(FluxCompatModule)
REGISTER_ZYGISK_COMPANION(companion_handler)
