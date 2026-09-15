#include "group_history.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#include "json.hpp"

using json = nlohmann::json;

namespace {

json EntryToJson(const GroupHistoryEntry& entry) {
    json value = {
        {"sequence", entry.sequence},
        {"time", entry.timestamp},
        {"sender", entry.sender},
        {"text", entry.text},
        {"images", json::array()}
    };
    for (const GroupHistoryImage& image : entry.images) {
        value["images"].push_back({
            {"mime_type", image.mime_type},
            {"base64", image.base64}
        });
    }
    return value;
}

bool EntryFromJson(const json& value, GroupHistoryEntry& entry, bool load_images) {
    if (!value.is_object() || !value.contains("sequence") || !value.contains("time")
        || !value.contains("sender") || !value.contains("text")) {
        return false;
    }
    try {
        entry.sequence = value.at("sequence").get<std::uint64_t>();
        entry.timestamp = value.at("time").get<std::int64_t>();
        entry.sender = value.at("sender").get<std::string>();
        entry.text = value.at("text").get<std::string>();
        entry.images.clear();
        if (!load_images && entry.text.empty() && value.contains("images")
            && value["images"].is_array() && !value["images"].empty()) {
            return false;
        }
        if (load_images && value.contains("images") && value["images"].is_array()) {
            for (const json& image_value : value["images"]) {
                if (!image_value.is_object()) {
                    continue;
                }
                const std::string mime_type = image_value.value("mime_type", "");
                const std::string base64 = image_value.value("base64", "");
                if (!base64.empty()) {
                    entry.images.push_back({
                        mime_type.empty() ? "image/jpeg" : mime_type,
                        base64
                    });
                }
            }
        }
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

} // namespace

GroupHistoryStore::GroupHistoryStore(std::size_t message_limit,
                                     std::size_t interaction_limit,
                                     bool image_history_enabled,
                                     std::string file_path)
    : message_limit_(message_limit),
      interaction_limit_(interaction_limit),
      image_history_enabled_(image_history_enabled),
      file_path_(std::move(file_path)) {
    std::error_code error;
    const std::filesystem::path parent = std::filesystem::path(file_path_).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
    }
}

bool GroupHistoryStore::Load(std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream input(file_path_);
    if (!input.is_open()) {
        return true;
    }

    try {
        const json root = json::parse(input);
        if (!root.is_object() || !root.contains("groups") || !root["groups"].is_object()) {
            error = "群聊历史文件格式无效: " + file_path_;
            return false;
        }

        groups_.clear();
        next_sequence_ = 1;
        for (auto group_it = root["groups"].begin(); group_it != root["groups"].end(); ++group_it) {
            if (!group_it.value().is_object()) {
                continue;
            }
            PerGroupHistory history;
            const json& group_json = group_it.value();
            const auto load_entries = [this](const json& values,
                                             std::deque<GroupHistoryEntry>& destination,
                                             std::size_t limit) {
                if (!values.is_array()) {
                    return;
                }
                for (const json& value : values) {
                    GroupHistoryEntry entry;
                    if (!EntryFromJson(value, entry, image_history_enabled_)) {
                        continue;
                    }
                    next_sequence_ = std::max(next_sequence_, entry.sequence + 1);
                    PushAndTrim(destination, std::move(entry), limit);
                }
            };
            load_entries(group_json.value("messages", json::array()),
                         history.messages, message_limit_);
            load_entries(group_json.value("mentions", json::array()),
                         history.mentions, interaction_limit_);
            load_entries(group_json.value("replies", json::array()),
                         history.replies, interaction_limit_);
            groups_.emplace(group_it.key(), std::move(history));
        }
        input.close();
        if (!SaveToFileLocked()) {
            error = "群聊历史裁剪后无法写回: " + file_path_;
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = "群聊历史加载失败: " + file_path_ + "，" + exception.what();
        return false;
    }
}

GroupHistorySnapshot GroupHistoryStore::RecordIncoming(const std::string& group_id,
                                                        std::int64_t timestamp,
                                                        const std::string& sender,
                                                        const std::string& text,
                                                        const std::vector<GroupHistoryImage>& images,
                                                        bool mentions_bot) {
    std::lock_guard<std::mutex> lock(mutex_);
    PerGroupHistory& history = groups_[group_id];

    GroupHistorySnapshot snapshot;
    snapshot.messages.assign(history.messages.begin(), history.messages.end());
    snapshot.mentions.assign(history.mentions.begin(), history.mentions.end());
    snapshot.replies.assign(history.replies.begin(), history.replies.end());

    GroupHistoryEntry entry{
        next_sequence_++, timestamp, sender, text,
        image_history_enabled_ ? images : std::vector<GroupHistoryImage>{}
    };
    PushAndTrim(history.messages, entry, message_limit_);
    if (mentions_bot) {
        PushAndTrim(history.mentions, std::move(entry), interaction_limit_);
    }
    if (!SaveToFileLocked()) {
        std::cerr << "[群聊历史保存失败] " << file_path_ << std::endl;
    }
    return snapshot;
}

void GroupHistoryStore::RecordBotReply(const std::string& group_id,
                                       std::int64_t timestamp,
                                       const std::string& bot_sender,
                                       const std::string& text,
                                       bool reply_to_mention) {
    std::lock_guard<std::mutex> lock(mutex_);
    PerGroupHistory& history = groups_[group_id];
    GroupHistoryEntry entry{next_sequence_++, timestamp, bot_sender, text, {}};
    PushAndTrim(history.messages, entry, message_limit_);
    if (reply_to_mention) {
        PushAndTrim(history.replies, std::move(entry), interaction_limit_);
    }
    if (!SaveToFileLocked()) {
        std::cerr << "[群聊历史保存失败] " << file_path_ << std::endl;
    }
}

void GroupHistoryStore::PushAndTrim(std::deque<GroupHistoryEntry>& entries,
                                    GroupHistoryEntry entry,
                                    std::size_t limit) {
    if (limit == 0) {
        entries.clear();
        return;
    }
    entries.push_back(std::move(entry));
    while (entries.size() > limit) {
        entries.pop_front();
    }
}

bool GroupHistoryStore::SaveToFileLocked() const {
    json root;
    root["version"] = 2;
    root["groups"] = json::object();
    for (const auto& group_pair : groups_) {
        json group_json;
        group_json["messages"] = json::array();
        group_json["mentions"] = json::array();
        group_json["replies"] = json::array();
        for (const GroupHistoryEntry& entry : group_pair.second.messages) {
            group_json["messages"].push_back(EntryToJson(entry));
        }
        for (const GroupHistoryEntry& entry : group_pair.second.mentions) {
            group_json["mentions"].push_back(EntryToJson(entry));
        }
        for (const GroupHistoryEntry& entry : group_pair.second.replies) {
            group_json["replies"].push_back(EntryToJson(entry));
        }
        root["groups"][group_pair.first] = std::move(group_json);
    }

    std::ofstream output(file_path_, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << root.dump(2);
    return output.good();
}
