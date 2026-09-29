// Host test for jni/base/SynthesisCore/SynthesisCore.cpp — the v3 status-file
// parser that fluxd runs on every inotify event.
//
// Protocol v4 adds keys to this file. The parser is the thing that has to keep
// working, on a v4 file and on a v3 one, so this pins its behaviour: unknown keys
// are ignored, absent keys keep their "unsupported" sentinels, and nothing about
// the existing fourteen fields changes.
#include "SynthesisCore.hpp"

#include "flux_test.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string temp_dir() {
    const char *tmp = std::getenv("TMPDIR");
    return tmp ? std::string(tmp) : std::string("/tmp");
}

/// Writes @p body to a scratch file and parses it. Returns the parser's verdict.
bool parse_text(const std::string &body, SynthesisCore &out) {
    const std::string path = temp_dir() + "/flux_synthesis_core_test.txt";
    {
        std::ofstream f(path, std::ios::trunc);
        f << body;
    }
    const bool ok = SynthesisCoreReader::read(out, path.c_str());
    std::remove(path.c_str());
    return ok;
}

/// A complete protocol 3 status file, in the order SynthesisCore writes it.
const char *kV3File =
    "synthesis_version 3\n"
    "focused_app com.mojang.minecraftpe 1234 10234\n"
    "screen_awake 1\n"
    "battery_saver 0\n"
    "zen_mode 0\n"
    "charging_state 1\n"
    "thermal_status 0.85\n"
    "audio_active 1\n"
    "thermal_api_available 1\n"
    "kernel_is_gki 1\n"
    "thermal_level 2\n"
    "battery_level 76\n"
    "battery_temp 38.5\n"
    "call_active 0\n";

void check_v3_payload(const SynthesisCore &s) {
    CHECK(s.focused_app == std::string("com.mojang.minecraftpe"));
    CHECK_EQ(static_cast<int>(s.focused_pid), 1234);
    CHECK_EQ(static_cast<int>(s.focused_uid), 10234);
    CHECK(s.screen_awake);
    CHECK(!s.battery_saver);
    CHECK_EQ(s.zen_mode, 0);
    CHECK(s.charging);
    CHECK(std::fabs(s.thermal_headroom - 0.85f) < 0.001f);
    CHECK(s.audio_active);
    CHECK(s.thermal_api_available);
    CHECK(s.kernel_is_gki);
    CHECK_EQ(s.thermal_level, 2);
    CHECK_EQ(s.battery_level, 76);
    CHECK(std::fabs(s.battery_temp - 38.5f) < 0.001f);
    CHECK(!s.call_active);
}

} // namespace

static void test_v3_file_parses_unchanged() {
    SynthesisCore s;
    CHECK(parse_text(kV3File, s));
    CHECK_EQ(s.synthesis_version, 3);
    check_v3_payload(s);
}

static void test_v4_file_parses_on_a_v3_parser() {
    // The heart of the compatibility claim: a v4 producer writes the same
    // fourteen keys plus new ones. This build must read every old field
    // correctly and step over the new keys without losing its place.
    const std::string v4 = std::string(
        "synthesis_version 4\n"
        "focused_app com.mojang.minecraftpe 1234 10234\n"
        "screen_awake 1\n"
        "battery_saver 0\n"
        "zen_mode 0\n"
        "charging_state 1\n"
        "thermal_status 0.85\n"
        "audio_active 1\n"
        "thermal_api_available 1\n"
        "kernel_is_gki 1\n"
        "thermal_level 2\n"
        "battery_level 76\n"
        "battery_temp 38.5\n"
        "call_active 0\n"
        "capability_schema 4\n"
        "capability_file /data/adb/.config/flux/capabilities.json\n");

    SynthesisCore s;
    CHECK(parse_text(v4, s));
    CHECK_EQ(s.synthesis_version, 4);
    check_v3_payload(s);
}

static void test_unknown_keys_between_known_ones() {
    // New keys will not always be appended at the end. An unknown key in the
    // middle must not stop the keys after it from being read.
    const std::string mixed =
        "synthesis_version 4\n"
        "focused_app com.example.app 11 1011\n"
        "some_future_key with a multi word value\n"
        "screen_awake 1\n"
        "another_future_key 12345\n"
        "battery_level 42\n";

    SynthesisCore s;
    CHECK(parse_text(mixed, s));
    CHECK(s.focused_app == std::string("com.example.app"));
    CHECK(s.screen_awake);
    CHECK_EQ(s.battery_level, 42);
}

static void test_absent_keys_keep_unsupported_sentinels() {
    // A v1 APK writes only the early fields. Everything else must read as
    // "unsupported", never as a plausible zero.
    SynthesisCore s;
    CHECK(parse_text("focused_app com.example.app 1 2\nscreen_awake 1\n", s));

    CHECK_EQ(s.synthesis_version, 1); // absent field means the oldest protocol
    CHECK(std::fabs(s.thermal_headroom - (-1.0f)) < 0.001f);
    CHECK_EQ(s.thermal_level, -1);
    CHECK_EQ(s.battery_level, -1);
    CHECK(std::isnan(s.battery_temp));
    CHECK(!s.thermal_api_available);
    CHECK(!s.call_active);
}

static void test_thermal_headroom_unsupported_sentinel_survives() {
    SynthesisCore s;
    CHECK(parse_text("synthesis_version 3\nthermal_status -1.00\n", s));
    CHECK(std::fabs(s.thermal_headroom - (-1.0f)) < 0.001f);
}

static void test_missing_file_reports_failure() {
    SynthesisCore s;
    CHECK(!SynthesisCoreReader::read(s, (temp_dir() + "/flux_no_such_status_file").c_str()));
}

static void test_empty_file_reports_no_fields() {
    SynthesisCore s;
    CHECK(!parse_text("", s));
}

static void test_garbage_does_not_crash_or_claim_success() {
    SynthesisCore s;
    CHECK(!parse_text("\n\n   \n!!!!\n", s));

    // A line longer than the parser's 256-byte buffer must not smear into the
    // next key: the value is truncated, the following line still parses.
    const std::string long_line = "focused_app " + std::string(400, 'a') + "\nbattery_level 55\n";
    SynthesisCore s2;
    CHECK(parse_text(long_line, s2));
    CHECK_EQ(s2.battery_level, 55);
}

static void test_reparse_resets_previous_state() {
    // The reader zeroes its output first, so a value from an earlier file can
    // never survive into a later one.
    SynthesisCore s;
    CHECK(parse_text(kV3File, s));
    CHECK_EQ(s.battery_level, 76);

    CHECK(parse_text("synthesis_version 3\nscreen_awake 0\n", s));
    CHECK_EQ(s.battery_level, -1);
    CHECK(s.focused_app.empty());
    CHECK(!s.screen_awake);
}

int main() {
    test_v3_file_parses_unchanged();
    test_v4_file_parses_on_a_v3_parser();
    test_unknown_keys_between_known_ones();
    test_absent_keys_keep_unsupported_sentinels();
    test_thermal_headroom_unsupported_sentinel_survives();
    test_missing_file_reports_failure();
    test_empty_file_reports_no_fields();
    test_garbage_does_not_crash_or_claim_success();
    test_reparse_resets_previous_state();
    return flux_test::report("synthesis_core_test");
}
