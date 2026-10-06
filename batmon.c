/*
 * File: batmon.c
 * Description: Win32 Native Battery Monitor & Control Center for Windows LiveCD / WinPE.
 *              - Windows 11 Inspired UI with custom GDI graphics.
 *              - Battery tracking + Volume Slider + Screen Brightness Slider.
 *              - Uses Hardware DDC or Software Gamma for guaranteed brightness control.
 *              - Flyout behavior (Hides when focus is lost).
 * License: Released into the Public Domain.
 * 
 * Compile using:
 * gcc -Os -s -o batmon.exe batmon.c -mwindows -luser32 -lgdi32 -lshell32 -lole32 -luuid
 */

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

// COM & Audio Headers
#include <initguid.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#define WM_USER_TRAY         (WM_USER + 1)
#define WM_USER_SHOW_MANAGER (WM_USER + 2)
#define ID_TRAY_EXIT         9999

// Globals
HWND hMainWnd = NULL;
NOTIFYICONDATA nid;

HFONT hFontHuge, hFontLarge, hFontNormal, hFontSmall;
BOOL g_bShowingDialog = FALSE; 
DWORD g_dwLastHideTime = 0;
SYSTEM_POWER_STATUS g_powerStatus = {0};

// UI & Layout
int g_wndHeight = 220; 
#define WND_WIDTH 320
#define HEADER_HEIGHT 60

// Theme Colors
#define CLR_BG        RGB(243, 243, 243)
#define CLR_HEADER    RGB(255, 255, 255)
#define CLR_TEXT      RGB(20, 20, 20)
#define CLR_TEXT_MUT  RGB(100, 100, 100)
#define CLR_GOOD      RGB(16, 185, 129) 
#define CLR_WARN      RGB(225, 29, 72)  
#define CLR_ACCENT    RGB(0, 120, 212)  
#define CLR_TRACK     RGB(210, 210, 210)
#define CLR_BATT_BG   RGB(210, 210, 210) 

// --- Audio & Brightness State ---

IAudioEndpointVolume *pEndpointVolume = NULL;
int g_volLevel = 0;
BOOL g_isMuted = FALSE;

BOOL bBrightnessAvailable = FALSE;
BOOL bUseGammaFallback = FALSE;
HANDLE hPhysicalMonitor = NULL;
int g_brightLevel = 100;
int g_brightMin = 0, g_brightMax = 100;

int g_dragMode = 0; // 0=None, 1=Volume, 2=Brightness
int g_hoverMute = 0;

typedef struct {
    HANDLE hPhysicalMonitor;
    WCHAR szPhysicalMonitorDescription[128];
} MY_PHYSICAL_MONITOR;

typedef BOOL (WINAPI *GetNumberOfPhysicalMonitorsFromHMONITOR_t)(HMONITOR, LPDWORD);
typedef BOOL (WINAPI *GetPhysicalMonitorsFromHMONITOR_t)(HMONITOR, DWORD, MY_PHYSICAL_MONITOR*);
typedef BOOL (WINAPI *GetMonitorBrightness_t)(HANDLE, LPDWORD, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *SetMonitorBrightness_t)(HANDLE, DWORD);

HMODULE hDxva2 = NULL;
GetNumberOfPhysicalMonitorsFromHMONITOR_t pGetNumMonitors = NULL;
GetPhysicalMonitorsFromHMONITOR_t pGetMonitors = NULL;
GetMonitorBrightness_t pGetBrightness = NULL;
SetMonitorBrightness_t pSetBrightness = NULL;
MY_PHYSICAL_MONITOR *pMonitorsArray = NULL;

// --- Subsystem Initialization ---

