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

#include <shellapi.h>

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
