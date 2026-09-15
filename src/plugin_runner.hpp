#pragma once

#include <string>
#include <vector>

struct PluginRunResult {
    bool started = false;
    unsigned long exit_code = 0;
    std::string output;
    std::string error;
};

// 不经过 shell，直接以参数数组启动插件 CLI，并捕获 UTF-8 输出。
PluginRunResult RunPluginCli(const std::string& executable,
                             const std::string& working_directory,
                             const std::vector<std::string>& arguments);

std::string FormatPluginCommand(const std::string& executable,
                                const std::vector<std::string>& arguments);
