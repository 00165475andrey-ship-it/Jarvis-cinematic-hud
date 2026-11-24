// ==WindhawkMod==
//
// @id              jarvis-cinematic-hud
// @name            J.A.R.V.I.S. 3.2 Cinematic HUD
// @description     Animated desktop HUD with system metrics and ARC reactor
// @version         3.2
// @author          JARVIS Project
// @include         explorer.exe
// @compilerOptions -std=c++17
// ==/WindhawkMod==

// ==WindhawkModSettings==
//
// - position: center
// $name: Положение панели
// $options:
// top: Сверху
// bottom: Снизу
// left: Слева
// right: Справа
// topLeft: Верхний левый угол
// topRight: Верхний правый угол
// bottomLeft: Нижний левый угол
// bottomRight: Нижний правый угол
// center: По центру
// centerUp: Центр — вверх
// centerDown: Центр — вниз
// centerLeft: Центр — влево
// centerRight: Центр — вправо
//
// - margin: 35
// $name: Отступ от края
//
// - centerOffset: 150
// $name: Смещение от центра
//
// - panelWidth: 600
// $name: Ширина панели
//
// - panelHeight: 380
// $name: Высота панели
//
// - opacity: 225
// $name: Прозрачность
// $description: От 100 до 255
//
// - animations: true
// $name: Анимации HUD
//
// - mouseEffects: true
// $name: Реакция на курсор
//
// - reactor: true
// $name: ARC Reactor
//
// - decorations: true
// $name: Дополнительная графика
// ==/WindhawkModSettings==

#include <windows.h>
#include <string>
#include <cmath>
#include <algorithm>
#include <cwchar>

static constexpr wchar_t CLASS_NAME[] = L"JARVIS_CINEMATIC_HUD";
static constexpr double PI = 3.14159265358979323846;

static constexpr UINT WM_JARVIS_SETTINGS = WM_APP + 10;
static constexpr UINT_PTR TIMER_ANIMATION = 1;

struct Settings {
    std::wstring position = L"center";
    int margin = 35;
    int offset = 150;
    int width = 600;
    int height = 380;
    int opacity = 225;
    bool animations = true;
    bool mouseEffects = true;
    bool reactor = true;
    bool decorations = true;
};

struct SystemData {
    int cpu = 0;
    int ram = 0;
    ULONGLONG diskFree = 0;
    SYSTEM_POWER_STATUS power{};
};

static Settings g_settings;
static SystemData g_data;

// Window and thread handles are only read/written by the HUD thread,
// except for the thread ID and stop event used by Windhawk callbacks.
static HWND g_panel = nullptr;
static HWND g_desktopView = nullptr;
static DWORD g_hudThreadId = 0;
static HANDLE g_hudThread = nullptr;
static HANDLE g_threadReadyEvent = nullptr;
static HANDLE g_stopEvent = nullptr;

static ULONGLONG g_oldIdle = 0;
static ULONGLONG g_oldKernel = 0;
static ULONGLONG g_oldUser = 0;
static bool g_cpuInitialized = false;

static float g_mousePower = 0.0f;
static float g_animation = 0.0f;

static void LoadSettings() {
    PCWSTR position = Wh_GetStringSetting(L"position");
    g_settings.position = position ? position : L"center";
    if (position) {
        Wh_FreeStringSetting(position);
    }

    g_settings.margin = std::clamp(Wh_GetIntSetting(L"margin"), 0, 500);
    g_settings.offset = std::clamp(Wh_GetIntSetting(L"centerOffset"), 0, 1000);
    g_settings.width = std::clamp(Wh_GetIntSetting(L"panelWidth"), 400, 1200);
    g_settings.height = std::clamp(Wh_GetIntSetting(L"panelHeight"), 280, 900);
    g_settings.opacity = std::clamp(Wh_GetIntSetting(L"opacity"), 100, 255);

    g_settings.animations = Wh_GetIntSetting(L"animations") != 0;
    g_settings.mouseEffects = Wh_GetIntSetting(L"mouseEffects") != 0;
    g_settings.reactor = Wh_GetIntSetting(L"reactor") != 0;
    g_settings.decorations = Wh_GetIntSetting(L"decorations") != 0;
}

