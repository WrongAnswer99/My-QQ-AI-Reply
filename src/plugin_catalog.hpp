#pragma once

#include <string>
#include <vector>

struct PluginInfo {
    std::string id;
    std::string name;
    std::string directory;
    std::string cli_executable;
    std::string gui_executable;
    std::string introduction;
    std::string readme;
};

// 扫描 plugins 下的一级目录；通过 README 标题和 exe 命名自动发现插件。
std::vector<PluginInfo> DiscoverPlugins(const std::string& root_directory = "plugins");

// 生成可复用于 /help 等场景的插件列表。
std::string BuildPluginList(const std::vector<PluginInfo>& plugins);

// 生成适合 QQ 私聊显示的插件选择菜单。
std::string BuildPluginMenu(const std::vector<PluginInfo>& plugins,
                            const std::string& quoted_text);
