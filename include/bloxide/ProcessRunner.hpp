#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace bloxide {
struct ProcessResult {
    int exitCode = -1;
    int signal = 0;
    bool timedOut = false;
};
ProcessResult runProcess(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments,
                         const std::filesystem::path& log, unsigned timeoutMs);
} // namespace bloxide
