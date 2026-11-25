/*
 * File: osk.c
 * Description: Win32 Native On-Screen Keyboard for Windows LiveCD / WinPE.
 *              - English QWERTY Layout with crisp Square Keys.
 *              - Esc key, Fn layer (F1-F12, Nav, PrtScn, symbols), Arrow keys, Set button.
 *              - Top grab bar with Hide, Docking toggle (Bottom/Top/Floating), and Paste Clipboard.
 *              - Sticky modifiers (Left Ctrl, Left Shift, Win, Right Ctrl, Right Alt, Caps).
 *              - Configurable auto-repeat, auto-popup on text focus, and height presets.
 *              - Timer-based polling engine (bypasses WinEventHook hangs).
 *              - IPC Rescue: Running the exe twice forces it open.
 * License: Released into the Public Domain.
 * 
 * Compile using:
 * gcc -Os -s -o osk.exe osk.c -mwindows -luser32 -lgdi32 -lshell32
 */

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define WM_USER_TRAY          (WM_USER + 1)
#define WM_USER_SHOW_MANAGER  (WM_USER + 2)
#define ID_TRAY_EXIT          9999
#define ID_TRAY_WINDOW_MODE   9998
#define ID_TRAY_DOCK_BOTTOM   9997
#define ID_TRAY_DOCK_TOP      9996
#define ID_TRAY_H_SMALL       9995
#define ID_TRAY_H_MED         9994
#define ID_TRAY_H_LARGE       9993
#define ID_TRAY_STICKY_LCTRL  9992
#define ID_TRAY_STICKY_WIN    9991
#define ID_TRAY_AUTO_REPEAT   9990
#define ID_TRAY_STICKY_LSHIFT 9989
#define ID_TRAY_AUTO_POPUP    9988
#define ID_TRAY_SKIP_UNDOCKED 9987
#define ID_TIMER_FOCUS        1
#define ID_TIMER_REPEAT_DELAY 2
#define ID_TIMER_REPEAT_RATE  3
#define VK_OSK_FN             0x1001
#define VK_OSK_SETTINGS       0x1002
#define GRAB_BAR_HEIGHT       28
// --- Globals ---
HWND hMainWnd = NULL;
NOTIFYICONDATA nid;

HFONT hFontNormal, hFontSmall;

// Settings
int g_oskHeight = 320;
BOOL g_bWindowMode = FALSE;
BOOL g_bDockTop = FALSE;
int g_floatingY = -1;

// Options (Defaults)
BOOL g_bStickyLCtrl  = TRUE;
BOOL g_bStickyWin    = FALSE;
BOOL g_bAutoRepeat   = TRUE;
BOOL g_bStickyLShift = TRUE;
BOOL g_bAutoPopup    = TRUE;
BOOL g_bSkipUndocked = TRUE;

// State & Dragging
BOOL g_bIsVisible = FALSE;
BOOL g_bManualOverride = TRUE; // Start visible immediately on launch
BOOL g_bTrayIconAdded = FALSE;
HWND g_hSuppressedWindow = NULL;

BOOL g_bShiftLock = FALSE;
BOOL g_bCaps = FALSE;
BOOL g_bLeftCtrlLock = FALSE;
BOOL g_bRightCtrlLock = FALSE;
BOOL g_bRightAltLock = FALSE;
BOOL g_bWinLock = FALSE;
BOOL g_bFnLock = FALSE;

int g_hoverKey = -1;
int g_downKey = -1;
BOOL g_bDidAutoRepeat = FALSE;

BOOL g_bHoverHide = FALSE;
BOOL g_bHoverPaste = FALSE;
BOOL g_bHasClipboard = FALSE;

BOOL g_bGrabTracking = FALSE;
BOOL g_bIsDragging = FALSE;
POINT g_ptGrabStart;
RECT g_rcWindowStart;

// Theme Colors
#define CLR_BG        RGB(230, 233, 237)
#define CLR_BAR       RGB(200, 205, 212)
#define CLR_KEY_BG    RGB(255, 255, 255)
#define CLR_KEY_HOVER RGB(240, 240, 240)
#define CLR_KEY_DOWN  RGB(220, 220, 220)
#define CLR_KEY_MOD   RGB(215, 220, 225)
#define CLR_TEXT      RGB(20, 20, 20)
#define CLR_TEXT_MUT  RGB(100, 100, 100)
#define CLR_ACCENT    RGB(0, 120, 212)
// --- Keyboard Layout ---
typedef struct {
    const char* norm;
    const char* shift;
    int vk;
    int widthUnits;
    BOOL isMod;
    RECT rect;
} KeyDef;

