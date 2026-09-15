#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "onebot_api.hpp"
#include "plugin_catalog.hpp"

// master 私聊中的 AI 插件选择、确认和 CLI 执行状态机。
class PluginInteractionManager {
public:
    // 返回 true 表示该消息已由插件交互处理。
    bool Handle(OneBotApi& api, const std::string& user_id,
                const std::string& message, const std::string& replied_text);

private:
    enum class Stage {
        plugin_confirmation,
        command_input,
        command_confirmation,
        completion_confirmation
    };

    struct DialogueEntry {
        std::string role;
        std::string content;
    };

    struct Session {
        Stage stage = Stage::plugin_confirmation;
        std::string quoted_text;
        std::string request_text;
        std::string supplements;
        std::vector<PluginInfo> plugins;
        std::string selected_plugin_id;
        std::vector<std::string> proposed_arguments;
        std::string execution_history;
        std::vector<DialogueEntry> dialogue; // 本轮插件会话不设条数上限
        bool auto_approval = false;
    };

    bool StartSession(OneBotApi& api, const std::string& user_id,
                      const std::string& message, const std::string& replied_text);
    bool ProposePlugin(OneBotApi& api, const std::string& user_id, Session& session);
    bool ProposeCommand(OneBotApi& api, const std::string& user_id, Session& session);
    bool ReviewCommand(const Session& session, const PluginInfo& plugin,
                       const std::string& description, std::string& reason);
    void ExecuteCommand(OneBotApi& api, const std::string& user_id, Session& session);
    bool SendAndRecord(OneBotApi& api, const std::string& user_id,
                       Session& session, const std::string& message);
    static std::string FormatDialogue(const Session& session);

    std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;
};
