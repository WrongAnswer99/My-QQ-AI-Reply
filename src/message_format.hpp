#pragma once

#include <string>

#include "json.hpp"

// 清理模型偶尔仿照历史输出的“[时间] 机器人(QQ):”前缀。
std::string CleanAIReply(const std::string& reply);

// 将回复中的 [@QQ号] 或 @QQ号 转成 OneBot 11 消息段数组。
nlohmann::json BuildGroupReplySegments(const std::string& reply);
