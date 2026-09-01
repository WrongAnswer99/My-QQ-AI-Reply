#pragma once

#include <string>

#include "json.hpp"

// OneBot 11 HTTP API 客户端（基于 libcurl）
class OneBotApi {
public:
    explicit OneBotApi(const std::string& base_url, const std::string& token = "");
    ~OneBotApi();

    // 发送私聊消息，成功返回 true
    bool SendPrivateMsg(const std::string& user_id, const std::string& message);

    // 发送群聊消息，成功返回 true
    bool SendGroupMsg(const std::string& group_id, const std::string& message);

    // 根据消息 id 获取消息内容（data.message 部分存入 message），成功返回 true
    bool GetMsg(const std::string& message_id, nlohmann::json& message);

private:
    // 调用任意 OneBot action，body_json 为动作参数，响应 JSON 存入 response
    bool PostJson(const std::string& action, const std::string& body_json, std::string& response);

    std::string base_url_;
    std::string token_;
};
