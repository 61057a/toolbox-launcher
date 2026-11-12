#pragma once
#include "common.h"

// 监听 OpenCode 便携版数据库里的「免费额度用尽」事件。
// 只用 Windows 自带的 winsqlite3.dll 读库,不引入 SQLite 源码。
//
// 额度错误在 session_message 里的形状(type='idle'):
//   {"outcome":"failed","error":{"type":"provider.free-limit","status":429,
//    "retryAfterMs":46458000,...}}
// assistant 消息的 reasoning 文本里也可能出现同样的字样,
// 所以查询只认 type='idle' 且带 outcome=failed 的行。
class QuotaWatcher
{
public:
    QuotaWatcher();
    ~QuotaWatcher();

    // 开始监视。水线设为当前时间,只关心之后新写入的错误,
    // 避免把上一次会话残留的额度错误当成新事件。
    void Watch(const std::wstring& dbPath);
    void Stop();
    bool Active() const;

    // 增量扫描。命中新的额度用尽事件返回 true,
    // retryAfterMs 为事件自带的恢复时间(毫秒),缺失时为 0。
    bool Poll(long long& retryAfterMs);

private:
    std::wstring m_dbPath;
    long long m_watermark;
    bool m_active;
};
