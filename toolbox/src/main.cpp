#include "common.h"
#include "main_window.h"
#include "ui.h"

// 入口:注册 GDI+、HiDPI 支持、创建主窗口。
// 本工具箱框架以单实例方式运行,同一时刻只允许一个进程。

static bool EnsureSingleInstance()
{
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\ToolkitFrame_Single");
    if (mutex == NULL)
    {
        return true; // 无法创建,放行
    }
    bool ok = (GetLastError() != ERROR_ALREADY_EXISTS);
    return ok;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    // 单实例
    if (!EnsureSingleInstance())
    {
        return 0;
    }

    // 高 DPI 支持 (Win10+)
    typedef HRESULT (*SetDpiAwarenessContextFn)(HDEVNOTIFY);
    HMODULE shcore = LoadLibraryW(L"ShCore.dll");
    if (shcore)
    {
        SetDpiAwarenessContextFn setCtx =
            (SetDpiAwarenessContextFn)GetProcAddress(shcore, "SetProcessDpiAwarenessContext");
        if (setCtx)
        {
            // PER_MONITOR_AWARE_V2 = -4
            setCtx((HANDLE)(-4));
        }
        FreeLibrary(shcore);
    }
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    g_scale = (float)dpi / 96.0f;
    ReleaseDC(NULL, hdc);

    InitFontFamily();

    // COM / GDI+ 全局初始化在 MainWindow::Create 里完成
    MainWindow::Create(hInstance);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 退出前不强制 uninit(GDI+ 跟随进程生命周期)
    return (int)msg.wParam;
}
