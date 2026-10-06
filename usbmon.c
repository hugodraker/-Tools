/*
 * File: usbmon.c
 * Description: Win32 Native USB Device Manager for Windows LiveCD / WinPE.
 *              - Real-time USB drive detection (SetupAPI).
 *              - Right-click context menu for Safe Removal (CM_Request_Device_Eject).
 *              - Auto-retries Tray Icon creation if shell loads late in WinPE.
 *              - Dynamic Icon: Gray (Empty) -> Green (Devices Inserted).
 * License: Released into the Public Domain.
 * 
 * Compile using:
 * gcc -Os -s -o usbmon.exe usbmon.c -mwindows -luser32 -lgdi32 -lshell32 -lsetupapi -lcfgmgr32
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

// SetupAPI & Configuration Manager
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define WM_USER_TRAY         (WM_USER + 1)
#define WM_USER_SHOW_MANAGER (WM_USER + 2)
#define ID_TRAY_EXIT         9999
#define ID_EJECT_BASE        5000

// Globals
HWND hMainWnd = NULL;
NOTIFYICONDATA nid;

HFONT hFontLarge, hFontNormal, hFontSmall;
BOOL g_bShowingDialog = FALSE; 
BOOL g_bTrayIconAdded = FALSE; // Tracks if the shell accepted our icon
DWORD g_dwLastHideTime = 0;

// UI & Layout
int g_wndHeight = 220; 
#define WND_WIDTH 320
#define HEADER_HEIGHT 60
#define ITEM_HEIGHT 60

// Theme Colors
#define CLR_BG        RGB(243, 243, 243)
#define CLR_HEADER    RGB(255, 255, 255)
#define CLR_TEXT      RGB(20, 20, 20)
#define CLR_TEXT_MUT  RGB(100, 100, 100)
#define CLR_HOVER     RGB(235, 235, 235)
#define CLR_SELECTED  RGB(249, 249, 249)
#define CLR_ACCENT    RGB(0, 120, 212)  

// --- USB State ---

#define MAX_USB_DEVS 32

typedef struct {
    char name[128];
    DEVINST devInstParent; // The USB Mass Storage node to safely eject
} UsbDevice;

UsbDevice usbDevs[MAX_USB_DEVS];
int numUsbDevs = 0;

int scrollY = 0;
int hoverIndex = -1;
int selectedIndex = -1;
int hoverButton = 0; // 0=None, 2=Close, 3=Refresh

// --- USB API Helpers ---

void ScanUsbDevices() {
    numUsbDevs = 0;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_DISKDRIVE, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD i = 0;
    
    while (SetupDiEnumDeviceInfo(hDevInfo, i++, &devInfoData) && numUsbDevs < MAX_USB_DEVS) {
        char enumerator[64] = {0};
        SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_ENUMERATOR_NAME, NULL, (PBYTE)enumerator, sizeof(enumerator), NULL);

        if (strcasecmp(enumerator, "USBSTOR") == 0 || strcasecmp(enumerator, "SCSI") == 0) {
            
            DWORD capabilities = 0;
            SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_CAPABILITIES, NULL, (PBYTE)&capabilities, sizeof(capabilities), NULL);
            
            if ((capabilities & CM_DEVCAP_REMOVABLE) || strcasecmp(enumerator, "USBSTOR") == 0) {
                
                char name[128] = {0};
                if (!SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)name, sizeof(name), NULL)) {
                    strcpy(name, "Unknown USB Device");
                }

                strcpy(usbDevs[numUsbDevs].name, name);

                DEVINST parentInst = 0;
                if (CM_Get_Parent(&parentInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                    usbDevs[numUsbDevs].devInstParent = parentInst;
                } else {
                    usbDevs[numUsbDevs].devInstParent = devInfoData.DevInst; 
                }

                numUsbDevs++;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
}

void EjectDevice(int idx) {
    if (idx < 0 || idx >= numUsbDevs) return;

    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    WCHAR vetoName[MAX_PATH] = {0};
    
    g_bShowingDialog = TRUE;

    CONFIGRET cr = CM_Request_Device_EjectW(usbDevs[idx].devInstParent, &vetoType, vetoName, MAX_PATH, 0);
    
    if (cr == CR_SUCCESS) {
        MessageBoxA(hMainWnd, "The USB device has been safely removed.", "Safe to Remove Hardware", MB_OK | MB_ICONINFORMATION);
    } else {
        char msg[512];
        sprintf(msg, "Failed to remove device. It may be in use by another program.\nError Code: %lu", cr);
        MessageBoxA(hMainWnd, msg, "Problem Ejecting USB", MB_OK | MB_ICONERROR);
    }

    g_bShowingDialog = FALSE;
    ScanUsbDevices();
    InvalidateRect(hMainWnd, NULL, FALSE);
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

void DrawUsbIconUI(HDC hdc, int x, int y, BOOL hover) {
    COLORREF colBody = hover ? CLR_ACCENT : RGB(100, 100, 100);
    COLORREF colMetal = RGB(160, 160, 160);

    // USB Metal Tip
    DrawSolidRect(hdc, x + 6, y + 5, 12, 10, colMetal);
    DrawSolidRect(hdc, x + 8, y + 7, 3, 3, CLR_BG);  
    DrawSolidRect(hdc, x + 13, y + 7, 3, 3, CLR_BG); 

    // USB Plastic Body
    DrawRoundRect(hdc, x + 3, y + 15, 18, 20, 6, colBody);
    DrawSolidRect(hdc, x + 8, y + 19, 8, 2, RGB(255, 255, 255));
}

// --- Tray Icon Logic ---

HICON CreateTrayIcon() {
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
        
        DWORD colOutline = 0xFF333333; // Dark Gray outline 
        DWORD colFill    = 0xFFFFFFFF; // White fill
        DWORD colActive  = 0xFF10B981; // Green accent if devices exist
        
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                BOOL draw = FALSE;
                DWORD color = colOutline;

                // Metal plug (x: 5 to 10, y: 1 to 5)
                if (y >= 1 && y <= 5 && x >= 5 && x <= 10) {
                    draw = TRUE;
                    if (y > 1 && y < 5 && x > 5 && x < 10) color = colFill;
                    // Draw holes
                    if (y == 3 && (x == 6 || x == 9)) color = colOutline;
                }

                // Main body (x: 3 to 12, y: 6 to 14)
                if (y >= 6 && y <= 14 && x >= 3 && x <= 12) {
                    draw = TRUE;
                    if (y > 6 && y < 14 && x > 3 && x < 12) {
                        color = (numUsbDevs > 0) ? colActive : colFill;
                    }
                }

                if (draw) pPixels[y * 16 + x] = color;
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
    
    // Failsafe if GDI drawing completely fails
    if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    return hIcon;
}

void UpdateTrayIcon() {
    if (nid.cbSize == 0) return;
    
    if (numUsbDevs == 0) {
        wcscpy(nid.szTip, L"No USB Devices");
    } else {
        // Use wsprintfW for guaranteed MinGW/WinPE C-Runtime compatibility
        wsprintfW(nid.szTip, L"USB Devices: %d Connected", numUsbDevs);
    }

    HICON hOld = nid.hIcon;
    nid.hIcon = CreateTrayIcon();
    
    if (!g_bTrayIconAdded) {
        g_bTrayIconAdded = Shell_NotifyIcon(NIM_ADD, &nid);
    } else {
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }
    
    if (hOld) DestroyIcon(hOld);
}

// --- UI Rendering ---

void PositionWindow(HWND hwnd) {
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0); 
    
    g_wndHeight = HEADER_HEIGHT + (numUsbDevs > 0 ? (numUsbDevs * ITEM_HEIGHT) : 100);
    int maxH = workArea.bottom - workArea.top - 40;
    if (g_wndHeight > maxH) g_wndHeight = maxH; 
    
    int x = workArea.right - WND_WIDTH - 12;
    int y = workArea.bottom - g_wndHeight - 12;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, WND_WIDTH, g_wndHeight, SWP_NOZORDER);
}

void PaintUI(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, WND_WIDTH, g_wndHeight);
    SelectObject(memDC, memBitmap);
    
    // Base Background
    DrawSolidRect(memDC, 0, 0, WND_WIDTH, g_wndHeight, CLR_BG);
    SetBkMode(memDC, TRANSPARENT);
    
    int currentY = HEADER_HEIGHT - scrollY;
    
    if (numUsbDevs == 0) {
        SetTextColor(memDC, CLR_TEXT_MUT);
        SelectObject(memDC, hFontNormal);
        TextOutA(memDC, 30, currentY + 30, "No USB storage devices found.", 29);
    } else {
        for (int i = 0; i < numUsbDevs; i++) {
            if (currentY + ITEM_HEIGHT > HEADER_HEIGHT && currentY < g_wndHeight) {
                
                COLORREF bgCol = CLR_BG;
                if (i == selectedIndex) bgCol = CLR_SELECTED;
                else if (i == hoverIndex) bgCol = CLR_HOVER;
                
                if (bgCol != CLR_BG) {
                    DrawRoundRect(memDC, 5, currentY, WND_WIDTH - 10, ITEM_HEIGHT, 10, bgCol);
                }

                DrawUsbIconUI(memDC, 15, currentY + 10, (i == hoverIndex || i == selectedIndex));

                SetTextColor(memDC, CLR_TEXT);
                SelectObject(memDC, hFontNormal);
                
                char dispName[64];
                strncpy(dispName, usbDevs[i].name, 35);
                if (strlen(usbDevs[i].name) > 35) strcpy(dispName + 32, "...");
                
                TextOutA(memDC, 55, currentY + 14, dispName, strlen(dispName));

                SetTextColor(memDC, CLR_TEXT_MUT);
                SelectObject(memDC, hFontSmall);
                TextOutA(memDC, 55, currentY + 34, "Right-click to Safely Remove", 28);
            }
            currentY += ITEM_HEIGHT;
        }
    }

    // Header
    DrawSolidRect(memDC, 0, 0, WND_WIDTH, HEADER_HEIGHT, CLR_HEADER);
    DrawSolidRect(memDC, 0, HEADER_HEIGHT - 1, WND_WIDTH, 1, RGB(220, 220, 220));

    SetTextColor(memDC, CLR_TEXT);
    SelectObject(memDC, hFontLarge);
    TextOutA(memDC, 15, 16, "USB Devices", 11);

    SetTextColor(memDC, (hoverButton == 3) ? CLR_ACCENT : CLR_TEXT_MUT);
    SelectObject(memDC, hFontNormal); 
    TextOutA(memDC, WND_WIDTH - 85, 20, "Refresh", 7);

    BitBlt(hdc, 0, 0, WND_WIDTH, g_wndHeight, memDC, 0, 0, SRCCOPY);
    
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// --- Window Procedure ---

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hFontLarge  = CreateFontA(24, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontNormal = CreateFontA(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontSmall  = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            
            ScanUsbDevices();
            SetTimer(hwnd, 1, 2000, NULL); 
            break;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && !g_bShowingDialog) {
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                    g_dwLastHideTime = GetTickCount();
                }
            }
            break;

        case WM_TIMER: {
            int oldNum = numUsbDevs;
            ScanUsbDevices();
            
            // Constantly retry tray addition if it failed on startup, OR update if device count changed
            if (!g_bTrayIconAdded || numUsbDevs != oldNum) {
                UpdateTrayIcon();
                if (IsWindowVisible(hwnd) && numUsbDevs != oldNum) {
                    PositionWindow(hwnd);
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

        case WM_MOUSEWHEEL: {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            scrollY -= (zDelta / WHEEL_DELTA) * 30;
            if (scrollY < 0) scrollY = 0;
            
            int maxScroll = (numUsbDevs * ITEM_HEIGHT) - (g_wndHeight - HEADER_HEIGHT);
            if (maxScroll < 0) maxScroll = 0;
            if (scrollY > maxScroll) scrollY = maxScroll;
            
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_MOUSEMOVE: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            
            int oldHover = hoverIndex;
            int oldBtn = hoverButton;
            hoverIndex = -1;
            hoverButton = 0;

            if (my < HEADER_HEIGHT) {
                if (mx > WND_WIDTH - 95) hoverButton = 3; 
            } else {
                int currentY = HEADER_HEIGHT - scrollY;
                for (int i = 0; i < numUsbDevs; i++) {
                    if (my >= currentY && my < currentY + ITEM_HEIGHT) {
                        hoverIndex = i;
                        break;
                    }
                    currentY += ITEM_HEIGHT;
                }
            }

            if (oldHover != hoverIndex || oldBtn != hoverButton) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int my = HIWORD(lParam);

            if (my < HEADER_HEIGHT && hoverButton == 0) {
                ReleaseCapture();
                SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }

            if (hoverButton == 3) { 
                ScanUsbDevices();
                selectedIndex = -1;
                scrollY = 0;
                PositionWindow(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (hoverIndex != -1) { 
                selectedIndex = hoverIndex;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_RBUTTONUP: {
            int my = HIWORD(lParam);
            
            if (my >= HEADER_HEIGHT) {
                int currentY = HEADER_HEIGHT - scrollY;
                for (int i = 0; i < numUsbDevs; i++) {
                    if (my >= currentY && my < currentY + ITEM_HEIGHT) {
                        selectedIndex = i;
                        InvalidateRect(hwnd, NULL, FALSE);

                        HMENU hPopup = CreatePopupMenu();
                        AppendMenuA(hPopup, MF_STRING, ID_EJECT_BASE + i, "Safely Remove Device");
                        
                        POINT pt;
                        GetCursorPos(&pt);
                        
                        g_bShowingDialog = TRUE;
                        SetForegroundWindow(hwnd);
                        TrackPopupMenu(hPopup, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                        DestroyMenu(hPopup);
                        g_bShowingDialog = FALSE;
                        
                        break;
                    }
                    currentY += ITEM_HEIGHT;
                }
            }
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            else if (LOWORD(wParam) >= ID_EJECT_BASE && LOWORD(wParam) < ID_EJECT_BASE + MAX_USB_DEVS) {
                int idx = LOWORD(wParam) - ID_EJECT_BASE;
                EjectDevice(idx);
            }
            break;
        }

        case WM_USER_SHOW_MANAGER: {
            ScanUsbDevices();
            PositionWindow(hwnd);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
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
                AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit USB Manager");
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
        }

        case WM_DESTROY:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            DeleteObject(hFontLarge); 
            DeleteObject(hFontNormal); 
            DeleteObject(hFontSmall);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// --- Main ---

int main(int argc, char *argv[]) {
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "WinPE_UsbMon_Tray_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowA("WinPEUsbMonWin11", NULL);
        if (hExisting) PostMessageA(hExisting, WM_USER_SHOW_MANAGER, 0, 0);
        return 0; 
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WinPEUsbMonWin11";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hMainWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, 
        "WinPEUsbMonWin11", "",
        WS_POPUP, 
        0, 0, WND_WIDTH, g_wndHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    HRGN hRgn = CreateRoundRectRgn(0, 0, WND_WIDTH, g_wndHeight, 16, 16);
    SetWindowRgn(hMainWnd, hRgn, TRUE);

    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA); 
    nid.hWnd = hMainWnd;
    nid.uID = 3; 
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TRAY;
    
    ScanUsbDevices();
    
    // Initial attempt to add the tray icon
    UpdateTrayIcon();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}