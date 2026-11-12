#include "launcher.h"
#include "ui.h"

#include <shellapi.h>
#include <tlhelp32.h>
#include <cwctype>
#include <memory>

namespace {

std::wstring TrimW(const std::wstring& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring LowerW(const std::wstring& s)
{
    std::wstring r = s;
    for (size_t i = 0; i < r.size(); ++i)
    {
        r[i] = (wchar_t)towlower(r[i]);
    }
    return r;
}

bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix)
{
    std::wstring p(prefix);
    if (s.size() < p.size()) return false;
    return LowerW(s.substr(0, p.size())) == LowerW(p);
}

bool IsUrl(const std::wstring& s)
{
    return StartsWithNoCase(s, L"http://") || StartsWithNoCase(s, L"https://") ||
           StartsWithNoCase(s, L"ftp://") || StartsWithNoCase(s, L"mailto:");
}

std::wstring ExpandEnv(const std::wstring& s)
{
    if (s.find(L'%') == std::wstring::npos)
    {
        return s;
    }
    DWORD need = ExpandEnvironmentStringsW(s.c_str(), NULL, 0);
    if (need == 0)
    {
        return s;
    }
    std::vector<wchar_t> buf(need + 1, 0);
    DWORD got = ExpandEnvironmentStringsW(s.c_str(), &buf[0], need + 1);
    if (got == 0 || got > need + 1)
    {
        return s;
    }
    return std::wstring(&buf[0]);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& rel)
{
    if (dir.empty()) return rel;
    if (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')
    {
        return dir + rel;
    }
    return dir + L"\\" + rel;
}

std::wstring DirName(const std::wstring& file)
{
    size_t p = file.find_last_of(L"\\/");
    if (p == std::wstring::npos) return std::wstring();
    return file.substr(0, p);
}

// 从 exe / lnk / 文件夹提取系统图标(GDI+ 位图)。失败返回空。
std::shared_ptr<Gdiplus::Bitmap> LoadFileIcon(const std::wstring& file)
{
    HICON hIcon = NULL;

    // 先按 64px 提取 exe 内嵌图标,放大到详情页大图标时更清晰
    UINT iconId = 0;
    UINT got = PrivateExtractIconsW(file.c_str(), 0, 64, 64, &hIcon, &iconId, 1, 0);
    if (got == 0 || !hIcon)
    {
        SHFILEINFOW sfi;
        ZeroMemory(&sfi, sizeof(sfi));
        UINT flags = SHGFI_ICON | SHGFI_LARGEICON;
        if (SHGetFileInfoW(file.c_str(), 0, &sfi, sizeof(sfi), flags))
        {
            hIcon = sfi.hIcon;
        }
    }
    if (!hIcon)
    {
        return std::shared_ptr<Gdiplus::Bitmap>();
    }

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromHICON(hIcon);
    DestroyIcon(hIcon);

    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok)
    {
        delete bmp;
        return std::shared_ptr<Gdiplus::Bitmap>();
    }
    return std::shared_ptr<Gdiplus::Bitmap>(bmp);
}

void FinalizeItem(LauncherItem& cur, const std::wstring& sectionName,
                  const std::wstring& exeDir)
{
    cur.id = sectionName;
    if (cur.category.empty()) cur.category = L"其他";
    if (cur.name.empty()) cur.name = sectionName;
    if (cur.icon.empty() && !cur.name.empty()) cur.icon = cur.name.substr(0, 1);

    cur.resolved = cur.path;
    cur.isUrl = IsUrl(cur.path);

    if (!cur.isUrl)
    {
        cur.resolved = ExpandEnv(cur.resolved);
        if (PathIsRelativeW(cur.resolved.c_str()))
        {
            cur.resolved = JoinPath(exeDir, cur.resolved);
        }
        cur.exists = PathFileExistsW(cur.resolved.c_str()) ? true : false;
        if (cur.path.empty())
        {
            cur.error = L"未配置 path";
        }
        else if (!cur.exists)
        {
            cur.error = L"找不到文件";
        }
        else
        {
            // 优先用 iconfile 指定的图标源;没配或提取失败时退回 path 本身
            if (!cur.iconFile.empty())
            {
                std::wstring iconPath = ExpandEnv(cur.iconFile);
                if (PathIsRelativeW(iconPath.c_str()))
                {
                    iconPath = JoinPath(exeDir, iconPath);
                }
                cur.iconBitmap = LoadFileIcon(iconPath);
            }
            if (!cur.iconBitmap)
            {
                cur.iconBitmap = LoadFileIcon(cur.resolved);
            }
        }
    }
    else
    {
        cur.exists = true;
    }
}

bool ShellExecuteItem(const LauncherItem& item, const wchar_t* verb, std::wstring& err)
{
    err.clear();
    if (!item.error.empty())
    {
        err = item.error + L"：" + item.resolved;
        return false;
    }

    std::wstring workDir;
    if (!item.isUrl)
    {
        workDir = DirName(item.resolved);
    }

    HINSTANCE r = ShellExecuteW(NULL, verb, item.resolved.c_str(),
                                item.args.empty() ? NULL : item.args.c_str(),
                                workDir.empty() ? NULL : workDir.c_str(),
                                SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
    {
        DWORD lastError = GetLastError();
        if (lastError == ERROR_CANCELLED || (INT_PTR)r == SE_ERR_ACCESSDENIED)
        {
            err = L"已取消管理员授权";
            return false;
        }
        wchar_t buf[64];
        swprintf_s(buf, 64, L"启动失败 (错误码 %d)", (int)(INT_PTR)r);
        err = buf;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 运行中检测
// ---------------------------------------------------------------------------

struct ProcEntry
{
    DWORD pid;
    std::wstring path;
};

typedef BOOL (WINAPI *QueryFullProcessImageNameWFn)(HANDLE, DWORD, LPWSTR, PDWORD);

// MinGW 4.9 的头文件里不一定声明 QueryFullProcessImageNameW,动态取。
QueryFullProcessImageNameWFn GetQueryFullProcessImageNameW()
{
    static QueryFullProcessImageNameWFn fn = NULL;
    static bool tried = false;
    if (!tried)
    {
        tried = true;
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (k32)
        {
            fn = (QueryFullProcessImageNameWFn)GetProcAddress(k32, "QueryFullProcessImageNameW");
        }
    }
    return fn;
}

bool GetProcessPath(DWORD pid, std::wstring& out)
{
    out.clear();
#if defined(PROCESS_QUERY_LIMITED_INFORMATION)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
#else
    HANDLE h = NULL;
#endif
    if (!h)
    {
        h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    }
    if (!h)
    {
        return false;
    }

    QueryFullProcessImageNameWFn fn = GetQueryFullProcessImageNameW();
    if (fn)
    {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = MAX_PATH * 2;
        if (fn(h, 0, buf, &n) && n > 0)
        {
            out.assign(buf, n);
        }
    }
    CloseHandle(h);
    return !out.empty();
}

void SnapshotProcesses(std::vector<ProcEntry>& list)
{
    list.clear();

    DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
    {
        return;
    }

    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == self)
            {
                continue;
            }
            ProcEntry e;
            e.pid = pe.th32ProcessID;
            if (GetProcessPath(e.pid, e.path))
            {
                list.push_back(e);
            }
        }
        while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

std::wstring FileExtLower(const std::wstring& file)
{
    size_t dot = file.find_last_of(L'.');
    size_t slash = file.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
    {
        return std::wstring();
    }
    return LowerW(file.substr(dot));
}

// 判断某个进程是否是条目的运行实例。
// exe 走精确路径比较;.bat / .cmd 的脚本宿主不在工具目录,
// 改用「进程路径位于工具目录下」判定(如 OpenCode 的 opencode.exe)。
bool PathMatchesItem(const LauncherItem& item, const std::wstring& procPath)
{
    if (item.isUrl || item.resolved.empty() || procPath.empty())
    {
        return false;
    }

    std::wstring ext = FileExtLower(item.resolved);
    if (ext == L".bat" || ext == L".cmd")
    {
        std::wstring dir = DirName(item.resolved);
        if (dir.empty())
        {
            return false;
        }
        dir += L"\\";
        return procPath.size() > dir.size() &&
               _wcsnicmp(procPath.c_str(), dir.c_str(), dir.size()) == 0;
    }

    return _wcsicmp(procPath.c_str(), item.resolved.c_str()) == 0;
}

struct FindWindowCtx
{
    DWORD pid;
    HWND hwnd;
};

BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM lParam)
{
    FindWindowCtx* ctx = (FindWindowCtx*)lParam;
    if (!IsWindowVisible(hwnd))
    {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER) != NULL)
    {
        return TRUE;
    }
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
    {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid)
    {
        ctx->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

} // namespace

std::wstring GetExeDir()
{
    wchar_t buf[MAX_PATH + 4];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (n == 0) return std::wstring();
    std::wstring full(buf, n);
    return DirName(full);
}

bool ReadTextFileUtf8(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 4 * 1024 * 1024)
    {
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    if (!out.empty())
    {
        if (!ReadFile(h, &out[0], (DWORD)out.size(), &read, NULL))
        {
            CloseHandle(h);
            out.clear();
            return false;
        }
    }
    CloseHandle(h);
    out.resize(read);
    // 去掉 UTF-8 BOM
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
    {
        out.erase(0, 3);
    }
    return true;
}

std::vector<LauncherItem> LoadLaunchers(const std::wstring& exeDir)
{
    std::vector<LauncherItem> items;

    std::string raw;
    if (!ReadTextFileUtf8(JoinPath(exeDir, L"tools.ini"), raw))
    {
        return items;
    }

    std::wstring text = Utf8ToWideStr(raw);
    std::vector<std::wstring> lines;
    {
        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t nl = text.find(L'\n', pos);
            std::wstring line = (nl == std::wstring::npos)
                                    ? text.substr(pos)
                                    : text.substr(pos, nl - pos);
            if (!line.empty() && line[line.size() - 1] == L'\r')
            {
                line.erase(line.size() - 1);
            }
            lines.push_back(line);
            if (nl == std::wstring::npos) break;
            pos = nl + 1;
        }
    }

    bool hasSection = false;
    LauncherItem cur;
    std::wstring sectionName;

    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring line = TrimW(lines[i]);
        if (line.empty()) continue;
        if (line[0] == L'#' || line[0] == L';') continue;

        if (line[0] == L'[')
        {
            if (hasSection)
            {
                FinalizeItem(cur, sectionName, exeDir);
                items.push_back(cur);
            }

            size_t close = line.find(L']');
            sectionName = (close == std::wstring::npos)
                              ? line.substr(1)
                              : line.substr(1, close - 1);
            sectionName = TrimW(sectionName);
            cur = LauncherItem();
            cur.isUrl = false;
            cur.exists = false;
            hasSection = !sectionName.empty();
            continue;
        }

        if (!hasSection) continue;

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = LowerW(TrimW(line.substr(0, eq)));
        std::wstring val = TrimW(line.substr(eq + 1));

        if (key == L"name") cur.name = val;
        else if (key == L"category") cur.category = val;
        else if (key == L"icon") cur.icon = val;
        else if (key == L"iconfile") cur.iconFile = val;
        else if (key == L"desc" || key == L"description") cur.desc = val;
        else if (key == L"path" || key == L"exe") cur.path = val;
        else if (key == L"args" || key == L"arguments") cur.args = val;
    }