// Locate the actual desktop icon view. The HUD is made a child of
// SHELLDLL_DefView (not its WorkerW/Progman owner), so its sibling z-order
// can place it behind the desktop icon list.
static HWND FindDesktopView() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        HWND view = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (view) {
            return view;
        }
    }

    struct SearchData {
        HWND result = nullptr;
    } search;

    EnumWindows(
        [](HWND top, LPARAM param) -> BOOL {
            auto* data = reinterpret_cast<SearchData*>(param);
            HWND view = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
            if (view) {
                data->result = view;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search)
    );

    return search.result;
}

static POINT CalculatePosition(int screenWidth, int screenHeight) {
    int x = (screenWidth - g_settings.width) / 2;
    int y = (screenHeight - g_settings.height) / 2;
    const int m = g_settings.margin;
    const int o = g_settings.offset;
    const std::wstring& p = g_settings.position;

    if (p == L"top") y = m;
    else if (p == L"bottom") y = screenHeight - g_settings.height - m;
    else if (p == L"left") x = m;
    else if (p == L"right") x = screenWidth - g_settings.width - m;
    else if (p == L"topLeft") { x = m; y = m; }
    else if (p == L"topRight") { x = screenWidth - g_settings.width - m; y = m; }
    else if (p == L"bottomLeft") { x = m; y = screenHeight - g_settings.height - m; }
    else if (p == L"bottomRight") {
        x = screenWidth - g_settings.width - m;
        y = screenHeight - g_settings.height - m;
    }
    else if (p == L"centerUp") y -= o;
    else if (p == L"centerDown") y += o;
    else if (p == L"centerLeft") x -= o;
    else if (p == L"centerRight") x += o;

    x = std::clamp(x, 0, std::max(0, screenWidth - g_settings.width));
    y = std::clamp(y, 0, std::max(0, screenHeight - g_settings.height));
    return { x, y };
}

static void ApplyPosition() {
    if (!g_panel || !IsWindow(g_panel) || !g_desktopView ||
        !IsWindow(g_desktopView)) {
        return;
    }

    RECT client{};
    if (!GetClientRect(g_desktopView, &client)) {
        return;
    }

    POINT pos = CalculatePosition(client.right - client.left,
                                  client.bottom - client.top);

    // HWND_BOTTOM keeps the HUD below the desktop icon-list sibling.
    // It remains a child of SHELLDLL_DefView and does not cover the taskbar.
    SetWindowPos(
        g_panel,
        HWND_BOTTOM,
        pos.x,
        pos.y,
        g_settings.width,
        g_settings.height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );
}

static ULONGLONG ToULL(FILETIME ft) {
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

static void UpdateSystemData() {
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULONGLONG i = ToULL(idle);
        ULONGLONG k = ToULL(kernel);
        ULONGLONG u = ToULL(user);

        if (g_cpuInitialized && k >= g_oldKernel && u >= g_oldUser && i >= g_oldIdle) {
            ULONGLONG total = (k - g_oldKernel) + (u - g_oldUser);
            ULONGLONG idleDiff = i - g_oldIdle;
            if (total > 0) {
                g_data.cpu = std::clamp(
                    static_cast<int>(100 - idleDiff * 100 / total), 0, 100);
            }
        }

        g_oldIdle = i;
        g_oldKernel = k;
        g_oldUser = u;
        g_cpuInitialized = true;
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        g_data.ram = static_cast<int>(memory.dwMemoryLoad);
    }

    ULARGE_INTEGER freeBytes{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFree{};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFree)) {
        g_data.diskFree = freeBytes.QuadPart;
    }

    GetSystemPowerStatus(&g_data.power);
}

static COLORREF Blue(int intensity) {
    intensity = std::clamp(intensity, 0, 255);
    return RGB(0, intensity / 2, intensity);
}

