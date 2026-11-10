/*
 * File: wificon.c
 * Description: Win32 Native Wi-Fi Manager for Windows LiveCD / WinPE.
 * Compile using:
 * gcc -Os -s -o wificon.exe wificon.c -mwindows -lwlanapi -luser32 -lgdi32 -lshell32 -liphlpapi -lws2_32 -lwininet
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wininet.h>
#include <wlanapi.h>
#include <windot11.h>
#include <shellapi.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define WM_USER_TRAY         (WM_USER + 1)
#define WM_USER_SHOW_MANAGER (WM_USER + 2)
#define WM_USER_UPDATE_WIFI  (WM_USER + 3)

#define ID_TRAY_EXIT         9999
#define ID_BTN_CONNECT_ENTER 9998
#define IDT_REFRESH_TIMER    1001

// Globals
HANDLE hWlan = NULL;
GUID wlanGuid;
HWND hMainWnd = NULL;
HWND hPassEdit = NULL;
NOTIFYICONDATAW nid;
WNDPROC OldEditProc = NULL;
UINT g_uTaskbarRestart = 0;

HFONT hFontLarge, hFontNormal, hFontSmall, hFontLink;
BOOL g_bShowingDialog = FALSE;
DWORD g_dwLastHideTime = 0;
DWORD g_dwLastShowTime = 0;
BOOL g_bCaptivePortal = FALSE;
BOOL g_bScanning = FALSE;
char g_szCurrentSSID[64] = {0};

#define MAX_NETWORKS 150
typedef struct {
    char ssid[64];
    int signal;
    BOOL isSecure;
    BOOL isConnected;
    int authAlg;
    int cipherAlg;
    BOOL hasProfile;
} WiFiNetwork;

WiFiNetwork networks[MAX_NETWORKS];
int numNetworks = 0;

// UI State
int scrollY = 0;
int maxScrollY = 0;
int hoverIndex = -1;
int selectedIndex = -1;
// 0=None, 1=Connect/Disconnect, 3=Refresh, 4=Properties, 5=Forget, 6=CaptivePortalLink
int hoverButton = 0;

// Drag-to-scroll & scrollbar state
BOOL isDraggingList = FALSE;
BOOL isDraggingThumb = FALSE;
int dragStartY = 0;
int dragStartScrollY = 0;
BOOL dragMoved = FALSE;

// Layout constants
#define WND_WIDTH 340
#define WND_HEIGHT 520
#define HEADER_HEIGHT 65
#define ITEM_NORMAL_H 60
#define ITEM_EXPAND_H 150
#define ITEM_EXPAND_CONN_H 115
#define SCROLLBAR_W 6

// Theme Colors (Win 11 Light)
#define CLR_BG        RGB(243, 243, 243)
#define CLR_HEADER    RGB(255, 255, 255)
#define CLR_HOVER     RGB(235, 235, 235)
#define CLR_SELECTED  RGB(249, 249, 249)
#define CLR_TEXT      RGB(20, 20, 20)
#define CLR_TEXT_MUT  RGB(100, 100, 100)
#define CLR_ACCENT    RGB(0, 120, 212)
#define CLR_ACCENT_HV RGB(0, 90, 158)
#define CLR_WARN      RGB(200, 60, 60)
#define CLR_WARN_HV   RGB(170, 40, 40)
#define CLR_BTN_NORM  RGB(225, 225, 225)
#define CLR_BTN_HOVER RGB(205, 205, 205)

const wchar_t *wpa2_template =
    L"<?xml version=\"1.0\"?>\n"
    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
    L"  <name>%ls</name>\n"
    L"  <SSIDConfig><SSID><name>%ls</name></SSID></SSIDConfig>\n"
    L"  <connectionType>ESS</connectionType>\n"
    L"  <connectionMode>auto</connectionMode>\n"
    L"  <MSM><security><authEncryption>\n"
    L"    <authentication>WPA2PSK</authentication><encryption>AES</encryption><useOneX>false</useOneX>\n"
    L"  </authEncryption><sharedKey>\n"
    L"    <keyType>passPhrase</keyType><protected>false</protected><keyMaterial>%ls</keyMaterial>\n"
    L"  </sharedKey></security></MSM>\n"
    L"</WLANProfile>";

const wchar_t *open_template =
    L"<?xml version=\"1.0\"?>\n"
    L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\n"
    L"  <name>%ls</name>\n"
    L"  <SSIDConfig><SSID><name>%ls</name></SSID></SSIDConfig>\n"
    L"  <connectionType>ESS</connectionType>\n"
    L"  <connectionMode>auto</connectionMode>\n"
    L"  <MSM><security><authEncryption>\n"
    L"    <authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX>\n"
    L"  </authEncryption></security></MSM>\n"
    L"</WLANProfile>";

void WINAPI WlanNotificationCallback(PWLAN_NOTIFICATION_DATA data, PVOID context) {
    if (data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM) {
        switch (data->NotificationCode) {
            case wlan_notification_acm_scan_complete:
            case wlan_notification_acm_scan_list_refresh:
            case wlan_notification_acm_connection_complete:
            case wlan_notification_acm_disconnected:
            case wlan_notification_acm_connection_attempt_fail:
                g_bScanning = FALSE;
                PostMessageA((HWND)context, WM_USER_UPDATE_WIFI, 0, 0);
                break;
        }
    }
}

DWORD WINAPI CheckCaptivePortalThread(LPVOID lpParam) {
    BOOL portalDetected = FALSE;
    HINTERNET hInternet = InternetOpenA("WinPEWifi", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (hInternet) {
        DWORD timeout = 4000;
        InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

        HINTERNET hUrl = InternetOpenUrlA(
            hInternet,
            "http://www.msftconnecttest.com/connecttest.txt",
            NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI,
            0);

        if (hUrl) {
            char buf[64] = {0};
            DWORD bytesRead = 0;
            if (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
                if (strstr(buf, "Microsoft Connect Test") == NULL) {
                    portalDetected = TRUE;
                }
            }
            InternetCloseHandle(hUrl);
        }
        InternetCloseHandle(hInternet);
    }

    g_bCaptivePortal = portalDetected;
    if (hMainWnd) {
        PostMessageA(hMainWnd, WM_USER_UPDATE_WIFI, 0, 0);
    }
    return 0;
}

// --- Core Wi-Fi ---

BOOL SelectBestWlanInterface() {
    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    if (WlanEnumInterfaces(hWlan, NULL, &pIfList) != ERROR_SUCCESS || !pIfList || pIfList->dwNumberOfItems == 0) {
        if (pIfList) WlanFreeMemory(pIfList);
        return FALSE;
    }

    int bestIdx = 0;
    for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
        if (pIfList->InterfaceInfo[i].isState == wlan_interface_state_connected) {
            bestIdx = (int)i;
            break;
        }
    }
    wlanGuid = pIfList->InterfaceInfo[bestIdx].InterfaceGuid;
    WlanFreeMemory(pIfList);
    return TRUE;
}

void ConnectNetwork(const char* ssid, const char* password, BOOL isSecure, BOOL hasProfile) {
    wchar_t wSsid[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, ssid, -1, wSsid, 256);

    // Only create/overwrite profile if we don't already have a saved profile,
    // or if the user explicitly typed a new password.
    if (!hasProfile || (password && strlen(password) > 0)) {
        wchar_t wProfileXml[2048] = {0};
        if (isSecure) {
            wchar_t wPass[65] = {0};
            MultiByteToWideChar(CP_UTF8, 0, password ? password : "", -1, wPass, 65);
            swprintf(wProfileXml, 2048, wpa2_template, wSsid, wSsid, wPass);
        } else {
            swprintf(wProfileXml, 2048, open_template, wSsid, wSsid);
        }

        DWORD dwReason = 0;
        WlanSetProfile(hWlan, &wlanGuid, 0, wProfileXml, NULL, TRUE, NULL, &dwReason);
    }

    WLAN_CONNECTION_PARAMETERS params;
    memset(&params, 0, sizeof(params));
    params.wlanConnectionMode = wlan_connection_mode_profile;
    params.strProfile = wSsid;
    params.dot11BssType = dot11_BSS_type_infrastructure;

    WlanConnect(hWlan, &wlanGuid, &params, NULL);
}

void ScanNetworks() {
    numNetworks = 0;
    PWLAN_AVAILABLE_NETWORK_LIST pBssList = NULL;
    if (WlanGetAvailableNetworkList(hWlan, &wlanGuid, 0, NULL, &pBssList) == ERROR_SUCCESS && pBssList) {
        for (DWORD i = 0; i < pBssList->dwNumberOfItems && numNetworks < MAX_NETWORKS; i++) {
            PWLAN_AVAILABLE_NETWORK net = &pBssList->Network[i];
            if (net->dot11Ssid.uSSIDLength == 0) continue;

            char ssid[64] = {0};
            DWORD copyLen = net->dot11Ssid.uSSIDLength < 63 ? net->dot11Ssid.uSSIDLength : 63;
            memcpy(ssid, net->dot11Ssid.ucSSID, copyLen);

            int existingIdx = -1;
            for (int j = 0; j < numNetworks; j++) {
                if (strcmp(networks[j].ssid, ssid) == 0) {
                    existingIdx = j;
                    break;
                }
            }

            BOOL isConn = (net->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
            BOOL hasProf = (net->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;

            if (existingIdx != -1) {
                if ((int)net->wlanSignalQuality > networks[existingIdx].signal) {
                    networks[existingIdx].signal = net->wlanSignalQuality;
                }
                if (isConn) networks[existingIdx].isConnected = TRUE;
                if (hasProf) networks[existingIdx].hasProfile = TRUE;
            } else {
                strcpy(networks[numNetworks].ssid, ssid);
                networks[numNetworks].signal = net->wlanSignalQuality;
                networks[numNetworks].isSecure = (net->dot11DefaultAuthAlgorithm != DOT11_AUTH_ALGO_80211_OPEN);
                networks[numNetworks].isConnected = isConn;
                networks[numNetworks].authAlg = net->dot11DefaultAuthAlgorithm;
                networks[numNetworks].cipherAlg = net->dot11DefaultCipherAlgorithm;

                if (hasProf) {
                    networks[numNetworks].hasProfile = TRUE;
                } else {
                    wchar_t wSsid[256] = {0};
                    MultiByteToWideChar(CP_UTF8, 0, ssid, -1, wSsid, 256);
                    LPWSTR profileXml = NULL;
                    DWORD flags = 0, access = 0;
                    if (WlanGetProfile(hWlan, &wlanGuid, wSsid, NULL, &profileXml, &flags, &access) == ERROR_SUCCESS) {
                        networks[numNetworks].hasProfile = TRUE;
                        WlanFreeMemory(profileXml);
                    } else {
                        networks[numNetworks].hasProfile = FALSE;
                    }
                }
                numNetworks++;
            }
        }
        WlanFreeMemory(pBssList);
    }

    // Sort connected network first, then by signal strength descending
    for (int i = 0; i < numNetworks - 1; i++) {
        for (int j = i + 1; j < numNetworks; j++) {
            BOOL swap = FALSE;
            if (!networks[i].isConnected && networks[j].isConnected) {
                swap = TRUE;
            } else if (networks[i].isConnected == networks[j].isConnected &&
                       networks[j].signal > networks[i].signal) {
                swap = TRUE;
            }
            if (swap) {
                WiFiNetwork tmp = networks[i];
                networks[i] = networks[j];
                networks[j] = tmp;
            }
        }
    }
}

const char* GetAuthString(int alg) {
    switch (alg) {
        case 1: return "Open";
        case 2: return "Shared";
        case 3: return "WPA";
        case 4: return "WPA-PSK";
        case 6: return "WPA2";
        case 7: return "WPA2-PSK";
        case 8: return "WPA3";
        case 9: return "WPA3-SAE";
        default: return "Unknown";
    }
}

const char* GetCipherString(int alg) {
    switch (alg) {
        case 0x00: return "None";
        case 0x01: return "WEP (40-bit)";
        case 0x02: return "TKIP";
        case 0x04: return "AES (CCMP)";
        case 0x05: return "WEP (104-bit)";
        case 0x101: return "WEP";
        default: return "Unknown";
    }
}

void GetAdapterIpAddresses(const GUID* pGuid, char* outStr, int maxLen) {
    outStr[0] = '\0';

    char targetGuidStr[64];
    sprintf(targetGuidStr, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        pGuid->Data1, pGuid->Data2, pGuid->Data3,
        pGuid->Data4[0], pGuid->Data4[1], pGuid->Data4[2], pGuid->Data4[3],
        pGuid->Data4[4], pGuid->Data4[5], pGuid->Data4[6], pGuid->Data4[7]);

    ULONG outBufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
    if (!pAddresses) {
        snprintf(outStr, maxLen, "Memory Error");
        return;
    }

    DWORD dwRetVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, NULL, pAddresses, &outBufLen);
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
        if (!pAddresses) {
            snprintf(outStr, maxLen, "Memory Error");
            return;
        }
        dwRetVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, NULL, pAddresses, &outBufLen);
    }

    if (dwRetVal == NO_ERROR) {
        PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
        BOOL found = FALSE;
        while (pCurrAddresses) {
            if (pCurrAddresses->AdapterName && strcasecmp(pCurrAddresses->AdapterName, targetGuidStr) == 0) {
                PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
                char ipList[512] = "";
                while (pUnicast) {
                    SOCKADDR* sa = pUnicast->Address.lpSockaddr;
                    char ipStr[INET6_ADDRSTRLEN] = {0};
                    if (sa->sa_family == AF_INET) {
                        InetNtopA(AF_INET, &(((struct sockaddr_in*)sa)->sin_addr), ipStr, sizeof(ipStr));
                    } else if (sa->sa_family == AF_INET6) {
                        InetNtopA(AF_INET6, &(((struct sockaddr_in6*)sa)->sin6_addr), ipStr, sizeof(ipStr));
                    }
                    if (ipStr[0] != '\0') {
                        if (ipList[0] != '\0') strncat(ipList, ", ", sizeof(ipList) - strlen(ipList) - 1);
                        strncat(ipList, ipStr, sizeof(ipList) - strlen(ipList) - 1);
                    }
                    pUnicast = pUnicast->Next;
                }
                if (ipList[0] != '\0') {
                    snprintf(outStr, maxLen, "%s", ipList);
                } else {
                    snprintf(outStr, maxLen, "None (Not assigned)");
                }
                found = TRUE;
                break;
            }
            pCurrAddresses = pCurrAddresses->Next;
        }
        if (!found) {
            snprintf(outStr, maxLen, "Not active / No IP");
        }
    } else {
        snprintf(outStr, maxLen, "Unavailable");
    }

    free(pAddresses);
}

HICON CreateWaveTrayIcon(int signal, BOOL connected) {
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
    if (!hbmp || !pPixels) {
        ReleaseDC(NULL, hScreenDC);
        return LoadIcon(NULL, IDI_APPLICATION);
    }
    memset(pPixels, 0, 16 * 16 * 4);

    int tier = 0;
    if (connected) {
        if (signal >= 75) tier = 3;
        else if (signal >= 50) tier = 2;
        else if (signal >= 25) tier = 1;
        else tier = 0;
    }

    const float cx = 7.5f;
    const float cy = 13.5f;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float dist = sqrtf(dx * dx + dy * dy);

            float alpha = 0.0f;
            int barIndex = -1;

            if (dist <= 2.2f) {
                barIndex = 0;
                if (dist <= 1.4f) alpha = 1.0f;
                else alpha = 1.0f - (dist - 1.4f) / 0.8f;
            } else if (dy < -0.2f) {
                float edgeDist = (-dy - fabsf(dx)) * 0.7071f;
                float edgeFactor = 0.0f;
                if (edgeDist >= 0.5f) edgeFactor = 1.0f;
                else if (edgeDist > -0.5f) edgeFactor = edgeDist + 0.5f;

                if (edgeFactor > 0.0f) {
                    float radialAlpha = 0.0f;
                    if (dist >= 3.8f && dist <= 6.2f) {
                        barIndex = 1;
                        radialAlpha = 1.0f - fabsf(dist - 5.0f) / 1.2f;
                    } else if (dist >= 7.3f && dist <= 9.7f) {
                        barIndex = 2;
                        radialAlpha = 1.0f - fabsf(dist - 8.5f) / 1.2f;
                    } else if (dist >= 10.8f && dist <= 13.2f) {
                        barIndex = 3;
                        radialAlpha = 1.0f - fabsf(dist - 12.0f) / 1.2f;
                    }
                    if (radialAlpha > 0.0f) {
                        alpha = radialAlpha * edgeFactor;
                    }
                }
            }

            if (alpha > 0.0f) {
                if (alpha > 1.0f) alpha = 1.0f;
                BOOL isLit = connected && (barIndex <= tier);

                BYTE r = isLit ? 0x10 : 0x88;
                BYTE g = isLit ? 0xB9 : 0x88;
                BYTE b = isLit ? 0x81 : 0x88;
                BYTE a = (BYTE)(alpha * 255.0f);

                r = (BYTE)((r * a) / 255);
                g = (BYTE)((g * a) / 255);
                b = (BYTE)((b * a) / 255);

                pPixels[y * 16 + x] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | b;
            }
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
    DWORD connectInfoSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
    PWLAN_CONNECTION_ATTRIBUTES pConnectInfo = NULL;
    BOOL connected = FALSE;
    int signal = -1;

    if (WlanQueryInterface(hWlan, &wlanGuid, wlan_intf_opcode_current_connection, NULL, &connectInfoSize, (PVOID*)&pConnectInfo, NULL) == ERROR_SUCCESS && pConnectInfo) {
        if (pConnectInfo->isState == wlan_interface_state_connected) {
            connected = TRUE;
            signal = pConnectInfo->wlanAssociationAttributes.wlanSignalQuality;
            char ssid[64] = {0};
            DWORD len = pConnectInfo->wlanAssociationAttributes.dot11Ssid.uSSIDLength;
            if (len > 63) len = 63;
            memcpy(ssid, pConnectInfo->wlanAssociationAttributes.dot11Ssid.ucSSID, len);

            if (strcmp(g_szCurrentSSID, ssid) != 0) {
                strcpy(g_szCurrentSSID, ssid);
                g_bCaptivePortal = FALSE;
                HANDLE hThread = CreateThread(NULL, 0, CheckCaptivePortalThread, NULL, 0, NULL);
                if (hThread) CloseHandle(hThread);
            }

            wchar_t wSsid[64] = {0};
            MultiByteToWideChar(CP_UTF8, 0, ssid, -1, wSsid, 64);
            if (g_bCaptivePortal) {
                swprintf(nid.szTip, 128, L"Action Required: %ls (%d%%)", wSsid, signal);
            } else {
                swprintf(nid.szTip, 128, L"Connected: %ls (%d%%)", wSsid, signal);
            }
        }
        WlanFreeMemory(pConnectInfo);
    }

    if (!connected) {
        g_szCurrentSSID[0] = '\0';
        g_bCaptivePortal = FALSE;
        wcscpy(nid.szTip, L"Wi-Fi: Not Connected");
        signal = -1;
    }

    HICON hOld = nid.hIcon;
    nid.hIcon = CreateWaveTrayIcon(signal, connected);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        Shell_NotifyIconW(NIM_ADD, &nid);
    }
    if (hOld) DestroyIcon(hOld);
}

// --- Keyboard Subclassing ---

LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_UP || wParam == VK_DOWN) {
            SendMessageA(GetParent(hWnd), uMsg, wParam, lParam);
            return 0;
        }
        if (wParam == VK_RETURN) {
            SendMessageA(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(ID_BTN_CONNECT_ENTER, 0), 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            ShowWindow(GetParent(hWnd), SW_HIDE);
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
    } else if (uMsg == WM_CHAR && (wParam == '\r' || wParam == '\n')) {
        return 0; // Suppress beep on Enter
    }
    return CallWindowProcA(OldEditProc, hWnd, uMsg, wParam, lParam);
}

// --- Positioning & Custom Draw Helpers ---

void PositionWindow(HWND hwnd) {
    RECT workArea;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0);
    int x = workArea.right - WND_WIDTH - 12;
    int y = workArea.bottom - WND_HEIGHT - 12;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, WND_WIDTH, WND_HEIGHT, SWP_SHOWWINDOW);
}

int GetTotalContentHeight() {
    int totalH = 0;
    for (int i = 0; i < numNetworks; i++) {
        totalH += (i == selectedIndex)
            ? (networks[i].isConnected ? ITEM_EXPAND_CONN_H : ITEM_EXPAND_H)
            : ITEM_NORMAL_H;
    }
    return totalH;
}

void ClampScroll() {
    int viewH = WND_HEIGHT - HEADER_HEIGHT;
    int totalH = GetTotalContentHeight();
    maxScrollY = totalH - viewH;
    if (maxScrollY < 0) maxScrollY = 0;
    if (scrollY < 0) scrollY = 0;
    if (scrollY > maxScrollY) scrollY = maxScrollY;
}

void DrawWaveIconUI(HDC hdc, int cx, int cy, int signal) {
    HBRUSH hBr = CreateSolidBrush(RGB(16, 185, 129));
    HBRUSH hBrMut = CreateSolidBrush(RGB(210, 210, 210));
    HPEN hPen = CreatePen(PS_SOLID, 3, RGB(16, 185, 129));
    HPEN hPenMut = CreatePen(PS_SOLID, 3, RGB(210, 210, 210));

    HPEN hOldPen = (HPEN)GetCurrentObject(hdc, OBJ_PEN);
    HBRUSH hOldBr = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);

    SelectObject(hdc, (signal >= 0) ? hBr : hBrMut);
    SelectObject(hdc, (signal >= 0) ? hPen : hPenMut);
    Ellipse(hdc, cx - 2, cy - 2, cx + 3, cy + 3);

    SelectObject(hdc, (signal >= 25) ? hPen : hPenMut);
    Arc(hdc, cx - 7, cy - 7, cx + 7, cy + 7, cx + 7, cy - 7, cx - 7, cy - 7);

    SelectObject(hdc, (signal >= 50) ? hPen : hPenMut);
    Arc(hdc, cx - 13, cy - 13, cx + 13, cy + 13, cx + 13, cy - 13, cx - 13, cy - 13);

    SelectObject(hdc, (signal >= 75) ? hPen : hPenMut);
    Arc(hdc, cx - 19, cy - 19, cx + 19, cy + 19, cx + 19, cy - 19, cx - 19, cy - 19);

    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBr);

    DeleteObject(hPen);
    DeleteObject(hPenMut);
    DeleteObject(hBr);
    DeleteObject(hBrMut);
}

void DrawSolidRect(HDC hdc, int x, int y, int w, int h, COLORREF col) {
    RECT r = {x, y, x + w, y + h};
    HBRUSH br = CreateSolidBrush(col);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int rad, COLORREF col) {
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_SOLID, 1, col);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, x, y, x + w, y + h, rad, rad);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

void UpdateEditControlLayout() {
    if (selectedIndex < 0 || selectedIndex >= numNetworks) {
        ShowWindow(hPassEdit, SW_HIDE);
        return;
    }

    int currentY = HEADER_HEIGHT - scrollY;
    for (int i = 0; i < numNetworks; i++) {
        int itemH = (i == selectedIndex)
            ? (networks[i].isConnected ? ITEM_EXPAND_CONN_H : ITEM_EXPAND_H)
            : ITEM_NORMAL_H;

        if (i == selectedIndex) {
            if (networks[i].isConnected || !networks[i].isSecure || networks[i].hasProfile) {
                ShowWindow(hPassEdit, SW_HIDE);
            } else {
                int editY = currentY + 85;
                if (editY < HEADER_HEIGHT || editY + 28 > WND_HEIGHT) {
                    ShowWindow(hPassEdit, SW_HIDE);
                } else {
                    MoveWindow(hPassEdit, 20, editY, 195, 28, TRUE);
                    ShowWindow(hPassEdit, SW_SHOW);
                }
            }
            return;
        }
        currentY += itemH;
    }
}

void PaintUI(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, WND_WIDTH, WND_HEIGHT);

    HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
    HGDIOBJ oldFont = GetCurrentObject(memDC, OBJ_FONT);

    DrawSolidRect(memDC, 0, 0, WND_WIDTH, WND_HEIGHT, CLR_BG);
    SetBkMode(memDC, TRANSPARENT);

    if (numNetworks == 0) {
        SetTextColor(memDC, CLR_TEXT_MUT);
        SelectObject(memDC, hFontNormal);
        RECT emptyR = {0, HEADER_HEIGHT, WND_WIDTH, WND_HEIGHT};
        const char* msg = g_bScanning ? "Scanning for Wi-Fi networks..." : "No Wi-Fi networks found";
        DrawTextA(memDC, msg, -1, &emptyR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    int currentY = HEADER_HEIGHT - scrollY;
    for (int i = 0; i < numNetworks; i++) {
        int itemH = (i == selectedIndex)
            ? (networks[i].isConnected ? ITEM_EXPAND_CONN_H : ITEM_EXPAND_H)
            : ITEM_NORMAL_H;

        if (currentY + itemH > HEADER_HEIGHT && currentY < WND_HEIGHT) {
            COLORREF bgCol = CLR_BG;
            if (i == selectedIndex) bgCol = CLR_SELECTED;
            else if (i == hoverIndex) bgCol = CLR_HOVER;

            if (bgCol != CLR_BG) {
                DrawRoundRect(memDC, 5, currentY, WND_WIDTH - 14, itemH, 10, bgCol);
            }

            DrawWaveIconUI(memDC, 28, currentY + 38, networks[i].signal);

            SetTextColor(memDC, networks[i].isConnected ? CLR_ACCENT : CLR_TEXT);
            SelectObject(memDC, hFontNormal);
            TextOutA(memDC, 58, currentY + 18, networks[i].ssid, (int)strlen(networks[i].ssid));

            if (networks[i].isConnected) {
                if (g_bCaptivePortal) {
                    SetTextColor(memDC, (hoverButton == 6) ? CLR_ACCENT_HV : CLR_WARN);
                    SelectObject(memDC, hFontLink);
                    TextOutA(memDC, WND_WIDTH - 118, currentY + 21, "Open Sign-In", 12);
                } else {
                    SetTextColor(memDC, CLR_TEXT_MUT);
                    SelectObject(memDC, hFontSmall);
                    TextOutA(memDC, WND_WIDTH - 95, currentY + 21, "Connected", 9);
                }
            }

            if (i == selectedIndex) {
                SetTextColor(memDC, CLR_TEXT);
                SelectObject(memDC, hFontNormal);

                if (networks[i].isConnected) {
                    COLORREF btnCol1 = (hoverButton == 1) ? CLR_WARN_HV : CLR_WARN;
                    DrawRoundRect(memDC, 20, currentY + 65, 95, 32, 8, btnCol1);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    RECT btnTxtR1 = {20, currentY + 65, 115, currentY + 97};
                    DrawTextA(memDC, "Disconnect", -1, &btnTxtR1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    COLORREF btnCol4 = (hoverButton == 4) ? CLR_BTN_HOVER : CLR_BTN_NORM;
                    DrawRoundRect(memDC, 122, currentY + 65, 95, 32, 8, btnCol4);
                    SetTextColor(memDC, CLR_TEXT);
                    RECT btnTxtR4 = {122, currentY + 65, 217, currentY + 97};
                    DrawTextA(memDC, "Properties", -1, &btnTxtR4, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    COLORREF btnCol5 = (hoverButton == 5) ? CLR_BTN_HOVER : CLR_BTN_NORM;
                    DrawRoundRect(memDC, 224, currentY + 65, 95, 32, 8, btnCol5);
                    SetTextColor(memDC, CLR_TEXT);
                    RECT btnTxtR5 = {224, currentY + 65, 319, currentY + 97};
                    DrawTextA(memDC, "Forget", -1, &btnTxtR5, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    if (networks[i].isSecure) {
                        if (networks[i].hasProfile) {
                            TextOutA(memDC, 20, currentY + 56, "Saved network. Ready to connect.", 32);

                            COLORREF btnCol5 = (hoverButton == 5) ? CLR_BTN_HOVER : CLR_BTN_NORM;
                            DrawRoundRect(memDC, 120, currentY + 85, 95, 30, 8, btnCol5);
                            SetTextColor(memDC, CLR_TEXT);
                            RECT btnTxtR5 = {120, currentY + 85, 215, currentY + 115};
                            DrawTextA(memDC, "Forget", -1, &btnTxtR5, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        } else {
                            TextOutA(memDC, 20, currentY + 56, "Enter network password:", 23);
                        }
                    } else {
                        TextOutA(memDC, 20, currentY + 56, "Open network. No password required.", 35);
                    }

                    COLORREF btnCol = (hoverButton == 1) ? CLR_ACCENT_HV : CLR_ACCENT;
                    DrawRoundRect(memDC, 225, currentY + 85, 95, 30, 8, btnCol);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    RECT btnTxtR = {225, currentY + 85, 320, currentY + 115};
                    DrawTextA(memDC, "Connect", -1, &btnTxtR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }
        currentY += itemH;
    }

    // Draw Scrollbar if content overflows
    int viewH = WND_HEIGHT - HEADER_HEIGHT;
    int totalH = GetTotalContentHeight();
    if (totalH > viewH) {
        int thumbH = (viewH * viewH) / totalH;
        if (thumbH < 30) thumbH = 30;
        int thumbY = HEADER_HEIGHT + (scrollY * (viewH - thumbH)) / maxScrollY;
        DrawRoundRect(memDC, WND_WIDTH - SCROLLBAR_W - 2, thumbY, SCROLLBAR_W, thumbH, 3,
                      isDraggingThumb ? CLR_ACCENT : RGB(180, 180, 180));
    }

    // Header
    DrawSolidRect(memDC, 0, 0, WND_WIDTH, HEADER_HEIGHT, CLR_HEADER);
    DrawSolidRect(memDC, 0, HEADER_HEIGHT - 1, WND_WIDTH, 1, RGB(220, 220, 220));

    SetTextColor(memDC, CLR_TEXT);
    SelectObject(memDC, hFontLarge);
    TextOutA(memDC, 15, 18, "Available networks", 18);

    SetTextColor(memDC, (hoverButton == 3) ? CLR_ACCENT : CLR_TEXT_MUT);
    SelectObject(memDC, hFontNormal);
    TextOutA(memDC, WND_WIDTH - 80, 22, "Refresh", 7);

    BitBlt(hdc, 0, 0, WND_WIDTH, WND_HEIGHT, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldFont);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// --- Window Procedure ---

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == g_uTaskbarRestart && g_uTaskbarRestart != 0) {
        Shell_NotifyIconW(NIM_ADD, &nid);
        UpdateTrayIcon();
        return 0;
    }

    switch (uMsg) {
        case WM_CREATE: {
            hFontLarge = CreateFontA(22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontNormal = CreateFontA(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontSmall = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontLink = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, 0, TRUE, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

            hPassEdit = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
                                        0, 0, 0, 0, hwnd, NULL, GetModuleHandleA(NULL), NULL);
            SendMessageA(hPassEdit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
            OldEditProc = (WNDPROC)SetWindowLongPtrA(hPassEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

            WlanRegisterNotification(hWlan, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, WlanNotificationCallback, hwnd, NULL, NULL);
            ScanNetworks();
            SetTimer(hwnd, IDT_REFRESH_TIMER, 3000, NULL);
            break;
        }

        case WM_TIMER:
            if (wParam == IDT_REFRESH_TIMER && IsWindowVisible(hwnd) && !isDraggingList && !isDraggingThumb) {
                ScanNetworks();
                ClampScroll();
                UpdateTrayIcon();
                UpdateEditControlLayout();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && !g_bShowingDialog) {
                // Ignore transient focus changes immediately after showing the window
                if (GetTickCount() - g_dwLastShowTime > 250 && IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                    ShowWindow(hPassEdit, SW_HIDE);
                    g_dwLastHideTime = GetTickCount();
                }
            }
            break;

        case WM_USER_UPDATE_WIFI:
            ScanNetworks();
            ClampScroll();
            UpdateTrayIcon();
            UpdateEditControlLayout();
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case WM_PAINT:
            PaintUI(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                ShowWindow(hPassEdit, SW_HIDE);
                break;
            }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                if (numNetworks == 0) break;

                int newIdx = selectedIndex;
                if (wParam == VK_DOWN) {
                    newIdx = (selectedIndex < numNetworks - 1) ? selectedIndex + 1 : selectedIndex;
                    if (selectedIndex == -1) newIdx = 0;
                } else if (wParam == VK_UP) {
                    newIdx = (selectedIndex > 0) ? selectedIndex - 1 : 0;
                }

                if (newIdx != selectedIndex) {
                    selectedIndex = newIdx;
                    SetWindowTextA(hPassEdit, "");

                    int yPos = HEADER_HEIGHT;
                    for (int i = 0; i < selectedIndex; i++) yPos += ITEM_NORMAL_H;
                    int targetH = networks[selectedIndex].isConnected ? ITEM_EXPAND_CONN_H : ITEM_EXPAND_H;

                    if (yPos - scrollY < HEADER_HEIGHT) {
                        scrollY = yPos - HEADER_HEIGHT;
                    } else if (yPos + targetH - scrollY > WND_HEIGHT) {
                        scrollY = yPos + targetH - WND_HEIGHT;
                    }

                    ClampScroll();
                    UpdateEditControlLayout();

                    if (IsWindowVisible(hPassEdit)) SetFocus(hPassEdit);
                    else SetFocus(hwnd);

                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (wParam == VK_RETURN) {
                if (selectedIndex != -1 && !networks[selectedIndex].isConnected) {
                    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CONNECT_ENTER, 0), 0);
                }
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            scrollY -= (zDelta / WHEEL_DELTA) * 35;
            ClampScroll();
            UpdateEditControlLayout();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_MOUSEMOVE: {
            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);

            if (isDraggingThumb) {
                int viewH = WND_HEIGHT - HEADER_HEIGHT;
                int totalH = GetTotalContentHeight();
                int thumbH = (viewH * viewH) / totalH;
                if (thumbH < 30) thumbH = 30;
                int trackH = viewH - thumbH;
                if (trackH > 0) {
                    int dy = my - dragStartY;
                    scrollY = dragStartScrollY + (dy * maxScrollY) / trackH;
                    ClampScroll();
                    UpdateEditControlLayout();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                break;
            }

            if (isDraggingList) {
                int dy = dragStartY - my;
                if (abs(dy) > 4) dragMoved = TRUE;
                if (dragMoved) {
                    scrollY = dragStartScrollY + dy;
                    ClampScroll();
                    UpdateEditControlLayout();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                break;
            }

            int oldHover = hoverIndex;
            int oldBtn = hoverButton;
            hoverIndex = -1;
            hoverButton = 0;

            if (my < HEADER_HEIGHT) {
                if (mx > WND_WIDTH - 95) hoverButton = 3;
            } else {
                int currentY = HEADER_HEIGHT - scrollY;
                for (int i = 0; i < numNetworks; i++) {
                    int itemH = (i == selectedIndex)
                        ? (networks[i].isConnected ? ITEM_EXPAND_CONN_H : ITEM_EXPAND_H)
                        : ITEM_NORMAL_H;

                    if (my >= currentY && my < currentY + itemH) {
                        hoverIndex = i;
                        if (networks[i].isConnected && g_bCaptivePortal &&
                            mx >= WND_WIDTH - 122 && mx <= WND_WIDTH - 15 &&
                            my >= currentY + 15 && my <= currentY + 42) {
                            hoverButton = 6;
                        } else if (i == selectedIndex) {
                            if (networks[i].isConnected) {
                                if (my >= currentY + 65 && my <= currentY + 97) {
                                    if (mx >= 20 && mx <= 115) hoverButton = 1;
                                    else if (mx >= 122 && mx <= 217) hoverButton = 4;
                                    else if (mx >= 224 && mx <= 319) hoverButton = 5;
                                }
                            } else {
                                if (my >= currentY + 85 && my <= currentY + 115) {
                                    if (mx >= 225 && mx <= 320) hoverButton = 1;
                                    else if (networks[i].isSecure && networks[i].hasProfile && mx >= 120 && mx <= 215) hoverButton = 5;
                                }
                            }
                        }
                        break;
                    }
                    currentY += itemH;
                }
            }

            if (oldHover != hoverIndex || oldBtn != hoverButton) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);

            if (my < HEADER_HEIGHT && hoverButton == 0) {
                ReleaseCapture();
                SendMessageA(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }

            if (my >= HEADER_HEIGHT && maxScrollY > 0 && mx >= WND_WIDTH - 14) {
                isDraggingThumb = TRUE;
                dragStartY = my;
                dragStartScrollY = scrollY;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (my >= HEADER_HEIGHT && hoverButton == 0) {
                isDraggingList = TRUE;
                dragMoved = FALSE;
                dragStartY = my;
                dragStartScrollY = scrollY;
                SetCapture(hwnd);
                return 0;
            }
            break;
        }

        case WM_LBUTTONUP: {
            if (isDraggingThumb) {
                isDraggingThumb = FALSE;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (isDraggingList) {
                isDraggingList = FALSE;
                ReleaseCapture();
                if (dragMoved) {
                    return 0;
                }
            }

            if (hoverButton == 6) {
                ShellExecuteA(NULL, "open", "http://www.msftconnecttest.com/redirect", NULL, NULL, SW_SHOWNORMAL);
            } else if (hoverButton == 3) {
                g_bScanning = TRUE;
                WlanScan(hWlan, &wlanGuid, NULL, NULL, NULL);
                ScanNetworks();
                selectedIndex = -1;
                scrollY = 0;
                ClampScroll();
                UpdateEditControlLayout();
                SetFocus(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hoverButton == 1 && selectedIndex != -1) {
                if (networks[selectedIndex].isConnected) {
                    WlanDisconnect(hWlan, &wlanGuid, NULL);
                    ScanNetworks();
                    selectedIndex = -1;
                    ClampScroll();
                    UpdateEditControlLayout();
                    SetFocus(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else {
                    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_CONNECT_ENTER, 0), 0);
                }
            } else if (hoverButton == 4 && selectedIndex != -1) {
                char password[256] = {0};
                DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                LPWSTR profileXml = NULL;
                DWORD access = 0;
                wchar_t wSsid[256] = {0};
                MultiByteToWideChar(CP_UTF8, 0, networks[selectedIndex].ssid, -1, wSsid, 256);

                if (WlanGetProfile(hWlan, &wlanGuid, wSsid, NULL, &profileXml, &flags, &access) == ERROR_SUCCESS && profileXml) {
                    wchar_t* keyStart = wcsstr(profileXml, L"<keyMaterial>");
                    if (keyStart) {
                        keyStart += 13;
                        wchar_t* keyEnd = wcsstr(keyStart, L"</keyMaterial>");
                        if (keyEnd) {
                            int len = (int)(keyEnd - keyStart);
                            if (len > 0 && len < 255) {
                                WideCharToMultiByte(CP_UTF8, 0, keyStart, len, password, 255, NULL, NULL);
                            }
                        }
                    }
                    WlanFreeMemory(profileXml);
                }

                char ipBuffer[256] = "None";
                GetAdapterIpAddresses(&wlanGuid, ipBuffer, sizeof(ipBuffer));

                char props[1024];
                snprintf(props, sizeof(props),
                    "Network Name (SSID):\t %s\nSignal Quality:\t\t %d%%\nSecurity:\t\t %s\nAuthentication:\t\t %s\nEncryption:\t\t %s\nIP Address(es):\t\t %s\nStatus:\t\t\t Connected",
                    networks[selectedIndex].ssid,
                    networks[selectedIndex].signal,
                    networks[selectedIndex].isSecure ? "Secured" : "Open",
                    GetAuthString(networks[selectedIndex].authAlg),
                    GetCipherString(networks[selectedIndex].cipherAlg),
                    ipBuffer);

                g_bShowingDialog = TRUE;
                if (strlen(password) > 0) {
                    strncat(props, "\n\nWould you like to copy the Wi-Fi password to the clipboard?", sizeof(props) - strlen(props) - 1);
                    if (MessageBoxA(hwnd, props, "Network Properties", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                        if (OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, strlen(password) + 1);
                            if (hg) {
                                memcpy(GlobalLock(hg), password, strlen(password) + 1);
                                GlobalUnlock(hg);
                                SetClipboardData(CF_TEXT, hg);
                            }
                            CloseClipboard();
                        }
                    }
                } else {
                    MessageBoxA(hwnd, props, "Network Properties", MB_OK | MB_ICONINFORMATION);
                }
                g_bShowingDialog = FALSE;
            } else if (hoverButton == 5 && selectedIndex != -1) {
                wchar_t wSsid[256] = {0};
                MultiByteToWideChar(CP_UTF8, 0, networks[selectedIndex].ssid, -1, wSsid, 256);

                WlanDeleteProfile(hWlan, &wlanGuid, wSsid, NULL);
                if (networks[selectedIndex].isConnected) {
                    WlanDisconnect(hWlan, &wlanGuid, NULL);
                }
                networks[selectedIndex].hasProfile = FALSE;
                ScanNetworks();
                ClampScroll();
                UpdateEditControlLayout();
                if (IsWindowVisible(hPassEdit)) SetFocus(hPassEdit);
                else SetFocus(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hoverIndex != -1 && hoverIndex != selectedIndex) {
                selectedIndex = hoverIndex;
                SetWindowTextA(hPassEdit, "");
                ClampScroll();
                UpdateEditControlLayout();
                if (IsWindowVisible(hPassEdit)) SetFocus(hPassEdit);
                else SetFocus(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_USER_SHOW_MANAGER: {
            g_dwLastShowTime = GetTickCount();
            SelectBestWlanInterface();
            PositionWindow(hwnd);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
            g_bScanning = TRUE;
            WlanScan(hWlan, &wlanGuid, NULL, NULL, NULL);
            ScanNetworks();
            ClampScroll();
            UpdateEditControlLayout();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_USER_TRAY: {
            if (lParam == WM_LBUTTONUP) {
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                    ShowWindow(hPassEdit, SW_HIDE);
                    g_dwLastHideTime = GetTickCount();
                } else if (GetTickCount() - g_dwLastHideTime > 200) {
                    SendMessageA(hwnd, WM_USER_SHOW_MANAGER, 0, 0);
                }
            } else if (lParam == WM_RBUTTONUP) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit Wi-Fi Manager");
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == ID_BTN_CONNECT_ENTER) {
                if (selectedIndex != -1 && !networks[selectedIndex].isConnected) {
                    char pass[65] = {0};
                    GetWindowTextA(hPassEdit, pass, 65);
                    ConnectNetwork(
                        networks[selectedIndex].ssid,
                        pass,
                        networks[selectedIndex].isSecure,
                        networks[selectedIndex].hasProfile);
                }
            }
            break;
        }

        case WM_DESTROY:
            KillTimer(hwnd, IDT_REFRESH_TIMER);
            WlanRegisterNotification(hWlan, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, NULL, NULL, NULL, NULL);
            Shell_NotifyIconW(NIM_DELETE, &nid);
            DeleteObject(hFontLarge);
            DeleteObject(hFontNormal);
            DeleteObject(hFontSmall);
            DeleteObject(hFontLink);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// --- CLI + Main ---

int main(int argc, char *argv[]) {
    BOOL isGuiMode = (argc == 1);

    if (!isGuiMode) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    if (isGuiMode) {
        HANDLE hMutex = CreateMutexA(NULL, FALSE, "WinPE_Wifi_Tray_Mutex");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND hExisting = FindWindowA("WinPEWifiWin11", NULL);
            if (hExisting) {
                AllowSetForegroundWindow(ASFW_ANY);
                PostMessageA(hExisting, WM_USER_SHOW_MANAGER, 0, 0);
            }
            CloseHandle(hMutex);
            return 0;
        }
    }

    DWORD dwMaxClient = 2, dwCurVersion = 0;
    if (WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hWlan) != ERROR_SUCCESS) {
        if (isGuiMode) MessageBoxA(NULL, "Could not open WLAN. Ensure 'wlansvc' is running.", "Error", MB_ICONERROR);
        else printf("Error: WLAN Handle failed.\n");
        return 1;
    }

    if (!SelectBestWlanInterface()) {
        if (isGuiMode) MessageBoxA(NULL, "No Wi-Fi adapters found.", "Error", MB_ICONERROR);
        else printf("Error: No Wi-Fi adapters found.\n");
        WlanCloseHandle(hWlan, NULL);
        return 1;
    }

    if (!isGuiMode) {
        if (strcmp(argv[1], "connect") == 0 && argc >= 3) {
            BOOL hasPass = (argc >= 4 && strlen(argv[3]) > 0);
            ConnectNetwork(argv[2], hasPass ? argv[3] : NULL, hasPass, FALSE);
            printf("Connection request sent.\n");
        }
        WlanCloseHandle(hWlan, NULL);
        FreeConsole();
        return 0;
    }

    g_uTaskbarRestart = RegisterWindowMessageA("TaskbarCreated");

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "WinPEWifiWin11";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hMainWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "WinPEWifiWin11", "",
        WS_POPUP,
        0, 0, WND_WIDTH, WND_HEIGHT,
        NULL, NULL, GetModuleHandleA(NULL), NULL);

    HRGN hRgn = CreateRoundRectRgn(0, 0, WND_WIDTH, WND_HEIGHT, 16, 16);
    SetWindowRgn(hMainWnd, hRgn, TRUE);

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hMainWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TRAY;
    nid.hIcon = CreateWaveTrayIcon(-1, FALSE);
    wcscpy(nid.szTip, L"Wi-Fi Manager");
    Shell_NotifyIconW(NIM_ADD, &nid);

    SendMessageA(hMainWnd, WM_USER_SHOW_MANAGER, 0, 0);
    UpdateTrayIcon();

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    WlanCloseHandle(hWlan, NULL);
    return 0;
}