void InitAudio() {
    IMMDeviceEnumerator *pEnum = NULL;
    CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, (LPVOID*)&pEnum);
    if (pEnum) {
        IMMDevice *pDevice = NULL;
        pEnum->lpVtbl->GetDefaultAudioEndpoint(pEnum, eRender, eConsole, &pDevice);
        if (pDevice) {
            pDevice->lpVtbl->Activate(pDevice, &IID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&pEndpointVolume);
            pDevice->lpVtbl->Release(pDevice);
        }
        pEnum->lpVtbl->Release(pEnum);
    }
}

void UpdateAudioState() {
    if (pEndpointVolume) {
        float vol = 0;
        pEndpointVolume->lpVtbl->GetMasterVolumeLevelScalar(pEndpointVolume, &vol);
        g_volLevel = (int)(vol * 100.0f + 0.5f);
        pEndpointVolume->lpVtbl->GetMute(pEndpointVolume, &g_isMuted);
    }
}

void SetAudioVolume(int level) {
    if (pEndpointVolume) {
        if (level < 0) level = 0;
        if (level > 100) level = 100;
        float vol = level / 100.0f;
        pEndpointVolume->lpVtbl->SetMasterVolumeLevelScalar(pEndpointVolume, vol, NULL);
        if (g_isMuted && level > 0) {
            pEndpointVolume->lpVtbl->SetMute(pEndpointVolume, FALSE, NULL);
        }
        UpdateAudioState();
    }
}

void ToggleMute() {
    if (pEndpointVolume) {
        pEndpointVolume->lpVtbl->SetMute(pEndpointVolume, !g_isMuted, NULL);
        UpdateAudioState();
    }
}

void InitBrightness() {
    hDxva2 = LoadLibraryA("dxva2.dll");
    if (hDxva2) {
        pGetNumMonitors = (void*)GetProcAddress(hDxva2, "GetNumberOfPhysicalMonitorsFromHMONITOR");
        pGetMonitors    = (void*)GetProcAddress(hDxva2, "GetPhysicalMonitorsFromHMONITOR");
        pGetBrightness  = (void*)GetProcAddress(hDxva2, "GetMonitorBrightness");
        pSetBrightness  = (void*)GetProcAddress(hDxva2, "SetMonitorBrightness");

        if (pGetNumMonitors && pGetMonitors && pGetBrightness && pSetBrightness) {
            HWND hwndDesktop = GetDesktopWindow();
            HMONITOR hMon = MonitorFromWindow(hwndDesktop, MONITOR_DEFAULTTOPRIMARY);
            DWORD numMons = 0;
            if (pGetNumMonitors(hMon, &numMons) && numMons > 0) {
                pMonitorsArray = malloc(numMons * sizeof(MY_PHYSICAL_MONITOR));
                if (pGetMonitors(hMon, numMons, pMonitorsArray)) {
                    hPhysicalMonitor = pMonitorsArray[0].hPhysicalMonitor;
                    DWORD minB, curB, maxB;
                    if (pGetBrightness(hPhysicalMonitor, &minB, &curB, &maxB)) {
                        bBrightnessAvailable = TRUE;
                        bUseGammaFallback = FALSE;
                        g_brightMin = minB;
                        g_brightMax = maxB;
                        g_brightLevel = curB;
                    }
                }
            }
        }
    }

    if (!bBrightnessAvailable) {
        HDC hdc = GetDC(NULL);
        WORD ramp[3][256];
        if (GetDeviceGammaRamp(hdc, ramp)) {
            bBrightnessAvailable = TRUE;
            bUseGammaFallback = TRUE;
            g_brightLevel = 100; 
            g_brightMin = 0;
            g_brightMax = 100;
        }
        ReleaseDC(NULL, hdc);
    }
}

