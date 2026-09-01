#pragma once

#include <string>
#include <vector>

// 记录管理类：master 通过"引用消息 + !note"添加记录，!note 查看全部记录。
// 持久化存储于 userdata/note.json（JSON 数组格式，天然支持换行/引号等转义）。
class Note {
public:
    explicit Note(const std::string& file_path = "userdata/note.json");

    // 启动时从文件加载已有记录
    void Load();

    // 添加一条记录并持久化到文件，返回当前记录总数
    size_t Add(const std::string& text);

    // 当前记录条数
    size_t Count() const;

    // 生成带编号的完整列表文本（无记录时返回空串），如 "1. xxx\n2. yyy"
    std::string ListAll() const;

private:
    // 将全部记录整体写回 JSON 文件，成功返回 true
    bool SaveToFile() const;

    std::string file_path_;
    std::vector<std::string> entries_;
};
