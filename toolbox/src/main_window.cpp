#include "main_window.h"
#include "resource.h"
#include "theme.h"
#include "ui.h"
#include "proxy.h"

#include <string.h>

// Win API 显式声明:MinGW 4.9 里 GetKeyboardState 返回 BOOL 而非 const BYTE*,
// 导致无法直接调 ToUnicodeW。这里动态加载,绕开类型问题。
static int (*g_pToUnicodeW)(UINT, UINT, const BYTE*, LPWSTR, int, UINT) = 0;

static void InitToUnicode()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        g_pToUnicodeW = (int (*)(UINT, UINT, const BYTE*, LPWSTR, int, UINT))
                        GetProcAddress(user32, "ToUnicodeW");
    }
}

using namespace Gdiplus;

// 绘制助手
static void FillSq(Graphics& g, const RectF& r, Color c)
{
    SolidBrush b(c);
    g.FillRectangle(&b, r);
}

static void FrameSq(Graphics& g, const RectF& r, Color c, float width = 1.0f)
{
    if (width <= 1.0f)
    {
        Pen p(c, 1.0f);
        g.DrawRectangle(&p, r.X + 0.5f, r.Y + 0.5f, r.Width - 1.0f, r.Height - 1.0f);
    }
    else
    {
        Pen p(c, width);
        g.DrawRectangle(&p, r.X + width / 2.0f, r.Y + width / 2.0f,
                        r.Width - width, r.Height - width);
    }
}

static void FillStrokeSq(Graphics& g, const RectF& r, Color fill, Color border,
                         float width = 1.0f)
{
    FillSq(g, r, fill);
    FrameSq(g, r, border, width);
}

static void DrawItemIcon(Graphics& g, const LauncherItem& item, const RectF& box,
                         bool selected, bool big)
{
    FillStrokeSq(g, box, big ? g_theme.accentSoft : g_theme.panelBg,
                 selected ? g_theme.selectedBorder : g_theme.stroke, 1.0f);

    if (item.iconBitmap)
    {
        Bitmap* bmp = item.iconBitmap.get();
        float pad = big ? SF(11) : SF(5);
        float avail = box.Width - pad * 2;
        float iw = (float)bmp->GetWidth();
        float ih = (float)bmp->GetHeight();
        if (iw > 0 && ih > 0)
        {
            float scale = avail / (iw > ih ? iw : ih);
            float w = iw * scale;
            float h = ih * scale;
            RectF dst(box.X + (box.Width - w) / 2.0f,
                      box.Y + (box.Height - h) / 2.0f, w, h);
            g.DrawImage(bmp, dst);
        }
    }
    else
    {
        Font f(g_uiFont.c_str(), big ? SF(28) : SF(13), FontStyleBold, UnitPixel);
        DrawTextLine(g, item.icon, f, box,
                     selected ? g_theme.accentText : g_theme.textSecondary,
                     StringAlignmentCenter, false);
    }
}

static std::wstring SettingsPath()
{
    return GetExeDir() + L"\\settings.ini";
}

static int LoadThemePreference()
{
    std::wstring path = SettingsPath();
    wchar_t buf[64];
    ZeroMemory(buf, sizeof(buf));
    GetPrivateProfileStringW(L"ui", L"theme", L"graphite", buf, 64, path.c_str());
    return ThemeIndexFromId(buf);
}

static void SaveThemePreference(int index)
{
    std::wstring path = SettingsPath();
    WritePrivateProfileStringW(L"ui", L"theme", ThemeId(index), path.c_str());
}

static std::wstring StatsPath()
{
    return GetExeDir() + L"\\stats.ini";
}

