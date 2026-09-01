#include "note.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

#include "json.hpp"

using json = nlohmann::json;

Note::Note(const std::string& file_path) : file_path_(file_path) {
    // 确保存储目录存在（不存在则创建）
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file_path_).parent_path(), ec);
}

void Note::Load() {
    entries_.clear();

    std::ifstream in(file_path_);
    if (!in.is_open()) {
        return;
    }

    try {
        json data = json::parse(in);
        if (data.is_array()) {
            for (const auto& item : data) {
                if (item.is_string()) {
                    entries_.push_back(item.get<std::string>());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[note 加载失败] " << file_path_ << "：" << e.what() << std::endl;
    }
}

size_t Note::Add(const std::string& text) {
    entries_.push_back(text);

    if (!SaveToFile()) {
        std::cerr << "[note 保存失败] " << file_path_ << std::endl;
    }
    return entries_.size();
}

size_t Note::Count() const {
    return entries_.size();
}

std::string Note::ListAll() const {
    std::string result;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (i > 0) {
            result += "\n";
        }
        result += std::to_string(i + 1) + ". " + entries_[i];
    }
    return result;
}

bool Note::SaveToFile() const {
    json data = json::array();
    for (const auto& entry : entries_) {
        data.push_back(entry);
    }

    std::ofstream out(file_path_);
    if (!out.is_open()) {
        return false;
    }
    out << data.dump(4); // 缩进 4 便于阅读
    return true;
}
