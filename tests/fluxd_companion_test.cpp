// Runs the provider's root companion (jni/zygisk/FluxCompatModule.cpp) for real: over a socketpair,
// with a temp state directory, a fake packages.list and plans written by the same make_plan()
// the daemon uses. The test binary's name contains "fluxd" on purpose: the companion only trusts a
// plan whose daemon pid is a live process called fluxd.
//
// What this proves: the decision path (UID owner, scope, transaction, boot, daemon liveness,
// expiry), the status files the daemon reads, and that ordinary packages never get a status file.
// What it cannot prove: anything inside a real Android app process (JNI, GOT slots of real libs).

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include "flux_test.hpp"

#include "ProviderPlan.hpp"

extern void flux_host_test_companion(int fd);

using namespace flux::compat;
using namespace flux::compat::provider;

namespace {

std::string g_dir;
constexpr const char *kPkg = "com.example.game";

std::string slurp(const std::string &p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void spit(const std::string &p, const std::string &c) { std::ofstream(p) << c; }
bool exists(const std::string &p) { return access(p.c_str(), F_OK) == 0; }

int64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}
std::string boot() {
    std::string s = slurp("/proc/sys/kernel/random/boot_id");
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

bool send_msg(int fd, const std::string &s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    return write(fd, &n, 4) == 4 && write(fd, s.data(), s.size()) == static_cast<ssize_t>(s.size());
}
bool recv_msg(int fd, std::string &out) {
    uint32_t n = 0;
    if (read(fd, &n, 4) != 4) return false;
    out.resize(n);
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, out.data() + got, n - got);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

ProfileLibrary library() {
    ProfileLibrary lib;
    std::string err;
    lib.load_json(R"({"identities":{
      "dev":{"layer":"device","fields":{"MODEL":"FlagX","BRAND":"Acme"}},
      "gpu":{"layer":"gpu","fields":{"gl_renderer":"Acme GPU 9","vk_device_name":"Acme GPU 9","vk_vendor_id":"0x1234"}}}})", err);
    return lib;
}

Resolution resolution(const std::string &pkg) {
    Resolution r;
    r.package = pkg;
    r.mode = Mode::Advanced;
    r.layers.resize(4);
    for (Layer l : {Layer::Device, Layer::Cpu, Layer::Gpu, Layer::Display}) r.layers[static_cast<size_t>(l)].layer = l;
    r.layers[0].required = true;
    r.layers[0].identity = "dev";
    r.layers[2].required = true;
    r.layers[2].identity = "gpu";
    r.should_apply = true;
    return r;
}

struct PlanKnobs {
    std::string pkg = kPkg;
    std::string boot_id = boot();
    int64_t daemon_pid = getpid();
    int64_t lease_ms = 3'600'000;
    int64_t armed_at = now_ms();
    ProcessScope scope{};
};

void arm(const PlanKnobs &k) {
    Plan p = make_plan(resolution(k.pkg), library(), k.boot_id, k.daemon_pid, k.armed_at, k.lease_ms, k.scope, "game");
    spit(g_dir + "/plans/" + k.pkg + ".json", plan_to_json(p));
}

struct Conn {
    int client = -1;
    std::thread server;
    Decision decision;
    bool ok = false;
    void finish() {
        if (client >= 0) close(client);
        client = -1;
        if (server.joinable()) server.join();
    }
};

/// What a process does at preAppSpecialize: connect, ask, read the decision.
Conn ask(int64_t pid, int uid, const std::string &process) {
    Conn c;
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    c.client = sv[0];
    int srv = sv[1];
    c.server = std::thread([srv] {
        flux_host_test_companion(srv);
        close(srv);
    });
    std::string req = "{\"pid\":" + std::to_string(pid) + ",\"uid\":" + std::to_string(uid) + ",\"process\":\"" + process + "\"}";
    std::string reply, err;
    c.ok = send_msg(c.client, req) && recv_msg(c.client, reply) && decision_from_json(reply, c.decision, err);
    return c;
}

std::string status_file(int64_t pid) { return g_dir + "/proc/" + std::to_string(pid) + ".json"; }
ProcStatus status_of(int64_t pid) {
    ProcStatus s;
    std::string err;
    proc_status_from_json(slurp(status_file(pid)), s, err);
    return s;
}

int64_t dead_pid() {
    pid_t p = fork();
    if (p == 0) _exit(0);
    int st;
    waitpid(p, &st, 0);
    return p; // reaped: no such process
}

void reset() {
    std::system(("rm -rf " + g_dir + " && mkdir -p " + g_dir + "/plans " + g_dir + "/proc").c_str());
    spit(g_dir + "/packages.list",
         "com.example.game 10123 0 /data/user/0/com.example.game default:targetSdkVersion=34 none 0 1\n"
         "com.example.other 10200 0 /data/user/0/com.example.other default:targetSdkVersion=34 none 0 1\n"
         "com.shared.a 10300 0 /data/user/0/com.shared.a default:targetSdkVersion=34 none 0 1\n"
         "com.shared.b 10300 0 /data/user/0/com.shared.b default:targetSdkVersion=34 none 0 1\n");
}

