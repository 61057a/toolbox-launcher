#include "stats.h"

#include <stdlib.h>

// 把 wchar 字符串转成 64 位无符号整数,失败返回 0。
static ULONGLONG ParseU64(const wchar_t* text)
{
    if (!text || !*text)
    {
        return 0;
    }
    return (ULONGLONG)_wcstoui64(text, NULL, 10);
}

StatsStore::StatsStore()
{
}

void StatsStore::Load(const std::wstring& path)
{
    m_path = path;
    m_stats.clear();

    // 先取所有节名。GetPrivateProfileSectionNames 返回的是双 null 结尾的串,
    // 文件不存在或为空时返回 0。
    DWORD size = 4096;
    std::vector<wchar_t> buf;
    DWORD n = 0;
    for (;;)
    {
        buf.resize(size);
        ZeroMemory(&buf[0], size * sizeof(wchar_t));
        n = GetPrivateProfileSectionNamesW(&buf[0], size, path.c_str());
        if (n < size - 2 || size >= 1024 * 1024)
        {
            break;
        }
        size *= 2;
    }

    if (n == 0)
    {
        return;
    }

    for (const wchar_t* p = &buf[0]; *p; p += wcslen(p) + 1)
    {
        std::wstring id(p);
        if (id.empty())
        {
            continue;
        }

        ToolStat st;
        st.count = (int)GetPrivateProfileIntW(id.c_str(), L"count", 0, path.c_str());

        wchar_t last[64];
        ZeroMemory(last, sizeof(last));
        GetPrivateProfileStringW(id.c_str(), L"last", L"", last, 64, path.c_str());
        st.last = ParseU64(last);

        m_stats[id] = st;
    }
}

void StatsStore::Save() const
{
    if (m_path.empty())
    {
        return;
    }

    for (std::map<std::wstring, ToolStat>::const_iterator it = m_stats.begin();
         it != m_stats.end(); ++it)
    {
        wchar_t buf[64];
        _itow(it->second.count, buf, 10);
        WritePrivateProfileStringW(it->first.c_str(), L"count", buf, m_path.c_str());

        wchar_t last[64];
        _ui64tow((ULONGLONG)it->second.last, last, 10);
        WritePrivateProfileStringW(it->first.c_str(), L"last", last, m_path.c_str());
    }
}

ToolStat StatsStore::Get(const std::wstring& id) const
{
    std::map<std::wstring, ToolStat>::const_iterator it = m_stats.find(id);
    if (it == m_stats.end())
    {
        return ToolStat();
    }
    return it->second;
}

void StatsStore::Record(const std::wstring& id)
{
    if (id.empty())
    {
        return;
    }

    ToolStat& st = m_stats[id];
    st.count += 1;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    st.last = li.QuadPart;
}

void StatsStore::RecentIds(std::vector<std::wstring>& ids, size_t maxCount) const
{
    ids.clear();

    std::vector<std::pair<ULONGLONG, std::wstring> > used;
    for (std::map<std::wstring, ToolStat>::const_iterator it = m_stats.begin();
         it != m_stats.end(); ++it)
    {
        if (it->second.last == 0 || it->second.count <= 0)
        {
            continue;
        }
        used.push_back(std::make_pair(it->second.last, it->first));
    }

    std::sort(used.begin(), used.end());
    for (size_t i = used.size(); i > 0 && ids.size() < maxCount; --i)
    {
        ids.push_back(used[i - 1].second);
    }
}
