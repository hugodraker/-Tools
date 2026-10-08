/*
 * File: osk.c
 * Description: Win32 Native On-Screen Keyboard for Windows LiveCD / WinPE.
 *              - English QWERTY Layout with crisp Square Keys.
 *              - Floating Mode: Grab Bar is always at the top.
 *              - Timer-based polling engine (bypasses WinEventHook hangs).
 *              - Explicit "Hide" button 1/4th across the grab bar to close stubborn instances.
 *              - Starts visible immediately (Manual Override mode).
 *              - WM_DISPLAYCHANGE hook automatically scales width on rotation.
 *              - Clickable grab bar cycles: Dock Bottom -> Dock Top -> Floating.
 *              - Dragging the grab bar instantly converts to Floating Mode.
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

#define WM_USER_TRAY         (WM_USER + 1)
#define WM_USER_SHOW_MANAGER (WM_USER + 2)
#define ID_TRAY_EXIT         9999
#define ID_TRAY_WINDOW_MODE  9998

#define ID_TIMER_FOCUS       1

#define GRAB_BAR_HEIGHT      28

// --- Globals ---
HWND hMainWnd = NULL;
NOTIFYICONDATA nid;

HFONT hFontNormal, hFontSmall;

// Settings 
int g_oskHeight = 320;
BOOL g_bWindowMode = FALSE;
BOOL g_bDockTop = FALSE;
int g_floatingY = -1; 

// State & Dragging
BOOL g_bIsVisible = FALSE;
BOOL g_bManualOverride = TRUE; // Start visible immediately on launch
BOOL g_bTrayIconAdded = FALSE;
HWND g_hSuppressedWindow = NULL;

BOOL g_bShiftLock = FALSE;
BOOL g_bCaps = FALSE;
int g_hoverKey = -1;
int g_downKey = -1;

BOOL g_bHoverHide = FALSE; // Tracks hover state for the Hide label

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

#define TOTAL_UNITS 150

KeyDef layout[] = {
    {"`", "~", VK_OEM_3, 10, FALSE, {0}}, {"1", "!", '1', 10, FALSE, {0}}, {"2", "@", '2', 10, FALSE, {0}}, 
    {"3", "#", '3', 10, FALSE, {0}}, {"4", "$", '4', 10, FALSE, {0}}, {"5", "%", '5', 10, FALSE, {0}}, 
    {"6", "^", '6', 10, FALSE, {0}}, {"7", "&", '7', 10, FALSE, {0}}, {"8", "*", '8', 10, FALSE, {0}}, 
    {"9", "(", '9', 10, FALSE, {0}}, {"0", ")", '0', 10, FALSE, {0}}, {"-", "_", VK_OEM_MINUS, 10, FALSE, {0}}, 
    {"=", "+", VK_OEM_PLUS, 10, FALSE, {0}}, {"Backspace", "Backspace", VK_BACK, 20, TRUE, {0}},

    {"Tab", "Tab", VK_TAB, 15, TRUE, {0}}, {"q", "Q", 'Q', 10, FALSE, {0}}, {"w", "W", 'W', 10, FALSE, {0}}, 
    {"e", "E", 'E', 10, FALSE, {0}}, {"r", "R", 'R', 10, FALSE, {0}}, {"t", "T", 'T', 10, FALSE, {0}}, 
    {"y", "Y", 'Y', 10, FALSE, {0}}, {"u", "U", 'U', 10, FALSE, {0}}, {"i", "I", 'I', 10, FALSE, {0}}, 
    {"o", "O", 'O', 10, FALSE, {0}}, {"p", "P", 'P', 10, FALSE, {0}}, {"[", "{", VK_OEM_4, 10, FALSE, {0}}, 
    {"]", "}", VK_OEM_6, 10, FALSE, {0}}, {"\\", "|", VK_OEM_5, 15, FALSE, {0}},

    {"Caps", "Caps", VK_CAPITAL, 18, TRUE, {0}}, {"a", "A", 'A', 10, FALSE, {0}}, {"s", "S", 'S', 10, FALSE, {0}}, 
    {"d", "D", 'D', 10, FALSE, {0}}, {"f", "F", 'F', 10, FALSE, {0}}, {"g", "G", 'G', 10, FALSE, {0}}, 
    {"h", "H", 'H', 10, FALSE, {0}}, {"j", "J", 'J', 10, FALSE, {0}}, {"k", "K", 'K', 10, FALSE, {0}}, 
    {"l", "L", 'L', 10, FALSE, {0}}, {";", ":", VK_OEM_1, 10, FALSE, {0}}, {"'", "\"", VK_OEM_7, 10, FALSE, {0}}, 
    {"Enter", "Enter", VK_RETURN, 22, TRUE, {0}},

    {"Shift", "Shift", VK_SHIFT, 23, TRUE, {0}}, {"z", "Z", 'Z', 10, FALSE, {0}}, {"x", "X", 'X', 10, FALSE, {0}}, 
    {"c", "C", 'C', 10, FALSE, {0}}, {"v", "V", 'V', 10, FALSE, {0}}, {"b", "B", 'B', 10, FALSE, {0}}, 
    {"n", "N", 'N', 10, FALSE, {0}}, {"m", "M", 'M', 10, FALSE, {0}}, {",", "<", VK_OEM_COMMA, 10, FALSE, {0}}, 
    {".", ">", VK_OEM_PERIOD, 10, FALSE, {0}}, {"/", "?", VK_OEM_2, 10, FALSE, {0}}, {"Shift", "Shift", VK_RSHIFT, 27, TRUE, {0}},

    {"Ctrl", "Ctrl", VK_CONTROL, 15, TRUE, {0}}, {"Win", "Win", VK_LWIN, 15, TRUE, {0}}, {"Alt", "Alt", VK_MENU, 15, TRUE, {0}}, 
    {"Space", "Space", VK_SPACE, 60, FALSE, {0}}, 
    {"Alt", "Alt", VK_RMENU, 15, TRUE, {0}}, {"Menu", "Menu", VK_APPS, 15, TRUE, {0}}, {"Ctrl", "Ctrl", VK_RCONTROL, 15, TRUE, {0}}
};

int numKeys = sizeof(layout) / sizeof(KeyDef);

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
    for(int i=0; szClass[i]; i++) szClass[i] = tolower(szClass[i]);

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

void CheckFocus() {
    if (g_bManualOverride) return; // Keep visible if user manually forced it open

    HWND hFg = GetForegroundWindow();
    
    // If no window is active, or if our OSK is active, don't change state
    if (!hFg || hFg == hMainWnd) return; 
    
    // Clear the suppression if focus changes to a different window
    if (g_hSuppressedWindow && hFg != g_hSuppressedWindow) {
        g_hSuppressedWindow = NULL;
    }

    // If the current window is suppressed, keep the OSK hidden
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

// --- Input Injection ---

void SendKeystroke(int vkCode) {
    BOOL bNeedShift = g_bShiftLock;

    if (g_bCaps && vkCode >= 'A' && vkCode <= 'Z') {
        bNeedShift = !bNeedShift;
    }

    int inputsCount = 0;
    INPUT inputs[4] = {0};

    if (bNeedShift && vkCode != VK_SHIFT && vkCode != VK_LSHIFT && vkCode != VK_RSHIFT) {
        inputs[inputsCount].type = INPUT_KEYBOARD;
        inputs[inputsCount].ki.wVk = VK_SHIFT;
        inputsCount++;
    }

    inputs[inputsCount].type = INPUT_KEYBOARD;
    inputs[inputsCount].ki.wVk = vkCode;
    inputsCount++;

    inputs[inputsCount].type = INPUT_KEYBOARD;
    inputs[inputsCount].ki.wVk = vkCode;
    inputs[inputsCount].ki.dwFlags = KEYEVENTF_KEYUP;
    inputsCount++;

    if (bNeedShift && vkCode != VK_SHIFT && vkCode != VK_LSHIFT && vkCode != VK_RSHIFT) {
        inputs[inputsCount].type = INPUT_KEYBOARD;
        inputs[inputsCount].ki.wVk = VK_SHIFT;
        inputs[inputsCount].ki.dwFlags = KEYEVENTF_KEYUP;
        inputsCount++;
    }

    SendInput(inputsCount, inputs, sizeof(INPUT));

    if (g_bShiftLock && vkCode != VK_SHIFT && vkCode != VK_LSHIFT && vkCode != VK_RSHIFT && vkCode != VK_CAPITAL) {
        g_bShiftLock = FALSE;
        InvalidateRect(hMainWnd, NULL, FALSE);
    }
}

// --- Hit Testing ---

BOOL IsInGrabBar(int my) {
    // Top of OSK for Floating AND Docked Bottom. Bottom of OSK for Docked Top.
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
    int rowStartKeys[] = {0, 14, 28, 41, 53, numKeys};
    
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
            
            if ((layout[i].vk == VK_SHIFT || layout[i].vk == VK_RSHIFT) && g_bShiftLock) kCol = CLR_ACCENT;
            if (layout[i].vk == VK_CAPITAL && g_bCaps) kCol = CLR_ACCENT;

            // Sharp Square Keys
            DrawSquareRect(memDC, layout[i].rect.left, layout[i].rect.top, 
                         layout[i].rect.right - layout[i].rect.left, 
                         layout[i].rect.bottom - layout[i].rect.top, kCol);
            
            BOOL isAccent = ((layout[i].vk == VK_SHIFT || layout[i].vk == VK_RSHIFT) && g_bShiftLock) || (layout[i].vk == VK_CAPITAL && g_bCaps);
            SetTextColor(memDC, isAccent ? RGB(255, 255, 255) : CLR_TEXT);
            
            SelectObject(memDC, layout[i].isMod ? hFontSmall : hFontNormal);
            const char* text = (g_bShiftLock || g_bCaps) ? layout[i].shift : layout[i].norm;
            RECT textRc = layout[i].rect;
            DrawTextA(memDC, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            curX += (layout[i].widthUnits * unitW);
        }
        curY += keyH + keyMargin;
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// --- Window Procedure ---

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hFontNormal = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontSmall  = CreateFontA(16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            
            // 300ms Timer: Smooth focus tracking 
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
                CheckFocus();
            }
            break;

        case WM_PAINT:
            PaintUI(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1; 

        case WM_MOUSEMOVE: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);

            if (g_bGrabTracking) {
                POINT pt; GetCursorPos(&pt);
                if (!g_bIsDragging) {
                    // Jitter Deadzone (Touch screen compatibility)
                    if (abs(pt.x - g_ptGrabStart.x) > 4 || abs(pt.y - g_ptGrabStart.y) > 4) {
                        g_bIsDragging = TRUE;
                        if (!g_bWindowMode) {
                            // Instant snap to Floating Window Mode when dragged
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

            // Hover logic for the Hide button
            BOOL oldHoverHide = g_bHoverHide;
            g_bHoverHide = FALSE;
            
            if (IsInGrabBar(my)) {
                int w = GetSystemMetrics(SM_CXSCREEN);
                int hideX = w / 4;
                if (mx >= hideX - 40 && mx <= hideX + 40) {
                    g_bHoverHide = TRUE;
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
            
            if (oldHoverHide != g_bHoverHide || oldHover != g_hoverKey || g_downKey != -1) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            
            if (IsInGrabBar(my)) {
                int w = GetSystemMetrics(SM_CXSCREEN);
                int hideX = w / 4;
                
                // If they explicitly click "Hide", dismiss the keyboard immediately
                if (mx >= hideX - 40 && mx <= hideX + 40) {
                    g_bManualOverride = FALSE; // Reset override so it hides naturally
                    g_hSuppressedWindow = GetForegroundWindow(); // Suppress the current window
                    HideOsk();
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
                    // Click to cycle modes
                    if (!g_bWindowMode && !g_bDockTop) {
                        g_bDockTop = TRUE; 
                    } else if (!g_bWindowMode && g_bDockTop) {
                        g_bWindowMode = TRUE;
                        RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
                        g_floatingY = (wa.bottom - wa.top) / 2 - (g_oskHeight / 2); // Center naturally
                    } else {
                        g_bWindowMode = FALSE; 
                        g_bDockTop = FALSE; 
                    }
                    PositionKeyboard();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                g_bIsDragging = FALSE;
                return 0;
            }

            if (g_downKey != -1) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);
                
                if (mx >= layout[g_downKey].rect.left && mx <= layout[g_downKey].rect.right &&
                    my >= layout[g_downKey].rect.top && my <= layout[g_downKey].rect.bottom) {
                    
                    int vk = layout[g_downKey].vk;
                    
                    if (vk == VK_SHIFT || vk == VK_RSHIFT) {
                        g_bShiftLock = !g_bShiftLock;
                    } else if (vk == VK_CAPITAL) {
                        g_bCaps = !g_bCaps;
                    } else {
                        SendKeystroke(vk);
                    }
                }
                g_downKey = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
            return (hit == HTCLIENT) ? HTCLIENT : hit; 
        }

        case WM_USER_SHOW_MANAGER: {
            // IPC WAKEUP
            g_bManualOverride = TRUE; 
            ShowOsk();
            if (!g_bTrayIconAdded) {
                g_bTrayIconAdded = Shell_NotifyIcon(NIM_ADD, &nid);
            }
            break;
        }

        case WM_USER_TRAY: {
            if (lParam == WM_RBUTTONUP) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING | (g_bWindowMode ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_WINDOW_MODE, "Window Mode");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit Keyboard");
                
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
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
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            else if (LOWORD(wParam) == ID_TRAY_WINDOW_MODE) {
                g_bWindowMode = !g_bWindowMode;
                PositionKeyboard();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            DeleteObject(hFontNormal); DeleteObject(hFontSmall);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
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
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}