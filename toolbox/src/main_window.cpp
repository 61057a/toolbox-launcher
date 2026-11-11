#include "main_window.h"
#include "resource.h"
#include "theme.h"
#include "ui.h"

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

// ---------------------------------------------------------------------------
// 全局通知
// ---------------------------------------------------------------------------
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
      m_scroll(0), m_scrollMax(0), m_selectedGlobal(-1),
      m_hoverRow(-1), m_hoverRegion(0), m_tracking(false),
      m_searchActive(false), m_tickTimerId(0)
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
    self->ReloadItems();
    self->m_tickTimerId = SetTimer(hwnd, 1, 60, NULL);

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
        mmi->ptMinTrackSize.y = S(520);
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
                if (idx < (int)m_items.size())
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

    std::vector<std::wstring> categories;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const LauncherItem& t = m_items[i];
        bool match = true;
        if (searching)
        {
            std::wstring hay = t.name + L" " + t.category + L" " + t.desc + L" " + t.path;
            for (size_t k = 0; k < hay.size(); ++k)
            {
                hay[k] = (wchar_t)towlower(hay[k]);
            }
            match = (hay.find(q) != std::wstring::npos);
        }
        if (!match)
        {
            continue;
        }
        bool found = false;
        for (size_t c = 0; c < categories.size(); ++c)
        {
            if (categories[c] == t.category)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            categories.push_back(t.category);
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

        for (size_t i = 0; i < m_items.size(); ++i)
        {
            const LauncherItem& t = m_items[i];
            if (t.category != categories[c])
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
            Row r;
            r.isGroup = false;
            r.globalIndex = (int)i;
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
    bool visible = false;
    int firstVisible = -1;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].isGroup)
        {
            if (firstVisible < 0) firstVisible = m_rows[i].globalIndex;
            if (m_rows[i].globalIndex == prevSelected)
            {
                visible = true;
                break;
            }
        }
    }
    if (!visible)
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
            break;
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
             (m_rows[idx].isGroup || m_rows[idx].globalIndex < 0));

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
    const LauncherItem* item = SelectedItem();
    if (!item)
    {
        return;
    }
    std::wstring err;
    if (LaunchLauncher(*item, err))
    {
        ShowNotice(L"已启动：" + item->name, true);
    }
    else
    {
        ShowNotice(err, false);
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
            DrawTextLine(g, t.name, labelFont, nameRect,
                         selected ? g_theme.textPrimary : g_theme.textSecondary,
                         StringAlignmentNear, true);

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
        DrawTextLine(g, L"启动工具", bf, bt,
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
        DrawTextLine(g, L"Enter 启动  ·  ↑↓ 切换  ·  / 搜索  ·  T 主题  ·  F5 刷新",
                     kf, kr, g_theme.textMuted, StringAlignmentNear, false);
    }

    // 使用提示
    float hintY = cardY + cardH + SF(18);
    if (hintY + SF(120) < m_backH)
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
