#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 一条带时间和发送者信息的群聊记录。
struct GroupHistoryImage {
    std::string mime_type;
    std::string base64;
};

struct GroupHistoryEntry {
    std::uint64_t sequence = 0;
    std::int64_t timestamp = 0;
    std::string sender;
    std::string text;
    std::vector<GroupHistoryImage> images;
};

struct GroupHistorySnapshot {
    std::vector<GroupHistoryEntry> messages;
    std::vector<GroupHistoryEntry> mentions;
    std::vector<GroupHistoryEntry> replies;
};

// 按群号维护滚动历史，并持久化到本地 JSON 文件。
class GroupHistoryStore {
public:
    GroupHistoryStore(std::size_t message_limit, std::size_t interaction_limit,
                      bool image_history_enabled,
                      std::string file_path = "userdata/group_history.json");

    // 从 JSON 文件恢复历史。文件不存在视为没有历史，不是错误。
    bool Load(std::string& error);

    // 原子地取得当前消息之前的历史，再记录本条群消息。
    // 如果本条消息 @ 了机器人，也同时加入 @ 历史。
    GroupHistorySnapshot RecordIncoming(const std::string& group_id,
                                        std::int64_t timestamp,
                                        const std::string& sender,
                                        const std::string& text,
                                        const std::vector<GroupHistoryImage>& images,
                                        bool mentions_bot);

    // 记录机器人已成功发送到群里的回复；reply_to_mention 为 true 时，
    // 同时加入“对 @ 的回复”历史。
    void RecordBotReply(const std::string& group_id,
                        std::int64_t timestamp,
                        const std::string& bot_sender,
                        const std::string& text,
                        bool reply_to_mention);

private:
    struct PerGroupHistory {
        std::deque<GroupHistoryEntry> messages;
        std::deque<GroupHistoryEntry> mentions;
        std::deque<GroupHistoryEntry> replies;
    };

    static void PushAndTrim(std::deque<GroupHistoryEntry>& entries,
                            GroupHistoryEntry entry,
                            std::size_t limit);
    bool SaveToFileLocked() const;

    std::size_t message_limit_;
    std::size_t interaction_limit_;
    bool image_history_enabled_;
    std::string file_path_;
    std::uint64_t next_sequence_ = 1;
    std::mutex mutex_;
    std::unordered_map<std::string, PerGroupHistory> groups_;
};
