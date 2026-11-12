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
