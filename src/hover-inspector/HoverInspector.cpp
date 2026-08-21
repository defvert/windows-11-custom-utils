// HoverInspector.cpp
// Windows 11 hover inspector utility.

#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <uiautomation.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <utility>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "UIAutomationCore.lib")

static const wchar_t* MUTEX_NAME = L"Global\\HoverInspectorSingleton";
static HWND tipWnd;
static std::wstring text;
static IUIAutomation* automation = nullptr;
static POINT targetPos{};
static POINT currentPos{};
static DWORD lastInfoUpdate = 0;
static const DWORD POSITION_INTERVAL = 16;   // ~60 FPS
static const DWORD INFO_INTERVAL = 120;      // UIA/Window info ~8 FPS
static POINT lastInfoPoint{ -1, -1 };
static SIZE tipSize{420, 180};

LRESULT CALLBACK TipProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);

        RECT r;
        GetClientRect(h, &r);

        HBRUSH b = CreateSolidBrush(RGB(14, 20, 29));
        FillRect(dc, &r, b);
        DeleteObject(b);

        r.left += 10;
        r.top += 8;
        r.right -= 10;
        r.bottom -= 8;

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(227, 229, 233));

        DrawTextW(
            dc,
            text.c_str(),
            -1,
            &r,
            DT_LEFT | DT_WORDBREAK | DT_NOPREFIX
        );

        EndPaint(h, &ps);
        return 0;
    }

    if (m == WM_CLOSE)
    {
        DestroyWindow(h);
        return 0;
    }

    if (m == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(h, m, w, l);
}

std::wstring GetProcessName(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == 0)
        return L"unknown";

    HANDLE p = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        pid
    );

    if (!p)
        return L"unknown";

    wchar_t buf[MAX_PATH]{};
    DWORD size = MAX_PATH;

    std::wstring result = L"unknown";

    if (QueryFullProcessImageNameW(p, 0, buf, &size))
    {
        const wchar_t* name = wcsrchr(buf, L'\\');

        if (name)
            result = name + 1;
        else
            result = buf;
    }

    CloseHandle(p);

    return result;
}

DWORD GetProcessId(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

std::wstring UIAutomationInfo(POINT pt)
{
    if (!automation)
        return L"";

    IUIAutomationElement* el = nullptr;

    if (FAILED(automation->ElementFromPoint(pt, &el)) || !el)
        return L"";

    BSTR name = nullptr;
    BSTR type = nullptr;
    BSTR automationId = nullptr;
    BSTR className = nullptr;

    BOOL isEnabled = FALSE;

    el->get_CurrentName(&name);
    el->get_CurrentLocalizedControlType(&type);
    el->get_CurrentAutomationId(&automationId);
    el->get_CurrentClassName(&className);
    el->get_CurrentIsEnabled(&isEnabled);

    VARIANT framework;
    VariantInit(&framework);

    std::wstringstream s;

    s << L"UI Element\n";

    if (name && name[0] != L'\0')
    {
        s << L"Name: "
          << name
          << L"\n";
    }

    if (type && type[0] != L'\0')
    {
        s << L"Type: "
          << type
          << L"\n";
    }

    if (SUCCEEDED(el->GetCurrentPropertyValue(
        UIA_FrameworkIdPropertyId,
        &framework)))
    {
        if (framework.vt == VT_BSTR &&
            framework.bstrVal &&
            framework.bstrVal[0] != L'\0')
        {
            s << L"Framework: "
              << framework.bstrVal
              << L"\n";
        }
    }

    if (automationId && automationId[0] != L'\0')
    {
        s << L"AutomationId: "
          << automationId
          << L"\n";
    }

    if (className && className[0] != L'\0')
    {
        s << L"ClassName: "
          << className
          << L"\n";
    }

    s << L"Enabled: "
      << (isEnabled ? L"True" : L"False")
      << L"\n";

    VariantClear(&framework);

    HWND hwnd = WindowFromPoint(pt);

    if (hwnd)
    {
        wchar_t title[512]{};
        wchar_t cls[256]{};

        GetWindowTextW(hwnd, title, 512);
        GetClassNameW(hwnd, cls, 256);

        DWORD pid = GetProcessId(hwnd);

        s << L"\n";
        s << L"Window\n";

        s << L"Title: "
          << title
          << L"\n";

        s << L"Class: "
          << cls
          << L"\n";

        s << L"Process: "
          << GetProcessName(hwnd)
          << L"\n";

        s << L"PID: "
          << pid;
    }

    if (name)
        SysFreeString(name);

    if (type)
        SysFreeString(type);

    if (automationId)
        SysFreeString(automationId);

    if (className)
        SysFreeString(className);

    el->Release();

    return s.str();
}

std::wstring WindowInfo(POINT pt)
{
    HWND hwnd = WindowFromPoint(pt);

    if (!hwnd)
        return L"No object";

    wchar_t title[512]{};
    wchar_t cls[256]{};

    GetWindowTextW(hwnd, title, 512);
    GetClassNameW(hwnd, cls, 256);

    DWORD pid = GetProcessId(hwnd);

    std::wstringstream s;

    s << L"Window\n";

    s << L"Title: "
      << title
      << L"\n";

    s << L"Class: "
      << cls
      << L"\n";

    s << L"Process: "
      << GetProcessName(hwnd)
      << L"\n";

    s << L"PID: "
      << pid;

    return s.str();
}

SIZE GetTextSize()
{
    HDC dc = GetDC(tipWnd);

    RECT r = {0, 0, 600, 0};

    DrawTextW(
        dc,
        text.c_str(),
        -1,
        &r,
        DT_LEFT |
        DT_WORDBREAK |
        DT_NOPREFIX |
        DT_CALCRECT
    );

    ReleaseDC(tipWnd, dc);

    SIZE size;
    size.cx = (r.right - r.left) + 20;
    size.cy = (r.bottom - r.top) + 16;

    return size;
}

void UpdateWindowRegion()
{
    RECT r;
    GetClientRect(tipWnd, &r);

    HRGN region = CreateRoundRectRgn(
        0,
        0,
        r.right + 1,
        r.bottom + 1,
        16,
        16
    );

    SetWindowRgn(tipWnd, region, TRUE);
}

POINT CalculateTooltipPosition(POINT cursor)
{
    POINT pos{};

    HMONITOR monitor = MonitorFromPoint(
        cursor,
        MONITOR_DEFAULTTONEAREST
    );

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);

    GetMonitorInfoW(monitor, &mi);

    RECT area = mi.rcWork;


    pos.x = cursor.x + 20;
    pos.y = cursor.y + 20;


    if (pos.x + tipSize.cx > area.right)
    {
        pos.x = cursor.x - tipSize.cx - 20;
    }


    if (pos.y + tipSize.cy > area.bottom)
    {
        pos.y = cursor.y - tipSize.cy - 20;
    }


    if (pos.x < area.left)
    {
        pos.x = area.left;
    }


    if (pos.y < area.top)
    {
        pos.y = area.top;
    }


    return pos;
}

