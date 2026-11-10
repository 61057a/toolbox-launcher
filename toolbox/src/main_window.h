#pragma once
#include "common.h"
#include "launcher.h"

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
    void PaintNotice(Gdiplus::Graphics& g);
    void PaintEmptyState(Gdiplus::Graphics& g);

    int HitTest(int x, int y);
    int ListItemAt(int y) const;
    Gdiplus::RectF LaunchButtonRect() const;
    Gdiplus::RectF RevealButtonRect() const;
    void ScrollListBy(int dy);

    void ReloadItems();
    void UpdateFilter();
    void SelectGlobal(int index);
    void MoveSelection(int delta);
    void LaunchSelected();
    void RevealSelected();
    void SwitchTheme();
    void FocusSearch();

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

    // 计时器(用于通知淡出 / 搜索光标闪烁)
    UINT m_tickTimerId;
};