static void Line(HDC dc, int x1, int y1, int x2, int y2,
                 COLORREF color, int thickness = 1) {
    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    if (!pen) return;
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void Circle(HDC dc, int cx, int cy, int radius,
                   COLORREF color, int thickness = 1) {
    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    if (!pen) return;

    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void Text(HDC dc, const wchar_t* value, int x, int y,
                 int size, COLORREF color, int weight = FW_NORMAL) {
    HFONT font = CreateFontW(
        size, 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
    );
    if (!font) return;

    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, value, static_cast<int>(wcslen(value)));
    SelectObject(dc, old);
    DeleteObject(font);
}

static void Metric(HDC dc, const wchar_t* name, const wchar_t* value,
                   int x, int y) {
    Text(dc, name, x, y, 13, RGB(80, 190, 225));
    Text(dc, value, x, y + 24, 23, RGB(215, 245, 255), FW_SEMIBOLD);
    Line(dc, x, y + 56, x + 115, y + 56, RGB(0, 75, 105));
}

static void DrawReactor(HDC dc, int cx, int cy, int radius) {
    const int power = static_cast<int>(g_mousePower * 100.0f);
    const int glow = 130 + power;

    Circle(dc, cx, cy, radius + 17,
           RGB(0, 45 + power / 5, 75 + power / 3), 2);
    Circle(dc, cx, cy, radius + 10,
           RGB(0, 110 + power / 2, 165 + power / 3), 2);
    Circle(dc, cx, cy, radius, Blue(glow), 3);
    Circle(dc, cx, cy, radius - 9, RGB(0, 100, 150), 2);
    Circle(dc, cx, cy, radius - 19, RGB(0, 180, 230), 2);

    for (int i = 0; i < 12; i++) {
        double angle = i * PI / 6 + g_animation * 0.8;
        int x1 = cx + static_cast<int>(cos(angle) * (radius - 10));
        int y1 = cy + static_cast<int>(sin(angle) * (radius - 10));
        int x2 = cx + static_cast<int>(cos(angle) * (radius - 25));
        int y2 = cy + static_cast<int>(sin(angle) * (radius - 25));
        Line(dc, x1, y1, x2, y2, RGB(0, 200, 255), 2);
    }

    HBRUSH core = CreateSolidBrush(
        RGB(0, 160 + power / 3, 230 + power / 6));
    if (core) {
        HGDIOBJ old = SelectObject(dc, core);
        Ellipse(dc, cx - 12, cy - 12, cx + 12, cy + 12);
        SelectObject(dc, old);
        DeleteObject(core);
    }

    Circle(dc, cx, cy, 7, RGB(180, 245, 255), 2);

    for (int i = 0; i < 24; i++) {
        double angle = i * PI / 12 + g_animation * 0.3;
        int x = cx + static_cast<int>(cos(angle) * (radius + 5));
        int y = cy + static_cast<int>(sin(angle) * (radius + 5));
        Circle(dc, x, y, 2, RGB(50, 220, 255));
    }
}

static void DrawDecorations(HDC dc) {
    const int w = g_settings.width;
    const int h = g_settings.height;

    for (int i = 0; i < 6; i++) {
        int y = 90 + i * 18;
        Line(dc, 20, y, 65 + i * 3, y, RGB(0, 95 + i * 12, 145));
        Line(dc, w - 65 - i * 3, y, w - 20, y, RGB(0, 95 + i * 12, 145));
    }

    int corners[4][2] = {
        {15, 15}, {w - 15, 15}, {15, h - 15}, {w - 15, h - 15}
    };

    for (auto& corner : corners) {
        Circle(dc, corner[0], corner[1], 5, RGB(0, 170, 220));
    }

    Line(dc, 25, h - 18, w - 25, h - 18, RGB(0, 115, 165));

    for (int i = 0; i < 18; i++) {
        int x = 30 + i * ((w - 60) / 18);
        Circle(dc, x, h - 18, 2, RGB(0, 180, 230));
    }

    Text(dc, L"J.A.R.V.I.S.  //  SYSTEM INTERFACE",
         30, h - 43, 10, RGB(70, 155, 190));
}

static void UpdateMouseReaction() {
    POINT cursor{};
    float target = 0.0f;

    if (g_settings.mouseEffects && g_panel && IsWindow(g_panel) &&
        GetCursorPos(&cursor) && ScreenToClient(g_panel, &cursor)) {
        const double dx = cursor.x - g_settings.width / 2.0;
        const double dy = cursor.y - 115.0;
        const double distance = std::sqrt(dx * dx + dy * dy);

        if (distance < 180.0) {
            target = static_cast<float>(1.0 - distance / 180.0);
        }
    }

    // Smoothly fade the effect out when the option is disabled or cursor moves away.
    g_mousePower += (target - g_mousePower) * 0.12f;
}

static void DrawPanel(HDC dc) {
    RECT rect{ 0, 0, g_settings.width, g_settings.height };

    HBRUSH background = CreateSolidBrush(RGB(8, 17, 29));
    if (background) {
        FillRect(dc, &rect, background);
        DeleteObject(background);
    }

    HPEN border = CreatePen(PS_SOLID, 1, RGB(0, 160, 210));
    if (border) {
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, 1, 1, g_settings.width - 1,
                  g_settings.height - 1, 24, 24);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(border);
    }

    Text(dc, L"J.A.R.V.I.S.", 25, 23, 27, RGB(0, 220, 255), FW_BOLD);
    Text(dc, L"SYSTEM ONLINE", 27, 58, 12, RGB(70, 230, 170), FW_BOLD);

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t clock[64]{};
    swprintf_s(clock, _countof(clock), L"%02d:%02d:%02d",
               time.wHour, time.wMinute, time.wSecond);
    Text(dc, clock, g_settings.width - 130, 28, 21,
         RGB(180, 225, 240), FW_SEMIBOLD);

    if (g_settings.reactor) {
        DrawReactor(dc, g_settings.width / 2, 115, 43);
    }

    if (g_settings.decorations) {
        DrawDecorations(dc);
    }

    wchar_t cpu[32]{};
    wchar_t ram[32]{};
    wchar_t disk[64]{};
    wchar_t power[32]{};

    swprintf_s(cpu, _countof(cpu), L"%d%%", g_data.cpu);
    swprintf_s(ram, _countof(ram), L"%d%%", g_data.ram);
    swprintf_s(disk, _countof(disk), L"%.1f GB",
               g_data.diskFree / 1073741824.0);

    if (g_data.power.BatteryLifePercent <= 100) {
        swprintf_s(power, _countof(power), L"%d%%",
                   g_data.power.BatteryLifePercent);
    } else {
        wcscpy_s(power, _countof(power), L"N/A");
    }

    Metric(dc, L"CPU", cpu, 30, 205);
    Metric(dc, L"RAM", ram, 170, 205);
    Metric(dc, L"GPU", L"N/A", 310, 205);
    Metric(dc, L"VRAM", L"N/A", 450, 205);
    Metric(dc, L"DISK C: FREE", disk, 30, 285);
    Metric(dc, L"POWER", power, 310, 285);

    Text(dc, L"TEMPERATURE: N/A",
         g_settings.width - 190, g_settings.height - 43,
         10, RGB(110, 170, 190));
}