    if (hasSection)
    {
        FinalizeItem(cur, sectionName, exeDir);
        items.push_back(cur);
    }

    return items;
}

bool LaunchLauncher(const LauncherItem& item, std::wstring& err)
{
    return ShellExecuteItem(item, L"open", err);
}

bool LaunchLauncherElevated(const LauncherItem& item, std::wstring& err)
{
    if (item.isUrl)
    {
        err = L"网页链接不需要管理员权限";
        return false;
    }
    return ShellExecuteItem(item, L"runas", err);
}

void UpdateRunningItems(std::vector<LauncherItem>& items)
{
    std::vector<ProcEntry> procs;
    SnapshotProcesses(procs);

    for (size_t i = 0; i < items.size(); ++i)
    {
        LauncherItem& item = items[i];
        item.running = false;
        item.runningPid = 0;

        if (item.isUrl || !item.error.empty() || !item.exists)
        {
            continue;
        }

        for (size_t p = 0; p < procs.size(); ++p)
        {
            if (PathMatchesItem(item, procs[p].path))
            {
                item.running = true;
                item.runningPid = procs[p].pid;
                break;
            }
        }
    }
}

DWORD FindProcessByExePath(const std::wstring& exePath)
{
    if (exePath.empty())
    {
        return 0;
    }
    std::vector<ProcEntry> procs;
    SnapshotProcesses(procs);
    for (size_t i = 0; i < procs.size(); ++i)
    {
        if (_wcsicmp(procs[i].path.c_str(), exePath.c_str()) == 0)
        {
            return procs[i].pid;
        }
    }
    return 0;
}

