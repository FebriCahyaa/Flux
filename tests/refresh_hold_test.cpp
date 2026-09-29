// Host test for jni/RefreshHold.cpp (no device needed):
//   g++ -std=c++20 -pthread -DFLUX_REFRESH_HOST_TEST -Ijni -o /tmp/refresh_hold_test
//       tests/refresh_hold_test.cpp jni/RefreshHold.cpp && /tmp/refresh_hold_test
#include "RefreshHold.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <thread>

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

struct Fake {
    std::map<std::string, std::string> settings{{"min_refresh_rate", "60.0"}, {"peak_refresh_rate", "120.0"}};
    std::string home = "com.miui.home";
    bool applied = true;
    int home_calls = 0;
    std::mutex m;

    RefreshHold::Io io() {
        return {
            .get_setting = [this](const std::string &k) { std::lock_guard l(m); return settings[k]; },
            .put_setting = [this](const std::string &k, const std::string &v) { std::lock_guard l(m); settings[k] = v; },
            .home_package = [this] { ++home_calls; return home; },
            .adaptive_applied = [this] { return applied; },
        };
    }
    std::string min() { std::lock_guard l(m); return settings["min_refresh_rate"]; }
};

using namespace std::chrono_literals;

static void test_parse() {
    CHECK(parse_home_package("priority=0 preferredOrder=0 match=0x108000 specificIndex=-1 isDefault=true\n"
                             "com.miui.home/.launcher.Launcher\n") == "com.miui.home");
    CHECK(parse_home_package("com.android.launcher3/.uioverrides.QuickstepLauncher") == "com.android.launcher3");
    CHECK(parse_home_package("android/com.android.internal.app.ResolverActivity\n").empty());
    CHECK(parse_home_package("No activity found\n").empty());
    CHECK(parse_home_package("").empty());
}

static void test_hold_and_delayed_release() {
    Fake f;
    RefreshHold h(f.io(), 50ms);
    h.update("com.miui.home", true, false, true);
    CHECK(h.held());
    CHECK(f.min() == "120.0");
    h.update("com.android.systemui", true, false, true);  // shade over home: still held
    CHECK(h.held());
    h.update("org.telegram.messenger", true, false, true);
    CHECK(h.held());  // not before the delay
    std::this_thread::sleep_for(150ms);
    CHECK(!h.held());
    CHECK(f.min() == "60.0");
}

static void test_back_home_cancels_release() {
    Fake f;
    RefreshHold h(f.io(), 80ms);
    h.update("com.miui.home", true, false, true);
    h.update("com.whatsapp", true, false, true);
    h.update("com.miui.home", true, false, true);  // back within the delay
    std::this_thread::sleep_for(150ms);
    CHECK(h.held());
    CHECK(f.min() == "120.0");
}

static void test_immediate_release() {
    Fake f;
    RefreshHold h(f.io(), 10s);
    h.update("com.miui.home", true, false, true);
    h.update("com.miui.home", true, true, true);  // game session
    CHECK(!h.held());
    CHECK(f.min() == "60.0");
    h.update("com.miui.home", true, false, true);
    h.update("com.miui.home", false, false, true);  // screen off
    CHECK(f.min() == "60.0");
    h.update("com.miui.home", true, false, true);
    h.update("com.miui.home", true, false, false);  // feature off
    CHECK(f.min() == "60.0");
    h.update("com.miui.home", true, false, true);
    h.release_now();
    CHECK(f.min() == "60.0");
}

static void test_no_hold_cases() {
    {
        Fake f;
        f.applied = false;  // adaptive range not in place, or a game owns the rate
        RefreshHold h(f.io(), 10ms);
        h.update("com.miui.home", true, false, true);
        CHECK(!h.held());
        CHECK(f.min() == "60.0");
    }
    {
        Fake f;
        f.settings["min_refresh_rate"] = "120.0";  // already pinned at peak
        RefreshHold h(f.io(), 10ms);
        h.update("com.miui.home", true, false, true);
        CHECK(!h.held());
    }
    {
        Fake f;
        f.home.clear();  // chooser, no default launcher: only the shade holds
        RefreshHold h(f.io(), 10ms);
        h.update("com.miui.home", true, false, true);
        CHECK(!h.held());
        h.update("com.android.systemui", true, false, true);
        CHECK(h.held());
    }
}

static void test_foreign_change_not_undone() {
    Fake f;
    RefreshHold h(f.io(), 10s);
    h.update("com.miui.home", true, false, true);
    f.settings["min_refresh_rate"] = "90.0";  // the user changed it meanwhile
    h.release_now();
    CHECK(f.min() == "90.0");
}

static void test_home_cached() {
    Fake f;
    RefreshHold h(f.io(), 10s);
    for (int i = 0; i < 5; ++i) h.update("com.whatsapp", true, false, true);
    CHECK(f.home_calls == 1);
    h.update("com.whatsapp", false, false, true);  // screen off resets the cache
    h.update("com.whatsapp", true, false, true);
    CHECK(f.home_calls == 2);
}

static void test_destructor_restores() {
    Fake f;
    {
        RefreshHold h(f.io(), 10s);
        h.update("com.miui.home", true, false, true);
        CHECK(f.min() == "120.0");
    }
    CHECK(f.min() == "60.0");
}

int main() {
    test_parse();
    test_hold_and_delayed_release();
    test_back_home_cancels_release();
    test_immediate_release();
    test_no_hold_cases();
    test_foreign_change_not_undone();
    test_home_cached();
    test_destructor_restores();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("refresh_hold_test: all passed");
    return 0;
}