static bool CreatePanel() {
    g_desktopView = FindDesktopView();
    if (!g_desktopView || !IsWindow(g_desktopView)) {
        Wh_Log(L"JARVIS: SHELLDLL_DefView not found");
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam,
                        LPARAM lParam) -> LRESULT {
        switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            DrawPanel(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_ANIMATION) {
                UpdateSystemData();
                if (g_settings.animations) {
                    g_animation += 0.08f;
                }
                UpdateMouseReaction();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_JARVIS_SETTINGS:
            LoadSettings();
            SetLayeredWindowAttributes(hwnd, 0,
                static_cast<BYTE>(g_settings.opacity), LWA_ALPHA);
            ApplyPosition();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            ApplyPosition();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ANIMATION);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Wh_Log(L"JARVIS: RegisterClassExW failed: %u", GetLastError());
        return false;
    }

    // The desktop icon view is the parent. HWND_BOTTOM puts this child behind
    // the icon-list child instead of creating a window beside the whole WorkerW.
    g_panel = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        CLASS_NAME,
        L"JARVIS 3.2",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, g_settings.width, g_settings.height,
        g_desktopView,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!g_panel) {
        Wh_Log(L"JARVIS: CreateWindowExW failed: %u", GetLastError());
        return false;
    }

    SetLayeredWindowAttributes(g_panel, 0,
        static_cast<BYTE>(g_settings.opacity), LWA_ALPHA);

    ApplyPosition();

    if (!SetTimer(g_panel, TIMER_ANIMATION, 33, nullptr)) {
        Wh_Log(L"JARVIS: SetTimer failed: %u", GetLastError());
    }

    UpdateSystemData();
    InvalidateRect(g_panel, nullptr, TRUE);
    Wh_Log(L"JARVIS: HUD window created; parent is SHELLDLL_DefView");
    return true;
}