#define TOTAL_UNITS 160
KeyDef layout[] = {
    // Row 0 (15 keys, 160 units)
    {"Esc", "Esc", VK_ESCAPE, 10, TRUE, {0}}, {"`", "~", VK_OEM_3, 10, FALSE, {0}}, {"1", "!", '1', 10, FALSE, {0}},
    {"2", "@", '2', 10, FALSE, {0}}, {"3", "#", '3', 10, FALSE, {0}}, {"4", "$", '4', 10, FALSE, {0}},
    {"5", "%", '5', 10, FALSE, {0}}, {"6", "^", '6', 10, FALSE, {0}}, {"7", "&", '7', 10, FALSE, {0}},
    {"8", "*", '8', 10, FALSE, {0}}, {"9", "(", '9', 10, FALSE, {0}}, {"0", ")", '0', 10, FALSE, {0}},
    {"-", "_", VK_OEM_MINUS, 10, FALSE, {0}}, {"=", "+", VK_OEM_PLUS, 10, FALSE, {0}}, {"Backspace", "Backspace", VK_BACK, 20, TRUE, {0}},

    // Row 1 (14 keys, 160 units)
    {"Tab", "Tab", VK_TAB, 20, TRUE, {0}}, {"q", "Q", 'Q', 10, FALSE, {0}}, {"w", "W", 'W', 10, FALSE, {0}},
    {"e", "E", 'E', 10, FALSE, {0}}, {"r", "R", 'R', 10, FALSE, {0}}, {"t", "T", 'T', 10, FALSE, {0}},
    {"y", "Y", 'Y', 10, FALSE, {0}}, {"u", "U", 'U', 10, FALSE, {0}}, {"i", "I", 'I', 10, FALSE, {0}},
    {"o", "O", 'O', 10, FALSE, {0}}, {"p", "P", 'P', 10, FALSE, {0}}, {"[", "{", VK_OEM_4, 10, FALSE, {0}},
    {"]", "}", VK_OEM_6, 10, FALSE, {0}}, {"\\", "|", VK_OEM_5, 20, FALSE, {0}},

    // Row 2 (13 keys, 160 units)
    {"Caps", "Caps", VK_CAPITAL, 22, TRUE, {0}}, {"a", "A", 'A', 10, FALSE, {0}}, {"s", "S", 'S', 10, FALSE, {0}},
    {"d", "D", 'D', 10, FALSE, {0}}, {"f", "F", 'F', 10, FALSE, {0}}, {"g", "G", 'G', 10, FALSE, {0}},
    {"h", "H", 'H', 10, FALSE, {0}}, {"j", "J", 'J', 10, FALSE, {0}}, {"k", "K", 'K', 10, FALSE, {0}},
    {"l", "L", 'L', 10, FALSE, {0}}, {";", ":", VK_OEM_1, 10, FALSE, {0}}, {"'", "\"", VK_OEM_7, 10, FALSE, {0}},
    {"Enter", "Enter", VK_RETURN, 28, TRUE, {0}},

    // Row 3 (13 keys, 160 units)
    {"Shift", "Shift", VK_SHIFT, 24, TRUE, {0}}, {"z", "Z", 'Z', 10, FALSE, {0}}, {"x", "X", 'X', 10, FALSE, {0}},
    {"c", "C", 'C', 10, FALSE, {0}}, {"v", "V", 'V', 10, FALSE, {0}}, {"b", "B", 'B', 10, FALSE, {0}},
    {"n", "N", 'N', 10, FALSE, {0}}, {"m", "M", 'M', 10, FALSE, {0}}, {",", "<", VK_OEM_COMMA, 10, FALSE, {0}},
    {".", ">", VK_OEM_PERIOD, 10, FALSE, {0}}, {"/", "?", VK_OEM_2, 10, FALSE, {0}}, {"^", "^", VK_UP, 12, TRUE, {0}},
    {"Shift", "Shift", VK_RSHIFT, 24, TRUE, {0}},

    // Row 4 (11 keys, 160 units)
    {"Ctrl", "Ctrl", VK_CONTROL, 12, TRUE, {0}}, {"Fn", "Fn", VK_OSK_FN, 11, TRUE, {0}}, {"Win", "Win", VK_LWIN, 11, TRUE, {0}},
    {"Alt", "Alt", VK_MENU, 11, TRUE, {0}}, {"Space", "Space", VK_SPACE, 44, FALSE, {0}},
    {"Alt", "Alt", VK_RMENU, 11, TRUE, {0}}, {"Ctrl", "Ctrl", VK_RCONTROL, 12, TRUE, {0}},
    {"<", "<", VK_LEFT, 12, TRUE, {0}}, {"v", "v", VK_DOWN, 12, TRUE, {0}}, {">", ">", VK_RIGHT, 12, TRUE, {0}},
    {"Set", "Set", VK_OSK_SETTINGS, 12, TRUE, {0}}
};

int numKeys = sizeof(layout) / sizeof(KeyDef);

// --- Fn Layer Definition (1:1 index match with layout[]) ---
typedef struct {
    const wchar_t* label; // NULL = keep normal key behavior
    int vk;               // Non-zero = send virtual key
    wchar_t ch;           // Non-zero = send Unicode char
} FnKeyDef;