void SetDisplayBrightness(int level) {
    if (!bBrightnessAvailable) return;
    if (level < 0) level = 0;
    if (level > 100) level = 100;

    if (bUseGammaFallback) {
        HDC hdc = GetDC(NULL);
        WORD ramp[3][256];
        for (int i = 0; i < 256; i++) {
            int val = (i * 65535 / 255) * level / 100;
            ramp[0][i] = ramp[1][i] = ramp[2][i] = (WORD)val;
        }
        SetDeviceGammaRamp(hdc, ramp);
        ReleaseDC(NULL, hdc);
        g_brightLevel = level;
    } 
    else if (pSetBrightness && hPhysicalMonitor) {
        DWORD target = g_brightMin + (level * (g_brightMax - g_brightMin) / 100);
        pSetBrightness(hPhysicalMonitor, target);
        g_brightLevel = level;
    }
}

// --- Custom Draw Helpers ---

void DrawSolidRect(HDC hdc, int x, int y, int w, int h, COLORREF col) {
    RECT r = {x, y, x+w, y+h};
    HBRUSH br = CreateSolidBrush(col);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int rad, COLORREF col) {
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, x, y, x+w, y+h, rad, rad);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

void DrawSpeakerIcon(HDC hdc, int x, int y, BOOL muted, BOOL hover) {
    COLORREF col = hover ? CLR_ACCENT : CLR_TEXT;
    if (muted) col = CLR_WARN;
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    SelectObject(hdc, br);
    SelectObject(hdc, pen);

    POINT pts[6] = { {x+2, y+6}, {x+6, y+6}, {x+12, y+2}, {x+12, y+14}, {x+6, y+10}, {x+2, y+10} };
    Polygon(hdc, pts, 6);

    if (muted) {
        HPEN penMute = CreatePen(PS_SOLID, 2, col);
        SelectObject(hdc, penMute);
        MoveToEx(hdc, x+16, y+5, NULL); LineTo(hdc, x+22, y+11);
        MoveToEx(hdc, x+22, y+5, NULL); LineTo(hdc, x+16, y+11);
        DeleteObject(penMute);
    } else {
        HPEN penWave = CreatePen(PS_SOLID, 2, col);
        SelectObject(hdc, penWave);
        Arc(hdc, x+10, y+5, x+16, y+11, x+13, y+11, x+13, y+5);
        Arc(hdc, x+8, y+2, x+20, y+14, x+14, y+14, x+14, y+2);
        DeleteObject(penWave);
    }
    DeleteObject(br);
    DeleteObject(pen);
}

void DrawSunIcon(HDC hdc, int cx, int cy) {
    HPEN pen = CreatePen(PS_SOLID, 2, CLR_TEXT);
    HBRUSH br = CreateSolidBrush(CLR_TEXT);
    SelectObject(hdc, pen);
    SelectObject(hdc, br);

    Ellipse(hdc, cx-4, cy-4, cx+4, cy+4);
    
    for (int i=0; i<8; i++) {
        double angle = i * (3.14159 / 4.0);
        int dx1 = (int)(cos(angle) * 6);
        int dy1 = (int)(sin(angle) * 6);
        int dx2 = (int)(cos(angle) * 9);
        int dy2 = (int)(sin(angle) * 9);
        MoveToEx(hdc, cx+dx1, cy+dy1, NULL);
        LineTo(hdc, cx+dx2, cy+dy2);
    }
    DeleteObject(pen);
    DeleteObject(br);
}

// --- Tray Icon Logic ---

