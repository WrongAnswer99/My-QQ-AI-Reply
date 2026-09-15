#include "plugin_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string ReadReadme(const std::filesystem::path& directory) {
    std::ifstream readme(directory / "README.md");
    if (!readme.is_open()) {
        return std::string();
    }
    std::ostringstream content;
    content << readme.rdbuf();
    return content.str();
}

std::string ReadDisplayName(const std::string& readme, const std::string& fallback) {
    std::istringstream input(readme);
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > 2 && line[0] == '#' && line[1] == ' ') {
            return line.substr(2);
        }
    }
    return fallback;
}

std::string ReadIntroduction(const std::string& readme) {
    std::istringstream input(readme);
    std::ostringstream introduction;
    std::string line;
    bool found_title = false;
    while (std::getline(input, line)) {
        if (!found_title) {
            if (line.size() > 2 && line[0] == '#' && line[1] == ' ') {
                found_title = true;
            }
            continue;
        }
        if (!line.empty() && line[0] == '#') {
            break;
        }
        introduction << line << "\n";
    }
    return introduction.str();
}

std::string ShortenQuotedText(const std::string& text) {
    constexpr std::size_t max_length = 100;
    if (text.size() <= max_length) {
        return text;
    }
    std::size_t safe_length = max_length;
    while (safe_length > 0
           && (static_cast<unsigned char>(text[safe_length]) & 0xc0) == 0x80) {
        --safe_length;
    }
    return text.substr(0, safe_length) + "...";
}

} // namespace

std::vector<PluginInfo> DiscoverPlugins(const std::string& root_directory) {
    std::vector<PluginInfo> plugins;
    std::error_code error;
    const std::filesystem::path root(root_directory);
    if (!std::filesystem::is_directory(root, error)) {
        return plugins;
    }

    for (const std::filesystem::directory_entry& directory_entry :
         std::filesystem::directory_iterator(root, error)) {
        if (error || !directory_entry.is_directory()) {
            continue;
        }
        PluginInfo plugin;
        plugin.id = directory_entry.path().filename().string();
        plugin.directory = std::filesystem::absolute(directory_entry.path()).string();
        plugin.readme = ReadReadme(directory_entry.path());
        plugin.name = ReadDisplayName(plugin.readme, plugin.id);
        plugin.introduction = ReadIntroduction(plugin.readme);

        std::error_code files_error;
        for (const std::filesystem::directory_entry& file_entry :
             std::filesystem::directory_iterator(directory_entry.path(), files_error)) {
            if (files_error || !file_entry.is_regular_file()) {
                continue;
            }
            const std::string filename = file_entry.path().filename().string();
            const std::string lower = ToLower(filename);
            if (ToLower(file_entry.path().extension().string()) != ".exe") {
                continue;
            }
            if (plugin.cli_executable.empty() && lower.find("cli") != std::string::npos) {
                plugin.cli_executable = std::filesystem::absolute(file_entry.path()).string();
            }
            if (plugin.gui_executable.empty() && lower.find("gui") != std::string::npos) {
                plugin.gui_executable = std::filesystem::absolute(file_entry.path()).string();
            }
        }
        if (!plugin.cli_executable.empty() || !plugin.gui_executable.empty()) {
            plugins.push_back(std::move(plugin));
        }
    }
    std::sort(plugins.begin(), plugins.end(),
              [](const PluginInfo& left, const PluginInfo& right) {
                  return left.id < right.id;
              });
    return plugins;
}

std::string BuildPluginList(const std::vector<PluginInfo>& plugins) {
    std::ostringstream reply;
    reply << "可用插件：\n";
    if (plugins.empty()) {
        reply << "（未在 plugins 目录发现可用插件）";
        return reply.str();
    }
    for (std::size_t index = 0; index < plugins.size(); ++index) {
        const PluginInfo& plugin = plugins[index];
        reply << index + 1 << ". " << plugin.name << " [" << plugin.id << "]";
        if (!plugin.cli_executable.empty() && !plugin.gui_executable.empty()) {
            reply << "（CLI / GUI）";
        } else if (!plugin.cli_executable.empty()) {
            reply << "（CLI）";
        } else {
            reply << "（GUI）";
        }
        if (index + 1 < plugins.size()) {
            reply << "\n";
        }
    }
    return reply.str();
}

std::string BuildPluginMenu(const std::vector<PluginInfo>& plugins,
                            const std::string& quoted_text) {
    std::ostringstream reply;
    reply << "已选中引用内容：\n" << ShortenQuotedText(quoted_text) << "\n\n"
          << BuildPluginList(plugins);
    if (!plugins.empty()) {
        reply << "\n\n后续可通过统一插件调用协议选择并执行。";
    }
    return reply.str();
}