FnKeyDef fnLayer[] = {
    // Row 0 (0..14): Esc, `->Ins, 1..= -> F1..F12, Backspace->Del
    {NULL, 0, 0}, {L"Ins", VK_INSERT, 0},
    {L"F1", VK_F1, 0}, {L"F2", VK_F2, 0}, {L"F3", VK_F3, 0}, {L"F4", VK_F4, 0},
    {L"F5", VK_F5, 0}, {L"F6", VK_F6, 0}, {L"F7", VK_F7, 0}, {L"F8", VK_F8, 0},
    {L"F9", VK_F9, 0}, {L"F10", VK_F10, 0}, {L"F11", VK_F11, 0}, {L"F12", VK_F12, 0},
    {L"Del", VK_DELETE, 0},

    // Row 1 (15..28): Tab, q..p, [, ], \ -> ¡ ¿ € ® ™ ¥ µ ∞ ° π « » ¦
    {NULL, 0, 0},
    {L"\x00A1", 0, 0x00A1}, {L"\x00BF", 0, 0x00BF}, {L"\x20AC", 0, 0x20AC}, {L"\x00AE", 0, 0x00AE},
    {L"\x2122", 0, 0x2122}, {L"\x00A5", 0, 0x00A5}, {L"\x00B5", 0, 0x00B5}, {L"\x221E", 0, 0x221E},
    {L"\x00B0", 0, 0x00B0}, {L"\x03C0", 0, 0x03C0}, {L"\x00AB", 0, 0x00AB}, {L"\x00BB", 0, 0x00BB},
    {L"\x00A6", 0, 0x00A6},

    // Row 2 (29..41): Caps->ScrLk, a..l, ;, ' -> ± § ÷ × £ ¢ ¶ • … – —, Enter
    {L"ScrLk", VK_SCROLL, 0},
    {L"\x00B1", 0, 0x00B1}, {L"\x00A7", 0, 0x00A7}, {L"\x00F7", 0, 0x00F7}, {L"\x00D7", 0, 0x00D7},
    {L"\x00A3", 0, 0x00A3}, {L"\x00A2", 0, 0x00A2}, {L"\x00B6", 0, 0x00B6}, {L"\x2022", 0, 0x2022},
    {L"\x2026", 0, 0x2026}, {L"\x2013", 0, 0x2013}, {L"\x2014", 0, 0x2014},
    {NULL, 0, 0},

    // Row 3 (42..54): LShift, z..m, , . / -> ¼ ½ © ¾ ² ³ √ ≤ ≥ ≠, Up->PgUp, RShift
    {NULL, 0, 0},
    {L"\x00BC", 0, 0x00BC}, {L"\x00BD", 0, 0x00BD}, {L"\x00A9", 0, 0x00A9}, {L"\x00BE", 0, 0x00BE},
    {L"\x00B2", 0, 0x00B2}, {L"\x00B3", 0, 0x00B3}, {L"\x221A", 0, 0x221A}, {L"\x2264", 0, 0x2264},
    {L"\x2265", 0, 0x2265}, {L"\x2260", 0, 0x2260},
    {L"PgUp", VK_PRIOR, 0},
    {NULL, 0, 0},

    // Row 4 (55..65): LCtrl, Fn, Win, LAlt, Space, RAlt->Menu, RCtrl->Pause, Left->Home, Down->PgDn, Right->End, Set->PrtScn
    {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0}, {NULL, 0, 0},
    {L"Menu", VK_APPS, 0}, {L"Pause", VK_PAUSE, 0},
    {L"Home", VK_HOME, 0}, {L"PgDn", VK_NEXT, 0}, {L"End", VK_END, 0},
    {L"PrtScn", VK_SNAPSHOT, 0}
};

// Add to Globals:
HWND g_hLastActiveWnd = NULL;

void SetKeyHardwareState(WORD vk, BOOL bDown);
// --- Custom Draw Helpers ---

void DrawSolidRect(HDC hdc, int x, int y, int w, int h, COLORREF col) {
    RECT r = {x, y, x+w, y+h};
    HBRUSH br = CreateSolidBrush(col);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

void DrawSquareRect(HDC hdc, int x, int y, int w, int h, COLORREF col) {
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    Rectangle(hdc, x, y, x+w, y+h);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

// --- Tray Icon ---

HICON CreateKeyboardTrayIcon() {
    HDC hScreenDC = GetDC(NULL);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    DWORD* pPixels = NULL;
    HBITMAP hbmp = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&pPixels, NULL, 0);

    if (pPixels) {
        memset(pPixels, 0, 16 * 16 * 4);
        DWORD colEdge = 0xFF444444;
        DWORD colKey = 0xFFFFFFFF;

        for (int y = 3; y <= 12; y++) {
            for (int x = 1; x <= 14; x++) {
                BOOL border = (y==3 || y==12 || x==1 || x==14);
                if (border) pPixels[y * 16 + x] = colEdge;
                else {
                    if ((x%3 != 0) && (y%2 != 0)) pPixels[y * 16 + x] = colKey;
                    else pPixels[y * 16 + x] = colEdge;
                }
            }
        }
    }

    BYTE maskBits[32] = {0};
    HBITMAP hbmMask = CreateBitmap(16, 16, 1, 1, maskBits);
    ICONINFO ii = { TRUE, 0, 0, hbmMask, hbmp };
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmMask);
    if (hbmp) DeleteObject(hbmp);
    ReleaseDC(NULL, hScreenDC);
    if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);
    return hIcon;
}

// Helper to ensure the target window is active before sending input
void EnsureTargetFocus() {
    HWND hFg = GetForegroundWindow();
    if ((!hFg || hFg == hMainWnd) && g_hLastActiveWnd && IsWindow(g_hLastActiveWnd)) {
        SetForegroundWindow(g_hLastActiveWnd);
    }
}

// --- Positioning Logic ---

void PositionKeyboard() {
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    // WINPE FAILSAFE: If SPI_GETWORKAREA fails or returns 0, use physical screen
    if (wa.bottom <= 0 || wa.right <= 0) {
        wa.left = 0; wa.top = 0;
        wa.right = w; wa.bottom = h;
    }

    if (g_oskHeight < 180) g_oskHeight = 180;
    if (g_oskHeight > h - 100) g_oskHeight = h - 100;

    if (g_floatingY == -1) g_floatingY = wa.bottom - g_oskHeight;

    int y = 0;
    if (g_bWindowMode) {
        y = g_floatingY;
    } else if (g_bDockTop) {
        y = wa.top;
    } else {
        y = wa.bottom - g_oskHeight;
    }

    // Keep safely on screen bounds
    if (y < 0) y = 0;
    if (y > h - g_oskHeight) y = h - g_oskHeight;
    if (g_bWindowMode) g_floatingY = y;

    SetWindowPos(hMainWnd, HWND_TOPMOST, wa.left, y, w, g_oskHeight, SWP_NOACTIVATE | SWP_NOZORDER);

    // Apply strict square edge region to window
    HRGN hRgn = CreateRectRgn(0, 0, w, g_oskHeight);
    SetWindowRgn(hMainWnd, hRgn, TRUE);
}