HICON CreateBatteryTrayIcon(SYSTEM_POWER_STATUS *sps) {
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
        BOOL noBattery = (sps->BatteryFlag == 128 || sps->BatteryLifePercent == 255);
        int percent = noBattery ? 0 : sps->BatteryLifePercent;
        BOOL isCharging = (sps->ACLineStatus == 1);
        
        DWORD colOutline = 0xFF333333; 
        DWORD colEmpty   = 0xFFDDDDDD; 
        DWORD colFill    = 0xFF10B981; 
        
        if (noBattery) colFill = 0xFF555555; 
        else if (percent <= 20 && !isCharging) colFill = 0xFFE11D48; 

        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                BOOL draw = FALSE;
                DWORD color = colOutline;

                if ((y == 4 || y == 12) && (x >= 1 && x <= 13)) draw = TRUE;
                if ((x == 1 || x == 13) && (y >= 4 && y <= 12)) draw = TRUE;
                if (x == 14 && (y >= 6 && y <= 10)) draw = TRUE;

                if (y > 4 && y < 12 && x > 1 && x < 13) {
                    int fillWidth = (percent * 11) / 100;
                    if ((x - 1) <= fillWidth) {
                        draw = TRUE;
                        color = colFill; 
                    } else {
                        draw = TRUE;
                        color = colEmpty; 
                    }
                }
                if (draw) pPixels[y * 16 + x] = color;
            }
        }

        if (isCharging && !noBattery) {
            pPixels[8 * 16 + 7] = 0xFF000000; 
            pPixels[7 * 16 + 7] = 0xFF000000;
            pPixels[9 * 16 + 7] = 0xFF000000;
            pPixels[8 * 16 + 6] = 0xFF000000;
            pPixels[8 * 16 + 8] = 0xFF000000;
        }
    }

    BYTE maskBits[32] = {0}; 
    HBITMAP hbmMask = CreateBitmap(16, 16, 1, 1, maskBits);
    ICONINFO ii = { TRUE, 0, 0, hbmMask, hbmp };
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmMask);
    DeleteObject(hbmp);
    ReleaseDC(NULL, hScreenDC);
    return hIcon;
}

void UpdateTrayIcon() {
    if (nid.cbSize == 0) return;
    GetSystemPowerStatus(&g_powerStatus);
    
    BOOL noBattery = (g_powerStatus.BatteryFlag == 128 || g_powerStatus.BatteryLifePercent == 255);
    
    if (noBattery) {
        wcscpy(nid.szTip, L"No Battery Detected");
    } else {
        wchar_t status[64];
        if (g_powerStatus.ACLineStatus == 1) {
            swprintf(status, 64, L"Charging: %d%%", g_powerStatus.BatteryLifePercent);
        } else {
            swprintf(status, 64, L"On Battery: %d%%", g_powerStatus.BatteryLifePercent);
        }
        wcsncpy(nid.szTip, status, 127);
    }

    HICON hOld = nid.hIcon;
    nid.hIcon = CreateBatteryTrayIcon(&g_powerStatus);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
    if (hOld) DestroyIcon(hOld);
}

// --- UI Rendering ---

void PositionWindow(HWND hwnd) {
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0); 
    
    // Increased base height to ensure enough padding at the bottom
    g_wndHeight = 175; 
    if (pEndpointVolume) g_wndHeight += 70; 
    if (bBrightnessAvailable) g_wndHeight += 70; 
    
    int x = workArea.right - WND_WIDTH - 12;
    int y = workArea.bottom - g_wndHeight - 12;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, WND_WIDTH, g_wndHeight, SWP_NOZORDER);
    
    // Crucial: Update the clipping region to match the dynamic height so sliders aren't cut off
    HRGN hRgn = CreateRoundRectRgn(0, 0, WND_WIDTH, g_wndHeight, 16, 16);
    SetWindowRgn(hwnd, hRgn, TRUE);
}

void DrawSlider(HDC memDC, int x, int y, int w, int h, int percent) {
    DrawRoundRect(memDC, x, y, w, h, h, CLR_TRACK);
    
    if (percent > 0) {
        int fillW = (percent * w) / 100;
        DrawRoundRect(memDC, x, y, fillW, h, h, CLR_ACCENT);
    }
    
    int tX = x + ((percent * w) / 100) - 8;
    if (tX < x - 8) tX = x - 8;
    if (tX > x + w - 8) tX = x + w - 8;
    
    HBRUSH thumbBr = CreateSolidBrush(RGB(255, 255, 255));
    HPEN thumbPen = CreatePen(PS_SOLID, 2, CLR_ACCENT);
    SelectObject(memDC, thumbBr);
    SelectObject(memDC, thumbPen);
    Ellipse(memDC, tX, y - 5, tX + 16, y + h + 5);
    DeleteObject(thumbBr);
    DeleteObject(thumbPen);
}