static DWORD WINAPI JarvisThread(LPVOID) {
    // Force creation of this thread's message queue before callbacks post to it.
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    g_hudThreadId = GetCurrentThreadId();

    if (g_threadReadyEvent) {
        SetEvent(g_threadReadyEvent);
    }

    LoadSettings();

    // Explorer can still be arranging the desktop when the mod is injected.
    bool created = false;
    for (int attempt = 0; attempt < 20 &&
         WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0; attempt++) {
        if (CreatePanel()) {
            created = true;
            break;
        }
        if (WaitForSingleObject(g_stopEvent, 500) == WAIT_OBJECT_0) {
            break;
        }
    }

    if (!created) {
        Wh_Log(L"JARVIS: Unable to create HUD after waiting for desktop view");
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // PostThreadMessage messages have no HWND, so DispatchMessageW would
        // ignore them. Apply settings explicitly on the HUD-owning thread.
        if (!msg.hwnd && msg.message == WM_JARVIS_SETTINGS) {
            LoadSettings();
            if (g_panel && IsWindow(g_panel)) {
                SetLayeredWindowAttributes(
                    g_panel, 0, static_cast<BYTE>(g_settings.opacity), LWA_ALPHA);
                ApplyPosition();
                InvalidateRect(g_panel, nullptr, TRUE);
            }
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_panel && IsWindow(g_panel)) {
        DestroyWindow(g_panel);
    }
    g_panel = nullptr;
    g_desktopView = nullptr;

    UnregisterClassW(CLASS_NAME, GetModuleHandleW(nullptr));
    return 0;
}

BOOL Wh_ModInit() {
    Wh_Log(L"JARVIS 3.2 initializing");

    g_threadReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_threadReadyEvent || !g_stopEvent) {
        Wh_Log(L"JARVIS: CreateEvent failed: %u", GetLastError());
        if (g_threadReadyEvent) CloseHandle(g_threadReadyEvent);
        if (g_stopEvent) CloseHandle(g_stopEvent);
        g_threadReadyEvent = nullptr;
        g_stopEvent = nullptr;
        return FALSE;
    }

    g_hudThread = CreateThread(nullptr, 0, JarvisThread, nullptr, 0, nullptr);
    if (!g_hudThread) {
        Wh_Log(L"JARVIS: CreateThread failed: %u", GetLastError());
        CloseHandle(g_threadReadyEvent);
        CloseHandle(g_stopEvent);
        g_threadReadyEvent = nullptr;
        g_stopEvent = nullptr;
        return FALSE;
    }

    // Wait only until the worker owns a message queue, not for window creation.
    if (WaitForSingleObject(g_threadReadyEvent, 5000) != WAIT_OBJECT_0) {
        Wh_Log(L"JARVIS: HUD thread did not initialize its message queue");
        SetEvent(g_stopEvent);
        PostThreadMessageW(GetThreadId(g_hudThread), WM_QUIT, 0, 0);
        WaitForSingleObject(g_hudThread, 5000);
        CloseHandle(g_hudThread);
        CloseHandle(g_threadReadyEvent);
        CloseHandle(g_stopEvent);
        g_hudThread = nullptr;
        g_threadReadyEvent = nullptr;
        g_stopEvent = nullptr;
        return FALSE;
    }

    return TRUE;
}

void Wh_ModSettingsChanged() {
    DWORD threadId = g_hudThreadId;
    if (threadId) {
        PostThreadMessageW(threadId, WM_JARVIS_SETTINGS, 0, 0);
    }
}

void Wh_ModUninit() {
    Wh_Log(L"JARVIS 3.2 shutting down");

    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }

    DWORD threadId = g_hudThreadId;
    if (threadId) {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }

    if (g_hudThread) {
        // The worker owns the HWND and must destroy it before the mod DLL unloads.
        // It checks the stop event during desktop discovery and exits its message
        // loop immediately after WM_QUIT.
        WaitForSingleObject(g_hudThread, INFINITE);
        CloseHandle(g_hudThread);
        g_hudThread = nullptr;
    }

    if (g_threadReadyEvent) {
        CloseHandle(g_threadReadyEvent);
        g_threadReadyEvent = nullptr;
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }

    g_hudThreadId = 0;
}