int KillProcessesByExePath(const std::wstring& exePath)
{
    if (exePath.empty())
    {
        return 0;
    }
    std::vector<ProcEntry> procs;
    SnapshotProcesses(procs);

    int killed = 0;
    for (size_t i = 0; i < procs.size(); ++i)
    {
        if (_wcsicmp(procs[i].path.c_str(), exePath.c_str()) != 0)
        {
            continue;
        }
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, procs[i].pid);
        if (!h)
        {
            continue;
        }
        if (TerminateProcess(h, 0))
        {
            ++killed;
        }
        CloseHandle(h);
    }
    return killed;
}

bool FocusRunningInstance(DWORD pid, std::wstring& err)
{
    err.clear();
    if (pid == 0)
    {
        err = L"没有找到运行中的进程";
        return false;
    }

    FindWindowCtx ctx;
    ctx.pid = pid;
    ctx.hwnd = NULL;
    EnumWindows(FindMainWindowProc, (LPARAM)&ctx);

    if (!ctx.hwnd)
    {
        // 控制台程序的窗口属于 conhost,不在目标进程名下。
        // 附加上去问一下,Windows Terminal 下可能拿到隐藏窗口。
        FreeConsole();
        if (AttachConsole(pid))
        {
            HWND con = GetConsoleWindow();
            FreeConsole();
            if (con && IsWindowVisible(con))
            {
                ctx.hwnd = con;
            }
        }
    }

    if (!ctx.hwnd)
    {
        err = L"程序已在运行，但没有找到可切换的窗口";
        return false;
    }

    if (IsIconic(ctx.hwnd))
    {
        ShowWindow(ctx.hwnd, SW_RESTORE);
    }
    BringWindowToTop(ctx.hwnd);
    SetForegroundWindow(ctx.hwnd);
    return true;
}

bool RevealLauncher(const LauncherItem& item, std::wstring& err)
{
    err.clear();
    if (item.isUrl)
    {
        err = L"不是本地文件";
        return false;
    }
    if (!item.exists)
    {
        err = L"找不到文件：" + item.resolved;
        return false;
    }
    std::wstring params = L"/select,\"" + item.resolved + L"\"";
    HINSTANCE r = ShellExecuteW(NULL, L"open", L"explorer.exe", params.c_str(),
                                NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
    {
        err = L"无法打开资源管理器";
        return false;
    }
    return true;
}