void PaintUI(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, WND_WIDTH, g_wndHeight);
    SelectObject(memDC, memBitmap);
    
    DrawSolidRect(memDC, 0, 0, WND_WIDTH, g_wndHeight, CLR_BG);
    SetBkMode(memDC, TRANSPARENT);
    
    DrawSolidRect(memDC, 0, 0, WND_WIDTH, HEADER_HEIGHT, CLR_HEADER);
    DrawSolidRect(memDC, 0, HEADER_HEIGHT - 1, WND_WIDTH, 1, RGB(220, 220, 220));
    SetTextColor(memDC, CLR_TEXT);
    SelectObject(memDC, hFontLarge);
    TextOutA(memDC, 15, 16, "Quick Settings", 14);

    int currentY = HEADER_HEIGHT;

    // --- Battery Section ---
    BOOL noBattery = (g_powerStatus.BatteryFlag == 128 || g_powerStatus.BatteryLifePercent == 255);
    int percent = noBattery ? 0 : g_powerStatus.BatteryLifePercent;
    BOOL isCharging = (g_powerStatus.ACLineStatus == 1);
    
    COLORREF fillCol = CLR_BATT_BG;
    if (!noBattery) {
        if (isCharging) fillCol = CLR_GOOD;
        else if (percent <= 20) fillCol = CLR_WARN;
        else fillCol = CLR_TEXT;
    }

    int bX = 25, bY = currentY + 30, bW = 80, bH = 46;
    HPEN hPenOut = CreatePen(PS_SOLID, 3, CLR_TEXT_MUT);
    HBRUSH hBrEmpty = CreateSolidBrush(CLR_BG);
    SelectObject(memDC, hBrEmpty);
    SelectObject(memDC, hPenOut);
    RoundRect(memDC, bX, bY, bX + bW, bY + bH, 8, 8);
    DrawSolidRect(memDC, bX + bW, bY + 12, 6, 22, CLR_TEXT_MUT);
    if (!noBattery && percent > 0) {
        int fillW = (percent * (bW - 6)) / 100;
        DrawRoundRect(memDC, bX + 3, bY + 3, fillW, bH - 6, 4, fillCol);
    }
    DeleteObject(hPenOut);
    DeleteObject(hBrEmpty);

    int tX = 125, tY = currentY + 22;
    if (noBattery) {
        SetTextColor(memDC, CLR_TEXT); SelectObject(memDC, hFontHuge); TextOutA(memDC, tX, tY - 5, "---", 3);
        SetTextColor(memDC, CLR_TEXT_MUT); SelectObject(memDC, hFontNormal); TextOutA(memDC, tX, tY + 45, "No battery detected", 19);
    } else {
        char pctStr[16]; sprintf(pctStr, "%d%%", percent);
        SetTextColor(memDC, CLR_TEXT); SelectObject(memDC, hFontHuge); TextOutA(memDC, tX, tY - 5, pctStr, strlen(pctStr));
        
        char statusStr[64] = {0};
        if (isCharging) strcpy(statusStr, (percent == 100) ? "Fully charged" : "Charging");
        else strcpy(statusStr, "Discharging");

        SetTextColor(memDC, isCharging ? CLR_GOOD : CLR_TEXT_MUT); SelectObject(memDC, hFontNormal); TextOutA(memDC, tX, tY + 45, statusStr, strlen(statusStr));

        if (!isCharging && g_powerStatus.BatteryLifeTime != (DWORD)-1) {
            int hrs = g_powerStatus.BatteryLifeTime / 3600;
            int mins = (g_powerStatus.BatteryLifeTime % 3600) / 60;
            char timeStr[64];
            if (hrs > 0) sprintf(timeStr, "%d hr %d min remaining", hrs, mins);
            else sprintf(timeStr, "%d min remaining", mins);
            SetTextColor(memDC, CLR_TEXT_MUT); SelectObject(memDC, hFontSmall); TextOutA(memDC, tX, tY + 70, timeStr, strlen(timeStr));
        } else if (isCharging) {
            SetTextColor(memDC, CLR_TEXT_MUT); SelectObject(memDC, hFontSmall); TextOutA(memDC, tX, tY + 70, "Plugged in (AC Power)", 21);
        }
    }
    currentY += 105;

    // --- Volume Section ---
    if (pEndpointVolume) {
        DrawSolidRect(memDC, 20, currentY, WND_WIDTH - 40, 1, RGB(225, 225, 225)); 
        currentY += 15;
        
        DrawSpeakerIcon(memDC, 20, currentY + 14, g_isMuted, g_hoverMute);
        DrawSlider(memDC, 60, currentY + 20, WND_WIDTH - 85, 6, g_isMuted ? 0 : g_volLevel);
        
        char vStr[8]; sprintf(vStr, "%d", g_isMuted ? 0 : g_volLevel);
        SetTextColor(memDC, CLR_TEXT_MUT); SelectObject(memDC, hFontSmall);
        TextOutA(memDC, WND_WIDTH - 80 + (25 - strlen(vStr)*4), currentY + 30, vStr, strlen(vStr));
        
        currentY += 55;
    }

    // --- Brightness Section ---
    if (bBrightnessAvailable) {
        DrawSolidRect(memDC, 20, currentY, WND_WIDTH - 40, 1, RGB(225, 225, 225)); 
        currentY += 15;

        DrawSunIcon(memDC, 32, currentY + 22);
        DrawSlider(memDC, 60, currentY + 20, WND_WIDTH - 85, 6, g_brightLevel);
        
        char bStr[8]; sprintf(bStr, "%d", g_brightLevel);
        SetTextColor(memDC, CLR_TEXT_MUT); SelectObject(memDC, hFontSmall);
        TextOutA(memDC, WND_WIDTH - 80 + (25 - strlen(bStr)*4), currentY + 30, bStr, strlen(bStr));
    }

    BitBlt(hdc, 0, 0, WND_WIDTH, g_wndHeight, memDC, 0, 0, SRCCOPY);
    
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// --- Input Processing ---

