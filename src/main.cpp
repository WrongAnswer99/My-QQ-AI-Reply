#include <winsock2.h> // 必须在 windows.h 之前
#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

#include "ai_client.hpp"
#include "config.hpp"
#include "http_server.hpp"
#include "note.hpp"
#include "onebot_api.hpp"
#include "json.hpp"

using json = nlohmann::json;

// 全局记录管理（master 通过"引用消息 + !note"添加，!note 查看全部记录）
static Note g_note;

// 从 OneBot 消息（字符串或消息段数组）中提取纯文本
static std::string ExtractPlainText(const json& message) {
    std::string text;
    if (message.is_string()) {
        return message.get<std::string>();
    }
    if (message.is_array()) {
        for (const auto& segment : message) {
            if (!segment.is_object()) {
                continue;
            }
            std::string type = segment.value("type", "");
            if (type == "text" && segment.contains("data") && segment["data"].contains("text")) {
                const json& t = segment["data"]["text"];
                if (t.is_string()) {
                    text += t.get<std::string>();
                } else if (t.is_array()) { // 某些实现 data.text 是字符串数组
                    for (const auto& piece : t) {
                        text += piece.get<std::string>();
                    }
                }
            } else if (type == "at" && segment.contains("data") && segment["data"].contains("qq")) {
                text += "[@" + segment["data"]["qq"].get<std::string>() + "]";
            }
        }
    }
    return text;
}

// 判断群消息中是否 @ 了机器人
static bool IsAtBot(const json& message, const std::string& bot_qq) {
    if (!message.is_array() || bot_qq.empty()) {
        return false;
    }
    for (const auto& segment : message) {
        if (!segment.is_object() || segment.value("type", "") != "at") {
            continue;
        }
        if (segment.contains("data") && segment["data"].contains("qq")) {
            const json& qq_json = segment["data"]["qq"];
            std::string qq = qq_json.is_string()
                ? qq_json.get<std::string>()
                : std::to_string(qq_json.get<long long>());
            if (qq == bot_qq) {
                return true;
            }
        }
    }
    return false;
}

