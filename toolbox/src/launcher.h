#pragma once
#include "common.h"
#include <memory>

// 外部工具启动项。每个工具对应 tools.ini 里的一个 [节]。
struct LauncherItem
{
    std::wstring id;         // tools.ini 里的节名,统计 / 隐藏状态按它记录
    std::wstring name;
    std::wstring category;
    std::wstring icon;       // 单字图标(如 "E" / "搜"),没有真实图标时显示
    std::wstring desc;
    std::wstring path;       // tools.ini 里写的原始路径(相对 exe 目录或绝对路径)
    std::wstring iconFile;   // 可选:图标来源(如 .bat 指向的 exe),相对 exe 目录或绝对路径
    std::wstring args;
    std::wstring resolved;   // 解析后的绝对路径 / URL
    bool isUrl;
    bool exists;
    bool hidden;             // 在启动器里隐藏(状态存在 settings.ini)
    bool running;            // 当前是否有对应进程在运行
    DWORD runningPid;        // 运行中进程的 PID,0 表示没有
    std::wstring error;      // 非空表示该项不可用
    std::shared_ptr<Gdiplus::Bitmap> iconBitmap; // 从 exe 提取的真实图标

    LauncherItem()
        : isUrl(false), exists(false), hidden(false), running(false), runningPid(0) {}
};

// 读取 <exeDir>\tools.ini。文件不存在返回空列表。
std::vector<LauncherItem> LoadLaunchers(const std::wstring& exeDir);

// 启动工具。成功返回 true;失败时 err 带原因。
bool LaunchLauncher(const LauncherItem& item, std::wstring& err);

// 以管理员身份启动工具。用户取消 UAC 时返回 false,err 说明原因。
bool LaunchLauncherElevated(const LauncherItem& item, std::wstring& err);

// 扫描进程列表,更新每个工具的 running / runningPid。
// .bat / .cmd 条目按所在目录前缀匹配(脚本宿主 cmd.exe 本身不在该目录)。
void UpdateRunningItems(std::vector<LauncherItem>& items);

// 按 exe 绝对路径查找进程,返回 PID;0 表示没找到。
DWORD FindProcessByExePath(const std::wstring& exePath);

// 结束该路径下的所有进程,返回结束的数量。
int KillProcessesByExePath(const std::wstring& exePath);

// 把指定进程的主窗口切到前台。控制台程序可能由 conhost 持有窗口。
bool FocusRunningInstance(DWORD pid, std::wstring& err);

// 在资源管理器中定位工具文件。
bool RevealLauncher(const LauncherItem& item, std::wstring& err);

// 当前进程 exe 所在目录(不带结尾反斜杠)。
std::wstring GetExeDir();

// 读取文本文件(UTF-8);失败返回 false。
bool ReadTextFileUtf8(const std::wstring& path, std::string& out);
