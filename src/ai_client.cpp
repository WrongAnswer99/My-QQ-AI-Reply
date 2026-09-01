#include "ai_client.hpp"

#include <curl/curl.h>
#include <mutex>

#include "json.hpp"

using json = nlohmann::json;

namespace {

// 回调函数，用于接收 HTTP 响应数据
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

} // namespace

namespace ai {

namespace {

Settings g_settings;          // 全局 AI 设置
std::once_flag g_init_flag;   // 保证 curl_global_init 只执行一次

void GlobalInitOnce() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

} // namespace

bool Init(const Settings& settings, std::string& error) {
    if (settings.api_key.empty() || settings.base_url.empty() || settings.model.empty()) {
        error = "AI 配置不完整（api_key / base_url / model 均不能为空）";
        return false;
    }

    g_settings = settings;
    std::call_once(g_init_flag, GlobalInitOnce);
    return true;
}

std::string Chat(const ChatRequest& request, std::string& error) {
    if (g_settings.api_key.empty()) {
        error = "AI 客户端未初始化，请先调用 ai::Init";
        return std::string();
    }

    // 构建 JSON 请求体
    json request_body;
    request_body["model"] = g_settings.model;
    request_body["messages"] = json::array();
    request_body["messages"].push_back({
        {"role", "system"},
        {"content", request.system_prompt}
    });
    request_body["messages"].push_back({
        {"role", "user"},
        {"content", request.user_message}
    });
    request_body["stream"] = false;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "curl_easy_init 失败";
        return std::string();
    }

    std::string request_json = request_body.dump();
    std::string read_buffer;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + g_settings.api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, g_settings.base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode result = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = std::string("HTTP 请求失败: ") + curl_easy_strerror(result);
        return std::string();
    }

    // 解析响应
    try {
        json response = json::parse(read_buffer);
        if (response.contains("error")) {
            error = "API 返回错误: " + response["error"].dump();
            return std::string();
        }
        if (response.contains("choices")) {
            return response["choices"][0]["message"]["content"].get<std::string>();
        }
        if (response.contains("text")) {
            return response["text"].get<std::string>();
        }
        error = "无法识别的响应: " + response.dump();
    } catch (const json::exception& e) {
        error = std::string("JSON 解析失败: ") + e.what();
        return std::string();
    }
    return std::string();
}

} // namespace ai
