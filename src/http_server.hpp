#pragma once

#include <functional>
#include <string>
#include <thread>
#include <winsock2.h>

// 极简 HTTP 服务端：仅用于接收 NapCat 的事件上报（POST JSON 请求）
class HttpServer {
public:
    // 请求处理器：接收 POST 请求 body，返回响应 body（空串表示 200 无内容）
    using RequestHandler = std::function<std::string(const std::string& body)>;

    explicit HttpServer(int port);
    ~HttpServer();

    // 启动服务：初始化 WinSock 并开始监听，失败返回 false
    bool Start();

    // 设置请求处理回调（处理 NapCat 上报的事件）
    void SetHandler(RequestHandler handler);

    // 停止服务并关闭监听套接字
    void Stop();

    int GetPort() const { return port_; }

private:
    void AcceptLoop();          // 监听循环：接受连接并为每个连接开一个线程处理
    void HandleClient(SOCKET client); // 处理单个连接：读取请求并响应

    int  port_;
    SOCKET listen_socket_ = INVALID_SOCKET;
    bool running_ = false;
    std::thread accept_thread_;
    RequestHandler handler_;
};
