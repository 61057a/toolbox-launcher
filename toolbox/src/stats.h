#pragma once
#include "common.h"
#include <map>

// 单个工具的使用记录。last 是 UTC FILETIME 的 64 位整数,0 表示从未启动。
struct ToolStat
{
    int count;
    ULONGLONG last;

    ToolStat() : count(0), last(0) {}
};

// 使用统计,保存在 <exe目录>\stats.ini。
// 格式与 tools.ini 一致,每节名对应工具的 id(tools.ini 里的节名):
//   [OpenCode]
//   count=3
//   last=133750000000000000
class StatsStore
{
public:
    StatsStore();

    void Load(const std::wstring& path);
    void Save() const;

    ToolStat Get(const std::wstring& id) const;
    void Record(const std::wstring& id);

    // 按最后启动时间倒序取最近的工具 id,最多 maxCount 个。
    void RecentIds(std::vector<std::wstring>& ids, size_t maxCount) const;

private:
    std::wstring m_path;
    std::map<std::wstring, ToolStat> m_stats;
};
