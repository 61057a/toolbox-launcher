#include "quota.h"

#include <cstdlib>
#include <cstring>

// winsqlite3.dll 是 Windows 10 自带的 SQLite,这里动态加载,
// 既不增加体积,也不依赖 OpenCode 的安装位置。
// 相关常量来自 sqlite3.h,只用到几个固定值。
namespace
{

const int kOpenReadOnly = 0x00000001;
const int kOk = 0;
const int kRow = 100;
const int kDone = 101;

struct SqliteApi
{
    typedef int (*OpenV2Fn)(const char*, void**, int, const char*);
    typedef int (*CloseFn)(void*);
    typedef int (*PrepareFn)(void*, const char*, int, void**, const char**);
    typedef int (*BindInt64Fn)(void*, int, long long);
    typedef int (*StepFn)(void*);
    typedef const unsigned char* (*ColumnTextFn)(void*, int);
    typedef long long (*ColumnInt64Fn)(void*, int);
    typedef int (*FinalizeFn)(void*);
    typedef int (*BusyTimeoutFn)(void*, int);

    HMODULE dll;
    OpenV2Fn openV2;
    CloseFn close;
    PrepareFn prepare;
    BindInt64Fn bindInt64;
    StepFn step;
    ColumnTextFn columnText;
    ColumnInt64Fn columnInt64;
    FinalizeFn finalize;
    BusyTimeoutFn busyTimeout;
    bool ok;

    SqliteApi()
        : dll(NULL), openV2(NULL), close(NULL), prepare(NULL), bindInt64(NULL),
          step(NULL), columnText(NULL), columnInt64(NULL), finalize(NULL),
          busyTimeout(NULL), ok(false)
    {
        dll = LoadLibraryW(L"winsqlite3.dll");
        if (!dll)
        {
            return;
        }

        openV2 = (OpenV2Fn)GetProcAddress(dll, "sqlite3_open_v2");
        close = (CloseFn)GetProcAddress(dll, "sqlite3_close");
        prepare = (PrepareFn)GetProcAddress(dll, "sqlite3_prepare_v2");
        bindInt64 = (BindInt64Fn)GetProcAddress(dll, "sqlite3_bind_int64");
        step = (StepFn)GetProcAddress(dll, "sqlite3_step");
        columnText = (ColumnTextFn)GetProcAddress(dll, "sqlite3_column_text");
        columnInt64 = (ColumnInt64Fn)GetProcAddress(dll, "sqlite3_column_int64");
        finalize = (FinalizeFn)GetProcAddress(dll, "sqlite3_finalize");
        busyTimeout = (BusyTimeoutFn)GetProcAddress(dll, "sqlite3_busy_timeout");

        ok = openV2 && close && prepare && bindInt64 && step && columnText &&
             columnInt64 && finalize;
    }
};

SqliteApi& Api()
{
    static SqliteApi api;
    return api;
}

std::string Narrow(const std::wstring& wide)
{
    if (wide.empty())
    {
        return std::string();
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                        &out[0], n, NULL, NULL);
    return out;
}

// OpenCode 的 time_created 是 Unix 毫秒
long long NowUnixMs()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000ULL) - 11644473600000LL;
}

bool Contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

// 从 JSON 文本里取 "retryAfterMs": 后的整数,取不到返回 0
long long ParseRetryAfterMs(const std::string& data)
{
    const char* key = "\"retryAfterMs\":";
    size_t p = data.find(key);
    if (p == std::string::npos)
    {
        return 0;
    }
    p += strlen(key);
    while (p < data.size() && (data[p] == ' ' || data[p] == '\t'))
    {
        ++p;
    }
    long long v = 0;
    bool any = false;
    while (p < data.size() && data[p] >= '0' && data[p] <= '9')
    {
        v = v * 10 + (data[p] - '0');
        any = true;
        ++p;
    }
    return any ? v : 0;
}

} // namespace

QuotaWatcher::QuotaWatcher() : m_watermark(0), m_active(false)
{
}

QuotaWatcher::~QuotaWatcher()
{
}

void QuotaWatcher::Watch(const std::wstring& dbPath)
{
    m_dbPath = dbPath;
    m_watermark = NowUnixMs();
    m_active = true;
}

void QuotaWatcher::Stop()
{
    m_active = false;
    m_dbPath.clear();
}

bool QuotaWatcher::Active() const
{
    return m_active;
}

bool QuotaWatcher::Poll(long long& retryAfterMs)
{
    retryAfterMs = 0;
    if (!m_active || m_dbPath.empty())
    {
        return false;
    }

    SqliteApi& api = Api();
    if (!api.ok)
    {
        return false;
    }

    std::string path = Narrow(m_dbPath);
    void* db = NULL;
    if (api.openV2(path.c_str(), &db, kOpenReadOnly, NULL) != kOk || !db)
    {
        // 数据库还没建好(OpenCode 首次启动)或正被独占,下次再试
        if (db)
        {
            api.close(db);
        }
        return false;
    }
    if (api.busyTimeout)
    {
        api.busyTimeout(db, 150); // 别把 UI 线程卡在数据库锁上
    }

    const char* sql =
        "SELECT time_created, data FROM session_message "
        "WHERE time_created > ?1 AND type = 'idle' "
        "AND (data LIKE '%free-limit%' OR data LIKE '%Free usage limit%') "
        "ORDER BY time_created ASC LIMIT 64";

    void* stmt = NULL;
    if (api.prepare(db, sql, -1, &stmt, NULL) != kOk || !stmt)
    {
        api.close(db);
        return false;
    }

    api.bindInt64(stmt, 1, m_watermark);

    bool hit = false;
    long long maxSeen = m_watermark;
    int rc = 0;
    while ((rc = api.step(stmt)) == kRow)
    {
        long long created = api.columnInt64(stmt, 0);
        const unsigned char* text = api.columnText(stmt, 1);
        std::string data = text ? (const char*)text : "";

        if (created > maxSeen)
        {
            maxSeen = created;
        }

        bool failed = Contains(data, "\"outcome\":\"failed\"");
        bool quota = Contains(data, "provider.free-limit") ||
                     Contains(data, "Free usage limit reached");
        if (failed && quota && !hit)
        {
            hit = true;
            retryAfterMs = ParseRetryAfterMs(data);
        }
    }

    api.finalize(stmt);
    api.close(db);

    m_watermark = maxSeen;
    return hit;
}
