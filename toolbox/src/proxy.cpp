// winsock2.h 必须在 windows.h 之前,common.h 里包含 windows.h
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "proxy.h"
#include "launcher.h"
#include "ui.h"

#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

namespace {

bool InitWinsock()
{
    static int state = 0; // 0 未初始化 / 1 成功 / -1 失败
    if (state == 0)
    {
        WSADATA wsa;
        ZeroMemory(&wsa, sizeof(wsa));
        state = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? 1 : -1;
    }
    return state > 0;
}

std::wstring DirNameOf(const std::wstring& file)
{
    size_t p = file.find_last_of(L"\\/");
    if (p == std::wstring::npos)
    {
        return std::wstring();
    }
    return file.substr(0, p);
}

// 只等端口,不关心对端是谁:连上即认为在监听。
bool StartPoolProcess(std::wstring& err)
{
    err.clear();
    std::wstring exe = ProxyPoolGuiPath();
    if (!PathFileExistsW(exe.c_str()))
    {
        err = L"找不到 ProxyPool：" + exe;
        return false;
    }
    if (FindProcessByExePath(exe) != 0)
    {
        return true; // 已经在跑,只是端口还没起来
    }

    std::wstring dir = DirNameOf(exe);
    HINSTANCE r = ShellExecuteW(NULL, L"open", exe.c_str(), NULL,
                                dir.empty() ? NULL : dir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
    {
        err = L"启动 ProxyPool 失败";
        return false;
    }
    return true;
}

} // namespace

bool IsLocalPortOpen(int port, int timeoutMs)
{
    if (!InitWinsock())
    {
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    bool open = false;
    int rc = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (rc == 0)
    {
        open = true; // 本机端口一般是立刻完成或立刻拒绝
    }
    else if (WSAGetLastError() == WSAEWOULDBLOCK)
    {
        fd_set wr;
        fd_set ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET(s, &wr);
        FD_SET(s, &ex);

        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int n = select(0, NULL, &wr, &ex, &tv);
        if (n > 0 && FD_ISSET(s, &wr))
        {
            int soError = 0;
            int len = sizeof(soError);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soError, &len) == 0)
            {
                open = (soError == 0);
            }
        }
    }

    closesocket(s);
    return open;
}

std::wstring ProxyPoolGuiPath()
{
    return GetExeDir() + L"\\tools\\ProxyPool\\proxypool-gui.exe";
}

bool StartProxyPool(std::wstring& err)
{
    return StartPoolProcess(err);
}

bool EnsureProxyPoolStarted(int port, int waitMs, std::wstring& err)
{
    err.clear();
    if (IsLocalPortOpen(port, 300))
    {
        return true;
    }
    if (!StartPoolProcess(err))
    {
        return false;
    }

    int waited = 0;
    while (waited < waitMs)
    {
        if (IsLocalPortOpen(port, 250))
        {
            return true;
        }
        Sleep(200);
        waited += 200;
    }

    err = L"代理池启动超时，8080 还没开始监听";
    return false;
}

bool ShowProxyPoolWindow(int port, std::wstring& err)
{
    err.clear();
    std::wstring exe = ProxyPoolGuiPath();
    DWORD pid = FindProcessByExePath(exe);
    if (pid == 0)
    {
        return StartPoolProcess(err) && EnsureProxyPoolStarted(port, 8000, err);
    }
    return FocusRunningInstance(pid, err);
}

bool StopProxyPool(std::wstring& err)
{
    err.clear();
    int killed = KillProcessesByExePath(ProxyPoolGuiPath());
    killed += KillProcessesByExePath(GetExeDir() + L"\\tools\\ProxyPool\\proxypool.exe");

    if (killed == 0)
    {
        err = L"代理池没有在运行";
        return false;
    }

    for (int i = 0; i < 25; ++i)
    {
        if (!IsLocalPortOpen(8080, 150))
        {
            return true;
        }
        Sleep(100);
    }

    err = L"已结束进程，但 8080 仍被占用";
    return false;
}