void SyncStickyModifiers() {
    static BOOL s_shiftDown = FALSE;
    static BOOL s_lctrlDown = FALSE;
    static BOOL s_rctrlDown = FALSE;
    static BOOL s_raltDown  = FALSE;
    static BOOL s_winDown   = FALSE;

    if (g_bShiftLock != s_shiftDown) {
        SetKeyHardwareState(VK_LSHIFT, g_bShiftLock);
        s_shiftDown = g_bShiftLock;
    }
    if (g_bLeftCtrlLock != s_lctrlDown) {
        SetKeyHardwareState(VK_LCONTROL, g_bLeftCtrlLock);
        s_lctrlDown = g_bLeftCtrlLock;
    }
    if (g_bRightCtrlLock != s_rctrlDown) {
        SetKeyHardwareState(VK_RCONTROL, g_bRightCtrlLock);
        s_rctrlDown = g_bRightCtrlLock;
    }
    if (g_bRightAltLock != s_raltDown) {
        SetKeyHardwareState(VK_RMENU, g_bRightAltLock);
        s_raltDown = g_bRightAltLock;
    }
    if (g_bWinLock != s_winDown) {
        SetKeyHardwareState(VK_LWIN, g_bWinLock);
        s_winDown = g_bWinLock;
    }
}

