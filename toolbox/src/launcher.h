#pragma once
#include "common.h"
#include <memory>

// 外部工具启动项。每个工具对应 tools.ini 里的一个 [节]。
struct LauncherItem
{
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
    std::wstring error;      // 非空表示该项不可用
    std::shared_ptr<Gdiplus::Bitmap> iconBitmap; // 从 exe 提取的真实图标
};

// 读取 <exeDir>\tools.ini。文件不存在返回空列表。
std::vector<LauncherItem> LoadLaunchers(const std::wstring& exeDir);

// 启动工具。成功返回 true;失败时 err 带原因。
bool LaunchLauncher(const LauncherItem& item, std::wstring& err);

// 在资源管理器中定位工具文件。
bool RevealLauncher(const LauncherItem& item, std::wstring& err);

// 当前进程 exe 所在目录(不带结尾反斜杠)。
std::wstring GetExeDir();

// 读取文本文件(UTF-8);失败返回 false。
bool ReadTextFileUtf8(const std::wstring& path, std::string& out);