void ProcessDrag(LPARAM lParam) {
    int mx = LOWORD(lParam);
    if (g_dragMode == 1 || g_dragMode == 2) {
        int val = (mx - 60) * 100 / (WND_WIDTH - 85);
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        
        if (g_dragMode == 1) SetAudioVolume(val);
        else if (g_dragMode == 2) SetDisplayBrightness(val);
        
        InvalidateRect(hMainWnd, NULL, FALSE);
    }
}

// --- Window Procedure ---

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hFontHuge   = CreateFontA(42, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontLarge  = CreateFontA(24, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontNormal = CreateFontA(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontSmall  = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            
            InitAudio();
            UpdateAudioState();
            InitBrightness();
            
            SetTimer(hwnd, 1, 1000, NULL); 
            break;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && !g_bShowingDialog) {
                if (IsWindowVisible(hwnd) && g_dragMode == 0) {
                    ShowWindow(hwnd, SW_HIDE);
                    g_dwLastHideTime = GetTickCount();
                }
            }
            break;

        case WM_TIMER: {
            SYSTEM_POWER_STATUS oldSps = g_powerStatus;
            int oldVol = g_volLevel;
            BOOL oldMute = g_isMuted;
            
            UpdateTrayIcon();
            if (g_dragMode == 0) UpdateAudioState();
            
            if (IsWindowVisible(hwnd) && g_dragMode == 0) {
                if (oldSps.BatteryLifePercent != g_powerStatus.BatteryLifePercent || 
                    oldSps.ACLineStatus != g_powerStatus.ACLineStatus ||
                    oldSps.BatteryLifeTime != g_powerStatus.BatteryLifeTime ||
                    oldVol != g_volLevel || oldMute != g_isMuted) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            break;
        }

        case WM_PAINT:
            PaintUI(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1; 

        case WM_LBUTTONDOWN: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            
            if (my < HEADER_HEIGHT) {
                ReleaseCapture();
                SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            } else {
                int cY = HEADER_HEIGHT + 105;
                int volY = 0, briY = 0;
                
                if (pEndpointVolume) {
                    volY = cY + 15;
                    cY += 70;
                }
                if (bBrightnessAvailable) {
                    briY = cY + 15;
                }
                
                if (pEndpointVolume && mx >= 15 && mx <= 45 && my >= volY && my <= volY + 40) {
                    ToggleMute();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                
                if (pEndpointVolume && my >= volY && my <= volY + 40 && mx >= 50) {
                    g_dragMode = 1;
                    SetCapture(hwnd);
                    ProcessDrag(lParam);
                } else if (bBrightnessAvailable && my >= briY && my <= briY + 40 && mx >= 50) {
                    g_dragMode = 2;
                    SetCapture(hwnd);
                    ProcessDrag(lParam);
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            
            if (g_dragMode > 0) {
                ProcessDrag(lParam);
            } else {
                int cY = HEADER_HEIGHT + 105;
                if (pEndpointVolume) {
                    int volY = cY + 15;
                    int oldHover = g_hoverMute;
                    g_hoverMute = (mx >= 15 && mx <= 45 && my >= volY && my <= volY + 40) ? 1 : 0;
                    if (g_hoverMute != oldHover) InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            break;
        }

        case WM_LBUTTONUP: {
            if (g_dragMode > 0) {
                g_dragMode = 0;
                ReleaseCapture();
            }
            break;
        }

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
            return (hit == HTCLIENT) ? HTCLIENT : hit; 
        }
        
        case WM_USER_SHOW_MANAGER: {
            PositionWindow(hwnd);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            UpdateAudioState();
            GetSystemPowerStatus(&g_powerStatus);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_USER_TRAY: {
            if (lParam == WM_LBUTTONUP) {
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                    g_dwLastHideTime = GetTickCount();
                } else {
                    if (GetTickCount() - g_dwLastHideTime > 200) {
                        SendMessage(hwnd, WM_USER_SHOW_MANAGER, 0, 0);
                    }
                }
            } else if (lParam == WM_RBUTTONUP) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit Control Center");
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_TRAY_EXIT) DestroyWindow(hwnd);
            break;
        }

        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            DeleteObject(hFontHuge); DeleteObject(hFontLarge); DeleteObject(hFontNormal); DeleteObject(hFontSmall);
            if (pEndpointVolume) pEndpointVolume->lpVtbl->Release(pEndpointVolume);
            CoUninitialize();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// --- Main ---

int main(int argc, char *argv[]) {
    CoInitialize(NULL);

    HANDLE hMutex = CreateMutexA(NULL, FALSE, "WinPE_BatMon_Tray_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowA("WinPEBatMonWin11", NULL);
        if (hExisting) PostMessageA(hExisting, WM_USER_SHOW_MANAGER, 0, 0);
        CoUninitialize();
        return 0; 
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WinPEBatMonWin11";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hMainWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
        "WinPEBatMonWin11", "",
        WS_POPUP, 
        0, 0, WND_WIDTH, g_wndHeight, // Note: PositionWindow correctly updates this later
        NULL, NULL, GetModuleHandle(NULL), NULL);

    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA); 
    nid.hWnd = hMainWnd;
    nid.uID = 2; 
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TRAY;
    
    GetSystemPowerStatus(&g_powerStatus);
    nid.hIcon = CreateBatteryTrayIcon(&g_powerStatus);
    wcscpy(nid.szTip, L"Control Center");
    
    Shell_NotifyIcon(NIM_ADD, &nid);

    SendMessage(hMainWnd, WM_USER_SHOW_MANAGER, 0, 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}