// ---------------------------------------------------------------------------
// 控制接口客户端
// ---------------------------------------------------------------------------
namespace
{

bool LocalHttpGet(int port, const std::string& target, std::string& body, int timeoutMs)
{
    body.clear();
    if (port <= 0 || !InitWinsock())
    {
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    int rc = connect(s, (sockaddr*)&addr, sizeof(addr));
    if (rc != 0)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            closesocket(s);
            return false;
        }

        fd_set wr;
        fd_set ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET(s, &wr);
        FD_SET(s, &ex);
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int n = select(0, NULL, &wr, &ex, &tv);
        int soError = 0;
        int len = sizeof(soError);
        if (n <= 0 || !FD_ISSET(s, &wr) ||
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soError, &len) != 0 ||
            soError != 0)
        {
            closesocket(s);
            return false;
        }
    }

    nonBlocking = 0;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    DWORD ioTimeout = (DWORD)timeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ioTimeout, sizeof(ioTimeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ioTimeout, sizeof(ioTimeout));

    std::string req = "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                      "Connection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < req.size())
    {
        int n = send(s, req.data() + sent, (int)(req.size() - sent), 0);
        if (n <= 0)
        {
            closesocket(s);
            return false;
        }
        sent += (size_t)n;
    }

    std::string raw;
    char buf[2048];
    for (;;)
    {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            break;
        }
        raw.append(buf, (size_t)n);
        if (raw.size() > 64 * 1024)
        {
            break; // 控制接口的响应只可能是几行
        }
    }
    closesocket(s);

    if (raw.compare(0, 12, "HTTP/1.1 200") != 0)
    {
        return false;
    }
    size_t h = raw.find("\r\n\r\n");
    if (h == std::string::npos)
    {
        return false;
    }
    body = raw.substr(h + 4);
    return true;
}

// 取 "key=value" 形式的字段,没有则返回空串
std::string Field(const std::string& body, const char* name)
{
    std::string key(name);
    key += '=';
    size_t pos = 0;
    while (pos < body.size())
    {
        size_t end = body.find('\n', pos);
        if (end == std::string::npos)
        {
            end = body.size();
        }
        std::string line = body.substr(pos, end - pos);
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }
        if (line.compare(0, key.size(), key) == 0)
        {
            return line.substr(key.size());
        }
        pos = end + 1;
    }
    return std::string();
}

// 节点 key 只可能是 ASCII(ip/域名:端口);其他字符一律不带进 URL
std::string AsciiKey(const std::wstring& wide)
{
    std::string s = WideToUtf8Str(wide);
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || c == '.' || c == ':' ||
                  c == '-' || c == '_';
        if (!ok)
        {
            return std::string();
        }
    }
    return s;
}

std::wstring ReasonOf(const std::string& body)
{
    std::string reason = Field(body, "reason");
    return reason.empty() ? std::wstring(L"代理池控制接口没有返回原因")
                          : Utf8ToWideStr(reason);
}

} // namespace

int ProxyControlPort(int proxyPort)
{
    if (proxyPort <= 0 || proxyPort >= 65535)
    {
        return 0;
    }
    return proxyPort + 1;
}

bool QueryProxyPool(int ctlPort, ProxyPoolStatus& status)
{
    status = ProxyPoolStatus();
    std::string body;
    if (!LocalHttpGet(ctlPort, "/status", body, 1200))
    {
        return false;
    }
    if (Field(body, "ok") != "1")
    {
        return false;
    }

    status.reachable = true;
    status.fixed = (Field(body, "mode") == "fixed");
    status.pinned = Utf8ToWideStr(Field(body, "pinned"));
    status.size = atoi(Field(body, "size").c_str());
    return true;
}

bool FixProxyPool(int ctlPort, std::wstring& pinned, std::wstring& err)
{
    pinned.clear();
    err.clear();

    std::string body;
    if (!LocalHttpGet(ctlPort, "/fixed", body, 2500))
    {
        err = L"代理池控制接口不可用（端口 " + std::to_wstring(ctlPort) + L"）";
        return false;
    }
    if (Field(body, "ok") != "1")
    {
        err = ReasonOf(body);
        return false;
    }

    pinned = Utf8ToWideStr(Field(body, "pinned"));
    if (pinned.empty())
    {
        err = L"代理池没有可用节点";
        return false;
    }
    return true;
}

bool RotateProxyPool(int ctlPort, const std::wstring& dead, int ttlSec,
                     std::wstring& pinned, std::wstring& err)
{
    pinned.clear();
    err.clear();

    if (ttlSec < 60)
    {
        ttlSec = 3600;
    }
    if (ttlSec > 7 * 24 * 3600)
    {
        ttlSec = 7 * 24 * 3600;
    }

    std::string target = "/rotate?ttl=" + std::to_string(ttlSec);
    std::string key = AsciiKey(dead);
    if (!key.empty())
    {
        target += "&dead=" + key;
    }

    std::string body;
    if (!LocalHttpGet(ctlPort, target, body, 2500))
    {
        err = L"代理池控制接口不可用";
        return false;
    }

    pinned = Utf8ToWideStr(Field(body, "pinned"));
    if (Field(body, "ok") != "1" || pinned.empty())
    {
        err = ReasonOf(body);
        return false;
    }
    return true;
}

bool AutoProxyPool(int ctlPort, std::wstring& err)
{
    err.clear();
    std::string body;
    if (!LocalHttpGet(ctlPort, "/auto", body, 1500) || Field(body, "ok") != "1")
    {
        err = L"代理池控制接口不可用";
        return false;
    }
    return true;
}

