#pragma once

#define _WIN32_WINNT 0x0A00
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <string>
#include <algorithm>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

// 图吧工具箱风格的自绘工具箱框架
// 主界面:左侧为工具列表(搜索 + 分组 + 快捷键数字直连),右侧为工具内容区。
// 每个工具通过统一的 Tool 接口注册,新工具只需实现 draw() 并在注册表里登记。
#define TOOLBOX_VERSION L"1.0.0"
#define APP_CLASS L"ToolkitFrameWin"
