#pragma once

#include <string>
#include <vector>

// AI 接口配置（对应 config.json 的 ai 段）
struct AiConfig {
    std::string api_key;        // 阿里云百炼 API Key
    std::string base_url;       // OpenAI 兼容接口地址（完整的 chat/completions 端点）
    std::string model;          // 模型名
    std::string system_prompt = "You are a helpful assistant."; // 系统提示词
};

// 机器人运行配置（对应 config.json）
struct Config {
    std::string napcat_http_base;        // NapCat HTTP API 地址，如 http://127.0.0.1:3000
    std::string napcat_token;            // NapCat HTTP API 的 access_token（没有则为空）
    int         http_report_port = 8081; // 机器人本地 HTTP 服务端口，NapCat 将事件上报到这里
    std::string bot_qq;                  // 机器人自己的 QQ 号（用于判断是否被 @）
    std::string master_qq;               // 主人 QQ 号（预留：后续可用于识别群主/管理员）
    bool        private_chat_enabled = true; // 是否回复私聊消息
    bool        group_need_at = true;        // 群聊是否要求被 @ 才回复
    std::vector<std::string> group_trigger_keywords; // 群聊触发关键词（未 @ 时含关键词也回复）

    AiConfig ai; // AI 接口配置

    // 从 JSON 文件加载配置，失败时返回 false 并填充 error
    static bool LoadFromFile(const std::string& path, Config& out_config, std::string& error);
};
