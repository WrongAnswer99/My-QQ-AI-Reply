#include <winsock2.h> // 必须在 windows.h 之前
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ai_client.hpp"
#include "config.hpp"
#include "group_history.hpp"
#include "http_server.hpp"
#include "message_format.hpp"
#include "onebot_api.hpp"
#include "json.hpp"

using json = nlohmann::json;

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
                const json& qq = segment["data"]["qq"];
                text += "[@" + (qq.is_string() ? qq.get<std::string>()
                                                   : std::to_string(qq.get<long long>())) + "]";
            }
        }
    }
    return text;
}

// 优先使用 OneBot 事件携带的 Unix 时间；缺失或格式异常时使用本机当前时间。
static std::int64_t ExtractTimestamp(const json& event) {
    if (event.contains("time") && event["time"].is_number_integer()) {
        return event["time"].get<std::int64_t>();
    }
    return static_cast<std::int64_t>(std::time(nullptr));
}

static std::string FormatTimestamp(std::int64_t timestamp) {
    std::time_t value = static_cast<std::time_t>(timestamp);
    std::tm local_time{};
    if (localtime_s(&local_time, &value) != 0) {
        return std::to_string(timestamp);
    }
    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

static std::string ExtractSenderLabel(const json& event, const std::string& user_id) {
    std::string name;
    if (event.contains("sender") && event["sender"].is_object()) {
        const json& sender = event["sender"];
        name = sender.value("card", "");
        if (name.empty()) {
            name = sender.value("nickname", "");
        }
    }
    return name.empty() ? "QQ:" + user_id : name + "(QQ:" + user_id + ")";
}

static bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static std::string Base64Encode(const std::string& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    unsigned int buffer = 0;
    int bits = -6;
    for (const unsigned char byte : bytes) {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 0) {
            encoded.push_back(alphabet[(buffer >> bits) & 0x3f]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        encoded.push_back(alphabet[((buffer << 8) >> (bits + 8)) & 0x3f]);
    }
    while (encoded.size() % 4 != 0) {
        encoded.push_back('=');
    }
    return encoded;
}

static bool ReadFileAsBase64(const std::string& path, std::string& base64) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ostringstream bytes;
    bytes << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return false;
    }
    base64 = Base64Encode(bytes.str());
    return !base64.empty();
}

static bool ExtractEmbeddedBase64(const std::string& value,
                                  std::string& mime_type,
                                  std::string& base64) {
    if (StartsWith(value, "base64://")) {
        base64 = value.substr(9);
        return !base64.empty();
    }
    if (!StartsWith(value, "data:")) {
        return false;
    }
    const std::string marker = ";base64,";
    const std::size_t marker_pos = value.find(marker);
    if (marker_pos == std::string::npos) {
        return false;
    }
    mime_type = value.substr(5, marker_pos - 5);
    base64 = value.substr(marker_pos + marker.size());
    return !base64.empty();
}

static std::string GuessImageMimeType(const std::string& source,
                                      const std::string& base64) {
    if (StartsWith(base64, "iVBOR")) {
        return "image/png";
    }
    if (StartsWith(base64, "/9j/")) {
        return "image/jpeg";
    }
    if (StartsWith(base64, "R0lGOD")) {
        return "image/gif";
    }
    if (StartsWith(base64, "UklGR")) {
        return "image/webp";
    }
    if (StartsWith(base64, "Qk")) {
        return "image/bmp";
    }

    std::string lower = source;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const std::size_t query = lower.find_first_of("?#");
    if (query != std::string::npos) {
        lower.resize(query);
    }
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".png") == 0) {
        return "image/png";
    }
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".gif") == 0) {
        return "image/gif";
    }
    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".webp") == 0) {
        return "image/webp";
    }
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".bmp") == 0) {
        return "image/bmp";
    }
    return "image/jpeg";
}

static std::string JsonScalarToString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    return std::string();
}

