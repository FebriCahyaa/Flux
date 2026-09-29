// Host test for jni/RenderBooster.cpp's topology detection (no device needed):
//   g++ -std=c++20 -Ijni -DFLUX_RENDER_HOST_TEST -o /tmp/render_booster_test
//       tests/render_booster_test.cpp jni/RenderBooster.cpp && /tmp/render_booster_test
//
// Covers detect_clusters()/boost_masks() against fake sysfs trees for real device shapes
// (2-cluster 4+4, 3-cluster 1+3+4 and 2+6, single cluster, no cpu_capacity at all) and the
// silicon-binning case that motivated the tolerance in detect_clusters(): cpu_capacity
// differing by a few percent between cores of the same physical cluster.
#include "RenderBooster.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

static int failures = 0;
#define CHECK(c)                                                                      \
    do {                                                                              \
        if (!(c)) {                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c);       \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

namespace fs = std::filesystem;

namespace {

std::string g_root;

void write(const std::string &path, const std::string &content) {
    const fs::path p = g_root + path;
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
}

/// Builds /cpuX/cpu_capacity (or /cpuX/cpufreq/cpuinfo_max_freq when capacities is empty) for
/// every core 0..N-1, where @p capacities[i] is that core's value (0 = no cpu_capacity file at
/// all, forcing the max_freq fallback path).
void build_topology(const std::vector<long> &capacities, const std::vector<long> &max_freqs = {}) {
    fs::remove_all(g_root);
    std::string present = "0-" + std::to_string(capacities.size() - 1);
    write("/present", present);
    for (size_t cpu = 0; cpu < capacities.size(); ++cpu) {
        const std::string base = "/cpu" + std::to_string(cpu);
        if (capacities[cpu] > 0) write(base + "/cpu_capacity", std::to_string(capacities[cpu]));
        const long mf = cpu < max_freqs.size() ? max_freqs[cpu] : capacities[cpu];
        if (mf > 0) write(base + "/cpufreq/cpuinfo_max_freq", std::to_string(mf));
    }
}

std::vector<int> cpus_of(const CpuCluster &c) {
    auto v = c.cpus;
    std::sort(v.begin(), v.end());
    return v;
}

} // namespace

static void test_parse_cpu_list() {
    CHECK((parse_cpu_list("0-3") == std::vector<int>{0, 1, 2, 3}));
    CHECK((parse_cpu_list("0,1,2,3") == std::vector<int>{0, 1, 2, 3}));
    CHECK((parse_cpu_list("0-3,6,7") == std::vector<int>{0, 1, 2, 3, 6, 7}));
    CHECK((parse_cpu_list("4-4") == std::vector<int>{4}));
    CHECK(parse_cpu_list("").empty());
    CHECK(parse_cpu_list("garbage").empty());
    CHECK((parse_cpu_list("0-2,garbage,5") == std::vector<int>{0, 1, 2, 5})); // bad token skipped, rest kept
    CHECK(parse_cpu_list("3-1").empty());     // reversed range: rejected, not silently swapped
    CHECK(parse_cpu_list("-1-3").empty());    // negative
    CHECK((parse_cpu_list("0-1, 2 ,3") == std::vector<int>{0, 1, 2, 3})); // whitespace around tokens
    CHECK((parse_cpu_list("2,0,1") == std::vector<int>{0, 1, 2}));        // sorted + deduped
    CHECK((parse_cpu_list("1,1,1") == std::vector<int>{1}));
}

static void test_is_render_thread() {
    CHECK(is_render_thread("UnityMain"));
    CHECK(is_render_thread("UnityGfxDeviceW"));
    CHECK(is_render_thread("RenderThread"));
    CHECK(is_render_thread("RenderThread 1"));
    CHECK(is_render_thread("GameThread"));
    CHECK(is_render_thread("RHIThread"));
    CHECK(is_render_thread("GLThread 5"));
    CHECK(is_render_thread("MainThread-UE"));
    CHECK(!is_render_thread("Binder:1234_2"));
    // Prefix matching is deliberate (Unreal names its threads "RenderThread 1", "RHIThread", …,
    // never the bare prefix alone), so anything starting with a known prefix counts.
    CHECK(is_render_thread("RenderThreadSomethingElse"));
    CHECK(!is_render_thread(""));
    CHECK(!is_render_thread("Render")); // shorter than any known prefix: no match
}

// 4+4 phones (e.g. Redmi Note 13 Pro 5G / SM6475): the case that motivated masks.game staying
// empty rather than putting every game thread on the 4 big cores (a real bug report, see
// changelog.md — that phone hit 96C running MLBB entirely on its big cluster).
static void test_two_cluster_4plus4() {
    build_topology({/*little x4*/ 300, 300, 300, 300, /*big x4*/ 1024, 1024, 1024, 1024});
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 2);
    CHECK((cpus_of(clusters[0]) == std::vector<int>{4, 5, 6, 7})); // big first (fastest)
    CHECK((cpus_of(clusters[1]) == std::vector<int>{0, 1, 2, 3}));

    auto masks = boost_masks(clusters);
    std::vector<int> render = masks.render;
    std::sort(render.begin(), render.end());
    CHECK((render == std::vector<int>{4, 5, 6, 7})); // render threads: the whole big cluster
    // Only two clusters: the game mask must stay empty (never push other threads onto the
    // sole non-little cluster alongside render threads — that is exactly the 96C bug).
    CHECK(masks.game.empty());
}