static std::wstring FormatLastUsed(ULONGLONG last)
{
    if (last == 0)
    {
        return std::wstring();
    }

    ULARGE_INTEGER li;
    li.QuadPart = last;
    FILETIME ft;
    ft.dwLowDateTime = li.LowPart;
    ft.dwHighDateTime = li.HighPart;

    FILETIME local;
    if (!FileTimeToLocalFileTime(&ft, &local))
    {
        return std::wstring();
    }
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&local, &st))
    {
        return std::wstring();
    }

    wchar_t buf[64];
    swprintf_s(buf, 64, L"%04d-%02d-%02d %02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

static bool SetClipboardText(const std::wstring& text)
{
    if (!OpenClipboard(NULL))
    {
        return false;
    }
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem)
    {
        CloseClipboard();
        return false;
    }

    void* dst = GlobalLock(mem);
    if (!dst)
    {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    memcpy(dst, text.c_str(), bytes);
    GlobalUnlock(mem);

    if (!SetClipboardData(CF_UNICODETEXT, mem))
    {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

// ---------------------------------------------------------------------------
// 全局通知
// ---------------------------------------------------------------------------
static bool IsOpenCodeEntry(const LauncherItem& item);

static std::wstring DirNameOf(const std::wstring& file)
{
    size_t p = file.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring() : file.substr(0, p);
}

static std::wstring g_lastNotice;
static ULONGLONG g_lastNoticeUntil = 0;
static bool g_noticeSuccess = false;

void MainWindow::ShowNotice(const std::wstring& message, bool success)
{
    g_lastNotice = message;
    g_noticeSuccess = success;
    g_lastNoticeUntil = GetTickCount64() + 2400;
}

// ---------------------------------------------------------------------------
// 构造 / 布局
// ---------------------------------------------------------------------------
MainWindow::MainWindow()
    : m_hwnd(NULL), m_gdiplusToken(0), m_backBitmap(NULL),
      m_backW(0), m_backH(0),
      m_titleH(0), m_sidebarW(0), m_searchH(0),
      m_listItemH(0), m_groupH(0), m_contentHeaderH(0),
      m_showHidden(false), m_menuWidth(0),
      m_scroll(0), m_scrollMax(0), m_selectedGlobal(-1),
      m_hoverRow(-1), m_hoverRegion(0), m_tracking(false),
      m_searchActive(false),
      m_tickTimerId(0), m_statusTimerId(0),
      m_proxyOnline(false), m_proxyStarting(false), m_proxyStartUntil(0),
      m_proxyAuto(true), m_proxyPort(8080),
      m_pinPending(false), m_pinUntil(0), m_quotaNextPoll(0), m_quotaRotations(0)
{
}

MainWindow::~MainWindow()
{
    if (m_backBitmap)
    {
        DeleteObject(m_backBitmap);
        m_backBitmap = NULL;
    }
}

void MainWindow::InitLayout()
{
    m_titleH = S(44);
    m_sidebarW = S(280);
    m_searchH = S(60);
    m_listItemH = S(50);
    m_groupH = S(32);
    m_contentHeaderH = S(64);
}
// ---------------------------------------------------------------------------
// 创建窗口
// ---------------------------------------------------------------------------
void MainWindow::Create(HINSTANCE instance)
{
    InitToUnicode();

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                 LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR);
    wc.lpszClassName = APP_CLASS;
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = sw * 3 / 4;
    int h = sh * 3 / 4;
    if (w < 900) w = 900;
    if (h < 640) h = 640;
    int x = (sw - w) / 2;
    int y = (sh - h) / 2 - 30;

    MainWindow* self = new MainWindow();
    HWND hwnd = CreateWindowExW(
        0, APP_CLASS, L"工具启动器",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_SYSMENU,
        x, y, w, h,
        NULL, NULL, instance, self);

    if (!hwnd)
    {
        delete self;
        return;
    }
    self->m_hwnd = hwnd;

    // Win11:关闭系统圆角,保持硬朗的直角窗口
    {
        typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm)
        {
            DwmSetWindowAttributeFn setAttr =
                (DwmSetWindowAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (setAttr)
            {
                // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_DONOTROUND = 1
                int pref = 1;
                setAttr(hwnd, 33, &pref, sizeof(pref));
            }
            FreeLibrary(dwm);
        }
    }

    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&self->m_gdiplusToken, &input, NULL);

    self->InitLayout();
    ApplyTheme(LoadThemePreference());
    self->LoadHiddenIds();
    self->LoadProxySettings();
    self->m_stats.Load(StatsPath());
    self->ReloadItems();
    self->m_proxyOnline = IsLocalPortOpen(self->m_proxyPort, 400);
    self->m_tickTimerId = SetTimer(hwnd, 1, 60, NULL);
    self->m_statusTimerId = SetTimer(hwnd, 2, 1500, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self)
        {
            self->m_hwnd = hwnd;
        }
    }
    if (self)
    {
        return self->Handle(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 消息处理
// ---------------------------------------------------------------------------
LRESULT MainWindow::Handle(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
    {
        if (m_tickTimerId)
        {
            KillTimer(m_hwnd, m_tickTimerId);
            m_tickTimerId = 0;
        }
        if (m_statusTimerId)
        {
            KillTimer(m_hwnd, m_statusTimerId);
            m_statusTimerId = 0;
        }

        HWND hwnd = m_hwnd;
        ULONG_PTR token = m_gdiplusToken;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        m_hwnd = NULL;

        // 顺序很重要:先销毁本对象(会析构 LauncherItem 里的 GDI+ Bitmap),
        // 再关闭 GDI+;反过来会让 Bitmap 析构访问已释放的 GDI+ 运行时并崩溃。
        delete this;
        if (token)
        {
            Gdiplus::GdiplusShutdown(token);
        }
        PostQuitMessage(0);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(m_hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCCALCSIZE:
        // 去掉 WS_THICKFRAME 带来的不可见边框,让客户区铺满整个窗口
        if (wParam == TRUE)
        {
            return 0;
        }
        break;

    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProcW(m_hwnd, msg, wParam, lParam);
        if (hit == HTCLIENT)
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(m_hwnd, &pt);
            int edge = S(6);
            bool left = pt.x < edge;
            bool right = pt.x >= m_backW - edge;
            bool top = pt.y < edge;
            bool bottom = pt.y >= m_backH - edge;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        return hit;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        OnPaint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (w > 0 && h > 0)
        {
            if (m_backBitmap)
            {
                DeleteObject(m_backBitmap);
                m_backBitmap = NULL;
            }
            m_backW = w;
            m_backH = h;
            HDC dc = GetDC(m_hwnd);
            m_backBitmap = CreateCompatibleBitmap(dc, w, h);
            ReleaseDC(m_hwnd, dc);
            UpdateFilter();
        }
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = S(820);
        mmi->ptMinTrackSize.y = S(560);
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1)
        {
            bool needPaint = m_searchActive;
            if (g_lastNoticeUntil != 0)
            {
                if (GetTickCount64() >= g_lastNoticeUntil)
                {
                    g_lastNoticeUntil = 0;
                }
                needPaint = true;
            }
            if (needPaint)
            {
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
        }
        else if (wParam == 2)
        {
            PollRunningState();
            PollProxyState();
            PollProxyQuota();
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            SetCursorForRegion(HitTest(x, y));
            return TRUE;
        }
        return 0;

    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int region = HitTest(x, y);
        int row = ListItemAt(y);
        bool changed = false;
        if (region == 3 && row != m_hoverRow)
        {
            m_hoverRow = row;
            changed = true;
        }
        if (region != m_hoverRegion)
        {
            m_hoverRegion = region;
            changed = true;
        }
        if (changed)
        {
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        TrackLeave();
        return 0;
    }

    case WM_MOUSELEAVE:
        m_tracking = false;
        m_hoverRow = -1;
        m_hoverRegion = 0;
        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    case WM_EXITSIZEMOVE:
        // 拖动 / 缩放结束后清掉残留的悬停状态
        m_tracking = false;
        m_hoverRow = -1;
        m_hoverRegion = 0;
        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(m_hwnd, &pt);
        if (pt.x < m_sidebarW && pt.y > m_titleH + m_searchH)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            ScrollListBy(-delta / WHEEL_DELTA * S(56));
        }
        return 0;
    }

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlType == ODT_MENU)
        {
            bool separator = false;
            for (size_t i = 0; i < m_menuEntries.size(); ++i)
            {
                if (m_menuEntries[i].separator &&
                    m_menuEntries[i].cmd == (int)mis->itemID)
                {
                    separator = true;
                    break;
                }
            }
            mis->itemWidth = (UINT)(separator ? S(10) : m_menuWidth);
            mis->itemHeight = (UINT)(separator ? S(9) : S(28));
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlType == ODT_MENU)
        {
            DrawMenuEntry(dis);
            return TRUE;
        }
        break;
    }

    case WM_CONTEXTMENU:
    {
        int sx = GET_X_LPARAM(lParam);
        int sy = GET_Y_LPARAM(lParam);
        if (sx == -1 && sy == -1)
        {
            POINT pt;
            pt.x = m_sidebarW / 2;
            pt.y = m_titleH + m_searchH + S(60);
            ClientToScreen(m_hwnd, &pt);
            sx = pt.x;
            sy = pt.y;
        }
        ShowContextMenu(sx, sy);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        int region = HitTest(x, y);

        if (region == 5)
        {
            DestroyWindow(m_hwnd);
            return 0;
        }
        if (region == 6)
        {
            ReloadItems();
            ShowNotice(L"已重新载入工具列表", true);
            InvalidateRect(m_hwnd, NULL, FALSE);
            return 0;
        }
        if (region == 1)
        {
            // 无边框窗口拖动:交给系统非客户区拖动逻辑。
            // 必须释放当前捕获并传真实屏幕坐标,否则拖不动。
            POINT pt;
            GetCursorPos(&pt);
            ReleaseCapture();
            SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION,
                         MAKELPARAM(pt.x, pt.y));
            return 0;
        }
        if (region == 2)
        {
            FocusSearch();
            return 0;
        }
        if (region == 3)
        {
            int row = ListItemAt(y);
            if (row >= 0 && row < (int)m_rows.size())
            {
                if (!m_rows[row].isGroup && m_rows[row].globalIndex >= 0)
                {
                    m_searchActive = false;
                    SelectGlobal(m_rows[row].globalIndex);
                }
            }
            SetFocus(m_hwnd);
            return 0;
        }
        if (region == 7)
        {
            LaunchSelected();
            return 0;
        }
        if (region == 8)
        {
            RevealSelected();
            return 0;
        }
        if (region == 9)
        {
            SwitchTheme();
            return 0;
        }
        if (region == 10)
        {
            StartProxyFromPanel();
            return 0;
        }
        if (region == 11)
        {
            StopProxyFromPanel();
            return 0;
        }
        if (region == 12)
        {
            ToggleProxyAuto();
            return 0;
        }
        if (region == 4)
        {
            SetFocus(m_hwnd);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        if (HitTest(x, y) == 3)
        {
            int row = ListItemAt(y);
            if (row >= 0 && row < (int)m_rows.size() && !m_rows[row].isGroup)
            {
                SelectGlobal(m_rows[row].globalIndex);
                LaunchSelected();
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
    {
        UINT vk = (UINT)wParam;
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (m_searchActive)
        {
            if (vk == VK_ESCAPE)
            {
                m_searchActive = false;
                m_search = L"";
                UpdateFilter();
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }
            if (vk == VK_BACK)
            {
                if (!m_search.empty())
                {
                    m_search.erase(m_search.size() - 1);
                }
                UpdateFilter();
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }
            if (vk == VK_RETURN)
            {
                m_searchActive = false;
                LaunchSelected();
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }
            if (vk == VK_UP)
            {
                MoveSelection(-1);
                return 0;
            }
            if (vk == VK_DOWN)
            {
                MoveSelection(1);
                return 0;
            }
        }
        else
        {
            if (vk == VK_ESCAPE)
            {
                if (!m_search.empty())
                {
                    m_search = L"";
                    UpdateFilter();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
                return 0;
            }
            if (vk == VK_F5)
            {
                ReloadItems();
                ShowNotice(L"已重新载入工具列表", true);
                InvalidateRect(m_hwnd, NULL, FALSE);
                return 0;
            }
            if (vk == VK_RETURN)
            {
                LaunchSelected();
                return 0;
            }
            if (vk == VK_UP)
            {
                MoveSelection(-1);
                return 0;
            }
            if (vk == VK_DOWN)
            {
                MoveSelection(1);
                return 0;
            }
            if (vk >= '1' && vk <= '9')
            {
                int idx = (int)(vk - '1');
                if (idx < (int)m_items.size() &&
                    (!m_items[idx].hidden || m_showHidden))
                {
                    SelectGlobal(idx);
                }
                return 0;
            }
            if (vk == VK_OEM_2 || (ctrl && (vk == 'F' || vk == 'f')))
            {
                FocusSearch();
                return 0;
            }
            if (vk == 'T' || vk == 't')
            {
                SwitchTheme();
                return 0;
            }
            if (vk == VK_APPS ||
                ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && vk == VK_F10))
            {
                SendMessageW(m_hwnd, WM_CONTEXTMENU, (WPARAM)m_hwnd, (LPARAM)-1);
                return 0;
            }
        }

        // 搜索框字符输入
        if (m_searchActive)
        {
            wchar_t wc = 0;
            int scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
            BYTE kb[256];
            GetKeyboardState((BYTE*)kb);
            if (g_pToUnicodeW && g_pToUnicodeW(vk, (UINT)scan, (const BYTE*)kb, &wc, 1, 0) == 1)
            {
                if (wc >= L' ' && m_search.size() < 64)
                {
                    m_search.push_back(wc);
                    UpdateFilter();
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }
        }
        return 0;
    }

    case WM_SETFOCUS:
        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    case WM_KILLFOCUS:
        InvalidateRect(m_hwnd, NULL, FALSE);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 数据
// ---------------------------------------------------------------------------
void MainWindow::ReloadItems()
{
    m_items = LoadLaunchers(GetExeDir());

    for (size_t i = 0; i < m_items.size(); ++i)
    {
        m_items[i].hidden = IsHiddenId(m_items[i].id);
    }
    UpdateRunningItems(m_items);

    if (m_selectedGlobal >= (int)m_items.size())
    {
        m_selectedGlobal = -1;
    }
    UpdateFilter();

    if (m_selectedGlobal < 0)
    {
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            if (!m_rows[i].isGroup)
            {
                m_selectedGlobal = m_rows[i].globalIndex;
                break;
            }
        }
    }
    m_scroll = 0;
    UpdateFilter();
}

void MainWindow::UpdateFilter()
{
    int prevSelected = m_selectedGlobal;
    m_rows.clear();

    std::wstring q = m_search;
    for (size_t i = 0; i < q.size(); ++i)
    {
        q[i] = (wchar_t)towlower(q[i]);
    }
    bool searching = !q.empty();

    // 先过滤出可见工具:隐藏项(除非打开「显示隐藏」)和搜索不匹配项都跳过。
    std::vector<int> visible;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const LauncherItem& t = m_items[i];
        if (t.hidden && !m_showHidden)
        {
            continue;
        }
        if (searching)
        {
            std::wstring hay = t.name + L" " + t.category + L" " + t.desc + L" " + t.path;
            for (size_t k = 0; k < hay.size(); ++k)
            {
                hay[k] = (wchar_t)towlower(hay[k]);
            }
            if (hay.find(q) == std::wstring::npos)
            {
                continue;
            }
        }
        visible.push_back((int)i);
    }

    // 最近使用:非搜索状态下置顶显示,最多 5 项。
    if (!searching)
    {
        std::vector<std::wstring> recentIds;
        m_stats.RecentIds(recentIds, 5);

        std::vector<int> recentIdx;
        for (size_t r = 0; r < recentIds.size(); ++r)
        {
            for (size_t v = 0; v < visible.size(); ++v)
            {
                if (m_items[visible[v]].id == recentIds[r])
                {
                    recentIdx.push_back(visible[v]);
                    break;
                }
            }
        }

        if (!recentIdx.empty())
        {
            Row g;
            g.isGroup = true;
            g.globalIndex = -1;
            g.label = L"最近使用";
            g.height = m_groupH;
            m_rows.push_back(g);

            for (size_t r = 0; r < recentIdx.size(); ++r)
            {
                Row row;
                row.isGroup = false;
                row.globalIndex = recentIdx[r];
                row.label = m_items[recentIdx[r]].name;
                row.height = m_listItemH;
                m_rows.push_back(row);
            }
        }
    }

    // 分类按工具在 tools.ini 里首次出现的顺序排列。
    std::vector<std::wstring> categories;
    for (size_t v = 0; v < visible.size(); ++v)
    {
        const std::wstring& cat = m_items[visible[v]].category;
        bool found = false;
        for (size_t c = 0; c < categories.size(); ++c)
        {
            if (categories[c] == cat)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            categories.push_back(cat);
        }
    }

    for (size_t c = 0; c < categories.size(); ++c)
    {
        Row g;
        g.isGroup = true;
        g.globalIndex = -1;
        g.label = categories[c];
        g.height = m_groupH;
        m_rows.push_back(g);

        for (size_t v = 0; v < visible.size(); ++v)
        {
            const LauncherItem& t = m_items[visible[v]];
            if (t.category != categories[c])
            {
                continue;
            }
            Row r;
            r.isGroup = false;
            r.globalIndex = visible[v];
            r.label = t.name;
            r.height = m_listItemH;
            m_rows.push_back(r);
        }
    }

    int total = 0;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        total += m_rows[i].height;
    }
    int listTop = m_titleH + m_searchH;
    int viewH = m_backH - listTop;
    if (viewH < 0) viewH = 0;
    m_scrollMax = (total > viewH) ? (total - viewH) : 0;
    if (m_scroll > m_scrollMax) m_scroll = m_scrollMax;
    if (m_scroll < 0) m_scroll = 0;

    // 选中项被过滤掉时,自动选中第一个可见项
    bool visibleSel = false;
    int firstVisible = -1;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].isGroup)
        {
            if (firstVisible < 0) firstVisible = m_rows[i].globalIndex;
            if (m_rows[i].globalIndex == prevSelected)
            {
                visibleSel = true;
                break;
            }
        }
    }
    if (!visibleSel)
    {
        m_selectedGlobal = firstVisible;
    }
}

