#pragma once
#include <bloxide/BuildConfig.hpp>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdint>
#include <iostream>

namespace bloxide {
inline std::string buildInfoJson() {
    std::ostringstream out;
    out << "{\"version\":" << std::quoted(BLOXIDE_VERSION)
        << ",\"commit\":" << std::quoted(BLOXIDE_COMMIT)
        << ",\"dirty\":" << (BLOXIDE_DIRTY ? "true" : "false")
        << ",\"configuration\":" << std::quoted(BLOXIDE_BUILD_CONFIGURATION)
        << ",\"compiler\":" << std::quoted(BLOXIDE_COMPILER)
        << ",\"platform\":" << std::quoted(BLOXIDE_PLATFORM)
        << ",\"optimization\":" << std::quoted(BLOXIDE_ACTUAL_OPTIMIZATION)
        << ",\"sanitizer\":" << std::quoted(BLOXIDE_SANITIZER)
        << ",\"sse2_compiled\":" << (BLOXIDE_HAS_SSE2 ? "true" : "false")
        << ",\"avx2_compiled\":" << (BLOXIDE_HAS_AVX2 ? "true" : "false")
        << ",\"dependency_provider\":" << std::quoted(BLOXIDE_DEPENDENCY_PROVIDER)
        << ",\"dependency_baseline\":" << std::quoted(BLOXIDE_DEPENDENCY_BASELINE)
        << ",\"determinism_profile\":"
        << std::quoted(std::string(BLOXIDE_PLATFORM) + "/" + BLOXIDE_COMPILER + "/strict") << "}";
    return out.str();
}
inline std::string versionText() {
    return std::string("Bloxide ") + BLOXIDE_VERSION + " (" + BLOXIDE_COMMIT +
           (BLOXIDE_DIRTY ? ", dirty" : "") + ", " + BLOXIDE_BUILD_CONFIGURATION + ")";
}
inline std::string textFingerprint(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}
inline bool printVersionIfRequested(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "--version")
        return false;
    std::cout << ((argc > 2 && std::string(argv[2]) == "--json") ? buildInfoJson() : versionText())
              << '\n';
    return true;
}
} // namespace bloxide