void SetKeyHardwareState(WORD vk, BOOL bDown) {
    INPUT in = {0};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyA(vk, 0);
    in.ki.dwFlags = bDown ? 0 : KEYEVENTF_KEYUP;
    if (vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN) {
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    SendInput(1, &in, sizeof(INPUT));
}

void ShowOsk() {
    if (g_bIsVisible) return;
    g_bIsVisible = TRUE;
    PositionKeyboard();
    ShowWindow(hMainWnd, SW_SHOWNOACTIVATE);
    InvalidateRect(hMainWnd, NULL, FALSE);
}

void HideOsk() {
    if (!g_bIsVisible) return;
    g_bIsVisible = FALSE;
    ShowWindow(hMainWnd, SW_HIDE);
}

// --- Focus Tracking ---

BOOL IsTextInput(HWND hwnd) {
    if (!hwnd) return FALSE;
    char szClass[256] = {0};
    GetClassNameA(hwnd, szClass, 256);
    for (int i = 0; szClass[i]; i++) szClass[i] = tolower(szClass[i]);

    if (strstr(szClass, "edit") ||
        strstr(szClass, "console") ||
        strstr(szClass, "scintilla") ||
        strstr(szClass, "chrome") ||
        strstr(szClass, "mozilla") ||
        strstr(szClass, "combo") ||
        strstr(szClass, "search") ||
        strstr(szClass, "wordpad") ||
        strstr(szClass, "rich")) {
        return TRUE;
    }
    return FALSE;
}

// Updated CheckFocus: tracks the last valid foreground window even when g_bManualOverride is TRUE
void CheckFocus() {
    HWND hFg = GetForegroundWindow();
    if (hFg && hFg != hMainWnd && IsWindowVisible(hFg)) {
        g_hLastActiveWnd = hFg;
    }

    if (!g_bAutoPopup || g_bManualOverride) return;

    if (!hFg || hFg == hMainWnd) return;

    if (g_hSuppressedWindow && hFg != g_hSuppressedWindow) {
        g_hSuppressedWindow = NULL;
    }

    if (hFg == g_hSuppressedWindow) {
        HideOsk();
        return;
    }

    BOOL bNeedsOsk = FALSE;
    DWORD pid;
    DWORD tid = GetWindowThreadProcessId(hFg, &pid);

    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
    if (GetGUIThreadInfo(tid, &gti)) {
        if (gti.hwndCaret != NULL) bNeedsOsk = TRUE;
        else if (IsTextInput(gti.hwndFocus)) bNeedsOsk = TRUE;
    } else {
        if (IsTextInput(hFg)) bNeedsOsk = TRUE;
    }

    if (bNeedsOsk) ShowOsk();
    else HideOsk();
}

// --- Settings Menu ---

void ShowSettingsMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING | ((!g_bWindowMode && !g_bDockTop) ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_DOCK_BOTTOM, "Dock Bottom");
    AppendMenuA(hMenu, MF_STRING | ((!g_bWindowMode && g_bDockTop) ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_DOCK_TOP, "Dock Top");
    AppendMenuA(hMenu, MF_STRING | (g_bWindowMode ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_WINDOW_MODE, "Floating Mode");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_oskHeight == 240 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_H_SMALL, "Height: Compact (240px)");
    AppendMenuA(hMenu, MF_STRING | (g_oskHeight == 320 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_H_MED, "Height: Normal (320px)");
    AppendMenuA(hMenu, MF_STRING | (g_oskHeight == 400 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_H_LARGE, "Height: Tall (400px)");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_bAutoPopup ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_AUTO_POPUP, "Auto Popup Keyboard");
    AppendMenuA(hMenu, MF_STRING | (g_bSkipUndocked ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_SKIP_UNDOCKED, "Skip Undocked Position");
    AppendMenuA(hMenu, MF_STRING | (g_bAutoRepeat ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_AUTO_REPEAT, "Auto Repeat Keys");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_bStickyLCtrl ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_STICKY_LCTRL, "Sticky Left Ctrl");
    AppendMenuA(hMenu, MF_STRING | (g_bStickyLShift ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_STICKY_LSHIFT, "Sticky Left Shift");
    AppendMenuA(hMenu, MF_STRING | (g_bStickyWin ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_STICKY_WIN, "Sticky Win Key");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit Keyboard");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// --- Input Injection ---

void SendPasteClipboard() {
    INPUT inputs[4] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, inputs, sizeof(INPUT));
}

void SendUnicodeChar(wchar_t ch) {
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = ch;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));
}

BOOL IsRepeatableKey(int keyIdx) {
    if (keyIdx < 0 || keyIdx >= numKeys) return FALSE;
    if (g_bFnLock && fnLayer[keyIdx].label) {
        if (fnLayer[keyIdx].ch != 0) return TRUE;
        int fvk = fnLayer[keyIdx].vk;
        if (fvk == VK_SNAPSHOT || fvk == VK_SCROLL || fvk == VK_PAUSE || fvk == VK_APPS) return FALSE;
        return TRUE;
    }
    int vk = layout[keyIdx].vk;
    if (vk == VK_OSK_FN || vk == VK_OSK_SETTINGS || vk == VK_CAPITAL) return FALSE;
    if (vk == VK_SHIFT && g_bStickyLShift) return FALSE;
    if (vk == VK_RSHIFT) return FALSE;
    if (vk == VK_CONTROL && g_bStickyLCtrl) return FALSE;
    if (vk == VK_RCONTROL || vk == VK_RMENU) return FALSE;
    if (vk == VK_LWIN && g_bStickyWin) return FALSE;
    return TRUE;
}

void StopKeyRepeat(HWND hwnd) {
    KillTimer(hwnd, ID_TIMER_REPEAT_DELAY);
    KillTimer(hwnd, ID_TIMER_REPEAT_RATE);
}

void SendKeystroke(int vkCode) {
    EnsureTargetFocus();

    // Handle Caps Lock inversion temporarily if Shift isn't already held
    BOOL bTempCapsShift = (g_bCaps && !g_bShiftLock && vkCode >= 'A' && vkCode <= 'Z');
    if (bTempCapsShift) {
        SetKeyHardwareState(VK_LSHIFT, TRUE);
    }

    DWORD extFlag = 0;
    if (vkCode == VK_UP || vkCode == VK_DOWN || vkCode == VK_LEFT || vkCode == VK_RIGHT ||
        vkCode == VK_PRIOR || vkCode == VK_NEXT || vkCode == VK_HOME || vkCode == VK_END ||
        vkCode == VK_INSERT || vkCode == VK_DELETE || vkCode == VK_SNAPSHOT ||
        vkCode == VK_APPS || vkCode == VK_LWIN) {
        extFlag = KEYEVENTF_EXTENDEDKEY;
    }

    WORD targetScan = (WORD)MapVirtualKeyA(vkCode, 0);
    INPUT inputs[2] = {0};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vkCode;
    inputs[0].ki.wScan = targetScan;
    inputs[0].ki.dwFlags = extFlag;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vkCode;
    inputs[1].ki.wScan = targetScan;
    inputs[1].ki.dwFlags = extFlag | KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));

    if (bTempCapsShift) {
        SetKeyHardwareState(VK_LSHIFT, FALSE);
    }

    // Release one-shot sticky modifiers after a non-modifier key is pressed
    BOOL bNeedsInvalidate = FALSE;
    if (g_bShiftLock && vkCode != VK_SHIFT && vkCode != VK_LSHIFT && vkCode != VK_RSHIFT && vkCode != VK_CAPITAL) {
        g_bShiftLock = FALSE;
        bNeedsInvalidate = TRUE;
    }
    if (g_bLeftCtrlLock && vkCode != VK_CONTROL && vkCode != VK_LCONTROL) {
        g_bLeftCtrlLock = FALSE;
        bNeedsInvalidate = TRUE;
    }
    if (g_bRightCtrlLock && vkCode != VK_RCONTROL) {
        g_bRightCtrlLock = FALSE;
        bNeedsInvalidate = TRUE;
    }
    if (g_bRightAltLock && vkCode != VK_RMENU) {
        g_bRightAltLock = FALSE;
        bNeedsInvalidate = TRUE;
    }
    if (g_bWinLock && vkCode != VK_LWIN) {
        g_bWinLock = FALSE;
        bNeedsInvalidate = TRUE;
    }

    if (bNeedsInvalidate) {
        SyncStickyModifiers();
        InvalidateRect(hMainWnd, NULL, FALSE);
    }
}


void SendKeyByIndex(int keyIdx) {
    if (g_bFnLock && fnLayer[keyIdx].label) {
        if (fnLayer[keyIdx].vk != 0) SendKeystroke(fnLayer[keyIdx].vk);
        else if (fnLayer[keyIdx].ch != 0) SendUnicodeChar(fnLayer[keyIdx].ch);
        return;
    }
    SendKeystroke(layout[keyIdx].vk);
}

// --- Hit Testing ---

BOOL IsInGrabBar(int my) {
    if (!g_bWindowMode && g_bDockTop) return (my >= g_oskHeight - GRAB_BAR_HEIGHT);
    else return (my <= GRAB_BAR_HEIGHT);
}

// --- UI Rendering ---

void PaintUI(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(memDC, memBitmap);

    DrawSolidRect(memDC, 0, 0, w, h, CLR_BG);
    SetBkMode(memDC, TRANSPARENT);

    // Grab Bar Position
    RECT grabRc = {0, 0, w, GRAB_BAR_HEIGHT};
    if (!g_bWindowMode && g_bDockTop) {
        grabRc.top = h - GRAB_BAR_HEIGHT;
        grabRc.bottom = h;
    }

    DrawSolidRect(memDC, grabRc.left, grabRc.top, w, GRAB_BAR_HEIGHT, CLR_BAR);
    SetTextColor(memDC, CLR_TEXT_MUT);
    SelectObject(memDC, hFontSmall);

    const char* barText = "";
    if (!g_bWindowMode) {
        barText = g_bDockTop ? "v   Docked Top   v" : "^   Docked Bottom   ^";
    }
    DrawTextA(memDC, barText, -1, &grabRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Render "Hide" Button at 1/4th width
    int hideX = w / 4;
    RECT hideRc = { hideX - 40, grabRc.top, hideX + 40, grabRc.bottom };

    if (g_bHoverHide) {
        DrawSolidRect(memDC, hideRc.left, hideRc.top, hideRc.right - hideRc.left, GRAB_BAR_HEIGHT, RGB(180, 185, 195));
    }
    SetTextColor(memDC, RGB(40, 40, 40));
    DrawTextA(memDC, "Hide", -1, &hideRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Render "Paste Clipboard" Button at 3/4th width when clipboard has content
    if (g_bHasClipboard) {
        int pasteX = (w * 3) / 4;
        RECT pasteRc = { pasteX - 65, grabRc.top, pasteX + 65, grabRc.bottom };
        if (g_bHoverPaste) {
            DrawSolidRect(memDC, pasteRc.left, pasteRc.top, pasteRc.right - pasteRc.left, GRAB_BAR_HEIGHT, RGB(180, 185, 195));
        }
        SetTextColor(memDC, CLR_ACCENT);
        DrawTextA(memDC, "Paste Clipboard", -1, &pasteRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (!g_bWindowMode && g_bDockTop) DrawSolidRect(memDC, 0, grabRc.top, w, 1, RGB(180, 185, 190));
    else DrawSolidRect(memDC, 0, GRAB_BAR_HEIGHT - 1, w, 1, RGB(180, 185, 190));

    // Keyboard Grid
    int padX = 8, padY = 8;
    int keyMargin = 4;
    int rowCount = 5;

    int keysAreaHeight = h - GRAB_BAR_HEIGHT - (padY * 2);
    int keyH = (keysAreaHeight - (keyMargin * (rowCount - 1))) / rowCount;
    float unitW = (float)(w - (padX * 2)) / TOTAL_UNITS;

    int curY = (!g_bWindowMode && g_bDockTop) ? padY : (GRAB_BAR_HEIGHT + padY);
    int rowStartKeys[] = {0, 15, 29, 42, 55, numKeys};

    for (int r = 0; r < rowCount; r++) {
        float curX = padX;
        for (int i = rowStartKeys[r]; i < rowStartKeys[r+1]; i++) {
            float kw = (layout[i].widthUnits * unitW) - keyMargin;

            layout[i].rect.left = (int)curX;
            layout[i].rect.top = curY;
            layout[i].rect.right = (int)(curX + kw);
            layout[i].rect.bottom = curY + keyH;

            COLORREF kCol = layout[i].isMod ? CLR_KEY_MOD : CLR_KEY_BG;
            if (i == g_downKey) kCol = CLR_KEY_DOWN;
            else if (i == g_hoverKey) kCol = CLR_KEY_HOVER;

            BOOL isAccent = ((layout[i].vk == VK_SHIFT || layout[i].vk == VK_RSHIFT) && g_bShiftLock) ||
                            (layout[i].vk == VK_CAPITAL && g_bCaps && !g_bFnLock) ||
                            (layout[i].vk == VK_CONTROL && g_bLeftCtrlLock) ||
                            (layout[i].vk == VK_RCONTROL && g_bRightCtrlLock && !g_bFnLock) ||
                            (layout[i].vk == VK_RMENU && g_bRightAltLock && !g_bFnLock) ||
                            (layout[i].vk == VK_LWIN && g_bWinLock) ||
                            (layout[i].vk == VK_OSK_FN && g_bFnLock);

            if (isAccent) kCol = CLR_ACCENT;

            DrawSquareRect(memDC, layout[i].rect.left, layout[i].rect.top,
                         layout[i].rect.right - layout[i].rect.left,
                         layout[i].rect.bottom - layout[i].rect.top, kCol);

            SetTextColor(memDC, isAccent ? RGB(255, 255, 255) : CLR_TEXT);

            RECT textRc = layout[i].rect;
            if (g_bFnLock && fnLayer[i].label) {
                BOOL isMultiChar = (fnLayer[i].label[1] != 0);
                SelectObject(memDC, isMultiChar ? hFontSmall : hFontNormal);
                DrawTextW(memDC, fnLayer[i].label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                SelectObject(memDC, layout[i].isMod ? hFontSmall : hFontNormal);
                const char* text = (g_bShiftLock || g_bCaps) ? layout[i].shift : layout[i].norm;
                DrawTextA(memDC, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            curX += (layout[i].widthUnits * unitW);
        }
        curY += keyH + keyMargin;
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hFontNormal = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontSmall  = CreateFontA(16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            g_bHasClipboard = (IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT));
            SetTimer(hwnd, ID_TIMER_FOCUS, 300, NULL);
            break;
        }

        case WM_DISPLAYCHANGE: {
            PositionKeyboard();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_TIMER:
            if (wParam == ID_TIMER_FOCUS) {
                if (!g_bTrayIconAdded) {
                    g_bTrayIconAdded = Shell_NotifyIcon(NIM_ADD, &nid);
                }
                BOOL hasClip = (IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT));
                if (hasClip != g_bHasClipboard) {
                    g_bHasClipboard = hasClip;
                    if (!g_bHasClipboard) g_bHoverPaste = FALSE;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                CheckFocus();
            } else if (wParam == ID_TIMER_REPEAT_DELAY) {
                KillTimer(hwnd, ID_TIMER_REPEAT_DELAY);
                if (g_downKey != -1 && g_bAutoRepeat && IsRepeatableKey(g_downKey)) {
                    g_bDidAutoRepeat = TRUE;
                    SendKeyByIndex(g_downKey);
                    SetTimer(hwnd, ID_TIMER_REPEAT_RATE, 60, NULL);
                }
            } else if (wParam == ID_TIMER_REPEAT_RATE) {
                if (g_downKey != -1 && g_bAutoRepeat && IsRepeatableKey(g_downKey)) {
                    SendKeyByIndex(g_downKey);
                } else {
                    StopKeyRepeat(hwnd);
                }
            }
            break;

        case WM_PAINT:
            PaintUI(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_MOUSEMOVE: {
            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);

            if (g_bGrabTracking) {
                POINT pt; GetCursorPos(&pt);
                if (!g_bIsDragging) {
                    if (abs(pt.x - g_ptGrabStart.x) > 4 || abs(pt.y - g_ptGrabStart.y) > 4) {
                        g_bIsDragging = TRUE;
                        if (!g_bWindowMode) {
                            g_bWindowMode = TRUE;
                            g_floatingY = g_rcWindowStart.top;
                            PositionKeyboard();
                        }
                    }
                }

                if (g_bIsDragging && g_bWindowMode) {
                    g_floatingY = g_rcWindowStart.top + (pt.y - g_ptGrabStart.y);
                    PositionKeyboard();
                }
                return 0;
            }

            // Stop auto-repeat if pointer drags off the held key
            if (g_downKey != -1) {
                if (mx < layout[g_downKey].rect.left || mx > layout[g_downKey].rect.right ||
                    my < layout[g_downKey].rect.top || my > layout[g_downKey].rect.bottom) {
                    StopKeyRepeat(hwnd);
                }
            }

            // Hover logic for Grab Bar buttons (Hide & Paste Clipboard)
            BOOL oldHoverHide = g_bHoverHide;
            BOOL oldHoverPaste = g_bHoverPaste;
            g_bHoverHide = FALSE;
            g_bHoverPaste = FALSE;

            if (IsInGrabBar(my)) {
                RECT rc; GetClientRect(hwnd, &rc);
                int w = rc.right;
                int hideX = w / 4;
                int pasteX = (w * 3) / 4;
                if (mx >= hideX - 40 && mx <= hideX + 40) {
                    g_bHoverHide = TRUE;
                } else if (g_bHasClipboard && mx >= pasteX - 65 && mx <= pasteX + 65) {
                    g_bHoverPaste = TRUE;
                }
            }

            // Hover logic for keys
            int oldHover = g_hoverKey;
            g_hoverKey = -1;
            if (!IsInGrabBar(my)) {
                for (int i = 0; i < numKeys; i++) {
                    if (mx >= layout[i].rect.left && mx <= layout[i].rect.right &&
                        my >= layout[i].rect.top && my <= layout[i].rect.bottom) {
                        g_hoverKey = i;
                        break;
                    }
                }
            }

            if (oldHoverHide != g_bHoverHide || oldHoverPaste != g_bHoverPaste || oldHover != g_hoverKey || g_downKey != -1) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);

            if (IsInGrabBar(my)) {
                RECT rc; GetClientRect(hwnd, &rc);
                int w = rc.right;
                int hideX = w / 4;
                int pasteX = (w * 3) / 4;

                if (mx >= hideX - 40 && mx <= hideX + 40) {
                    g_bManualOverride = FALSE;
                    g_hSuppressedWindow = GetForegroundWindow();
                    HideOsk();
                    return 0;
                }

                if (g_bHasClipboard && mx >= pasteX - 65 && mx <= pasteX + 65) {
                    EnsureTargetFocus();
                    SendPasteClipboard();
                    return 0;
                }

                g_bGrabTracking = TRUE;
                g_bIsDragging = FALSE;
                GetCursorPos(&g_ptGrabStart);
                GetWindowRect(hwnd, &g_rcWindowStart);
                SetCapture(hwnd);
                return 0;
            }

            for (int i = 0; i < numKeys; i++) {
                if (mx >= layout[i].rect.left && mx <= layout[i].rect.right &&
                    my >= layout[i].rect.top && my <= layout[i].rect.bottom) {
                    g_downKey = i;
                    g_bDidAutoRepeat = FALSE;
                    SetCapture(hwnd);
                    if (g_bAutoRepeat && IsRepeatableKey(i)) {
                        SetTimer(hwnd, ID_TIMER_REPEAT_DELAY, 450, NULL);
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_bGrabTracking) {
                g_bGrabTracking = FALSE;
                ReleaseCapture();

                if (!g_bIsDragging) {
                    if (g_bSkipUndocked) {
                        g_bWindowMode = FALSE;
                        g_bDockTop = !g_bDockTop;
                    } else {
                        if (!g_bWindowMode && !g_bDockTop) {
                            g_bDockTop = TRUE;
                        } else if (!g_bWindowMode && g_bDockTop) {
                            g_bWindowMode = TRUE;
                            RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
                            g_floatingY = (wa.bottom - wa.top) / 2 - (g_oskHeight / 2);
                        } else {
                            g_bWindowMode = FALSE;
                            g_bDockTop = FALSE;
                        }
                    }
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                g_bIsDragging = FALSE;
                return 0;
            }

            if (g_downKey != -1) {
                int mx = (short)LOWORD(lParam);
                int my = (short)HIWORD(lParam);
                int keyIdx = g_downKey;
                BOOL didRepeat = g_bDidAutoRepeat;

                StopKeyRepeat(hwnd);
                ReleaseCapture();
                g_downKey = -1;
                g_bDidAutoRepeat = FALSE;

                if (!didRepeat &&
                    mx >= layout[keyIdx].rect.left && mx <= layout[keyIdx].rect.right &&
                    my >= layout[keyIdx].rect.top && my <= layout[keyIdx].rect.bottom) {

                    if (g_bFnLock && fnLayer[keyIdx].label) {
                        SendKeyByIndex(keyIdx);
                    } else {
                        int vk = layout[keyIdx].vk;

                        if (vk == VK_SHIFT) {
                            if (g_bStickyLShift) {
                                g_bShiftLock = !g_bShiftLock;
                                EnsureTargetFocus();
                                SyncStickyModifiers();
                            } else {
                                SendKeystroke(vk);
                            }
                        } else if (vk == VK_RSHIFT) {
                            g_bShiftLock = !g_bShiftLock;
                            EnsureTargetFocus();
                            SyncStickyModifiers();
                        } else if (vk == VK_CAPITAL) {
                            g_bCaps = !g_bCaps;
                        } else if (vk == VK_CONTROL) {
                            if (g_bStickyLCtrl) {
                                g_bLeftCtrlLock = !g_bLeftCtrlLock;
                                EnsureTargetFocus();
                                SyncStickyModifiers();
                            } else {
                                SendKeystroke(vk);
                            }
                        } else if (vk == VK_RCONTROL) {
                            g_bRightCtrlLock = !g_bRightCtrlLock;
                            EnsureTargetFocus();
                            SyncStickyModifiers();
                        } else if (vk == VK_RMENU) {
                            g_bRightAltLock = !g_bRightAltLock;
                            EnsureTargetFocus();
                            SyncStickyModifiers();
                        } else if (vk == VK_LWIN) {
                            if (g_bStickyWin) {
                                g_bWinLock = !g_bWinLock;
                                EnsureTargetFocus();
                                SyncStickyModifiers();
                            } else {
                                SendKeystroke(vk);
                            }
                        } else if (vk == VK_OSK_FN) {
                            g_bFnLock = !g_bFnLock;
                        } else if (vk == VK_OSK_SETTINGS) {
                            POINT pt = { layout[keyIdx].rect.right, layout[keyIdx].rect.top };
                            ClientToScreen(hwnd, &pt);
                            InvalidateRect(hwnd, NULL, FALSE);
                            ShowSettingsMenu(hwnd, pt);
                            return 0;
                        } else {
                            SendKeystroke(vk);
                        }
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_CAPTURECHANGED: {
            StopKeyRepeat(hwnd);
            g_bGrabTracking = FALSE;
            g_bIsDragging = FALSE;
            if (g_downKey != -1) {
                g_downKey = -1;
                g_bDidAutoRepeat = FALSE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcA(hwnd, uMsg, wParam, lParam);
            return (hit == HTCLIENT) ? HTCLIENT : hit;
        }

        case WM_USER_SHOW_MANAGER: {
            g_bManualOverride = TRUE;
            ShowOsk();
            if (!g_bTrayIconAdded) {
                g_bTrayIconAdded = Shell_NotifyIcon(NIM_ADD, &nid);
            }
            break;
        }

        case WM_USER_TRAY: {
            if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                ShowSettingsMenu(hwnd, pt);
            }
            else if (lParam == WM_LBUTTONUP) {
                if (g_bIsVisible) {
                    g_bManualOverride = FALSE;
                    HideOsk();
                } else {
                    g_bManualOverride = TRUE;
                    ShowOsk();
                }
            }
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
                case ID_TRAY_WINDOW_MODE:
                    g_bWindowMode = !g_bWindowMode;
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case ID_TRAY_DOCK_BOTTOM:
                    g_bWindowMode = FALSE;
                    g_bDockTop = FALSE;
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case ID_TRAY_DOCK_TOP:
                    g_bWindowMode = FALSE;
                    g_bDockTop = TRUE;
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case ID_TRAY_H_SMALL:
                    g_oskHeight = 240;
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case ID_TRAY_H_MED:
                    g_oskHeight = 320;
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case ID_TRAY_H_LARGE:
                    g_oskHeight = 400;
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case ID_TRAY_AUTO_POPUP:
                    g_bAutoPopup = !g_bAutoPopup;
                    break;
                case ID_TRAY_SKIP_UNDOCKED:
                    g_bSkipUndocked = !g_bSkipUndocked;
                    break;
                case ID_TRAY_AUTO_REPEAT:
                    g_bAutoRepeat = !g_bAutoRepeat;
                    if (!g_bAutoRepeat) StopKeyRepeat(hwnd);
                    break;
                case ID_TRAY_STICKY_LCTRL:
                    g_bStickyLCtrl = !g_bStickyLCtrl;
                    if (!g_bStickyLCtrl && g_bLeftCtrlLock) {
                        g_bLeftCtrlLock = FALSE;
                        SyncStickyModifiers();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;
                case ID_TRAY_STICKY_LSHIFT:
                    g_bStickyLShift = !g_bStickyLShift;
                    if (!g_bStickyLShift && g_bShiftLock) {
                        g_bShiftLock = FALSE;
                        SyncStickyModifiers();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;
                case ID_TRAY_STICKY_WIN:
                    g_bStickyWin = !g_bStickyWin;
                    if (!g_bStickyWin && g_bWinLock) {
                        g_bWinLock = FALSE;
                        SyncStickyModifiers();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    break;
            }
            break;
        }

        case WM_DESTROY:
            StopKeyRepeat(hwnd);
            g_bShiftLock = FALSE;
            g_bLeftCtrlLock = FALSE;
            g_bRightCtrlLock = FALSE;
            g_bRightAltLock = FALSE;
            g_bWinLock = FALSE;
            SyncStickyModifiers();
            Shell_NotifyIcon(NIM_DELETE, &nid);
            DeleteObject(hFontNormal); DeleteObject(hFontSmall);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
// --- Main ---

int main(int argc, char *argv[]) {
    // Single Instance Guard
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "WinPE_OSK_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowA("WinPEOSKWin11", NULL);
        if (hExisting) PostMessageA(hExisting, WM_USER_SHOW_MANAGER, 0, 0);
        return 0;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WinPEOSKWin11";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    // WS_EX_NOACTIVATE is CRITICAL so the keyboard doesn't steal focus
    hMainWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "WinPEOSKWin11", "",
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), g_oskHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!hMainWnd) return 1;

    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hMainWnd;
    nid.uID = 4;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TRAY;
    nid.hIcon = CreateKeyboardTrayIcon();
    wcscpy(nid.szTip, L"On-Screen Keyboard");

    g_bTrayIconAdded = Shell_NotifyIcon(NIM_ADD, &nid);

    // Start completely visible so the user knows it launched successfully
    ShowOsk();

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return 0;
}