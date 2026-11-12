#pragma once
#include "common.h"
#include "launcher.h"
#include "stats.h"
#include "quota.h"

// 主窗口:左侧搜索 + 分组工具列表,右侧工具详情 / 启动区。
// 整个窗口只有一个 HWND,全部 GDI+ 自绘。
// 工具来自 <exe目录>\tools.ini,程序文件放在 <exe目录>\tools\ 下。

class MainWindow
{
public:
    static void Create(HINSTANCE instance);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    MainWindow();
    ~MainWindow();

    LRESULT Handle(UINT msg, WPARAM wParam, LPARAM lParam);

    void InitLayout();
    void OnPaint();

    void PaintSidebar(Gdiplus::Graphics& g);
    void PaintTitleBar(Gdiplus::Graphics& g);
    void PaintSearchBox(Gdiplus::Graphics& g);
    void PaintContent(Gdiplus::Graphics& g);
    void PaintProxyPanel(Gdiplus::Graphics& g);
    void PaintNotice(Gdiplus::Graphics& g);
    void PaintEmptyState(Gdiplus::Graphics& g);

    int HitTest(int x, int y);
    int ListItemAt(int y) const;
    Gdiplus::RectF LaunchButtonRect() const;
    Gdiplus::RectF RevealButtonRect() const;
    Gdiplus::RectF ProxyPanelRect() const;
    Gdiplus::RectF ProxyStartButtonRect() const;
    Gdiplus::RectF ProxyStopButtonRect() const;
    Gdiplus::RectF ProxyToggleRect() const;
    void ScrollListBy(int dy);

    void ReloadItems();
    void UpdateFilter();
    void SelectGlobal(int index);
    void MoveSelection(int delta);
    void LaunchSelected();
    void LaunchItem(int index, bool elevated, bool forceNew);
    void RevealSelected();
    void SwitchTheme();
    void FocusSearch();

    // 右键菜单 / 隐藏 / 运行状态
    void ShowContextMenu(int screenX, int screenY);
    void ExecuteMenuCommand(int cmd, int itemIndex);
    void AppendMenuText(HMENU menu, const wchar_t* text, int cmd,
                        bool disabled, bool checked);
    void AppendMenuSeparator(HMENU menu);
    void DrawMenuEntry(DRAWITEMSTRUCT* dis);
    void PollRunningState();
    void PollProxyState();
    void StartProxyFromPanel();
    void StopProxyFromPanel();
    void ToggleProxyAuto();
    void LoadProxySettings();
    void SaveProxyAuto();
    void MaybeStartProxyFor(LauncherItem& item);

    // 固定出口 + 免费额度监测(OpenCode 专用)
    void AttachProxyForOpenCode(const LauncherItem& item);
    bool TryPinProxy();
    void PollProxyQuota();
    bool AnyOpenCodeRunning() const;
    int ProxyCtlPort() const;

    void LoadHiddenIds();
    void SaveHiddenIds();
    void SetItemHidden(int index, bool hidden);
    bool IsHiddenId(const std::wstring& id) const;

    const LauncherItem* SelectedItem() const;

    void TrackLeave();
    void SetCursorForRegion(int region);

    static void ShowNotice(const std::wstring& message, bool success);

    HWND m_hwnd;
    UINT_PTR m_gdiplusToken;
    HBITMAP m_backBitmap;
    int m_backW, m_backH;

    // 布局
    int m_titleH;
    int m_sidebarW;
    int m_searchH;
    int m_listItemH;
    int m_groupH;
    int m_contentHeaderH;

    // 数据
    std::vector<LauncherItem> m_items;
    StatsStore m_stats;
    std::vector<std::wstring> m_hiddenIds;
    bool m_showHidden;

    // 自绘右键菜单
    struct MenuEntry {
        std::wstring text;
        int cmd;
        bool disabled;
        bool checked;
        bool separator;

        MenuEntry() : cmd(0), disabled(false), checked(false), separator(false) {}
    };
    std::vector<MenuEntry> m_menuEntries;
    int m_menuWidth;

    // 列表(已按搜索过滤)
    struct Row {
        int globalIndex;   // 工具全局索引;分组行 = -1
        bool isGroup;
        std::wstring label;
        int height;
    };
    std::vector<Row> m_rows;
    int m_scroll;
    int m_scrollMax;
    int m_selectedGlobal;
    int m_hoverRow;
    int m_hoverRegion;
    bool m_tracking;

    // 搜索
    std::wstring m_search;
    bool m_searchActive;

    // 计时器(用于通知淡出 / 搜索光标闪烁 / 运行状态轮询)
    UINT m_tickTimerId;
    UINT m_statusTimerId;

    // 本地代理池面板
    bool m_proxyOnline;      // 127.0.0.1:8080 正在监听
    bool m_proxyStarting;    // 面板点了启动,还在等端口
    ULONGLONG m_proxyStartUntil;
    bool m_proxyAuto;        // 启动 OpenCode 时自动拉起代理池
    int m_proxyPort;

    // OpenCode 固定出口 + 免费额度监测
    bool m_pinPending;           // 已发起固定请求,等控制接口就绪
    ULONGLONG m_pinUntil;
    std::wstring m_proxyPinned;  // 当前固定节点(空 = 自动轮换)
    QuotaWatcher m_quota;
    ULONGLONG m_quotaNextPoll;
    int m_quotaRotations;        // 本次会话因额度用尽换了几次出口
    std::wstring m_openCodeDb;   // 本次 OpenCode 会话的数据库路径
};
