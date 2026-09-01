#include "onebot_api.hpp"

#include <curl/curl.h>

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

OneBotApi::OneBotApi(const std::string& base_url, const std::string& token)
    : base_url_(base_url), token_(token) {}

OneBotApi::~OneBotApi() {}

bool OneBotApi::SendPrivateMsg(const std::string& user_id, const std::string& message) {
    json body;
    body["user_id"] = std::stoll(user_id);
    body["message"] = message;

    std::string response;
    return PostJson("send_private_msg", body.dump(), response);
}

bool OneBotApi::SendGroupMsg(const std::string& group_id, const std::string& message) {
    json body;
    body["group_id"] = std::stoll(group_id);
    body["message"] = message;

    std::string response;
    return PostJson("send_group_msg", body.dump(), response);
}

bool OneBotApi::GetMsg(const std::string& message_id, json& message) {
    json body;
    body["message_id"] = std::stoll(message_id);

    std::string response;
    if (!PostJson("get_msg", body.dump(), response)) {
        return false;
    }

    try {
        json resp = json::parse(response);
        if (resp.value("status", "") != "ok" || !resp.contains("data")) {
            return false;
        }
        message = resp["data"]["message"];
        return true;
    } catch (...) {
        return false;
    }
}

bool OneBotApi::PostJson(const std::string& action, const std::string& body_json, std::string& response) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return false;
    }

    bool ok = false;
    std::string url = base_url_ + "/" + action;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!token_.empty()) {
        headers = curl_slist_append(headers, ("Authorization: Bearer " + token_).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            // OneBot 响应格式: {"status":"ok", ...}
            try {
                json resp = json::parse(response);
                ok = (resp.value("status", "") == "ok");
            } catch (...) {
                ok = false;
            }
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}
