# QQ-AIreply

一个部署在 QQ 上的 AI 机器人。基于 [NapCat](https://github.com/NapNeko/NapCatQQ)（OneBot 11 实现）接入 QQ 消息，对接阿里云百炼（DashScope）OpenAI 兼容接口，实现群聊 / 私聊的智能问答。

## 功能特性

- **私聊问答**：任何人都可与机器人私聊（可在配置中关闭）
- **群聊问答**：默认需要被 `@` 才回复，也可配置触发关键词
- **回复上下文**：引用（回复）某条消息时，机器人会把被引用消息的原文一并带给 AI 理解
- **角色扮演**：通过提示词支持让机器人扮演任意角色
- **记录功能**：主人引用消息并发 `!note` 可归档内容，`!note` 查看全部记录
- **主人指令**：私聊中 `!` 开头的消息按指令处理（`!help` / `!status` / `!note`），主人消息永不交给 AI

## 工作原理

```
QQ 客户端 ──> NapCat（注入 QQ 进程，解析事件）
                 │  正向 HTTP 上报（事件）
                 ▼
QQ-AIreply 机器人（本地 HTTP 上报服务，默认 8081 端口）
                 │  libcurl 调用
                 ▼
阿里云百炼（DashScope OpenAI 兼容 chat/completions）
                 │  回复文本
                 ▼
机器人经 NapCat HTTP API 发送群聊 / 私聊消息
```

## 目录结构

```
.
├── src/               # 机器人源码（C++17）
│   ├── main.cpp       # 主程序：事件分发、群聊/私聊逻辑、主人指令
│   ├── config.*       # config.json 解析
│   ├── ai_client.*    # 阿里云百炼 AI 接口客户端（libcurl）
│   ├── onebot_api.*   # OneBot 11 HTTP API 客户端
│   ├── http_server.*  # 本地 HTTP 上报服务（NapCat 事件推送入口）
│   ├── note.*         # 记录功能（持久化到 userdata/note.json）
│   └── test_ai.cpp    # AI 接口独立测试程序
├── thirdparty/        # 第三方头文件（nlohmann/json）
├── config.json        # 运行配置（含密钥，已被 git 忽略，不入库）
├── config_example.json# 配置模板（占位符，克隆后复制为 config.json 填写）
├── CMakeLists.txt     # 构建脚本
├── start_bot.ps1      # 一键启动 NapCat + 机器人
└── NapCat.Shell/      # NapCat 运行目录（自行下载，不入库）
```

## 环境要求

- Windows + [MSYS2 MinGW64](https://www.msys2.org/)（含 gcc、cmake、libcurl）
- **NapCat（Shell 版）**：本项目在 **v4.18.19** 上测试通过，保证支持该版本；其他版本兼容性未知，建议使用相同版本。下载后解压到项目根目录下的 `NapCat.Shell/`
  - 下载链接：<https://github.com/NapNeko/NapCatQQ/releases/download/v4.18.19/NapCat.Shell.zip>
- **QQ 客户端**：NapCat 对各 QQ 客户端版本兼容性不同，需选择与所用 NapCat 匹配的 QQ 版本，可前往 <https://rodert.github.io/qq-versions/> 下载任意版本。本项目在 **9.9.30_260429**（Windows x64）上测试通过：
  - 下载链接：<https://github.com/Rodert/qq-versions/releases/download/qq-windows-x64-9.9.30-20260429/QQ_9.9.30_260429_x64_01.exe>
  - 安装后请将实际路径填入 `config.json` 的 `qq_path`（详见下文配置说明）
- 一个用于登录 NapCat 的 QQ 号作为机器人本体
- 阿里云百炼 API Key（[开通地址](https://bailian.console.aliyun.com/)）

## 快速开始

### 1. 配置

```powershell
# 复制配置模板并填写真实信息
Copy-Item config_example.json config.json
```

编辑 `config.json`（复制自 `config_example.json`），按下面的说明逐项填写。下面以 `config_example.json` 为准，解释每一个字段：

**顶层字段**

| 字段 | 说明 |
| --- | --- |
| `napcat_http_base` | NapCat 正向 HTTP 服务的地址，格式 `http://IP:端口`。默认 NapCat 端口为 3000，若在 WebUI 中改动过端口需同步修改 |
| `napcat_token` | NapCat HTTP API 的 `access_token`。需登录 NapCat WebUI（默认 `http://127.0.0.1:6099/webui`），在「网络配置」中查看或设置该 token，**必须与 NapCat 中配置的完全一致**；若 NapCat 未启用鉴权则留空 |
| `http_report_port` | 机器人本地 HTTP 上报服务的监听端口（默认 8081）。NapCat 会把消息事件推送至此，需与 NapCat WebUI 中配置的 HTTP 上报地址端口一致 |
| `bot_qq` | 机器人自己的 QQ 号（即登录 NapCat 的那个账号），用于判断群消息中是否 `@` 了机器人 |
| `master_qq` | 主人 QQ 号。主人私聊机器人的消息按指令处理（`!help` / `!status` / `!note`），**永不交给 AI**。**若留空则没有"主人"概念**：私聊中任何 `!` 指令都不生效，所有用户的私聊消息（`private_chat_enabled=true` 时）都会交给 AI 回复 |
| `qq_path` | 本机 QQ 客户端的完整安装路径，**必须替换为你实际安装的位置**，例如 `C:\Program Files\Tencent\QQNT\QQ.exe`。`start_bot.ps1` 会用它启动 QQ |
| `private_chat_enabled` | 是否允许机器人回复**私聊**消息：`true` 回复所有人私聊；`false` 只处理主人指令，其他人私聊不回复 |
| `group_need_at` | 群聊中是否必须被 `@` 才回复：`true` 仅当被 `@`（或命中触发关键词）时回复；`false` 群里所有消息都会触发 |
| `group_trigger_keywords` | 群聊触发关键词数组。消息即使没被 `@`，只要包含其中任意关键词也会回复；`[]` 表示不启用关键词触发 |

**`ai` 对象字段（阿里云百炼配置）**

| 字段 | 说明 |
| --- | --- |
| `ai.api_key` | 阿里云百炼 API Key（百炼控制台创建），用于调用大模型接口，**含敏感信息请勿提交仓库** |
| `ai.base_url` | OpenAI 兼容接口地址，**须为完整的 `chat/completions` 端点**。公共地址如 `https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions`；也可在百炼控制台获取账号专属的工作空间地址 |
| `ai.model` | 使用的模型名，如 `qwen-max`、`qwen-plus` 等，需与 `base_url` 所在账号/工作空间支持的模型一致 |
| `ai.system_prompt` | 系统提示词，用于定义机器人的昵称、语气、回复规则、角色扮演等，可按需自由编写 |

**AI 响应解析说明**

本项目在阿里云百炼 OpenAI 兼容接口上测试，测试使用的模型为 **`qwen-max`**。程序按以下优先级解析返回的 JSON（见 `src/ai_client.cpp`）：

1. 含 `error` 字段 → 报错并放弃本次回复；
2. 含 `choices` 数组 → 取 `choices[0].message.content`（OpenAI 兼容标准格式）；
3. 含 `text` 字段 → 直接取文本（部分接口的简化格式）。

> 注意：以上仅覆盖已知返回结构。若更换其他平台/模型（如其他服务商的兼容接口、DashScope 原生接口等），其返回 JSON 结构可能不同，需要同步调整 `src/ai_client.cpp` 中的响应解析逻辑。

> 注意：`config.json` 含敏感信息已被 `.gitignore` 排除，切勿提交到仓库。

### 2. 构建

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

> 若 libcurl / MinGW 不在默认位置，请按本机环境修改 `CMakeLists.txt` 顶部的 `MSYS2_PREFIX` 路径。

### 3. 在 NapCat 中配置上报

先用 `start_bot.ps1` 启动 NapCat 并扫码登录 QQ，然后打开 WebUI（默认 `http://127.0.0.1:6099/webui`），在「网络配置」中新增一个**正向 HTTP 服务**并设置**消息事件上报**：

- **正向 HTTP 服务**（机器人调用 NapCat API 发消息用）：
  - 监听地址：`0.0.0.0`，端口 `3000`（需与 `config.json` 的 `napcat_http_base` 一致）
  - `access_token`：此处设置/查看的 token 就是 `config.json` 的 `napcat_token`，**两边必须完全一致**（留空表示不鉴权，则 `napcat_token` 也留空）
- **HTTP 上报服务**（NapCat 推消息事件给机器人用）：
  - 上报地址：`http://127.0.0.1:8081/`（端口需与 `config.json` 的 `http_report_port` 一致）

### 4. 启动

```powershell
# 一键启动 NapCat + 机器人
powershell -ExecutionPolicy Bypass -File .\start_bot.ps1
```

也可以手动分别运行：先启动 NapCat，再运行 `build\qq_ai_reply.exe`。

## 使用说明

- **群聊**：`@机器人 问题`（默认需要被 @；若关闭了 `group_need_at` 或消息含触发关键词则无需 @）
- **私聊**：直接发消息即可（`private_chat_enabled` 控制开关）
- **主人指令**（仅 `master_qq` 私聊有效，均以 `!` 开头）：
  - `!help` — 查看帮助
  - `!status` — 查看运行状态
  - `!note` — 查看记录列表；先引用（回复）某条消息再发 `!note` 可将其加入记录

> **若 `master_qq` 留空**：机器人不再区分主人与普通用户——私聊中 `!` 指令不生效，所有用户的私聊消息都会（在 `private_chat_enabled=true` 时）交给 AI 回复，相当于"来者不拒"模式。群聊行为不受 `master_qq` 影响。
- **回复上下文**：引用一条消息发送，机器人会结合被引用消息理解后回答

## 隐私与安全

- API Key、Token、QQ 号等敏感信息仅存于本地 `config.json`，不入库
- 运行数据（记录等）保存在 `userdata/`，NapCat 运行数据保存在 `NapCat.Shell/`，均不入库
- 请勿将本机器人用于任何违法或骚扰用途