static std::vector<GroupHistoryImage> ExtractImages(OneBotApi& api, const json& message) {
    std::vector<GroupHistoryImage> images;
    if (!message.is_array()) {
        return images;
    }
    for (const json& segment : message) {
        if (!segment.is_object() || segment.value("type", "") != "image"
            || !segment.contains("data") || !segment["data"].is_object()) {
            continue;
        }
        const json& data = segment["data"];
        const std::string file = data.contains("file")
            ? JsonScalarToString(data["file"]) : std::string();
        const std::string file_id = data.contains("file_id")
            ? JsonScalarToString(data["file_id"]) : std::string();
        const std::string path = data.contains("path")
            ? JsonScalarToString(data["path"]) : std::string();
        const std::string url = data.contains("url")
            ? JsonScalarToString(data["url"]) : std::string();
        const std::string source = !url.empty() ? url : (!path.empty() ? path : file);

        std::string mime_type;
        std::string base64;
        bool loaded = ExtractEmbeddedBase64(file, mime_type, base64);
        if (!loaded && !file_id.empty()) {
            loaded = api.GetFileBase64(file_id, base64);
            if (loaded) {
                std::string embedded_mime;
                std::string embedded_base64;
                if (ExtractEmbeddedBase64(base64, embedded_mime, embedded_base64)) {
                    mime_type = std::move(embedded_mime);
                    base64 = std::move(embedded_base64);
                }
            }
        }
        if (!loaded && !path.empty()) {
            loaded = ReadFileAsBase64(path, base64);
        }
        if (!loaded && !url.empty()) {
            std::string bytes;
            if (api.DownloadUrl(url, bytes)) {
                base64 = Base64Encode(bytes);
                loaded = !base64.empty();
            }
        }
        if (!loaded && !file.empty() && !StartsWith(file, "http://")
            && !StartsWith(file, "https://") && !StartsWith(file, "file://")) {
            loaded = ReadFileAsBase64(file, base64);
        }
        if (!loaded || base64.empty()) {
            std::cerr << "[图片历史] 无法取得图片 Base64，已跳过该图片" << std::endl;
            continue;
        }
        if (mime_type.empty()) {
            mime_type = GuessImageMimeType(source, base64);
        }
        images.push_back({std::move(mime_type), std::move(base64)});
    }
    return images;
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

static std::string FormatHistoryEntry(const GroupHistoryEntry& entry) {
    std::string result = "[" + FormatTimestamp(entry.timestamp) + "] " + entry.sender + ": ";
    const std::string text = entry.sender.compare(0, std::string("机器人").size(), "机器人") == 0
        ? CleanAIReply(entry.text)
        : entry.text;
    result += text.empty() ? "[图片消息]" : text;
    if (!entry.images.empty()) {
        result += " [附带图片 " + std::to_string(entry.images.size()) + " 张]";
    }
    return result;
}

static std::vector<ai::Message::Image> ToAIImages(
    const std::vector<GroupHistoryImage>& images) {
    std::vector<ai::Message::Image> result;
    result.reserve(images.size());
    for (const GroupHistoryImage& image : images) {
        result.push_back({image.mime_type, image.base64});
    }
    return result;
}

static ai::ChatRequest BuildGroupChatRequest(const std::string& system_prompt,
                                             const std::string& user_prompt,
                                             const std::string& group_id,
                                             const GroupHistorySnapshot& history,
                                             std::int64_t current_timestamp,
                                             const std::string& current_sender,
                                             const std::string& current_message,
                                             const std::vector<GroupHistoryImage>& current_images) {
    ai::ChatRequest request;
    std::ostringstream background;
    request.system_prompt = system_prompt;
    if (!user_prompt.empty()) {
        request.messages.push_back({"user", "【用户提示词】\n" + user_prompt});
    }
    background << "【最近群聊记录】\n"
               << "群号：" << group_id << "\n"
               << "最近群聊记录（由旧到新）：\n";
    if (history.messages.empty()) {
        background << "（暂无历史）";
    } else {
        for (const GroupHistoryEntry& entry : history.messages) {
            background << FormatHistoryEntry(entry) << "\n";
        }
    }
    ai::Message background_message{"user", background.str()};
    for (const GroupHistoryEntry& entry : history.messages) {
        std::vector<ai::Message::Image> entry_images = ToAIImages(entry.images);
        background_message.images.insert(background_message.images.end(),
                                         entry_images.begin(), entry_images.end());
    }
    request.messages.push_back(std::move(background_message));

    struct InteractionItem {
        const GroupHistoryEntry* entry;
        const char* role;
    };
    std::vector<InteractionItem> interactions;
    interactions.reserve(history.mentions.size() + history.replies.size());
    for (const GroupHistoryEntry& entry : history.mentions) {
        interactions.push_back({&entry, "user"});
    }
    for (const GroupHistoryEntry& entry : history.replies) {
        interactions.push_back({&entry, "assistant"});
    }
    std::sort(interactions.begin(), interactions.end(),
              [](const InteractionItem& left, const InteractionItem& right) {
                  return left.entry->sequence < right.entry->sequence;
              });
    for (const InteractionItem& item : interactions) {
        const std::string content = std::string(item.role) == "assistant"
            ? CleanAIReply(item.entry->text)
            : FormatHistoryEntry(*item.entry);
        request.messages.push_back({
            item.role,
            content,
            ToAIImages(item.entry->images)
        });
    }

    request.messages.push_back({
        "user",
        "[" + FormatTimestamp(current_timestamp) + "] " + current_sender
            + " 当前发来的消息：\n" + current_message,
        ToAIImages(current_images)
    });
    return request;
}

// 调用 AI 并发送回复，返回是否成功发送
static bool ReplyWithAI(OneBotApi& api, const std::string& target_type,
                        const std::string& target_id, const ai::ChatRequest& request,
                        std::string* sent_reply = nullptr) {
    std::string error;
    std::string reply = CleanAIReply(ai::Chat(request, error));
    if (reply.empty()) {
        std::cerr << "[AI 错误] " << error << std::endl;
        return false;
    }

    bool sent = false;
    if (target_type == "group") {
        sent = api.SendGroupMsgSegments(target_id, BuildGroupReplySegments(reply));
    } else if (target_type == "private") {
        sent = api.SendPrivateMsg(target_id, reply);
    }

    if (sent) {
        std::cout << "[回复] " << reply << std::endl;
        if (sent_reply != nullptr) {
            *sent_reply = reply;
        }
    } else {
        std::cerr << "[发送失败] " << target_type << " " << target_id << std::endl;
    }
    return sent;
}

// 处理 master（主人）私聊指令，例如 !help !status（更多指令在此扩展）
// 指令前缀为 !（/ 在 QQ 中会触发表情）
static void HandleMasterCommand(OneBotApi& api, const std::string& user_id,
                                const std::string& message) {
    std::string reply;
    if (message == "!help") {
        reply = "可用指令：\n!help - 显示帮助\n!status - 查看运行状态";
    } else if (message == "!status") {
        reply = "QQ-AIreply 机器人运行中。NapCat 状态请查看 NapCat 窗口。";
    } else {
        reply = "未识别的指令：" + message + "\n输入 !help 查看可用指令。";
    }

    if (!api.SendPrivateMsg(user_id, reply)) {
        std::cerr << "[指令发送失败] " << user_id << std::endl;
    }
}

// 处理 master（主人）私聊消息：! 开头为指令，其他内容一律忽略（不进 AI、不回复）
static void HandleMasterPrivate(OneBotApi& api, const std::string& user_id,
                                const std::string& message) {
    if (message.empty() || message[0] != '!') {
        return; // 非指令内容：忽略
    }
    HandleMasterCommand(api, user_id, message);
}

int main() {
    // 控制台 UTF-8 输出，避免中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 加载配置
    Config config;
    std::string config_error;
    if (!Config::LoadFromFile("config.json", config, config_error)) {
        std::cerr << "[错误] " << config_error << std::endl;
        return 1;
    }
    config.ai.system_prompt = config.ExpandPromptVariables(config.ai.system_prompt);
    config.ai.user_prompt = config.ExpandPromptVariables(config.ai.user_prompt);
    std::cout << "[启动] 配置加载成功，NapCat HTTP 地址: " << config.napcat_http_base << std::endl;

    // 初始化 AI 客户端
    std::string ai_error;
    if (!ai::Init({ config.ai.api_key, config.ai.base_url, config.ai.model }, ai_error)) {
        std::cerr << "[错误] " << ai_error << std::endl;
        return 1;
    }
    std::cout << "[启动] AI 客户端初始化成功，模型: " << config.ai.model << std::endl;

    OneBotApi api(config.napcat_http_base, config.napcat_token);
    GroupHistoryStore group_history(config.group_history_limit,
                                    config.group_interaction_history_limit,
                                    config.image_history_enabled);
    std::string history_error;
    if (!group_history.Load(history_error)) {
        std::cerr << "[警告] " << history_error << "，本次将从空历史继续运行" << std::endl;
    }

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

            if (message_type == "group") {
                std::vector<GroupHistoryImage> images;
                if (config.image_history_enabled) {
                    images = ExtractImages(api, event["message"]);
                }
                if (plain_text.empty() && images.empty()) {
                    return std::string();
                }
                std::string group_id = std::to_string(event.value("group_id", 0LL));
                bool at_bot = IsAtBot(event["message"], config.bot_qq);
                bool hit_keyword = HitKeyword(plain_text, config.group_trigger_keywords);
                std::int64_t timestamp = ExtractTimestamp(event);
                std::string sender = ExtractSenderLabel(event, user_id);
                GroupHistorySnapshot history = group_history.RecordIncoming(
                    group_id, timestamp, sender, plain_text, images, at_bot);
                if (config.group_need_at && !at_bot && !hit_keyword) {
                    return std::string(); // 不回复，但消息已进入该群的历史
                }
                std::cout << "[群聊] 群 " << group_id << " 成员 " << user_id << ": " << plain_text << std::endl;
                std::string current_message = plain_text.empty()
                    ? "[用户发送了一张或多张图片]"
                    : BuildAIMessage(api, event["message"], plain_text);
                ai::ChatRequest request = BuildGroupChatRequest(
                    config.ai.system_prompt, config.ai.user_prompt, group_id,
                    history, timestamp, sender,
                    current_message, images);
                std::string reply;
                if (ReplyWithAI(api, "group", group_id, request, &reply)) {
                    const std::string bot_sender = config.bot_qq.empty()
                        ? "机器人"
                        : "机器人(QQ:" + config.bot_qq + ")";
                    group_history.RecordBotReply(group_id, std::time(nullptr), bot_sender,
                                                 reply, at_bot);
                }
            } else if (message_type == "private") {
                if (plain_text.empty()) {
                    return std::string();
                }
                // master（主人）私聊消息按指令/内容处理，永不交给 AI
                if (!config.master_qq.empty() && user_id == config.master_qq) {
                    std::cout << "[主人] " << user_id << ": " << plain_text << std::endl;
                    HandleMasterPrivate(api, user_id, plain_text);
                } else if (config.private_chat_enabled) {
                    std::cout << "[私聊] " << user_id << ": " << plain_text << std::endl;
                    std::string ai_message = BuildAIMessage(api, event["message"], plain_text);
                    ai::ChatRequest request{config.ai.system_prompt, {}};
                    if (!config.ai.user_prompt.empty()) {
                        request.messages.push_back({
                            "user", "【用户提示词】\n" + config.ai.user_prompt
                        });
                    }
                    request.messages.push_back({"user", ai_message});
                    ReplyWithAI(api, "private", user_id, request);
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
