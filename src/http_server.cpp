#include "http_server.hpp"

#include <cctype>
#include <cstring>
#include <iostream>

namespace {

// 从请求头中解析 Content-Length（不区分大小写）
size_t ParseContentLength(const std::string& header) {
    std::string lower = header;
    for (auto& ch : lower) {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }

    size_t pos = lower.find("content-length:");
    if (pos == std::string::npos) {
        return 0;
    }

    size_t value_start = pos + std::string("content-length:").size();
    while (value_start < lower.size() && isspace(static_cast<unsigned char>(lower[value_start]))) {
        ++value_start;
    }

    size_t value_end = value_start;
    while (value_end < lower.size() && isdigit(static_cast<unsigned char>(lower[value_end]))) {
        ++value_end;
    }

    if (value_end == value_start) {
        return 0;
    }
    return static_cast<size_t>(std::stoull(lower.substr(value_start, value_end - value_start)));
}

// 发送 HTTP 200 响应
void SendResponse(SOCKET client, const std::string& response_body) {
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(response_body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + response_body;
    send(client, response.data(), static_cast<int>(response.size()), 0);
}

// 读取 chunked 编码的请求体（RFC 7230 6.3），成功返回 true
// pending 为已经读到的数据（空行之后的部分），读取结果写入 body
bool ReadChunkedBody(SOCKET client, std::string& pending, std::string& body) {
    std::string raw = std::move(pending);

    while (true) {
        // 读取一个 chunk 的大小行（十六进制，以 CRLF 结束）
        size_t line_end = raw.find("\r\n");
        while (line_end == std::string::npos) {
            char buf[4096];
            int received = recv(client, buf, sizeof(buf), 0);
            if (received <= 0) {
                return false;
            }
            raw.append(buf, received);
            if (raw.size() > 64 * 1024) {
                return false;
            }
            line_end = raw.find("\r\n");
        }

        std::string size_line = raw.substr(0, line_end);
        raw.erase(0, line_end + 2);

        // 忽略 chunk 扩展（分号后的内容）
        size_t semicolon = size_line.find(';');
        if (semicolon != std::string::npos) {
            size_line = size_line.substr(0, semicolon);
        }
        // 去掉首尾空白
        size_t start = size_line.find_first_not_of(" \t");
        size_t end   = size_line.find_last_not_of(" \t");
        if (start == std::string::npos) {
            return false;
        }
        size_line = size_line.substr(start, end - start + 1);

        size_t chunk_size = 0;
        try {
            chunk_size = static_cast<size_t>(std::stoul(size_line, nullptr, 16));
        } catch (...) {
            return false;
        }

        // 大小为 0 表示传输结束，读取 trailer（可为空）直至空行
        if (chunk_size == 0) {
            while (true) {
                // 空行（单独的 CRLF）表示消息体结束
                if (raw.size() >= 2 && raw.compare(0, 2, "\r\n") == 0) {
                    raw.erase(0, 2);
                    break;
                }
                // 读取一行 trailer 字段
                size_t line_end = raw.find("\r\n");
                while (line_end == std::string::npos) {
                    char buf[4096];
                    int received = recv(client, buf, sizeof(buf), 0);
                    if (received <= 0) {
                        return false;
                    }
                    raw.append(buf, received);
                    if (raw.size() > 64 * 1024) {
                        return false;
                    }
                    line_end = raw.find("\r\n");
                }
                raw.erase(0, line_end + 2);
            }
            break;
        }

        // 读取 chunk 数据及其后的 CRLF
        while (raw.size() < chunk_size + 2) {
            char buf[4096];
            int received = recv(client, buf, sizeof(buf), 0);
            if (received <= 0) {
                return false;
            }
            raw.append(buf, received);
            if (raw.size() > 64 * 1024 + chunk_size) {
                return false;
            }
        }
        body.append(raw, 0, chunk_size);
        raw.erase(0, chunk_size + 2);
    }
    return true;
}

} // namespace

HttpServer::HttpServer(int port) : port_(port) {}

HttpServer::~HttpServer() {
    Stop();
}

void HttpServer::SetHandler(RequestHandler handler) {
    handler_ = std::move(handler);
}

bool HttpServer::Start() {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }

    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port_));

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&HttpServer::AcceptLoop, this);
    return true;
}

void HttpServer::Stop() {
    running_ = false;
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void HttpServer::AcceptLoop() {
    while (running_) {
        SOCKET client = accept(listen_socket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!running_) {
                break;
            }
            continue;
        }
        std::thread(&HttpServer::HandleClient, this, client).detach();
    }
}

void HttpServer::HandleClient(SOCKET client) {
    // 设置接收超时（毫秒），防止异常连接把处理线程永久卡住
    int timeout_ms = 10000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

    // 读取请求头（直到空行 "\r\n\r\n"）
    std::string raw;
    char buf[4096];
    while (raw.find("\r\n\r\n") == std::string::npos) {
        int received = recv(client, buf, sizeof(buf), 0);
        if (received <= 0) {
            closesocket(client);
            return;
        }
        raw.append(buf, received);
        if (raw.size() > 64 * 1024) {
            closesocket(client);
            return;
        }
    }

    size_t header_end = raw.find("\r\n\r\n");
    std::string header = raw.substr(0, header_end);
    std::string initial_body = raw.substr(header_end + 4);

    // 判断是否 chunked 传输编码
    std::string header_lower = header;
    for (auto& ch : header_lower) {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }
    bool is_chunked = header_lower.find("transfer-encoding: chunked") != std::string::npos;

    std::string body;
    if (is_chunked) {
        // chunked 编码：按块大小解析请求体
        std::string pending = std::move(initial_body);
        if (!ReadChunkedBody(client, pending, body)) {
            closesocket(client);
            return;
        }
    } else {
        // 普通 Content-Length 方式
        body = std::move(initial_body);
        size_t content_length = ParseContentLength(header);
        while (body.size() < content_length) {
            int received = recv(client, buf, sizeof(buf), 0);
            if (received <= 0) {
                closesocket(client);
                return;
            }
            body.append(buf, received);
        }
    }

    std::string response_body;
    if (handler_) {
        response_body = handler_(body);
    }
    SendResponse(client, response_body);
    closesocket(client);
}
