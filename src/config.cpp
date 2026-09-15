#include "config.hpp"

#include <fstream>

#include "json.hpp"

using json = nlohmann::json;

bool Config::LoadFromFile(const std::string& path, Config& out_config, std::string& error) {
    std::ifstream in(path);
    if (!in.is_open()) {
        error = "无法打开配置文件: " + path;
        return false;
    }

    try {
        json cfg = json::parse(in);
        out_config.napcat_http_base     = cfg.value("napcat_http_base", "http://127.0.0.1:3000");
        out_config.napcat_token         = cfg.value("napcat_token", "");
        out_config.http_report_port     = cfg.value("http_report_port", 8081);
        out_config.bot_qq               = cfg.value("bot_qq", "");
        out_config.master_qq            = cfg.value("master_qq", "");
        out_config.private_chat_enabled = cfg.value("private_chat_enabled", true);
        out_config.group_need_at        = cfg.value("group_need_at", true);

        const int group_history_limit = cfg.value("group_history_limit", 100);
        const int interaction_history_limit =
            cfg.value("group_interaction_history_limit", 30);
        if (group_history_limit < 0 || interaction_history_limit < 0) {
            error = "群聊历史数量配置不能为负数";
            return false;
        }
        out_config.group_history_limit = static_cast<std::size_t>(group_history_limit);
        out_config.group_interaction_history_limit =
            static_cast<std::size_t>(interaction_history_limit);
        out_config.image_history_enabled = cfg.value("image_history_enabled", false);

        out_config.group_trigger_keywords.clear();
        if (cfg.contains("group_trigger_keywords") && cfg["group_trigger_keywords"].is_array()) {
            for (const auto& keyword : cfg["group_trigger_keywords"]) {
                out_config.group_trigger_keywords.push_back(keyword.get<std::string>());
            }
        }

        // AI 接口配置
        if (cfg.contains("ai") && cfg["ai"].is_object()) {
            const json& ai = cfg["ai"];
            out_config.ai.api_key       = ai.value("api_key", "");
            out_config.ai.base_url      = ai.value("base_url", "");
            out_config.ai.model         = ai.value("model", "");
            out_config.ai.system_prompt = ai.value("system_prompt", out_config.ai.system_prompt);
        }
        return true;
    } catch (const std::exception& e) {
        error = "配置文件解析失败: ";
        error += e.what();
        return false;
    }
}
