#include "Brand.hpp"

namespace zairenkai::brand {

std::string command_name(std::string_view argv0) {
    const auto slash = argv0.find_last_of('/');
    const auto base = slash == std::string_view::npos ? argv0 : argv0.substr(slash + 1);
    return std::string(base == kPublicBinary ? kPublicBinary : kDaemonBinary);
}

std::string cli_banner() {
    return std::string(kPlatform) + " CLI (" + std::string(kLegacyRuntimeLabel) + " runtime)";
}

std::string version_line(std::string_view version) {
    return std::string(kPlatform) + " " + std::string(version) + " (" + std::string(kLegacyRuntimeLabel) + " runtime)";
}

} // namespace zairenkai::brand