void UpdatePosition()
{
    POINT pt;
    GetCursorPos(&pt);

    targetPos = CalculateTooltipPosition(pt);


    currentPos.x = LONG(
        currentPos.x + (targetPos.x - currentPos.x) * 0.9
    );

    currentPos.y = LONG(
        currentPos.y + (targetPos.y - currentPos.y) * 0.9
    );


    SetWindowPos(
        tipWnd,
        HWND_TOPMOST,
        currentPos.x,
        currentPos.y,
        0,
        0,
        SWP_NOSIZE |
        SWP_NOACTIVATE |
        SWP_NOOWNERZORDER
    );
}

void UpdateInfo()
{
    POINT pt;
    GetCursorPos(&pt);

    if (pt.x == lastInfoPoint.x &&
        pt.y == lastInfoPoint.y)
    {
        return;
    }

    lastInfoPoint = pt;

    std::wstring newText = UIAutomationInfo(pt);

    if(newText.empty())
        newText = WindowInfo(pt);

    if(newText != text)
    {
        text = std::move(newText);

        SIZE size = GetTextSize();
        tipSize = size;

        SetWindowPos(
            tipWnd,
            HWND_TOPMOST,
            0,
            0,
            size.cx,
            size.cy,
            SWP_NOMOVE |
            SWP_NOACTIVATE |
            SWP_NOOWNERZORDER
        );

        UpdateWindowRegion();
        InvalidateRect(tipWnd, nullptr, TRUE);
    }
}

int WINAPI WinMain(HINSTANCE h,HINSTANCE,LPSTR,int)
{
    HANDLE mutex = CreateMutexW(nullptr,TRUE,MUTEX_NAME);

    if(GetLastError()==ERROR_ALREADY_EXISTS)
    {
        HWND old = FindWindowW(L"HoverInspectorWindow",nullptr);
        if(old)
            PostMessageW(old,WM_CLOSE,0,0);
        return 0;
    }

    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);

    CLSID clsid{};

    HRESULT hr = CLSIDFromString(
        L"{FF48DBA4-60EF-4201-AA87-54103EEF594E}",
        &clsid
    );

    if (SUCCEEDED(hr))
    {
        CoCreateInstance(
            clsid,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation)
        );
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc=TipProc;
    wc.hInstance=h;
    wc.lpszClassName=L"HoverInspectorWindow";

    RegisterClassW(&wc);

    tipWnd = CreateWindowExW(
    WS_EX_TOOLWINDOW |
    WS_EX_TOPMOST |
    WS_EX_LAYERED,
    wc.lpszClassName,
    L"",
    WS_POPUP,
    0, 0, 420, 180,
    nullptr, nullptr, h, nullptr
    );

    HRGN region = CreateRoundRectRgn(
        0, 0,
        420 + 1,
        180 + 1,
        16,
        16
    );

    SetWindowRgn(tipWnd, region, TRUE);

    SetLayeredWindowAttributes(
        tipWnd,0,235,LWA_ALPHA);

    ShowWindow(tipWnd,SW_SHOWNOACTIVATE);

    GetCursorPos(&currentPos);

    currentPos.x += 20;
    currentPos.y += 20;

    targetPos = currentPos;

    MSG msg{};

    DWORD lastPositionUpdate = GetTickCount();
    lastInfoUpdate = GetTickCount();

    while(true)
    {
        while(PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if(msg.message == WM_QUIT)
                goto exit;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DWORD now = GetTickCount();

        // ~60 FPS
        if(now - lastPositionUpdate >= POSITION_INTERVAL)
        {
            UpdatePosition();
            lastPositionUpdate = now;
        }

        if(now - lastInfoUpdate >= INFO_INTERVAL)
        {
            UpdateInfo();
            lastInfoUpdate = now;
        }

        Sleep(1);
    }

exit:
    if(automation)
        automation->Release();

    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);

    return 0;
}