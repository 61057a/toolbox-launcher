#include "theme.h"

// 两套配色:
//   石墨 - 暖炭灰底 + 低饱和铜色,接近纸面印刷的深色方案
//   宣纸 - 米白纸面 + 深松绿,亮色但不刺眼
// 刻意避开高饱和纯蓝 / 紫色渐变这类"AI 味"配色。

static const ThemeColors kThemes[] = {
    // 石墨 (dark)
    {
        Gdiplus::Color(25, 24, 23),    // windowBg
        Gdiplus::Color(32, 31, 29),    // sidebarBg
        Gdiplus::Color(23, 22, 21),    // contentBg
        Gdiplus::Color(38, 36, 33),    // panelBg
        Gdiplus::Color(33, 31, 29),    // panelBg2
        Gdiplus::Color(55, 52, 47),    // stroke
        Gdiplus::Color(85, 80, 74),    // strokeStrong

        Gdiplus::Color(234, 228, 218), // textPrimary
        Gdiplus::Color(171, 163, 150), // textSecondary
        Gdiplus::Color(124, 117, 106), // textMuted

        Gdiplus::Color(192, 138, 94),  // accent
        Gdiplus::Color(206, 154, 108), // accentHover
        Gdiplus::Color(59, 44, 33),    // accentSoft
        Gdiplus::Color(222, 180, 140), // accentText
        Gdiplus::Color(28, 23, 18),    // onAccent

        Gdiplus::Color(127, 165, 127), // success
        Gdiplus::Color(198, 107, 98),  // danger

        Gdiplus::Color(42, 40, 37),    // hoverBg
        Gdiplus::Color(58, 44, 33),    // selectedBg
        Gdiplus::Color(192, 138, 94),  // selectedBorder
        Gdiplus::Color(138, 130, 117), // groupText

        Gdiplus::Color(29, 28, 26),    // hintBg

        Gdiplus::Color(35, 52, 42),    // noticeOkBg
        Gdiplus::Color(110, 152, 120), // noticeOkBorder
        Gdiplus::Color(207, 227, 210), // noticeOkText
        Gdiplus::Color(56, 37, 34),    // noticeErrBg
        Gdiplus::Color(168, 99, 92),   // noticeErrBorder
        Gdiplus::Color(239, 202, 197)  // noticeErrText
    },
    // 宣纸 (light)
    {
        Gdiplus::Color(245, 242, 234), // windowBg
        Gdiplus::Color(237, 232, 221), // sidebarBg
        Gdiplus::Color(250, 248, 242), // contentBg
        Gdiplus::Color(255, 255, 255), // panelBg
        Gdiplus::Color(252, 250, 245), // panelBg2
        Gdiplus::Color(220, 213, 199), // stroke
        Gdiplus::Color(191, 183, 166), // strokeStrong

        Gdiplus::Color(42, 39, 34),    // textPrimary
        Gdiplus::Color(96, 90, 81),    // textSecondary
        Gdiplus::Color(142, 135, 121), // textMuted

        Gdiplus::Color(63, 95, 79),    // accent
        Gdiplus::Color(51, 80, 67),    // accentHover
        Gdiplus::Color(225, 233, 227), // accentSoft
        Gdiplus::Color(53, 81, 63),    // accentText
        Gdiplus::Color(255, 255, 255), // onAccent

        Gdiplus::Color(78, 139, 98),   // success
        Gdiplus::Color(178, 89, 80),   // danger

        Gdiplus::Color(231, 226, 214), // hoverBg
        Gdiplus::Color(223, 232, 225), // selectedBg
        Gdiplus::Color(63, 95, 79),    // selectedBorder
        Gdiplus::Color(148, 141, 126), // groupText

        Gdiplus::Color(247, 244, 236), // hintBg

        Gdiplus::Color(228, 239, 231), // noticeOkBg
        Gdiplus::Color(127, 169, 140), // noticeOkBorder
        Gdiplus::Color(47, 90, 61),    // noticeOkText
        Gdiplus::Color(247, 231, 228), // noticeErrBg
        Gdiplus::Color(192, 137, 128), // noticeErrBorder
        Gdiplus::Color(142, 63, 55)    // noticeErrText
    }
};

static const wchar_t* kThemeIds[] = { L"graphite", L"paper" };
static const wchar_t* kThemeNames[] = { L"石墨", L"宣纸" };

static int g_themeIndex = 0;

ThemeColors g_theme = kThemes[0];

int ThemeCount()
{
    return (int)(sizeof(kThemes) / sizeof(kThemes[0]));
}

const wchar_t* ThemeName(int index)
{
    if (index < 0 || index >= ThemeCount()) index = 0;
    return kThemeNames[index];
}

const wchar_t* ThemeId(int index)
{
    if (index < 0 || index >= ThemeCount()) index = 0;
    return kThemeIds[index];
}

bool ThemeIsDark(int index)
{
    return index == 0;
}

int ThemeIndexFromId(const std::wstring& id)
{
    for (int i = 0; i < ThemeCount(); ++i)
    {
        if (id == kThemeIds[i])
        {
            return i;
        }
    }
    return 0;
}

void ApplyTheme(int index)
{
    if (index < 0 || index >= ThemeCount())
    {
        index = 0;
    }
    g_themeIndex = index;
    g_theme = kThemes[index];
}

int CurrentTheme()
{
    return g_themeIndex;
}
