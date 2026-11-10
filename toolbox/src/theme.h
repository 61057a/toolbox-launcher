#pragma once
#include "common.h"

// 主题配色。所有界面颜色都从这里取,切换时只需改 g_theme。
struct ThemeColors
{
    Gdiplus::Color windowBg;
    Gdiplus::Color sidebarBg;
    Gdiplus::Color contentBg;
    Gdiplus::Color panelBg;
    Gdiplus::Color panelBg2;
    Gdiplus::Color stroke;
    Gdiplus::Color strokeStrong;

    Gdiplus::Color textPrimary;
    Gdiplus::Color textSecondary;
    Gdiplus::Color textMuted;

    Gdiplus::Color accent;
    Gdiplus::Color accentHover;
    Gdiplus::Color accentSoft;
    Gdiplus::Color accentText;
    Gdiplus::Color onAccent;

    Gdiplus::Color success;
    Gdiplus::Color danger;

    Gdiplus::Color hoverBg;
    Gdiplus::Color selectedBg;
    Gdiplus::Color selectedBorder;
    Gdiplus::Color groupText;

    Gdiplus::Color hintBg;

    Gdiplus::Color noticeOkBg;
    Gdiplus::Color noticeOkBorder;
    Gdiplus::Color noticeOkText;
    Gdiplus::Color noticeErrBg;
    Gdiplus::Color noticeErrBorder;
    Gdiplus::Color noticeErrText;
};

extern ThemeColors g_theme;

int ThemeCount();
const wchar_t* ThemeName(int index);
const wchar_t* ThemeId(int index);
bool ThemeIsDark(int index);
int ThemeIndexFromId(const std::wstring& id);

// 应用第 index 套主题(越界时按 0 处理)。
void ApplyTheme(int index);

// 当前主题。
int CurrentTheme();