// 1+3+4 (a single prime core, e.g. many Snapdragon 8 Gen 2/3 phones).
static void test_three_cluster_1plus3plus4() {
    build_topology({/*little x4*/ 300, 300, 300, 300, /*big x3*/ 700, 700, 700, /*prime x1*/ 1024});
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 3);
    CHECK((cpus_of(clusters[0]) == std::vector<int>{7}));          // prime
    CHECK((cpus_of(clusters[1]) == std::vector<int>{4, 5, 6}));    // big
    CHECK((cpus_of(clusters[2]) == std::vector<int>{0, 1, 2, 3})); // little

    auto masks = boost_masks(clusters);
    std::vector<int> render = masks.render, game = masks.game;
    std::sort(render.begin(), render.end());
    std::sort(game.begin(), game.end());
    // Prime alone is 1 core (<2): render grows to include big too.
    CHECK((render == std::vector<int>{4, 5, 6, 7}));
    // 3+ clusters, prime+big = 4 cores (>=4): game threads get prime+big, never little.
    CHECK((game == std::vector<int>{4, 5, 6, 7}));
}

// 2+6 (small prime/big group, e.g. some MediaTek Dimensity phones): prime+big below 4 cores
// must leave masks.game empty rather than squeezing every game thread onto 2 cores.
static void test_three_cluster_small_fast_group() {
    build_topology({/*little x6*/ 300, 300, 300, 300, 300, 300, /*big x2*/ 1024, 1024});
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 2); // only "big" and "little" here: 2 clusters, not 3
    auto masks = boost_masks(clusters);
    std::vector<int> render = masks.render;
    std::sort(render.begin(), render.end());
    CHECK((render == std::vector<int>{6, 7}));
    CHECK(masks.game.empty()); // 2 clusters: never populated regardless of size
}

// Silicon-binning variance: cores of the same physical cluster reporting slightly different
// cpu_capacity (a few percent) must still be recognised as one cluster, not fragmented.
static void test_binning_variance_same_cluster() {
    build_topology({/*little, +-1%*/ 300, 303, 298, 301,
                    /*big, +-2%*/ 1024, 1005, 1040, 1015});
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 2);
    CHECK(clusters[0].cpus.size() == 4);
    CHECK(clusters[1].cpus.size() == 4);
}

// A real cluster boundary must still be detected even when it happens to be less than the
// binning-tolerance band would allow if it were misread as one cluster — little/big gaps in
// practice are always far larger than the ~15% tolerance, so this checks that a deliberately
// modest but still real gap (30%) is not merged away.
static void test_real_boundary_not_merged() {
    build_topology({300, 300, 300, 300, 420, 420, 420, 420}); // ~40% gap
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 2);
}

// No cpu_capacity anywhere: falls back to cpuinfo_max_freq, same grouping behaviour.
static void test_max_freq_fallback() {
    build_topology({0, 0, 0, 0, 0, 0, 0, 0}, {1800000, 1800000, 1800000, 1800000, 2800000, 2800000, 2800000, 2800000});
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 2);
    CHECK((cpus_of(clusters[0]) == std::vector<int>{4, 5, 6, 7}));
}

// One cpu_capacity missing among many: the whole detection falls back to max_freq (a partial
// cpu_capacity rollout on a kernel must not silently misdetect topology from the incomplete data).
static void test_partial_capacity_falls_back() {
    build_topology({300, 300, 300, 0 /* missing */, 1024, 1024, 1024, 1024},
                   {1800000, 1800000, 1800000, 1800000, 2800000, 2800000, 2800000, 2800000});
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 2);
    CHECK((cpus_of(clusters[0]) == std::vector<int>{4, 5, 6, 7}));
}

// Single cluster (or completely unknown topology): affinity cannot help; both masks stay empty.
static void test_single_cluster() {
    build_topology({500, 500, 500, 500});
    auto clusters = detect_clusters(g_root);
    CHECK(clusters.size() == 1);
    auto masks = boost_masks(clusters);
    CHECK(masks.render.empty());
    CHECK(masks.game.empty());
}

static void test_empty_topology() {
    fs::remove_all(g_root);
    auto clusters = detect_clusters(g_root); // no /present, no /possible: nothing to read
    CHECK(clusters.empty());
    auto masks = boost_masks(clusters);
    CHECK(masks.render.empty());
    CHECK(masks.game.empty());
}

int main() {
    char tmpl[] = "/tmp/flux-render-test-XXXXXX";
    if (!mkdtemp(tmpl)) return 1;
    g_root = tmpl;

    test_parse_cpu_list();
    test_is_render_thread();
    test_two_cluster_4plus4();
    test_three_cluster_1plus3plus4();
    test_three_cluster_small_fast_group();
    test_binning_variance_same_cluster();
    test_real_boundary_not_merged();
    test_max_freq_fallback();
    test_partial_capacity_falls_back();
    test_single_cluster();
    test_empty_topology();

    fs::remove_all(tmpl);
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("render_booster_test: all passed");
    return 0;
}
