#include <bloxide/BuildInfo.hpp>
#include <bloxide/ProcessRunner.hpp>
#include <bloxide/RuntimePaths.hpp>
#include <bloxide/Noise.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        auto args = bloxide::platformArguments(argc, argv);
        if (args.size() > 1 && args[1] == "--child-exit")
            return 17;
        if (args.size() > 1 && args[1] == "--child-wait") {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            return 0;
        }
        if (args.size() > 1 && args[1] == "--child-echo") {
            for (std::size_t i = 2; i < args.size(); ++i)
                std::cout << args[i] << '\n';
            return 0;
        }
        auto check = [](bool value, const char* message) {
            if (!value)
                throw std::runtime_error(message);
        };
        auto root =
            std::filesystem::current_path() / "testing" /
            std::filesystem::u8path(
                "portable space-việt-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        auto exe =
            bloxide::executableDirectory() / bloxide::executableName("BloxidePortabilityTests");
        auto log = root / "child.log";
        auto result = bloxide::runProcess(
            exe, {"--child-echo", "space value", "quote\"value", "trailing\\", u8"tiếng Việt"}, log,
            10000);
        check(result.exitCode == 0 && !result.timedOut, "argv child failed");
        std::ifstream input(log);
        std::string content((std::istreambuf_iterator<char>(input)), {});
        check(content == "space value\nquote\"value\ntrailing\\\ntiếng Việt\n" ||
                  content == "space value\r\nquote\"value\r\ntrailing\\\r\ntiếng Việt\r\n",
              "argument corruption");
        result = bloxide::runProcess(exe, {"--child-exit"}, log, 10000);
        check(result.exitCode == 17, "exit code lost");
        result = bloxide::runProcess(exe, {"--child-wait"}, log, 40);
        check(result.timedOut && result.exitCode == 124, "timeout failed");
        check(bloxide::buildInfoJson().find("\"version\":\"1.5.0\"") != std::string::npos,
              "build identity missing");
        check(bloxide::noiseBackendSupported(bloxide::NoiseBackend::Scalar), "scalar unavailable");
#if !BLOXIDE_HAS_SSE2
        check(!bloxide::noiseBackendSupported(bloxide::NoiseBackend::SSE2),
              "uncompiled SSE2 advertised");
#endif
#if !BLOXIDE_HAS_AVX2
        check(!bloxide::noiseBackendSupported(bloxide::NoiseBackend::AVX2),
              "uncompiled AVX2 advertised");
#endif
        std::cout << "Portable subprocess, timeout, UTF-8 paths, build identity and scalar "
                     "capability passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