void MainWindow::SelectGlobal(int index)
{
    if (index < 0 || index >= (int)m_items.size())
    {
        return;
    }
    m_selectedGlobal = index;

    // 让选中项滚动到可见范围
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].isGroup && m_rows[i].globalIndex == index)
        {
            int rel = 0;
            for (size_t k = 0; k < i; ++k)
            {
                rel += m_rows[k].height;
            }
            int rowH = m_rows[i].height;
            int viewTop = m_titleH + m_searchH;
            int viewH = m_backH - viewTop;
            if (viewH < 0) viewH = 0;
            if (rel < m_scroll)
            {
                m_scroll = rel;
            }
            else if (rel + rowH > m_scroll + viewH)
            {
                m_scroll = rel + rowH - viewH;
            }
            break;
        }
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::MoveSelection(int delta)
{
    if (m_rows.empty())
    {
        return;
    }
    int cur = -1;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].isGroup && m_rows[i].globalIndex == m_selectedGlobal)
        {
            cur = (int)i;
            if (delta >= 0)
            {
                break; // 向下:从第一处出现开始
            }
            // 向上:记住最后一处出现,跨过「最近使用」里的重复项
        }
    }
    int idx = cur;
    if (idx < 0)
    {
        idx = (delta >= 0) ? -1 : (int)m_rows.size();
    }
    int guard = 0;
    do
    {
        idx += (delta > 0 ? 1 : -1);
        if (idx < 0) idx = (int)m_rows.size() - 1;
        if (idx >= (int)m_rows.size()) idx = 0;
        ++guard;
    } while (guard < (int)m_rows.size() * 2 &&
             (m_rows[idx].isGroup || m_rows[idx].globalIndex < 0 ||
              (cur >= 0 && m_rows[idx].globalIndex == m_selectedGlobal)));

    if (idx >= 0 && idx < (int)m_rows.size() && !m_rows[idx].isGroup)
    {
        m_searchActive = false;
        SelectGlobal(m_rows[idx].globalIndex);
    }
}

