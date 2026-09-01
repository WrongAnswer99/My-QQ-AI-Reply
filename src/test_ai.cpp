#include <iostream>
#include <string>

#include "ai_client.hpp"
#include "config.hpp"

// AI 接口独立测试程序：
//   用法: test_ai.exe [消息文本]
//   不带参数时使用内置的默认测试消息
int main(int argc, char* argv[]) {
    // 加载配置
    Config config;
    std::string config_error;
    if (!Config::LoadFromFile("config.json", config, config_error)) {
        std::cerr << "[错误] " << config_error << std::endl;
        return 1;
    }

    // 初始化 AI 客户端
    std::string init_error;
    if (!ai::Init({ config.ai.api_key, config.ai.base_url, config.ai.model }, init_error)) {
        std::cerr << "[初始化失败] " << init_error << std::endl;
        return 1;
    }
    std::cout << "[信息] 模型: " << config.ai.model << std::endl;

    // 构造测试消息
    std::string message = "你好，请用一句话介绍一下你自己";
    if (argc > 1) {
        message = argv[1];
    }
    std::cout << "[请求] " << message << std::endl;

    // 调用 AI
    std::string error;
    std::string reply = ai::Chat({ config.ai.system_prompt, message }, error);
    if (reply.empty()) {
        std::cerr << "[AI 错误] " << error << std::endl;
        return 1;
    }

    std::cout << "[回复] " << reply << std::endl;
    return 0;
}
