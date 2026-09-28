// Minimal check macros shared by the host tests.
//
// The two tests that predate the CMake harness each carry their own copy of this
// macro; they are left untouched so their documented standalone g++ command still
// works. New tests include this instead.
#pragma once

#include <cstdio>
#include <string>

namespace flux_test {

inline int failures = 0;

inline std::string to_text(const std::string &v) { return v; }
inline std::string to_text(const char *v) { return v ? v : "(null)"; }
inline std::string to_text(bool v) { return v ? "true" : "false"; }
template <typename T>
inline std::string to_text(const T &v) {
    return std::to_string(v);
}

inline void fail(const char *file, int line, const std::string &what) {
    std::fprintf(stderr, "%s:%d: %s\n", file, line, what.c_str());
    ++failures;
}

/// Prints the outcome and returns the process exit status.
inline int report(const char *name) {
    if (failures) {
        std::fprintf(stderr, "%s: %d failure(s)\n", name, failures);
        return 1;
    }
    std::printf("%s: all passed\n", name);
    return 0;
}

} // namespace flux_test

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) flux_test::fail(__FILE__, __LINE__, "CHECK(" #cond ")");  \
    } while (0)

/// Like CHECK but prints both sides, which is what you want on a value mismatch.
#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        const auto &flux_a = (actual);                                        \
        const auto &flux_e = (expected);                                      \
        if (!(flux_a == flux_e)) {                                            \
            flux_test::fail(__FILE__, __LINE__,                               \
                            "CHECK_EQ(" #actual ", " #expected "): got <" +    \
                                flux_test::to_text(flux_a) + "> want <" +      \
                                flux_test::to_text(flux_e) + ">");             \
        }                                                                     \
    } while (0)