// 群消息文本是否命中触发关键词
static bool HitKeyword(const std::string& text, const std::vector<std::string>& keywords) {
    for (const auto& keyword : keywords) {
        if (!keyword.empty() && text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 提取消息中被回复的消息 id（reply 段的 data.id），无则返回空串
static std::string ExtractReplyId(const json& message) {
    if (!message.is_array()) {
        return std::string();
    }
    for (const auto& segment : message) {
        if (!segment.is_object() || segment.value("type", "") != "reply") {
            continue;
        }
        if (segment.contains("data") && segment["data"].contains("id")) {
            const json& id = segment["data"]["id"];
            return id.is_string() ? id.get<std::string>() : std::to_string(id.get<long long>());
        }
    }
    return std::string();
}

// 获取消息中被引用（reply 段）消息的原文，无引用则返回空串
static std::string GetRepliedText(OneBotApi& api, const json& message) {
    std::string reply_id = ExtractReplyId(message);
    if (reply_id.empty()) {
        return std::string();
    }
    json replied_message;
    if (!api.GetMsg(reply_id, replied_message)) {
        return std::string();
    }
    return ExtractPlainText(replied_message);
}

// 构造发送给 AI 的消息：若消息是"回复某条消息"，则附上被回复消息的原文作为上下文
static std::string BuildAIMessage(OneBotApi& api, const json& message, const std::string& plain_text) {
    std::string replied_text = GetRepliedText(api, message);
    if (replied_text.empty()) {
        return plain_text;
    }
    return "【用户回复的消息】" + replied_text + "\n【用户当前消息】" + plain_text;
}

// 调用 AI 并发送回复，返回是否成功发送
static bool ReplyWithAI(OneBotApi& api, const std::string& target_type,
                        const std::string& target_id, const std::string& system_prompt,
                        const std::string& user_message) {
    std::string error;
    std::string reply = ai::Chat({ system_prompt, user_message }, error);
    if (reply.empty()) {
        std::cerr << "[AI 错误] " << error << std::endl;
        return false;
    }

    bool sent = false;
    if (target_type == "group") {
        sent = api.SendGroupMsg(target_id, reply);
    } else if (target_type == "private") {
        sent = api.SendPrivateMsg(target_id, reply);
    }

    if (sent) {
        std::cout << "[回复] " << reply << std::endl;
    } else {
        std::cerr << "[发送失败] " << target_type << " " << target_id << std::endl;
    }
    return sent;
}

// 处理 master（主人）私聊指令，例如 !help !status !note（更多指令在此扩展）
// 指令前缀为 !（/ 在 QQ 中会触发表情）；replied_text：该指令消息若引用了某条消息，则为其原文（无引用为空串）
static void HandleMasterCommand(OneBotApi& api, const std::string& user_id,
                                const std::string& message, const std::string& replied_text) {
    std::string reply;
    if (message == "!help") {
        reply = "可用指令：\n!help - 显示帮助\n!status - 查看运行状态\n!note - 查看记录\n回复某条消息后发 !note 可将内容加入记录";
    } else if (message == "!status") {
        reply = "QQ-AIreply 机器人运行中。NapCat 状态请查看 NapCat 窗口。";
    } else if (message == "!note") {
        if (!replied_text.empty()) {
            g_note.Add(replied_text);
            reply = "已加入记录（共 " + std::to_string(g_note.Count()) + " 条）：\n" + replied_text;
        } else if (g_note.Count() == 0) {
            reply = "暂无记录。回复某条消息后发 !note 即可加入记录。";
        } else {
            reply = "记录列表（共 " + std::to_string(g_note.Count()) + " 条）：\n" + g_note.ListAll();
        }
    } else {
        reply = "未识别的指令：" + message + "\n输入 !help 查看可用指令。";
    }

    if (!api.SendPrivateMsg(user_id, reply)) {
        std::cerr << "[指令发送失败] " << user_id << std::endl;
    }
}

// 处理 master（主人）私聊消息：! 开头为指令，其他内容一律忽略（不进 AI、不回复）
// replied_text：该消息若引用了某条消息，则为其原文（无引用为空串）
static void HandleMasterPrivate(OneBotApi& api, const std::string& user_id,
                                const std::string& message, const std::string& replied_text) {
    if (message.empty() || message[0] != '!') {
        return; // 非指令内容：忽略
    }
    HandleMasterCommand(api, user_id, message, replied_text);
}

int main() {
    // 控制台 UTF-8 输出，避免中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 加载持久化的 note 记录
    g_note.Load();

    // 加载配置
    Config config;
    std::string config_error;
    if (!Config::LoadFromFile("config.json", config, config_error)) {
        std::cerr << "[错误] " << config_error << std::endl;
        return 1;
    }
    std::cout << "[启动] 配置加载成功，NapCat HTTP 地址: " << config.napcat_http_base << std::endl;

    // 初始化 AI 客户端
    std::string ai_error;
    if (!ai::Init({ config.ai.api_key, config.ai.base_url, config.ai.model }, ai_error)) {
        std::cerr << "[错误] " << ai_error << std::endl;
        return 1;
    }
    std::cout << "[启动] AI 客户端初始化成功，模型: " << config.ai.model << std::endl;

    OneBotApi api(config.napcat_http_base, config.napcat_token);

    // HTTP 上报服务：处理 NapCat 推送过来的事件
    HttpServer server(config.http_report_port);
    server.SetHandler([&](const std::string& body) -> std::string {
        if (body.empty()) {
            return std::string();
        }
        try {
            json event = json::parse(body);
            if (event.value("post_type", "") != "message") {
                return std::string(); // 忽略非消息事件
            }

            std::string message_type = event.value("message_type", "");
            std::string user_id      = std::to_string(event.value("user_id", 0LL));
            std::string plain_text   = ExtractPlainText(event["message"]);
            if (plain_text.empty()) {
                return std::string();
            }

            if (message_type == "group") {
                std::string group_id = std::to_string(event.value("group_id", 0LL));
                bool at_bot = IsAtBot(event["message"], config.bot_qq);
                bool hit_keyword = HitKeyword(plain_text, config.group_trigger_keywords);
                if (config.group_need_at && !at_bot && !hit_keyword) {
                    return std::string(); // 未被 @ 且未命中关键词，不回复
                }
                std::cout << "[群聊] 群 " << group_id << " 成员 " << user_id << ": " << plain_text << std::endl;
                std::string ai_message = BuildAIMessage(api, event["message"], plain_text);
                ReplyWithAI(api, "group", group_id, config.ai.system_prompt, ai_message);
            } else if (message_type == "private") {
                // master（主人）私聊消息按指令/内容处理，永不交给 AI
                if (!config.master_qq.empty() && user_id == config.master_qq) {
                    std::cout << "[主人] " << user_id << ": " << plain_text << std::endl;
                    std::string replied_text = GetRepliedText(api, event["message"]);
                    HandleMasterPrivate(api, user_id, plain_text, replied_text);
                } else if (config.private_chat_enabled) {
                    std::cout << "[私聊] " << user_id << ": " << plain_text << std::endl;
                    std::string ai_message = BuildAIMessage(api, event["message"], plain_text);
                    ReplyWithAI(api, "private", user_id, config.ai.system_prompt, ai_message);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[事件解析错误] " << e.what() << std::endl;
        }
        return std::string();
    });

    if (!server.Start()) {
        std::cerr << "[错误] HTTP 上报服务启动失败，端口 " << config.http_report_port << std::endl;
        return 1;
    }
    std::cout << "[启动] 事件上报服务已监听端口 " << config.http_report_port << std::endl;
    std::cout << "[提示] 在 NapCat 中配置 HTTP 上报地址: http://127.0.0.1:"
              << config.http_report_port << "/ ，按 Ctrl+C 退出" << std::endl;

    // 主循环：常驻运行，按 Ctrl+C 终止
    while (true) {
        Sleep(1000);
    }

    server.Stop();
    return 0;
}
