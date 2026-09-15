#include "plugin_runner.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), length);
    return result;
}

std::wstring QuoteWindowsArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring BuildCommandLine(const std::string& executable,
                              const std::vector<std::string>& arguments) {
    std::wstring command = QuoteWindowsArgument(Utf8ToWide(executable));
    for (const std::string& argument : arguments) {
        command.push_back(L' ');
        command += QuoteWindowsArgument(Utf8ToWide(argument));
    }
    return command;
}

} // namespace

PluginRunResult RunPluginCli(const std::string& executable,
                             const std::string& working_directory,
                             const std::vector<std::string>& arguments) {
    PluginRunResult result;
    std::error_code path_error;
    if (!std::filesystem::is_regular_file(executable, path_error)) {
        result.error = "插件 CLI 不存在: " + executable;
        return result;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        result.error = "无法创建插件输出管道";
        return result;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};

    std::wstring command_line = BuildCommandLine(executable, arguments);
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    const std::wstring executable_wide = Utf8ToWide(executable);
    const std::wstring working_directory_wide = Utf8ToWide(working_directory);
    const BOOL created = CreateProcessW(
        executable_wide.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,
        working_directory_wide.empty() ? nullptr : working_directory_wide.c_str(),
        &startup, &process);
    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        result.error = "无法启动插件 CLI，Windows 错误码: " + std::to_string(GetLastError());
        return result;
    }

    result.started = true;
    constexpr std::size_t output_limit = 256 * 1024;
    char buffer[4096];
    DWORD bytes_read = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
        if (result.output.size() < output_limit) {
            const std::size_t remaining = output_limit - result.output.size();
            result.output.append(buffer, std::min<std::size_t>(bytes_read, remaining));
        }
    }
    WaitForSingleObject(process.hProcess, 60000);
    GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_pipe);
    return result;
}

std::string FormatPluginCommand(const std::string& executable,
                                const std::vector<std::string>& arguments) {
    std::string command = std::filesystem::path(executable).filename().string();
    for (const std::string& argument : arguments) {
        command += " \"" + argument + "\"";
    }
    return command;
}
