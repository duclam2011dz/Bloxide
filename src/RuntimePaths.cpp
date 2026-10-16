#include <bloxide/RuntimePaths.hpp>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
namespace bloxide {
namespace {
std::filesystem::path environmentPath(const char* key) {
#ifdef _WIN32
    std::wstring name;
    while (*key)
        name.push_back(wchar_t(*key++));
    auto value = _wgetenv(name.c_str());
    return value ? std::filesystem::path(value) : std::filesystem::path{};
#else
    auto value = std::getenv(key);
    return value ? std::filesystem::u8path(value) : std::filesystem::path{};
#endif
}
} // namespace
std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    auto length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (!length || length >= buffer.size())
        throw std::runtime_error("Cannot resolve executable path");
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
#elif defined(__APPLE__)
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::vector<char> buffer(length);
    if (_NSGetExecutablePath(buffer.data(), &length))
        throw std::runtime_error("Cannot resolve executable path");
    return std::filesystem::canonical(buffer.data()).parent_path();
#else
    std::vector<char> buffer(4096);
    for (;;) {
        auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0)
            throw std::runtime_error("Cannot resolve /proc/self/exe");
        if (std::size_t(length) < buffer.size())
            return std::filesystem::path(std::string(buffer.data(), std::size_t(length)))
                .parent_path();
        if (buffer.size() >= 1024 * 1024)
            throw std::runtime_error("Executable path too long");
        buffer.resize(buffer.size() * 2);
    }
#endif
}
std::filesystem::path userDataDirectory() {
    auto overridePath = environmentPath("BLOXIDE_DATA_HOME");
    if (!overridePath.empty())
        return std::filesystem::absolute(overridePath);
#ifdef _WIN32
    // SDL_GetPrefPath("Bloxide", "Bloxide") compatible layout.
    auto base = environmentPath("APPDATA");
#elif defined(__APPLE__)
    auto base = environmentPath("HOME") / "Library/Application Support";
#else
    auto base = environmentPath("XDG_DATA_HOME");
    if (base.empty()) {
        auto home = environmentPath("HOME");
        if (home.empty())
            throw std::runtime_error("HOME/XDG_DATA_HOME is unavailable");
        base = home / ".local/share";
    }
#endif
    if (base.empty())
        throw std::runtime_error("User data directory is unavailable");
    return base / "Bloxide/Bloxide";
}
std::filesystem::path diagnosticDirectory(const std::string& category) {
    auto id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return userDataDirectory() / "diagnostics" / category / id;
}
std::vector<std::string> platformArguments(int argc, char** argv) {
    std::vector<std::string> result;
#ifdef _WIN32
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        throw std::runtime_error("Cannot decode command line");
    for (int i = 0; i < count; ++i) {
        auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, nullptr, 0,
                                        nullptr, nullptr);
        if (!size) {
            LocalFree(wide);
            throw std::runtime_error("Invalid Unicode argument");
        }
        std::string value(std::size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, value.data(), size, nullptr,
                            nullptr);
        value.pop_back();
        result.push_back(std::move(value));
    }
    LocalFree(wide);
    (void)argc;
    (void)argv;
#else
    for (int i = 0; i < argc; ++i)
        result.emplace_back(argv[i]);
#endif
    return result;
}
std::string executableName(const std::string& stem) {
#ifdef _WIN32
    return stem + ".exe";
#else
    return stem;
#endif
}
} // namespace bloxide
