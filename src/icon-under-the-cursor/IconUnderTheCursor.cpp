#define UNICODE
#define _UNICODE

#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <string>
#include <filesystem>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

constexpr int ICON_SIZE = 32;
constexpr int OFFSET_X = 18;
constexpr int OFFSET_Y = 18;

HWND g_hwnd = nullptr;
ULONG_PTR g_gdiplusToken = 0;
Bitmap* g_bitmap = nullptr;

bool g_running = true;

// persistent backing bitmap/DC for fast updates
HDC g_memDC = nullptr;
HBITMAP g_hBitmap = nullptr;
void* g_bits = nullptr;
POINT g_prevCursor = { -1, -1 };

// ----------------------------------------------------
// Get PATH:
// C:\Users\<user>\Pictures\ico.png
// ----------------------------------------------------

std::wstring GetIconPath()
{
    wchar_t userPath[MAX_PATH];

    if (SUCCEEDED(
        SHGetFolderPathW(
            nullptr,
            CSIDL_PERSONAL,
            nullptr,
            SHGFP_TYPE_CURRENT,
            userPath)))
    {
        std::filesystem::path p(userPath);
        p /= L"..";
        p /= L"Pictures";
        p /= L"ico.png";

        return std::filesystem::weakly_canonical(p).wstring();
    }

    return L"";
}


// ----------------------------------------------------
// Download PNG
// ----------------------------------------------------

bool LoadIconImage()
{
    std::wstring path = GetIconPath();

    if (!std::filesystem::exists(path))
        return false;

    g_bitmap = Bitmap::FromFile(path.c_str());

    if (!g_bitmap)
        return false;

    if (g_bitmap->GetLastStatus() != Ok)
    {
        delete g_bitmap;
        g_bitmap = nullptr;
        return false;
    }

    // existing checks stay...
    // after g_bitmap initialized and status == Ok:

    // create memDC and DIBSection once
    HDC screenDC = GetDC(nullptr);
    g_memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ICON_SIZE;
    bmi.bmiHeader.biHeight = -ICON_SIZE; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_hBitmap = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS, &g_bits, nullptr, 0);
    SelectObject(g_memDC, g_hBitmap);

    // render g_bitmap into g_memDC once
    {
        Graphics graphics(g_memDC);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetCompositingMode(CompositingModeSourceCopy);
        graphics.DrawImage(g_bitmap, Rect(0,0,ICON_SIZE,ICON_SIZE));
    }

    ReleaseDC(nullptr, screenDC);

    return true;
}


// ----------------------------------------------------
// Render layer
// ----------------------------------------------------

void UpdateLayer()
{
    if (!g_hBitmap || !g_memDC || !g_bitmap)
        return;

    POINT pt;
    GetCursorPos(&pt);

    // if cursor didn't move, nothing to update
    if (pt.x == g_prevCursor.x && pt.y == g_prevCursor.y)
        return;

    g_prevCursor = pt;

    SIZE size = { ICON_SIZE, ICON_SIZE };
    POINT src = { 0, 0 };
    POINT pos = { pt.x + OFFSET_X, pt.y + OFFSET_Y };

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screenDC = GetDC(nullptr);
    UpdateLayeredWindow(
        g_hwnd,
        screenDC,
        &pos,
        &size,
        g_memDC,
        &src,
        0,
        &blend,
        ULW_ALPHA);
    ReleaseDC(nullptr, screenDC);
}


// ----------------------------------------------------
// Window procedure
// ----------------------------------------------------

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{

    switch(msg)
    {

    case WM_DESTROY:

        g_running=false;

        if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = nullptr; }
        if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }

        PostQuitMessage(0);

        return 0;
    }


    return DefWindowProc(hwnd,msg,wParam,lParam);
}


// ----------------------------------------------------
// Window creation
// ----------------------------------------------------

bool CreateOverlay(HINSTANCE hInstance)
{

    WNDCLASSEXW wc{};

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CursorIconOverlay";


    if(!RegisterClassExW(&wc))
        return false;


    g_hwnd =
        CreateWindowExW(

            WS_EX_LAYERED |
            WS_EX_TRANSPARENT |
            WS_EX_TOOLWINDOW |
            WS_EX_TOPMOST,

            wc.lpszClassName,

            L"",
            WS_POPUP,

            0,
            0,
            ICON_SIZE,
            ICON_SIZE,

            nullptr,
            nullptr,
            hInstance,
            nullptr);


    if(!g_hwnd)
        return false;


    ShowWindow(
        g_hwnd,
        SW_SHOWNOACTIVATE);




    return true;
}



// ----------------------------------------------------
// Mutex
// ----------------------------------------------------

bool AlreadyRunning()
{
    HANDLE mutex =
        CreateMutexW(
            nullptr,
            TRUE,
            L"CursorIconUtilityMutex");


    if(GetLastError()==ERROR_ALREADY_EXISTS)
    {
        HWND hwnd =
            FindWindowW(
                L"CursorIconOverlay",
                nullptr);


        if(hwnd)
        {
            PostMessage(hwnd,WM_CLOSE,0,0);
        }

        return true;
    }


    return false;
}



// ----------------------------------------------------
// WinMain
// ----------------------------------------------------

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int)
{


    if(AlreadyRunning())
        return 0;



    GdiplusStartupInput gdiplusStartupInput;


    if(
        GdiplusStartup(
            &g_gdiplusToken,
            &gdiplusStartupInput,
            nullptr)
        != Ok)
    {
        return 1;
    }


    if(!LoadIconImage())
    {
        MessageBoxW(
            nullptr,
            L"Не найден файл:\n%USERPROFILE%\\Pictures\\ico.png",
            L"Cursor Icon",
            MB_ICONERROR);

        GdiplusShutdown(g_gdiplusToken);

        return 1;
    }



    SetProcessDPIAware();


    if(!CreateOverlay(hInstance))
    {
        if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = nullptr; }
        if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }
        delete g_bitmap;

        GdiplusShutdown(
            g_gdiplusToken);

        return 1;
    }



    MSG msg{};


    // Prefer low-latency polling for cursor movement
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    while (g_running)
    {
        // wait briefly for messages or timeout (1 ms)
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (!g_running) break;
        }
        UpdateLayer();
    }



    delete g_bitmap;

    GdiplusShutdown(
        g_gdiplusToken);


    return 0;
}