#pragma once
#include "common.h"

// 本地代理池(ProxyPool)的检测与控制。
// 启动器面板和 OpenCode 启动流程共用这里的实现。

// 127.0.0.1:port 是否正在监听。非阻塞 connect,最多等 timeoutMs 毫秒。
bool IsLocalPortOpen(int port, int timeoutMs = 250);

// 代理池 GUI 的默认位置:<exe目录>\tools\ProxyPool\proxypool-gui.exe
std::wstring ProxyPoolGuiPath();

// 启动代理池 GUI(窗口可见)。只负责拉起进程,不等端口就绪。
bool StartProxyPool(std::wstring& err);

// 拉起代理池并等待端口就绪;已就绪时直接返回 true。
bool EnsureProxyPoolStarted(int port, int waitMs, std::wstring& err);

// 把代理池窗口切到前台;没在运行就先启动。
bool ShowProxyPoolWindow(int port, std::wstring& err);

// 结束 proxy-pool 的 GUI 与命令行进程,并等端口释放。
bool StopProxyPool(std::wstring& err);

// ---------------------------------------------------------------------------
// ProxyPool 本机控制接口(默认 127.0.0.1:代理端口+1,只绑本机)
// 用于「启动 OpenCode 时固定出口,额度用尽后再换一个出口」。
// ---------------------------------------------------------------------------

// 代理端口对应的控制端口;port 不合理时返回 0。
int ProxyControlPort(int proxyPort);

struct ProxyPoolStatus
{
    bool reachable;          // 控制接口能访问
    bool fixed;              // 当前是否固定节点
    int size;                // 池里可用节点数
    std::wstring pinned;     // 固定节点的 key,例如 1.2.3.4:8080

    ProxyPoolStatus() : reachable(false), fixed(false), size(0) {}
};

bool QueryProxyPool(int ctlPort, ProxyPoolStatus& status);

// 固定到一个健康节点;已经固定在健康节点上时不会换,避免出口 IP 乱跳。
bool FixProxyPool(int ctlPort, std::wstring& pinned, std::wstring& err);

// 换到下一个健康节点,并把 dead(旧节点)在 ttlSec 秒内标记为额度用尽。
bool RotateProxyPool(int ctlPort, const std::wstring& dead, int ttlSec,
                     std::wstring& pinned, std::wstring& err);

// 恢复自动轮换。
bool AutoProxyPool(int ctlPort, std::wstring& err);
