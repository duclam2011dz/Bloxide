#include <bloxide/ProcessRunner.hpp>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <cerrno>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
extern char** environ;
#endif
namespace bloxide {
#ifdef _WIN32
namespace {
std::wstring wide(const std::string& text) {
    auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
                                    nullptr, 0);
    if (!size && !text.empty())
        throw std::runtime_error("Invalid UTF-8 process argument");
    std::wstring result(std::size_t(size), L'\0');
    if (size)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
                            result.data(), size);
    return result;
}
std::wstring quote(const std::wstring& text) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (auto c : text) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}
} // namespace
#endif
ProcessResult runProcess(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments,
                         const std::filesystem::path& log, unsigned timeoutMs) {
    if (!timeoutMs)
        throw std::invalid_argument("Process timeout must be positive");
    ProcessResult result;
#ifdef _WIN32
    auto command = quote(executable.wstring());
    for (auto& arg : arguments)
        command += L" " + quote(wide(arg));
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE file = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open child log: " + log.u8string());
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdOutput = file;
    start.hStdError = file;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    auto ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &start, &process);
    CloseHandle(file);
    if (!ok)
        throw std::runtime_error("Cannot launch child: " + executable.u8string());
    auto status = WaitForSingleObject(process.hProcess, timeoutMs);
    if (status == WAIT_TIMEOUT || status == WAIT_FAILED) {
        result.timedOut = status == WAIT_TIMEOUT;
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result.exitCode = result.timedOut ? 124 : int(code);
#else
    std::string path = executable.native();
    std::vector<std::string> storage{path};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& arg : storage)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t files;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&files))
        throw std::runtime_error("spawn file actions failed");
    posix_spawnattr_init(&attributes);
    int setup = posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, log.c_str(),
                                                 O_WRONLY | O_CREAT | O_TRUNC, 0600);
    setup |= posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);
    setup |= posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    setup |= posix_spawnattr_setpgroup(&attributes, 0);
    pid_t pid = 0;
    int error =
        setup ? setup : posix_spawn(&pid, path.c_str(), &files, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attributes);
    if (error)
        throw std::runtime_error("Cannot launch child/log: " + path + " (error " +
                                 std::to_string(error) + ")");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int status = 0;
    for (;;) {
        auto state = waitpid(pid, &status, WNOHANG);
        if (state == pid)
            break;
        if (state < 0 && errno != EINTR)
            throw std::runtime_error("waitpid failed");
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timedOut = true;
            kill(-pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (WIFSIGNALED(status))
        result.signal = WTERMSIG(status);
    result.exitCode = result.timedOut     ? 124
                      : WIFEXITED(status) ? WEXITSTATUS(status)
                                          : 128 + result.signal;
#endif
    return result;
}
} // namespace bloxide
