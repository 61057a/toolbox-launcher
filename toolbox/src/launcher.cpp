#include "launcher.h"
#include "ui.h"

#include <shellapi.h>
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
            cur.iconBitmap = LoadFileIcon(cur.resolved);
        }
    }
    else
    {
        cur.exists = true;
    }
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

    HINSTANCE r = ShellExecuteW(NULL, L"open", item.resolved.c_str(),
                                item.args.empty() ? NULL : item.args.c_str(),
                                workDir.empty() ? NULL : workDir.c_str(),
                                SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
    {
        wchar_t buf[64];
        swprintf_s(buf, 64, L"启动失败 (错误码 %d)", (int)(INT_PTR)r);
        err = buf;
        return false;
    }
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