void MainWindow::FocusSearch()
{
    m_searchActive = true;
    SetFocus(m_hwnd);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

const LauncherItem* MainWindow::SelectedItem() const
{
    if (m_selectedGlobal < 0 || m_selectedGlobal >= (int)m_items.size())
    {
        return NULL;
    }
    return &m_items[m_selectedGlobal];
}

void MainWindow::LaunchSelected()
{
    LaunchItem(m_selectedGlobal, false, false);
}

void MainWindow::LaunchItem(int index, bool elevated, bool forceNew)
{
    if (index < 0 || index >= (int)m_items.size())
    {
        return;
    }

    LauncherItem& item = m_items[index];
    bool openCode = IsOpenCodeEntry(item);

    // 已经在运行:切到已有窗口,避免重复启动(OpenCode 会抢 4010 端口)。
    if (!forceNew && !elevated && item.running)
    {
        std::wstring err;
        if (FocusRunningInstance(item.runningPid, err))
        {
            ShowNotice(L"已切换到运行中的窗口：" + item.name, true);
        }
        else
        {
            ShowNotice(L"已在运行：" + item.name + L"（" + err + L"）", true);
        }
        if (openCode)
        {
            AttachProxyForOpenCode(item);
        }
        InvalidateRect(m_hwnd, NULL, FALSE);
        return;
    }

    if (!elevated)
    {
        MaybeStartProxyFor(item);
    }

    std::wstring err;
    bool ok = elevated ? LaunchLauncherElevated(item, err)
                       : LaunchLauncher(item, err);
    if (ok)
    {
        m_stats.Record(item.id);
        m_stats.Save();
        UpdateRunningItems(m_items);
        // 让「最近使用」分组立即出现
        UpdateFilter();
        SelectGlobal(index);
        if (m_proxyStarting)
        {
            ShowNotice(L"已启动：" + item.name + L"（正在等代理池就绪）", true);
        }
        else
        {
            ShowNotice(elevated ? L"已以管理员身份启动：" + item.name
                                : L"已启动：" + item.name, true);
        }

        // 固定出口要等代理池端口就绪,这里先把「待固定 / 额度监测」挂上
        if (openCode)
        {
            AttachProxyForOpenCode(item);
        }
    }
    else
    {
        ShowNotice(err, false);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

static bool IsOpenCodeEntry(const LauncherItem& item)
{
    if (item.isUrl || item.resolved.empty())
    {
        return false;
    }
    std::wstring lower = item.resolved;
    for (size_t i = 0; i < lower.size(); ++i)
    {
        lower[i] = (wchar_t)towlower(lower[i]);
    }
    const wchar_t* suffix = L"start-opencode.bat";
    size_t n = wcslen(suffix);
    return lower.size() >= n && lower.compare(lower.size() - n, n, suffix) == 0;
}

void MainWindow::LoadProxySettings()
{
    std::wstring path = SettingsPath();
    m_proxyAuto = GetPrivateProfileIntW(L"proxy", L"auto", 1, path.c_str()) != 0;

    int port = (int)GetPrivateProfileIntW(L"proxy", L"port", 8080, path.c_str());
    if (port <= 0 || port > 65535)
    {
        port = 8080;
    }
    m_proxyPort = port;
}

void MainWindow::SaveProxyAuto()
{
    std::wstring path = SettingsPath();
    WritePrivateProfileStringW(L"proxy", L"auto", m_proxyAuto ? L"1" : L"0", path.c_str());
}

void MainWindow::PollProxyState()
{
    bool online = IsLocalPortOpen(m_proxyPort, 120);
    bool starting = m_proxyStarting;

    if (online && starting)
    {
        starting = false;
        ShowNotice(L"代理池已就绪：127.0.0.1:8080", true);
    }
    else if (starting && GetTickCount64() > m_proxyStartUntil)
    {
        starting = false;
        ShowNotice(L"代理池启动超时，看看它自己的窗口有没有报错", false);
    }

    if (online != m_proxyOnline || starting != m_proxyStarting)
    {
        // 代理池被重新拉起:恢复固定与额度监测(OpenCode 还在跑的话)
        if (online && !m_proxyOnline && !m_openCodeDb.empty() &&
            !m_quota.Active() && AnyOpenCodeRunning())
        {
            m_quota.Watch(m_openCodeDb);
            m_pinPending = true;
            m_pinUntil = GetTickCount64() + 20000;
            m_quotaNextPoll = GetTickCount64() + 2500;
        }
        m_proxyOnline = online;
        m_proxyStarting = starting;
        InvalidateRect(m_hwnd, NULL, FALSE);
    }

    if (!m_proxyOnline)
    {
        return;
    }

    // 刚启动 OpenCode:代理池可能还在起,控制接口可用后再固定
    if (m_pinPending)
    {
        if (!TryPinProxy() && GetTickCount64() > m_pinUntil)
        {
            m_pinPending = false;
            ShowNotice(L"没能固定出口：代理池控制接口无响应（重启代理池可恢复）", false);
        }
        return;
    }

    // 代理池每次刷新都会重建节点列表,固定可能被清掉;这里顺手补上。
    // 同时把用户在代理池窗口里手动点选的节点同步到面板。
    if (!m_proxyPinned.empty() && m_quota.Active())
    {
        ProxyPoolStatus st;
        if (!QueryProxyPool(ProxyCtlPort(), st))
        {
            return;
        }
        if (!st.fixed)
        {
            std::wstring pinned;
            std::wstring err;
            if (FixProxyPool(ProxyCtlPort(), pinned, err) &&
                !pinned.empty() && pinned != m_proxyPinned)
            {
                m_proxyPinned = pinned;
                ShowNotice(L"代理出口已重新固定：" + pinned, true);
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
        }
        else if (!st.pinned.empty() && st.pinned != m_proxyPinned)
        {
            m_proxyPinned = st.pinned;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
    }
}

void MainWindow::StartProxyFromPanel()
{
    std::wstring err;
    if (m_proxyOnline)
    {
        if (ShowProxyPoolWindow(m_proxyPort, err))
        {
            ShowNotice(L"已打开代理池窗口", true);
        }
        else
        {
            ShowNotice(err, false);
        }
    }
    else if (StartProxyPool(err))
    {
        m_proxyStarting = true;
        m_proxyStartUntil = GetTickCount64() + 25000;
        ShowNotice(L"正在启动代理池，等它开始监听 8080…", true);
    }
    else
    {
        ShowNotice(err, false);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::StopProxyFromPanel()
{
    std::wstring err;
    if (StopProxyPool(err))
    {
        m_proxyOnline = false;
        m_proxyStarting = false;
        m_quota.Stop();
        m_pinPending = false;
        m_proxyPinned.clear();
        ShowNotice(L"已停止代理池", true);
    }
    else
    {
        ShowNotice(err, false);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::ToggleProxyAuto()
{
    m_proxyAuto = !m_proxyAuto;
    SaveProxyAuto();
    ShowNotice(m_proxyAuto ? L"启动 OpenCode 时自动开启代理池"
                           : L"已关闭 OpenCode 自动代理",
               true);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

// 启动 OpenCode 前把代理池拉起来。只拉起进程,端口由脚本自己等,
// 这样不会把启动器卡住十几秒。
void MainWindow::MaybeStartProxyFor(LauncherItem& item)
{
    if (!m_proxyAuto || !IsOpenCodeEntry(item))
    {
        return;
    }
    if (m_proxyOnline || m_proxyStarting)
    {
        return;
    }

    std::wstring err;
    if (StartProxyPool(err))
    {
        m_proxyStarting = true;
        m_proxyStartUntil = GetTickCount64() + 25000;
    }
    else
    {
        ShowNotice(err + L"，本次直连", false);
    }
}

// ---------------------------------------------------------------------------
// OpenCode 固定出口 + 免费额度自动换出口
// ---------------------------------------------------------------------------

// 说明:
//   - OpenCode 启动后把出口固定到一个健康节点,整个会话出口 IP 不变,
//     这样额度是「一个 IP 的额度」,不会因为按请求轮换而漂移;
//   - 免费额度用尽时,OpenCode 会把错误写进自己的数据库(session_message),
//     这里读出来,让代理池把旧节点按 retryAfterMs 冷却,再换下一个出口;
//   - 换完出口后需要用户在 OpenCode 里再发一条消息(当前不自动重发)。
void MainWindow::AttachProxyForOpenCode(const LauncherItem& item)
{
    if (!m_proxyAuto && !m_proxyOnline)
    {
        return; // 自动代理关着、代理池也没在跑,bat 会直连,无需固定
    }

    // bat 把 HOME / XDG_DATA_HOME 指到自身目录,
    // 所以便携版的数据库在 <bat 目录>\.local\share\opencode\opencode.db
    m_openCodeDb = DirNameOf(item.resolved) +
                   L"\\.local\\share\\opencode\\opencode.db";
    m_quota.Watch(m_openCodeDb);
    m_quotaRotations = 0;

    m_pinPending = true;
    m_pinUntil = GetTickCount64() + 20000;
    m_quotaNextPoll = GetTickCount64() + 2500;

    TryPinProxy();
}

bool MainWindow::TryPinProxy()
{
    if (!m_pinPending)
    {
        return true;
    }

    std::wstring pinned;
    std::wstring err;
    if (!FixProxyPool(ProxyCtlPort(), pinned, err))
    {
        return false;
    }

    m_pinPending = false;
    m_proxyPinned = pinned;
    ShowNotice(L"已固定代理出口 " + pinned + L"，额度用尽时自动换", true);
    InvalidateRect(m_hwnd, NULL, FALSE);
    return true;
}

int MainWindow::ProxyCtlPort() const
{
    return ProxyControlPort(m_proxyPort);
}

bool MainWindow::AnyOpenCodeRunning() const
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (IsOpenCodeEntry(m_items[i]) && m_items[i].running)
        {
            return true;
        }
    }
    return false;
}

void MainWindow::PollProxyQuota()
{
    if (!m_quota.Active())
    {
        return;
    }

    ULONGLONG now = GetTickCount64();
    if (now < m_quotaNextPoll)
    {
        return;
    }
    m_quotaNextPoll = now + 2000;

    if (!AnyOpenCodeRunning())
    {
        // OpenCode 都退出了,不再关心它数据库里的历史记录
        m_quota.Stop();
        m_pinPending = false;
        return;
    }

    long long retryAfterMs = 0;
    if (!m_quota.Poll(retryAfterMs))
    {
        return;
    }

    // retryAfterMs 是官方给的额度恢复时间(约 12 小时),
    // 直接当旧节点的冷却时长,恢复前不再选它
    int ttlSec = (int)(retryAfterMs / 1000);
    if (ttlSec < 60)
    {
        ttlSec = 3600;
    }

    std::wstring pinned;
    std::wstring err;
    if (RotateProxyPool(ProxyCtlPort(), m_proxyPinned, ttlSec, pinned, err))
    {
        m_proxyPinned = pinned;
        ++m_quotaRotations;
        ShowNotice(L"OpenCode 免费额度用尽，已换出口 " + pinned +
                       L"，再发一条消息即可继续",
                   true);
    }
    else
    {
        ShowNotice(L"OpenCode 免费额度用尽，但没有可用的备用出口：" + err, false);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::PollRunningState()
{
    std::vector<char> before(m_items.size(), 0);
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        before[i] = m_items[i].running ? 1 : 0;
    }

    UpdateRunningItems(m_items);

    bool changed = false;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        char now = m_items[i].running ? 1 : 0;
        if (now != before[i])
        {
            changed = true;
            break;
        }
    }
    if (changed)
    {
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

bool MainWindow::IsHiddenId(const std::wstring& id) const
{
    for (size_t i = 0; i < m_hiddenIds.size(); ++i)
    {
        if (m_hiddenIds[i] == id)
        {
            return true;
        }
    }
    return false;
}

void MainWindow::LoadHiddenIds()
{
    m_hiddenIds.clear();

    std::wstring path = SettingsPath();
    wchar_t buf[4096];
    ZeroMemory(buf, sizeof(buf));
    GetPrivateProfileStringW(L"ui", L"hidden", L"", buf, 4096, path.c_str());

    std::wstring all(buf);
    size_t start = 0;
    while (start <= all.size())
    {
        size_t comma = all.find(L',', start);
        std::wstring part = (comma == std::wstring::npos)
                                ? all.substr(start)
                                : all.substr(start, comma - start);

        size_t a = 0;
        size_t b = part.size();
        while (a < b && iswspace(part[a])) ++a;
        while (b > a && iswspace(part[b - 1])) --b;
        part = part.substr(a, b - a);
        if (!part.empty())
        {
            m_hiddenIds.push_back(part);
        }

        if (comma == std::wstring::npos)
        {
            break;
        }
        start = comma + 1;
    }
}

void MainWindow::SaveHiddenIds()
{
    std::wstring joined;
    for (size_t i = 0; i < m_hiddenIds.size(); ++i)
    {
        if (i > 0)
        {
            joined += L",";
        }
        joined += m_hiddenIds[i];
    }

    std::wstring path = SettingsPath();
    if (joined.empty())
    {
        WritePrivateProfileStringW(L"ui", L"hidden", NULL, path.c_str());
    }
    else
    {
        WritePrivateProfileStringW(L"ui", L"hidden", joined.c_str(), path.c_str());
    }
}

void MainWindow::SetItemHidden(int index, bool hidden)
{
    if (index < 0 || index >= (int)m_items.size())
    {
        return;
    }

    LauncherItem& item = m_items[index];
    item.hidden = hidden;

    if (hidden)
    {
        if (!IsHiddenId(item.id))
        {
            m_hiddenIds.push_back(item.id);
        }
    }
    else
    {
        for (size_t i = 0; i < m_hiddenIds.size(); ++i)
        {
            if (m_hiddenIds[i] == item.id)
            {
                m_hiddenIds.erase(m_hiddenIds.begin() + i);
                break;
            }
        }
    }

    SaveHiddenIds();
    UpdateFilter();
    ShowNotice(hidden ? L"已隐藏：" + item.name : L"已取消隐藏：" + item.name, true);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::ShowContextMenu(int screenX, int screenY)
{
    POINT pt;
    pt.x = screenX;
    pt.y = screenY;
    ScreenToClient(m_hwnd, &pt);

    int itemIndex = -1;
    if (pt.x < m_sidebarW && pt.y >= m_titleH + m_searchH)
    {
        int row = ListItemAt(pt.y);
        if (row >= 0 && row < (int)m_rows.size() && !m_rows[row].isGroup)
        {
            itemIndex = m_rows[row].globalIndex;
        }
    }
    else if (pt.x >= m_sidebarW)
    {
        itemIndex = m_selectedGlobal;
    }

    if (itemIndex >= 0)
    {
        m_searchActive = false;
        SelectGlobal(itemIndex);
    }

    m_menuEntries.clear();
    m_menuWidth = S(196);

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }

    HBRUSH back = CreateSolidBrush(RGB(g_theme.panelBg.GetR(), g_theme.panelBg.GetG(),
                                       g_theme.panelBg.GetB()));
    MENUINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_STYLE | MIM_BACKGROUND;
    mi.dwStyle = MNS_NOCHECK;
    mi.hbrBack = back;
    SetMenuInfo(menu, &mi);

    if (itemIndex >= 0)
    {
        const LauncherItem& item = m_items[itemIndex];

        AppendMenuText(menu, item.running ? L"切换到窗口" : L"启动", 1001,
                       !item.error.empty(), false);
        AppendMenuText(menu, L"以管理员身份运行", 1002,
                       !item.error.empty() || item.isUrl, false);
        AppendMenuText(menu, L"再启动一个", 1003, !item.running, false);

        AppendMenuSeparator(menu);
        AppendMenuText(menu, L"打开所在目录", 1004,
                       item.isUrl || !item.exists, false);
        AppendMenuText(menu, L"复制路径", 1005, false, false);
        AppendMenuText(menu, L"复制名称", 1006, false, false);

        AppendMenuSeparator(menu);
        AppendMenuText(menu, item.hidden ? L"取消隐藏该条目" : L"隐藏该条目",
                       1007, false, item.hidden);
        AppendMenuText(menu, L"刷新列表", 1008, false, false);
        if (!m_hiddenIds.empty())
        {
            AppendMenuText(menu, L"取消隐藏全部", 1009, false, false);
        }
    }
    else
    {
        AppendMenuText(menu, L"刷新列表", 1008, false, false);
        AppendMenuText(menu, L"显示隐藏的条目", 1010, false, m_showHidden);
        AppendMenuText(menu, L"取消隐藏全部", 1009, m_hiddenIds.empty(), false);
        AppendMenuSeparator(menu);
        AppendMenuText(menu, L"切换主题", 1011, false, false);
    }

    SetForegroundWindow(m_hwnd);
    int cmd = (int)TrackPopupMenu(menu,
                                  TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                  screenX, screenY, 0, m_hwnd, NULL);
    DestroyMenu(menu);
    if (back)
    {
        DeleteObject(back);
    }
    // TrackPopupMenu 的已知怪癖:不补这条消息,菜单可能关不掉。
    PostMessageW(m_hwnd, WM_NULL, 0, 0);

    if (cmd != 0)
    {
        ExecuteMenuCommand(cmd, itemIndex);
    }
}

void MainWindow::AppendMenuText(HMENU menu, const wchar_t* text, int cmd,
                                bool disabled, bool checked)
{
    MenuEntry e;
    e.text = text;
    e.cmd = cmd;
    e.disabled = disabled;
    e.checked = checked;
    m_menuEntries.push_back(e);

    UINT flags = MF_OWNERDRAW;
    if (disabled)
    {
        flags |= MF_GRAYED;
    }
    AppendMenuW(menu, flags, (UINT_PTR)cmd, NULL);
}

void MainWindow::AppendMenuSeparator(HMENU menu)
{
    MenuEntry e;
    e.cmd = 0;
    e.separator = true;
    m_menuEntries.push_back(e);

    AppendMenuW(menu, MF_OWNERDRAW | MF_GRAYED, 0, NULL);
}

void MainWindow::DrawMenuEntry(DRAWITEMSTRUCT* dis)
{
    const MenuEntry* entry = NULL;
    for (size_t i = 0; i < m_menuEntries.size(); ++i)
    {
        if (m_menuEntries[i].cmd == (int)dis->itemID &&
            (m_menuEntries[i].separator || !m_menuEntries[i].text.empty()))
        {
            entry = &m_menuEntries[i];
            break;
        }
    }
    if (!entry)
    {
        return;
    }

    Graphics g(dis->hDC);
    RectF r((REAL)dis->rcItem.left, (REAL)dis->rcItem.top,
            (REAL)(dis->rcItem.right - dis->rcItem.left),
            (REAL)(dis->rcItem.bottom - dis->rcItem.top));

    if (entry->separator)
    {
        SolidBrush bg(g_theme.panelBg);
        g.FillRectangle(&bg, r);
        Pen line(g_theme.stroke, 1.0f);
        float y = r.Y + r.Height / 2.0f;
        g.DrawLine(&line, r.X + SF(10), y, r.X + r.Width - SF(10), y);
        return;
    }

    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    FillSq(g, r, selected ? g_theme.selectedBg : g_theme.panelBg);
    if (selected)
    {
        FillSq(g, RectF(r.X, r.Y, SF(2), r.Height), g_theme.accent);
    }
    if (entry->checked)
    {
        FillSq(g, RectF(r.X + SF(11), r.Y + r.Height / 2.0f - SF(3),
                        SF(6), SF(6)), g_theme.accent);
    }

    Color textColor = entry->disabled
                          ? g_theme.textMuted
                          : (selected ? g_theme.textPrimary : g_theme.textSecondary);
    Font f(g_uiFont.c_str(), SF(12), FontStyleRegular, UnitPixel);
    RectF tr(r.X + SF(28), r.Y, r.Width - SF(38), r.Height);
    DrawTextLine(g, entry->text, f, tr, textColor, StringAlignmentNear, true);
}

void MainWindow::ExecuteMenuCommand(int cmd, int itemIndex)
{
    bool hasItem = (itemIndex >= 0 && itemIndex < (int)m_items.size());

    if (!hasItem)
    {
        switch (cmd)
        {
        case 1008:
            ReloadItems();
            ShowNotice(L"已重新载入工具列表", true);
            break;
        case 1009:
            m_hiddenIds.clear();
            for (size_t i = 0; i < m_items.size(); ++i)
            {
                m_items[i].hidden = false;
            }
            m_showHidden = false;
            SaveHiddenIds();
            UpdateFilter();
            ShowNotice(L"已取消隐藏全部条目", true);
            break;
        case 1010:
            m_showHidden = !m_showHidden;
            UpdateFilter();
            ShowNotice(m_showHidden ? L"已显示隐藏的条目" : L"已收起隐藏的条目", true);
            break;
        case 1011:
            SwitchTheme();
            break;
        }
        InvalidateRect(m_hwnd, NULL, FALSE);
        return;
    }

    LauncherItem& item = m_items[itemIndex];
    switch (cmd)
    {
    case 1001:
        LaunchItem(itemIndex, false, false);
        break;
    case 1002:
        LaunchItem(itemIndex, true, true);
        break;
    case 1003:
        LaunchItem(itemIndex, false, true);
        break;
    case 1004:
        RevealSelected();
        break;
    case 1005:
        ShowNotice(SetClipboardText(item.resolved) ? L"路径已复制"
                                                   : L"复制失败（剪贴板被占用）",
                   true);
        break;
    case 1006:
        ShowNotice(SetClipboardText(item.name) ? L"名称已复制"
                                               : L"复制失败（剪贴板被占用）",
                   true);
        break;
    case 1007:
        SetItemHidden(itemIndex, !item.hidden);
        break;
    case 1008:
        ReloadItems();
        ShowNotice(L"已重新载入工具列表", true);
        break;
    case 1009:
        m_hiddenIds.clear();
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            m_items[i].hidden = false;
        }
        m_showHidden = false;
        SaveHiddenIds();
        UpdateFilter();
        ShowNotice(L"已取消隐藏全部条目", true);
        break;
    }

    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::SwitchTheme()
{
    int next = (CurrentTheme() + 1) % ThemeCount();
    ApplyTheme(next);
    SaveThemePreference(next);
    ShowNotice(std::wstring(L"已切换主题：") + ThemeName(next), true);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::RevealSelected()
{
    const LauncherItem* item = SelectedItem();
    if (!item)
    {
        return;
    }
    std::wstring err;
    if (RevealLauncher(*item, err))
    {
        ShowNotice(L"已在资源管理器中定位", true);
    }
    else
    {
        ShowNotice(err, false);
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// 命中测试
// ---------------------------------------------------------------------------
RectF MainWindow::LaunchButtonRect() const
{
    int x0 = m_sidebarW;
    float cardX = x0 + SF(28);
    float cardY = m_contentHeaderH + SF(24);
    return RectF(cardX + SF(32), cardY + SF(212), SF(160), SF(46));
}

RectF MainWindow::RevealButtonRect() const
{
    RectF b = LaunchButtonRect();
    return RectF(b.X + b.Width + SF(12), b.Y, SF(172), SF(46));
}

RectF MainWindow::ProxyPanelRect() const
{
    float h = SF(148);
    float cardX = m_sidebarW + SF(28);
    float cardW = (float)(m_backW - m_sidebarW) - SF(56);
    return RectF(cardX, (float)m_backH - h - SF(20), cardW, h);
}

RectF MainWindow::ProxyStartButtonRect() const
{
    RectF p = ProxyPanelRect();
    return RectF(p.X + SF(20), p.Y + SF(96), SF(132), SF(34));
}

RectF MainWindow::ProxyStopButtonRect() const
{
    RectF b = ProxyStartButtonRect();
    return RectF(b.X + b.Width + SF(8), b.Y, SF(84), SF(34));
}

RectF MainWindow::ProxyToggleRect() const
{
    RectF p = ProxyPanelRect();
    return RectF(p.X + p.Width - SF(20) - SF(44), p.Y + SF(104), SF(44), SF(22));
}

int MainWindow::HitTest(int x, int y)
{
    if (y < m_titleH)
    {
        if (x < m_sidebarW)
        {
            int closeW = S(48);
            int refreshW = S(42);
            int themeW = S(42);
            if (x >= m_sidebarW - closeW)
            {
                return 5;
            }
            if (x >= m_sidebarW - closeW - refreshW)
            {
                return 6;
            }
            if (x >= m_sidebarW - closeW - refreshW - themeW)
            {
                return 9;
            }
            return 1;
        }
        return 4;
    }
    if (x < m_sidebarW)
    {
        if (y < m_titleH + m_searchH)
        {
            return 2;
        }
        return 3;
    }

    const LauncherItem* item = SelectedItem();
    {
        float fx = (float)x;
        float fy = (float)y;
        if (ProxyPanelRect().Contains(fx, fy))
        {
            if (ProxyStartButtonRect().Contains(fx, fy)) return 10;
            if (ProxyStopButtonRect().Contains(fx, fy)) return 11;
            if (ProxyToggleRect().Contains(fx, fy)) return 12;
            return 4;
        }
    }
    if (item)
    {
        float fx = (float)x;
        float fy = (float)y;
        RectF lb = LaunchButtonRect();
        RectF rb = RevealButtonRect();
        if (lb.Contains(fx, fy)) return 7;
        if (rb.Contains(fx, fy)) return 8;
    }
    return 4;
}

int MainWindow::ListItemAt(int y) const
{
    int listTop = m_titleH + m_searchH;
    int rel = y - listTop + m_scroll;
    if (rel < 0) return -1;
    int cumulative = 0;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        cumulative += m_rows[i].height;
        if (rel < cumulative)
        {
            return (int)i;
        }
    }
    return -1;
}

void MainWindow::ScrollListBy(int dy)
{
    m_scroll += dy;
    if (m_scroll < 0) m_scroll = 0;
    if (m_scroll > m_scrollMax) m_scroll = m_scrollMax;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void MainWindow::TrackLeave()
{
    if (!m_tracking)
    {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        m_tracking = true;
    }
}

void MainWindow::SetCursorForRegion(int region)
{
    switch (region)
    {
    case 1:
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        break;
    case 2:
    case 3:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        break;
    default:
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        break;
    }
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
void MainWindow::OnPaint()
{
    if (m_backW <= 0 || m_backH <= 0)
    {
        return;
    }
    if (!m_backBitmap)
    {
        HDC dc = GetDC(m_hwnd);
        m_backBitmap = CreateCompatibleBitmap(dc, m_backW, m_backH);
        ReleaseDC(m_hwnd, dc);
    }

    {
        HDC hdcMem = CreateCompatibleDC(NULL);
        HGDIOBJ old = SelectObject(hdcMem, m_backBitmap);
        {
            Graphics g(hdcMem);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);

            SolidBrush bg(g_theme.windowBg);
            g.FillRectangle(&bg, (REAL)0, (REAL)0, (REAL)m_backW, (REAL)m_backH);

            PaintSidebar(g);
            PaintContent(g);
            PaintNotice(g);
        }
        SelectObject(hdcMem, old);
        DeleteDC(hdcMem);
    }

    HDC hdc = GetDC(m_hwnd);
    if (hdc)
    {
        HDC hdcMem = CreateCompatibleDC(NULL);
        HGDIOBJ old = SelectObject(hdcMem, m_backBitmap);
        BitBlt(hdc, 0, 0, m_backW, m_backH, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, old);
        DeleteDC(hdcMem);
        ReleaseDC(m_hwnd, hdc);
    }
}

void MainWindow::PaintTitleBar(Graphics& g)
{
    SolidBrush tb(g_theme.sidebarBg);
    g.FillRectangle(&tb, (REAL)0, (REAL)0, (REAL)m_sidebarW, (REAL)m_titleH);

    Pen line(g_theme.stroke, 1.0f);
    g.DrawLine(&line, (REAL)0, (REAL)m_titleH, (REAL)m_sidebarW, (REAL)m_titleH);

    SolidBrush logoDot(g_theme.accent);
    g.FillRectangle(&logoDot, SF(16), SF(17), SF(10), SF(10));
    // 右侧两道短线,做成技术感的标识
    g.FillRectangle(&logoDot, SF(28), SF(17), SF(3), SF(4));
    g.FillRectangle(&logoDot, SF(28), SF(23), SF(6), SF(4));

    Font f(g_uiFont.c_str(), SF(13), FontStyleBold, UnitPixel);
    RectF r(SF(46), 0, (REAL)(m_sidebarW - SF(130)), (REAL)m_titleH);
    DrawTextLine(g, L"工具启动器", f, r, g_theme.textPrimary,
                 StringAlignmentNear, true);

    int closeW = S(48);
    int refreshW = S(42);
    int themeW = S(42);
    int x0 = m_sidebarW - closeW;

    // 主题切换按钮(圆环:左空右实 = 明暗对比)
    {
        int tx = x0 - refreshW - themeW;
        if (m_hoverRegion == 9)
        {
            SolidBrush hb(g_theme.hoverBg);
            g.FillRectangle(&hb, (REAL)tx, (REAL)0, (REAL)themeW, (REAL)m_titleH);
        }
        float cx = tx + themeW / 2.0f;
        float cy = m_titleH / 2.0f;
        float rad = SF(6.5f);
        SolidBrush half(g_theme.textSecondary);
        g.FillPie(&half, cx - rad, cy - rad, rad * 2, rad * 2, -90.0f, 180.0f);
        Pen ring(g_theme.textSecondary, SF(1.4f));
        g.DrawEllipse(&ring, cx - rad, cy - rad, rad * 2, rad * 2);
    }

    // 刷新按钮
    if (m_hoverRegion == 6)
    {
        SolidBrush hb(g_theme.hoverBg);
        g.FillRectangle(&hb, (REAL)(x0 - refreshW), (REAL)0, (REAL)refreshW, (REAL)m_titleH);
    }
    {
        Pen p(g_theme.textSecondary, SF(1.6f));
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        float cx = x0 - refreshW / 2.0f;
        float cy = m_titleH / 2.0f;
        float rad = SF(6.5f);
        g.DrawArc(&p, cx - rad, cy - rad, rad * 2, rad * 2, -50.0f, 300.0f);
        PointF tip(cx + rad * 0.92f, cy - rad * 0.55f);
        PointF a(tip.X - SF(4), tip.Y - SF(1));
        PointF b(tip.X + SF(1), tip.Y + SF(4));
        g.DrawLine(&p, tip, a);
        g.DrawLine(&p, tip, b);
    }

    // 关闭按钮
    bool hoverClose = (m_hoverRegion == 5);
    if (hoverClose)
    {
        SolidBrush closeBg(g_theme.danger);
        g.FillRectangle(&closeBg, (REAL)x0, (REAL)0, (REAL)closeW, (REAL)m_titleH);
    }
    Pen pen(hoverClose ? Color(255, 255, 255) : g_theme.textPrimary, SF(1.6f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    float cx = x0 + closeW / 2.0f;
    float cy = m_titleH / 2.0f;
    float k = SF(5.0f);
    g.DrawLine(&pen, cx - k, cy - k, cx + k, cy + k);
    g.DrawLine(&pen, cx + k, cy - k, cx - k, cy + k);
}

void MainWindow::PaintSearchBox(Graphics& g)
{
    int top = m_titleH + S(10);
    int hgt = S(38);
    RectF box(SF(14), (REAL)top, (REAL)(m_sidebarW - S(28)), (REAL)hgt);

    Color border = m_searchActive ? g_theme.selectedBorder : g_theme.stroke;
    FillStrokeSq(g, box, g_theme.windowBg, border,
                 m_searchActive ? 1.5f : 1.0f);

    // 左侧强调条
    if (m_searchActive)
    {
        FillSq(g, RectF(box.X, box.Y, SF(2), box.Height), g_theme.accent);
    }

    // 放大镜
    {
        Pen p(m_searchActive ? g_theme.accentText : g_theme.textMuted, SF(1.5f));
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        float cx = box.X + SF(18);
        float cy = box.Y + box.Height / 2.0f;
        g.DrawEllipse(&p, cx - SF(5.5f), cy - SF(5.5f), SF(11), SF(11));
        g.DrawLine(&p, cx + SF(4.2f), cy + SF(4.2f), cx + SF(8.2f), cy + SF(8.2f));
    }

    float textX = box.X + SF(36);
    Font f(g_uiFont.c_str(), SF(12), FontStyleRegular, UnitPixel);

    if (m_search.empty())
    {
        RectF r(textX, box.Y, box.Width - SF(64), box.Height);
        DrawTextLine(g, L"搜索工具", f, r,
                     m_searchActive ? g_theme.textSecondary : g_theme.textMuted,
                     StringAlignmentNear, false);
        if (!m_searchActive)
        {
            Font sf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
            RectF sr(box.X, box.Y, box.Width - SF(14), box.Height);
            DrawTextLine(g, L"/", sf, sr, g_theme.textMuted,
                         StringAlignmentFar, false);
        }
    }
    else
    {
        float tw = MeasureText(g, m_search, f);
        if (tw > box.Width - SF(56)) tw = box.Width - SF(56);
        RectF r(textX, box.Y, box.Width - SF(48), box.Height);
        DrawTextLine(g, m_search, f, r, g_theme.textPrimary,
                     StringAlignmentNear, true);

        if (m_searchActive && (GetTickCount64() / 500) % 2 == 0)
        {
            float cx = textX + tw + SF(1);
            Pen caret(g_theme.accentText, SF(1.5f));
            g.DrawLine(&caret, cx, box.Y + SF(9), cx, box.Y + box.Height - SF(9));
        }
    }
}

void MainWindow::PaintSidebar(Graphics& g)
{
    SolidBrush sideBrush(g_theme.sidebarBg);
    g.FillRectangle(&sideBrush, (REAL)0, (REAL)0, (REAL)m_sidebarW, (REAL)m_backH);

    PaintTitleBar(g);
    PaintSearchBox(g);

    int listTop = m_titleH + m_searchH;
    int viewH = m_backH - listTop;
    if (viewH <= 0) viewH = 0;

    g.SetClip(RectF(0, (REAL)listTop, (REAL)m_sidebarW, (REAL)viewH));

    int rel = listTop - m_scroll;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        int y = rel;
        int h = m_rows[i].height;
        rel += h;
        if (y + h < listTop || y > m_backH)
        {
            continue;
        }

        if (m_rows[i].isGroup)
        {
            Font font(g_uiFont.c_str(), SF(10), FontStyleBold, UnitPixel);
            RectF r(SF(16), (REAL)y, (REAL)(m_sidebarW - SF(32)), (REAL)h);
            DrawTextLine(g, m_rows[i].label, font, r, g_theme.groupText,
                         StringAlignmentNear, true);
            float tw = MeasureText(g, m_rows[i].label, font);
            float lx = SF(16) + tw + SF(10);
            float ly = y + h / 2.0f;
            if (lx < m_sidebarW - SF(20))
            {
                Pen line(g_theme.stroke, 1.0f);
                g.DrawLine(&line, lx, ly, (REAL)(m_sidebarW - SF(16)), ly);
            }
        }
        else
        {
            const LauncherItem& t = m_items[m_rows[i].globalIndex];
            bool selected = (m_selectedGlobal == m_rows[i].globalIndex);
            bool hovered = (m_hoverRow == (int)i);

            RectF r(SF(8), (REAL)(y + S(2)), (REAL)(m_sidebarW - S(20)),
                    (REAL)(h - S(4)));
            if (selected)
            {
                FillSq(g, r, g_theme.selectedBg);
                FillSq(g, RectF(r.X, r.Y, SF(3), r.Height), g_theme.accent);
                FrameSq(g, r, g_theme.selectedBorder);
            }
            else if (hovered)
            {
                FillSq(g, r, g_theme.hoverBg);
                FillSq(g, RectF(r.X, r.Y, SF(2), r.Height), g_theme.strokeStrong);
            }

            // 图标块(真实 exe 图标)
            RectF iconR(r.X + SF(10), r.Y + (r.Height - SF(30)) / 2.0f, SF(30), SF(30));
            DrawItemIcon(g, t, iconR, selected, false);

            Font labelFont(g_uiFont.c_str(), SF(12), FontStyleRegular, UnitPixel);
            RectF nameRect(iconR.X + iconR.Width + SF(12), (REAL)(y + S(2)),
                           r.Width - SF(58), (REAL)(h - S(4)));
            Color nameColor = selected ? g_theme.textPrimary : g_theme.textSecondary;
            if (t.hidden)
            {
                nameColor = g_theme.textMuted;
            }
            DrawTextLine(g, t.name, labelFont, nameRect, nameColor,
                         StringAlignmentNear, true);

            if (t.running)
            {
                SolidBrush runDot(g_theme.success);
                g.FillEllipse(&runDot, r.X + r.Width - SF(32),
                              r.Y + r.Height / 2.0f - SF(3), SF(6), SF(6));
            }

            int number = m_rows[i].globalIndex + 1;
            if (number <= 9)
            {
                Font hotFont(g_uiFont.c_str(), SF(9), FontStyleRegular, UnitPixel);
                RectF hotRect(r.X, r.Y, r.Width - SF(10), r.Height);
                DrawTextLine(g, std::to_wstring(number), hotFont, hotRect,
                             selected ? g_theme.accentText : g_theme.textMuted,
                             StringAlignmentFar, false);
            }

            if (!t.error.empty())
            {
                SolidBrush dot(g_theme.danger);
                g.FillEllipse(&dot, r.X + r.Width - SF(18),
                              r.Y + SF(8), SF(5), SF(5));
            }
        }
    }

    g.ResetClip();

    if (m_scrollMax > 0)
    {
        int trackTop = listTop + S(4);
        int trackH = m_backH - trackTop - S(4);
        if (trackH <= 0)
        {
            return;
        }
        int total = 0;
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            total += m_rows[i].height;
        }
        int thumbH = trackH * viewH / (m_scrollMax + viewH);
        if (thumbH < S(30)) thumbH = S(30);
        int thumbY = trackTop + m_scroll * (trackH - thumbH) / (m_scrollMax + viewH);

        FillSq(g, RectF((REAL)(m_sidebarW - S(5)), (REAL)trackTop,
                        SF(3), (REAL)trackH), g_theme.stroke);
        FillSq(g, RectF((REAL)(m_sidebarW - S(6)), (REAL)thumbY,
                        SF(5), (REAL)thumbH), g_theme.strokeStrong);
    }
}

void MainWindow::PaintEmptyState(Graphics& g)
{
    int x0 = m_sidebarW;
    float cx = x0 + (m_backW - x0) / 2.0f;
    float cy = m_contentHeaderH + (m_backH - m_contentHeaderH) / 2.0f - SF(20);

    RectF tile(cx - SF(30), cy - SF(74), SF(60), SF(60));
    FillStrokeSq(g, tile, g_theme.panelBg, g_theme.stroke, 1.0f);
    FillSq(g, RectF(tile.X, tile.Y, SF(3), tile.Height), g_theme.accent);
    Font plus(g_uiFont.c_str(), SF(26), FontStyleRegular, UnitPixel);
    DrawTextLine(g, L"+", plus, tile, g_theme.textMuted, StringAlignmentCenter, false);

    std::wstring title;
    std::wstring sub;
    if (m_items.empty())
    {
        title = L"还没有配置工具";
        sub = L"在 tools.ini 中添加工具后按 F5 刷新";
    }
    else
    {
        title = L"没有匹配的工具";
        sub = L"换个关键词试试，或按 Esc 清空搜索";
    }

    Font tf(g_uiFont.c_str(), SF(17), FontStyleBold, UnitPixel);
    RectF tr(x0 + SF(20), cy, (REAL)(m_backW - x0 - S(40)), SF(30));
    DrawTextLine(g, title, tf, tr, g_theme.textSecondary,
                 StringAlignmentCenter, false);

    Font sf(g_uiFont.c_str(), SF(11), FontStyleRegular, UnitPixel);
    RectF sr(x0 + SF(20), cy + SF(34), (REAL)(m_backW - x0 - S(40)), SF(24));
    DrawTextLine(g, sub, sf, sr, g_theme.textMuted,
                 StringAlignmentCenter, false);
}

void MainWindow::PaintProxyPanel(Graphics& g)
{
    RectF p = ProxyPanelRect();

    FillSq(g, p, g_theme.panelBg2);
    FrameSq(g, p, g_theme.stroke);
    FillSq(g, RectF(p.X, p.Y, SF(3), p.Height), g_theme.accent);

    // 标题
    Font tf(g_uiFont.c_str(), SF(12), FontStyleBold, UnitPixel);
    RectF tr(p.X + SF(20), p.Y + SF(11), SF(180), SF(22));
    DrawTextLine(g, L"本地代理", tf, tr, g_theme.textPrimary,
                 StringAlignmentNear, false);

    // 右上角状态
    std::wstring status;
    Color dotColor;
    if (m_proxyOnline)
    {
        status = L"在线";
        dotColor = g_theme.success;
    }
    else if (m_proxyStarting)
    {
        status = L"启动中";
        dotColor = g_theme.accent;
    }
    else
    {
        status = L"未启动";
        dotColor = g_theme.textMuted;
    }

    Font sf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
    float sw = MeasureText(g, status, sf);
    float sx = p.X + p.Width - SF(20) - sw;
    SolidBrush dot(dotColor);
    g.FillEllipse(&dot, sx - SF(13), p.Y + SF(21), SF(7), SF(7));
    RectF sr(sx, p.Y + SF(15), sw + SF(2), SF(18));
    DrawTextLine(g, status, sf, sr, g_theme.textSecondary,
                 StringAlignmentNear, false);

    // 说明 / 地址
    Font df(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
    RectF dr(p.X + SF(20), p.Y + SF(38), p.Width - SF(140), SF(18));
    DrawTextLine(g, L"ProxyPool 免费代理池 · 启动 OpenCode 时自动接入",
                 df, dr, g_theme.textMuted, StringAlignmentNear, true);

    wchar_t addr[64];
    swprintf_s(addr, 64, L"地址 127.0.0.1:%d", m_proxyPort);
    RectF ar(p.X + SF(20), p.Y + SF(60), p.Width - SF(40), SF(18));
    DrawTextLine(g, addr, df, ar, g_theme.textSecondary,
                 StringAlignmentNear, false);

    // 右侧:当前出口模式
    std::wstring route;
    Color routeColor = g_theme.textMuted;
    if (m_proxyOnline && !m_proxyPinned.empty())
    {
        route = L"固定出口 " + m_proxyPinned + L" · 额度用尽自动换";
        if (m_quotaRotations > 0)
        {
            route += L"（已换 " + std::to_wstring(m_quotaRotations) + L" 次）";
        }
        routeColor = g_theme.textSecondary;
    }
    else if (m_proxyOnline && m_pinPending)
    {
        route = L"正在固定出口…";
    }
    else if (m_proxyOnline)
    {
        route = L"自动轮换 · 启动 OpenCode 时固定出口";
    }
    else
    {
        route = L"启动后固定出口，额度用尽自动换";
    }

    RectF rr(p.X + SF(180), p.Y + SF(60), p.Width - SF(200), SF(18));
    DrawTextLine(g, route, df, rr, routeColor, StringAlignmentFar, true);

    // 启动 / 打开窗口
    RectF sb = ProxyStartButtonRect();
    bool hoverStart = (m_hoverRegion == 10);
    bool primary = !m_proxyOnline;
    Color startFill = primary ? (hoverStart ? g_theme.accentHover : g_theme.accent)
                              : (hoverStart ? g_theme.hoverBg : g_theme.panelBg);
    FillSq(g, sb, startFill);
    FrameSq(g, sb, primary ? g_theme.accent : g_theme.strokeStrong);

    const wchar_t* startLabel = m_proxyOnline ? L"打开代理池窗口"
                              : (m_proxyStarting ? L"正在启动…" : L"启动代理池");
    Font bf(g_uiFont.c_str(), SF(11), FontStyleBold, UnitPixel);
    DrawTextLine(g, startLabel, bf, sb,
                 primary ? g_theme.onAccent : g_theme.textPrimary,
                 StringAlignmentCenter, false);

    // 停止
    RectF tb = ProxyStopButtonRect();
    bool hoverStop = (m_hoverRegion == 11);
    bool stopEnabled = m_proxyOnline || m_proxyStarting;
    FillSq(g, tb, (hoverStop && stopEnabled) ? g_theme.hoverBg : Color(0, 0, 0, 0));
    FrameSq(g, tb, stopEnabled ? g_theme.strokeStrong : g_theme.stroke);

    Font stf(g_uiFont.c_str(), SF(11), FontStyleRegular, UnitPixel);
    Color stopText = stopEnabled
                         ? (hoverStop ? g_theme.danger : g_theme.textSecondary)
                         : g_theme.textMuted;
    DrawTextLine(g, L"停止", stf, tb, stopText, StringAlignmentCenter, false);

    // 自动代理开关
    RectF tg = ProxyToggleRect();
    bool hoverToggle = (m_hoverRegion == 12);
    FillSq(g, tg, m_proxyAuto ? g_theme.accent : g_theme.panelBg);
    FrameSq(g, tg, hoverToggle ? g_theme.accentHover
                               : (m_proxyAuto ? g_theme.accent : g_theme.strokeStrong));

    float knob = tg.Height - SF(6);
    float knobX = m_proxyAuto ? tg.X + tg.Width - SF(3) - knob : tg.X + SF(3);
    FillSq(g, RectF(knobX, tg.Y + SF(3), knob, knob),
           m_proxyAuto ? g_theme.onAccent : g_theme.textMuted);

    Font lf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
    RectF lr(tg.X - SF(10) - SF(90), tg.Y - SF(4), SF(90), SF(30));
    DrawTextLine(g, L"自动代理", lf, lr,
                 m_proxyAuto ? g_theme.textSecondary : g_theme.textMuted,
                 StringAlignmentFar, false);
}

void MainWindow::PaintContent(Graphics& g)
{
    int x0 = m_sidebarW;

    // 背景与分隔线
    {
        Pen sep(g_theme.stroke, 1.0f);
        g.DrawLine(&sep, (REAL)x0, (REAL)0, (REAL)x0, (REAL)m_backH);

        SolidBrush h(g_theme.contentBg);
        g.FillRectangle(&h, (REAL)(x0 + 1), (REAL)0,
                        (REAL)(m_backW - x0 - 1), (REAL)m_contentHeaderH);
        Pen line(g_theme.stroke, 1.0f);
        g.DrawLine(&line, (REAL)x0, (REAL)m_contentHeaderH,
                   (REAL)m_backW, (REAL)m_contentHeaderH);
    }

    const LauncherItem* item = SelectedItem();
    if (!item)
    {
        PaintEmptyState(g);
        PaintProxyPanel(g);
        return;
    }

    // 顶部标题
    {
        FillSq(g, RectF((REAL)(x0 + S(28)), S(13), SF(3), S(36)), g_theme.accent);

        Font cFont(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
        RectF cr((REAL)(x0 + S(40)), S(8), (REAL)(m_backW - x0 - S(68)), S(16));
        DrawTextLine(g, item->category, cFont, cr, g_theme.textMuted,
                     StringAlignmentNear, true);

        Font tFont(g_uiFont.c_str(), SF(17), FontStyleBold, UnitPixel);
        RectF tr((REAL)(x0 + S(40)), S(25), (REAL)(m_backW - x0 - S(68)), S(32));
        DrawTextLine(g, item->name, tFont, tr, g_theme.textPrimary,
                     StringAlignmentNear, true);

        Font idxFont(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
        wchar_t idxBuf[64];
        swprintf_s(idxBuf, 64, L"%d / %d", m_selectedGlobal + 1, (int)m_items.size());
        RectF ir((REAL)(x0 + S(28)), S(8), (REAL)(m_backW - x0 - S(56)), S(16));
        DrawTextLine(g, idxBuf, idxFont, ir, g_theme.textMuted,
                     StringAlignmentFar, false);
    }

    // 详情卡片
    float cardX = x0 + SF(28);
    float cardY = m_contentHeaderH + SF(24);
    float cardW = (float)(m_backW - x0) - SF(56);
    float cardH = SF(288);

    FillSq(g, RectF(cardX, cardY, cardW, cardH), g_theme.panelBg2);
    FrameSq(g, RectF(cardX, cardY, cardW, cardH), g_theme.stroke);
    FillSq(g, RectF(cardX, cardY, SF(3), cardH), g_theme.accent);

    // 大图标(优先真实 exe 图标)
    RectF bigIcon(cardX + SF(32), cardY + SF(32), SF(84), SF(84));
    DrawItemIcon(g, *item, bigIcon, true, true);

    // 名称 / 分类
    float textX = bigIcon.X + bigIcon.Width + SF(24);
    {
        Font nf(g_uiFont.c_str(), SF(22), FontStyleBold, UnitPixel);
        RectF nr(textX, cardY + SF(34), cardW - SF(210), SF(36));
        DrawTextLine(g, item->name, nf, nr, g_theme.textPrimary,
                     StringAlignmentNear, true);

        Font cf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
        float cw = MeasureText(g, item->category, cf) + SF(20);
        RectF chip(textX, cardY + SF(78), cw, SF(24));
        FillSq(g, chip, g_theme.accentSoft);
        FrameSq(g, chip, g_theme.stroke);
        FillSq(g, RectF(chip.X, chip.Y, SF(2), chip.Height), g_theme.accent);
        DrawTextLine(g, item->category, cf, chip, g_theme.accentText,
                     StringAlignmentCenter, false);

        // 状态
        {
            std::wstring status;
            Color dotColor;
            if (!item->error.empty())
            {
                status = item->error;
                dotColor = g_theme.danger;
            }
            else if (item->running)
            {
                status = L"运行中";
                dotColor = g_theme.success;
            }
            else if (item->isUrl)
            {
                status = L"网页链接";
                dotColor = g_theme.accent;
            }
            else
            {
                status = L"已就绪";
                dotColor = g_theme.success;
            }
            Font sf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
            float tw = MeasureText(g, status, sf);
            float sx = cardX + cardW - SF(28) - tw;
            float sy = cardY + SF(40);
            SolidBrush dot(dotColor);
            g.FillEllipse(&dot, sx - SF(14), sy + SF(4), SF(7), SF(7));
            RectF sr(sx, sy - SF(2), tw + SF(2), SF(20));
            DrawTextLine(g, status, sf, sr,
                         item->error.empty() ? g_theme.textSecondary : g_theme.danger,
                         StringAlignmentNear, false);
        }

        // 使用统计
        {
            ToolStat st = m_stats.Get(item->id);
            std::wstring last = FormatLastUsed(st.last);
            std::wstring statText = L"尚未使用过";
            if (st.count > 0 && !last.empty())
            {
                wchar_t buf[64];
                swprintf_s(buf, 64, L"启动 %d 次", st.count);
                statText = std::wstring(buf) + L" · 上次 " + last;
            }

            Font stf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
            RectF sr(textX, cardY + SF(110), cardW - SF(210), SF(20));
            DrawTextLine(g, statText, stf, sr, g_theme.textMuted,
                         StringAlignmentNear, false);
        }
    }

    // 描述
    {
        Font df(g_uiFont.c_str(), SF(12), FontStyleRegular, UnitPixel);
        RectF dr(cardX + SF(32), cardY + SF(136), cardW - SF(64), SF(24));
        DrawTextLine(g, item->desc.empty() ? L"（暂无描述）" : item->desc,
                     df, dr, g_theme.textSecondary, StringAlignmentNear, true);
    }

    // 路径框
    {
        RectF pathBox(cardX + SF(32), cardY + SF(168), cardW - SF(64), SF(34));
        FillSq(g, pathBox, g_theme.panelBg);
        FrameSq(g, pathBox, g_theme.stroke);
        FillSq(g, RectF(pathBox.X, pathBox.Y, SF(2), pathBox.Height),
               g_theme.strokeStrong);
        Font pf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
        RectF pr(pathBox.X + SF(14), pathBox.Y, pathBox.Width - SF(28), pathBox.Height);
        DrawTextLine(g, item->resolved, pf, pr, g_theme.textMuted,
                     StringAlignmentNear, true);
    }

    // 按钮
    {
        RectF lb = LaunchButtonRect();
        bool hover = (m_hoverRegion == 7);
        bool disabled = !item->error.empty();
        Color fill = disabled ? g_theme.panelBg
                              : (hover ? g_theme.accentHover : g_theme.accent);
        FillSq(g, lb, fill);
        FrameSq(g, lb, disabled ? g_theme.stroke : g_theme.accent);

        // 左侧小三角
        if (!disabled)
        {
            GraphicsPath tri;
            PointF p1(lb.X + SF(22), lb.Y + lb.Height / 2.0f - SF(6));
            PointF p2(lb.X + SF(22), lb.Y + lb.Height / 2.0f + SF(6));
            PointF p3(lb.X + SF(32), lb.Y + lb.Height / 2.0f);
            PointF pts[3] = { p1, p2, p3 };
            tri.AddPolygon(pts, 3);
            SolidBrush tb(g_theme.onAccent);
            g.FillPath(&tb, &tri);
        }

        Font bf(g_uiFont.c_str(), SF(13), FontStyleBold, UnitPixel);
        RectF bt(lb.X + SF(24), lb.Y, lb.Width - SF(24), lb.Height);
        DrawTextLine(g, item->running ? L"切换到窗口" : L"启动工具", bf, bt,
                     disabled ? g_theme.textMuted : g_theme.onAccent,
                     StringAlignmentCenter, false);

        RectF rb = RevealButtonRect();
        bool rhover = (m_hoverRegion == 8);
        FillSq(g, rb, rhover ? g_theme.hoverBg : Color(0, 0, 0, 0));
        FrameSq(g, rb, rhover ? g_theme.strokeStrong : g_theme.stroke);
        Font rf(g_uiFont.c_str(), SF(12), FontStyleRegular, UnitPixel);
        DrawTextLine(g, L"打开所在目录", rf, rb,
                     rhover ? g_theme.textPrimary : g_theme.textSecondary,
                     StringAlignmentCenter, false);
    }

    // 快捷键提示
    {
        Font kf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
        RectF kr(cardX + SF(32), cardY + cardH - SF(30),
                 cardW - SF(64), SF(20));
        DrawTextLine(g, L"Enter 启动  ·  ↑↓ 切换  ·  / 搜索  ·  T 主题  ·  F5 刷新  ·  右键 菜单",
                     kf, kr, g_theme.textMuted, StringAlignmentNear, false);
    }

    // 使用提示(空间不够时让位给代理面板)
    float hintY = cardY + cardH + SF(18);
    if (hintY + SF(118) < ProxyPanelRect().Y - SF(14))
    {
        float hintW = cardW;
        FillSq(g, RectF(cardX, hintY, hintW, SF(118)), g_theme.hintBg);
        FrameSq(g, RectF(cardX, hintY, hintW, SF(118)), g_theme.stroke);
        FillSq(g, RectF(cardX, hintY, SF(2), SF(118)), g_theme.strokeStrong);

        Font hf(g_uiFont.c_str(), SF(11), FontStyleBold, UnitPixel);
        RectF hr(cardX + SF(20), hintY + SF(10), hintW - SF(40), SF(20));
        DrawTextLine(g, L"添加更多工具", hf, hr, g_theme.textSecondary,
                     StringAlignmentNear, false);

        Font sf(g_uiFont.c_str(), SF(10), FontStyleRegular, UnitPixel);
        const wchar_t* lines[3] = {
            L"1. 把工具程序放到 dist\\tools\\<名称>\\ 目录下",
            L"2. 在 tools.ini 中新增一节，填写 name / category / iconfile / icon / desc / path",
            L"3. 按 F5 或点击标题栏刷新按钮重新载入"
        };
        for (int i = 0; i < 3; ++i)
        {
            RectF lr(cardX + SF(20), hintY + SF(34) + i * SF(22),
                     hintW - SF(40), SF(20));
            DrawTextLine(g, lines[i], sf, lr, g_theme.textMuted,
                         StringAlignmentNear, true);
        }
    }

    PaintProxyPanel(g);
}

void MainWindow::PaintNotice(Graphics& g)
{
    if (g_lastNoticeUntil == 0)
    {
        return;
    }
    if (GetTickCount64() >= g_lastNoticeUntil)
    {
        g_lastNoticeUntil = 0;
        return;
    }

    int x0 = m_sidebarW;
    Font f(g_uiFont.c_str(), SF(12), FontStyleRegular, UnitPixel);
    float textW = MeasureText(g, g_lastNotice, f);
    float pillW = textW + SF(34);
    float maxW = (float)(m_backW - x0) - SF(80);
    if (pillW > maxW) pillW = maxW;
    if (pillW < SF(120)) pillW = SF(120);
    float pillH = SF(34);
    float px = x0 + ((m_backW - x0) - pillW) / 2.0f;
    float py = m_backH - pillH - SF(22);

    Color fill = g_noticeSuccess ? g_theme.noticeOkBg : g_theme.noticeErrBg;
    Color border = g_noticeSuccess ? g_theme.noticeOkBorder : g_theme.noticeErrBorder;
    RectF pillRect(px, py, pillW, pillH);
    FillSq(g, pillRect, fill);
    FrameSq(g, pillRect, border);
    FillSq(g, RectF(px, py, SF(3), pillH), border);

    SolidBrush dot(border);
    g.FillEllipse(&dot, px + SF(14), py + pillH / 2.0f - SF(3), SF(6), SF(6));

    Font tf(g_uiFont.c_str(), SF(12), FontStyleRegular, UnitPixel);
    RectF tr(px + SF(24), py, pillW - SF(34), pillH);
    DrawTextLine(g, g_lastNotice, tf, tr,
                 g_noticeSuccess ? g_theme.noticeOkText : g_theme.noticeErrText,
                 StringAlignmentNear, true);
}