// -----------------------------------------------------------------------------------------------------------

void test_target_process_is_matched_and_tracked() {
    reset();
    arm({});
    auto c = ask(getpid(), 10123, kPkg);
    CHECK(c.ok);
    CHECK(c.decision.result == Decision::Result::Target);
    CHECK_EQ(c.decision.package, kPkg);
    CHECK(!c.decision.transaction_id.empty());
    CHECK_EQ(c.decision.items.build.at("MODEL"), "FlagX");
    CHECK_EQ(c.decision.items.gl.at("renderer"), "Acme GPU 9");
    CHECK(c.decision.items.build.count("SOC_MODEL") == 0);       // CPU layer not in the plan

    // provider.json says the companion (hence the provider) is loaded in this boot
    ProviderInfo pi;
    std::string err;
    CHECK(provider_info_from_json(slurp(g_dir + "/provider.json"), pi, err));
    CHECK(pi.loaded && pi.boot_id == boot());

    ProcStatus s = status_of(getpid());
    CHECK(s.state == ProcState::Matched);
    CHECK_EQ(s.package, kPkg);
    CHECK_EQ(s.transaction_id, c.decision.transaction_id);

    // The module reports its outcome: device verified, gpu only installed (not yet observed)
    send_msg(c.client, R"({"layers":[{"layer":"device","state":"verified","detail":"Build fields read back"},
                                     {"layer":"gpu","state":"installed","detail":"gl/egl slots=2"}]})");
    usleep(150'000);
    s = status_of(getpid());
    CHECK(s.state == ProcState::Applied);                          // NOT verified: the GPU layer has not answered anything yet
    // First real GL query answered in the process
    send_msg(c.client, R"({"observed":"gl"})");
    usleep(150'000);
    s = status_of(getpid());
    CHECK(s.state == ProcState::Verified);
    bool gpu_observed = false;
    for (auto &l : s.layers) gpu_observed = gpu_observed || (l.layer == Layer::Gpu && l.state == "observed");
    CHECK(gpu_observed);

    // fd closed but the process is still alive (zygote reclaimed the fd): the status is NOT ended
    c.finish();
    s = status_of(getpid());
    CHECK(s.state == ProcState::Verified);
}

void test_process_end_is_recorded() {
    reset();
    arm({});
    int64_t gone = dead_pid();
    auto c = ask(gone, 10123, kPkg);
    CHECK(c.decision.result == Decision::Result::Target);
    c.finish();                                                    // EOF and the pid is dead
    CHECK(status_of(gone).state == ProcState::Ended);
}

void test_other_processes_and_packages_are_not_targets() {
    reset();
    arm({});
    auto remote = ask(5001, 10123, std::string(kPkg) + ":remote"); // main-only scope
    CHECK(remote.ok && remote.decision.result == Decision::Result::NotTarget);
    remote.finish();
    CHECK(!exists(status_file(5001)));                              // ordinary and out-of-scope processes leave no trace

    auto other = ask(5002, 10200, "com.example.other");             // installed, but no plan
    CHECK(other.decision.result == Decision::Result::NotTarget);
    other.finish();
    CHECK(!exists(status_file(5002)));

    auto chrome = ask(5003, 10999, "com.android.chrome");           // not in packages.list at all
    CHECK(chrome.decision.result == Decision::Result::NotTarget);
    chrome.finish();

    // A process that merely LOOKS like the game but belongs to another UID owner
    auto lookalike = ask(5004, 10200, kPkg);
    CHECK(lookalike.decision.result == Decision::Result::NotTarget);
    lookalike.finish();
    CHECK(!exists(status_file(5004)));
}

void test_multi_process_scopes() {
    reset();
    PlanKnobs k;
    k.scope = ProcessScope{ProcessScope::Kind::Listed, {std::string(kPkg) + ":engine"}};
    arm(k);
    auto main_p = ask(6001, 10123, kPkg);
    CHECK(main_p.decision.result == Decision::Result::NotTarget);   // main is not listed
    main_p.finish();
    auto engine = ask(6002, 10123, std::string(kPkg) + ":engine");
    CHECK(engine.decision.result == Decision::Result::Target);
    engine.finish();
    auto remote = ask(6003, 10123, std::string(kPkg) + ":remote");
    CHECK(remote.decision.result == Decision::Result::NotTarget);
    remote.finish();
    CHECK(exists(status_file(6002)) && !exists(status_file(6001)) && !exists(status_file(6003)));
}

void test_rejections_are_reported_with_a_reason() {
    struct Case { const char *name; PlanKnobs knobs; Reject expect; };
    PlanKnobs expired;
    expired.armed_at = now_ms() - 7'200'000;
    expired.lease_ms = 3'600'000;
    PlanKnobs other_boot;
    other_boot.boot_id = "some-other-boot-id";
    PlanKnobs dead_daemon;
    dead_daemon.daemon_pid = dead_pid();
    for (auto &cs : {Case{"expired", expired, Reject::Expired}, Case{"other boot", other_boot, Reject::BootMismatch},
                     Case{"daemon gone", dead_daemon, Reject::DaemonGone}}) {
        reset();
        arm(cs.knobs);
        int64_t pid = 7000 + static_cast<int64_t>(cs.expect);
        auto c = ask(pid, 10123, kPkg);
        CHECK(c.ok);
        CHECK(c.decision.result == Decision::Result::Rejected);
        CHECK(c.decision.reject == cs.expect);
        c.finish();
        ProcStatus s = status_of(pid);                              // the daemon can show WHY nothing was applied
        CHECK(s.state == ProcState::Failed);
        CHECK(!s.layers.empty());
        CHECK(s.layers[0].state == "failed");
        (void)cs.name;
    }
}

void test_tampered_or_garbage_plans_are_not_applied() {
    reset();
    Plan p = make_plan(resolution(kPkg), library(), boot(), getpid(), now_ms(), 3'600'000, {}, "game");
    p.identities["device"]["MODEL"] = "Edited"; // changed after arming, transaction id no longer matches
    spit(g_dir + "/plans/" + kPkg + ".json", plan_to_json(p));
    auto c = ask(8001, 10123, kPkg);
    CHECK(c.decision.result == Decision::Result::Rejected && c.decision.reject == Reject::TransactionMismatch);
    c.finish();

    spit(g_dir + "/plans/" + kPkg + ".json", "{not json");
    auto g = ask(8002, 10123, kPkg);
    CHECK(g.decision.result == Decision::Result::NotTarget);        // unreadable plan: real identity
    g.finish();
}

void test_isa_claim_in_plan_is_refused() {
    reset();
    Plan p = make_plan(resolution(kPkg), library(), boot(), getpid(), now_ms(), 3'600'000, {}, "game");
    p.layers.push_back(Layer::Cpu);
    p.identities["cpu"] = {{"hwcap_features", "sve2"}};
    p.transaction_id = compute_transaction_id(p);
    spit(g_dir + "/plans/" + kPkg + ".json", plan_to_json(p));
    auto c = ask(8101, 10123, kPkg);
    CHECK(c.decision.result == Decision::Result::Target);           // device + gpu still apply
    CHECK(c.decision.items.build.count("SOC_MODEL") == 0);
    bool cpu_refused = false;
    for (auto &l : c.decision.layers) cpu_refused = cpu_refused || (l.layer == Layer::Cpu && !l.usable);
    CHECK(cpu_refused);                                              // the fake CPU feature never reaches the process
    c.finish();
}

void test_secondary_user_and_shared_uid() {
    reset();
    arm({});
    auto sec = ask(9001, 1010123, kPkg);                             // Android user 10, same app id
    CHECK(sec.decision.result == Decision::Result::Target);
    sec.finish();

    reset();
    PlanKnobs k;
    k.pkg = "com.shared.b"; // two packages share a UID; only the second one is armed
    arm(k);
    auto b = ask(9002, 10300, "com.shared.b");
    CHECK(b.decision.result == Decision::Result::Target);
    CHECK_EQ(b.decision.package, "com.shared.b");
    b.finish();
    auto a = ask(9003, 10300, "com.shared.a");                        // shares the UID but is not armed
    CHECK(a.decision.result == Decision::Result::NotTarget);
    a.finish();
}

void test_malformed_requests_do_not_crash_or_leave_files() {
    reset();
    arm({});
    for (const char *bad : {"", "{", "[]", "{\"pid\":0,\"uid\":10123,\"process\":\"x\"}", "{\"pid\":5,\"uid\":-1,\"process\":\"x\"}",
                            "{\"pid\":5,\"uid\":10123,\"process\":\"\"}"}) {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        std::thread t([fd = sv[1]] { flux_host_test_companion(fd); close(fd); });
        send_msg(sv[0], bad);
        close(sv[0]);
        t.join();
    }
    CHECK(!exists(status_file(5)) && !exists(status_file(0)));
}

} // namespace

int main() {
    char tmpl[] = "/tmp/flux_companion_XXXXXX";
    g_dir = mkdtemp(tmpl);
    setenv("FLUX_TEST_STATE_DIR", g_dir.c_str(), 1);
    setenv("FLUX_TEST_PACKAGES_LIST", (g_dir + "/packages.list").c_str(), 1);

    test_target_process_is_matched_and_tracked();
    test_process_end_is_recorded();
    test_other_processes_and_packages_are_not_targets();
    test_multi_process_scopes();
    test_rejections_are_reported_with_a_reason();
    test_tampered_or_garbage_plans_are_not_applied();
    test_isa_claim_in_plan_is_refused();
    test_secondary_user_and_shared_uid();
    test_malformed_requests_do_not_crash_or_leave_files();

    std::system(("rm -rf " + g_dir).c_str());
    return flux_test::report("fluxd_companion_test");
}
