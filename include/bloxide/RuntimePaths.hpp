#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace bloxide {
std::filesystem::path executableDirectory();
std::filesystem::path userDataDirectory();
std::filesystem::path diagnosticDirectory(const std::string& category);
std::vector<std::string> platformArguments(int argc, char** argv);
std::string executableName(const std::string& stem);
} // namespace bloxide
