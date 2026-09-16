/* ============================================================================
 * Workspacer - Multi-Tab Application Workspace Manager
 *
 * COMPILATION INSTRUCTIONS:
 *
 * With GCC (MinGW-w64):
 *    gcc -Os -s -Wl,--subsystem,windows -mwindows -o wkspacer.exe wkspacer.c -lgdi32 -lcomdlg32 -lcomctl32 -lshlwapi -lshell32 -lole32 -luuid
 *
 * REQUIREMENTS: Windows XP or later
 * DEPENDENCIES: Win32 API only (USER32, GDI32, COMDLG32, COMCTL32, SHELL32, SHLWAPI, OLE32, UUID)
 * ============================================================================ */

#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <initguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER 92
#endif

/* ==================== CONSTANTS & COLORS ==================== */
#define COL_NAV_RGB          RGB(0x1B, 0x36, 0x4A)
#define COL_PANEL_RGB        RGB(0xEC, 0xEF, 0xF1)
#define COL_FILTER_BG        RGB(0xDD, 0xE3, 0xE8)
#define COL_STATUS_RGB       RGB(0x24, 0x36, 0x47)
#define COL_TEXT_RGB         RGB(0xFF, 0xFF, 0xFF)
#define COL_WORKSPACE_RGB    RGB(0x1E, 0x1E, 0x1E)

#define COL_TAB_INACTIVE     RGB(0xE2, 0xE8, 0xF0)
#define COL_TAB_ACTIVE       RGB(0x93, 0xC5, 0xFD)
#define COL_TAB_TEXT_IN      RGB(0x33, 0x41, 0x55)
#define COL_TAB_TEXT_ACT     RGB(0x1B, 0x36, 0x4A)

#define DEFAULT_WIDTH        1024
#define DEFAULT_HEIGHT       720
#define STATUS_HEIGHT        28
#define TAB_WIDTH            85
#define TAB_HEIGHT           28
#define MAX_TABS             50

/* Control IDs */
#define IDC_HEADER           1001
#define IDC_FILTER           1002
#define IDC_WORKSPACE        1003
#define IDC_STATUS_PANEL     1004
#define IDC_INP_PROG         1005
#define IDC_INP_PARAMS       1006
#define IDC_BTN_BROWSE       1007
#define IDC_BTN_SAVE         1008
#define IDC_BTN_DELETE       1009
#define IDC_BTN_CLOSE        1010
#define IDC_OUTPUT_EDIT      1011

#define IDT_SYNC_WATCHDOG    2001

/* ==================== DATA STRUCTURES ==================== */
typedef struct {
    char szProgram[MAX_PATH];
    char szParams[1024];
    char szTitle[128];
    HWND hWndTask;
    DWORD dwPID;
    BOOL bPoppedOut;
    BOOL bUseParamTitle;
    BOOL bCaptureOutput;
    int iOffsetTop;      
    int iOffsetBottom;
    char *pszOutput;
    RECT rcTab;
} TAB_ITEM;

typedef struct {
    DWORD dwTargetPID;
    BOOL  bIsExplorer;
    BOOL  bIsCmd;
    HWND  hWndFound;
} FIND_WND_CTX;

static TAB_ITEM g_Tabs[MAX_TABS];
static int      g_nTabCount        = 0;
static int      g_nActiveTab       = -1;
static int      g_iHeaderH         = 38;
static int      g_iFilterH         = 48;
static BOOL     g_bFullScreen      = FALSE;
static char     g_szIniPath[MAX_PATH];

/* Windows & Controls */
static HWND g_hMainWnd      = NULL;
static HWND g_hHeader       = NULL;
static HWND g_hFilter       = NULL;
static HWND g_hWorkspace    = NULL;
static HWND g_hStatus       = NULL;
static HWND g_hOutputEdit   = NULL;
static HWND g_hInpProg      = NULL;
static HWND g_hInpParams    = NULL;
static HWND g_hBtnBrowse    = NULL;
static HWND g_hBtnSave      = NULL;
static HWND g_hBtnDelete    = NULL;
static HWND g_hBtnClose     = NULL;

/* Fonts & Brushes */
static HFONT g_hFontSegoe   = NULL;
static HFONT g_hFontSegoeB  = NULL;
static HFONT g_hFontTahoma  = NULL;
static HBRUSH g_hBrushNav   = NULL;
static HBRUSH g_hBrushFilter= NULL;
static HBRUSH g_hBrushStatus= NULL;
static HBRUSH g_hBrushWork  = NULL;

/* Mouse Drag & Hover State */
static BOOL g_bMouseDownPrev    = FALSE;
static BOOL g_bDragStartedOnTab = FALSE;
static int  g_nDragSourceTab    = -1;
static int  g_nDragInitTab      = -1;
static int  g_nHoverTabIdx      = -1;
static DWORD g_dwHoverStart     = 0;
static BOOL g_bSplitterDragging = FALSE;
static RECT g_rcAddTab;

/* Pre-Launch Window Snapshot */
static HWND g_aPreSnap[1024];
static int  g_nPreSnapCount = 0;

/* Forward Declarations */
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK HeaderWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK FilterWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WorkspaceWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK StatusWndProc(HWND, UINT, WPARAM, LPARAM);

static void _GetCleanName(const char *pszPath, char *pszOut, size_t nMax);
static void _ExtractParamFileName(const char *pszParams, char *pszOut, size_t nMax);
static void _ResolveDropTarget(const char *pszInput, char *pszProgOut, char *pszParamsOut);
static void _LoadTabsFromIni(void);
static void _SaveAllTabsToIni(void);
static void _SaveWindowState(void);
static void _LayoutTabs(void);
static void _UpdateLayout(int iWidth, int iHeight);
static void _SelectNav(int nIndex);
static void _AddNewTab(const char *pszProg, const char *pszParams);
static void _DeleteCurrentTab(void);
static void _ApplyAndExecuteCurrentTab(void);
static void _PopOutWindow(HWND hWnd);
static void _EmbedWindow(HWND hWnd);
static void _ToggleRestoreTab(void);
static void _StartOrShowTask(int nIndex);
static void _CloseEmbeddedTask(int nIndex);
static void _FocusEmbeddedWindow(HWND hWnd);
static void _SyncActiveWindowPos(void);
static void _RunQuickConsoleCapture(int nIndex, const char *pszProg, const char *pszParams);
static void _SetStatus(const char *pszText);
static int  _GetTabAtPoint(int x, int y);
static int  _GetCaptionOffset(void);

/* Window Enumeration Callbacks */
static BOOL CALLBACK SnapshotEnumProc(HWND hWnd, LPARAM lParam) {
    (void)lParam;
    if (g_nPreSnapCount < 1024) {
        g_aPreSnap[g_nPreSnapCount++] = hWnd;
    }
    return TRUE;
}

static BOOL IsInSnapshot(HWND hWnd) {
    for (int i = 0; i < g_nPreSnapCount; i++) {
        if (g_aPreSnap[i] == hWnd) return TRUE;
    }
    return FALSE;
}

static BOOL CALLBACK FindNewWindowProc(HWND hWnd, LPARAM lParam) {
    FIND_WND_CTX *pCtx = (FIND_WND_CTX *)lParam;
    if (!IsWindow(hWnd) || !IsWindowVisible(hWnd)) return TRUE;

    char szClass[64];
    GetClassNameA(hWnd, szClass, sizeof(szClass));

    if (lstrcmpiA(szClass, "tooltips_class32") == 0 ||
        lstrcmpiA(szClass, "Progman") == 0 ||
        lstrcmpiA(szClass, "Shell_TrayWnd") == 0) {
        return TRUE;
    }

    DWORD dwWndPID = 0;
    GetWindowThreadProcessId(hWnd, &dwWndPID);

    if (pCtx->dwTargetPID != 0 && dwWndPID == pCtx->dwTargetPID) {
        pCtx->hWndFound = hWnd;
        return FALSE;
    }

    if (!IsInSnapshot(hWnd)) {
        if (pCtx->bIsExplorer && lstrcmpiA(szClass, "CabinetWClass") == 0) {
            pCtx->hWndFound = hWnd;
            return FALSE;
        }
        if (pCtx->bIsCmd && (lstrcmpiA(szClass, "ConsoleWindowClass") == 0 || lstrcmpiA(szClass, "CASCADIA_HOSTING_WINDOW_CLASS") == 0)) {
            pCtx->hWndFound = hWnd;
            return FALSE;
        }
        if (!pCtx->bIsExplorer && !pCtx->bIsCmd) {
            RECT rc;
            GetWindowRect(hWnd, &rc);
            if ((rc.right - rc.left) > 60 && (rc.bottom - rc.top) > 40) {
                pCtx->hWndFound = hWnd;
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* ==================== ENTRY POINT ==================== */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)nCmdShow;

    OleInitialize(NULL);
    InitCommonControls();

    char szModule[MAX_PATH];
    GetModuleFileNameA(hInstance, szModule, MAX_PATH);
    PathRemoveExtensionA(szModule);
    snprintf(g_szIniPath, sizeof(g_szIniPath), "%s.ini", szModule);

    if (strstr(lpCmdLine, "-f") || strstr(lpCmdLine, "/f") || strstr(lpCmdLine, "-F") || strstr(lpCmdLine, "/F")) {
        g_bFullScreen = TRUE;
    }

    g_iFilterH = GetPrivateProfileIntA("Layout", "FilterH", 48, g_szIniPath);

    g_hFontSegoe   = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    g_hFontSegoeB  = CreateFontA(-12, 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    g_hFontTahoma  = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Tahoma");

    g_hBrushNav    = CreateSolidBrush(COL_NAV_RGB);
    g_hBrushFilter = CreateSolidBrush(COL_FILTER_BG);
    g_hBrushStatus = CreateSolidBrush(COL_STATUS_RGB);
    g_hBrushWork   = CreateSolidBrush(COL_WORKSPACE_RGB);

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = "WkspacerMainClass";
    RegisterClassExA(&wc);

    wc.lpfnWndProc   = HeaderWndProc;
    wc.lpszClassName = "WkspacerHeaderClass";
    RegisterClassExA(&wc);

    wc.lpfnWndProc   = FilterWndProc;
    wc.lpszClassName = "WkspacerFilterClass";
    RegisterClassExA(&wc);

    wc.lpfnWndProc   = WorkspaceWndProc;
    wc.lpszClassName = "WkspacerWorkClass";
    RegisterClassExA(&wc);

    wc.lpfnWndProc   = StatusWndProc;
    wc.lpszClassName = "WkspacerStatusClass";
    RegisterClassExA(&wc);

    int iW = GetPrivateProfileIntA("Window", "Width",  DEFAULT_WIDTH,  g_szIniPath);
    int iH = GetPrivateProfileIntA("Window", "Height", DEFAULT_HEIGHT, g_szIniPath);
    int iX = GetPrivateProfileIntA("Window", "X", -1, g_szIniPath);
    int iY = GetPrivateProfileIntA("Window", "Y", -1, g_szIniPath);

    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

    if (g_bFullScreen) {
        dwStyle = WS_POPUP | WS_CLIPCHILDREN;
        iX = 0;
        iY = 0;
        iW = GetSystemMetrics(SM_CXSCREEN);
        iH = GetSystemMetrics(SM_CYSCREEN);
    } else {
        if (iX == -1 || iY == -1) {
            iX = CW_USEDEFAULT;
            iY = CW_USEDEFAULT;
        }
    }

    g_hMainWnd = CreateWindowExA(WS_EX_ACCEPTFILES, "WkspacerMainClass", "Workspacer", dwStyle,
                                 iX, iY, iW, iH, NULL, NULL, hInstance, NULL);

    g_hHeader = CreateWindowExA(WS_EX_ACCEPTFILES, "WkspacerHeaderClass", "",
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                0, 0, 100, 100, g_hMainWnd, (HMENU)IDC_HEADER, hInstance, NULL);

    g_hFilter = CreateWindowExA(WS_EX_ACCEPTFILES, "WkspacerFilterClass", "",
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                0, 0, 100, 100, g_hMainWnd, (HMENU)IDC_FILTER, hInstance, NULL);

    g_hWorkspace = CreateWindowExA(WS_EX_ACCEPTFILES, "WkspacerWorkClass", "",
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                   0, 0, 100, 100, g_hMainWnd, (HMENU)IDC_WORKSPACE, hInstance, NULL);

    g_hStatus = CreateWindowExA(WS_EX_ACCEPTFILES, "WkspacerStatusClass", "",
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                0, 0, 100, 100, g_hMainWnd, (HMENU)IDC_STATUS_PANEL, hInstance, NULL);

    DragAcceptFiles(g_hMainWnd, TRUE);
    DragAcceptFiles(g_hHeader, TRUE);
    DragAcceptFiles(g_hFilter, TRUE);
    DragAcceptFiles(g_hWorkspace, TRUE);

    g_hInpProg = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 0, 0, 0, 0, g_hFilter, (HMENU)IDC_INP_PROG, hInstance, NULL);
    SendMessage(g_hInpProg, WM_SETFONT, (WPARAM)g_hFontSegoe, TRUE);

    g_hInpParams = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   0, 0, 0, 0, g_hFilter, (HMENU)IDC_INP_PARAMS, hInstance, NULL);
    SendMessage(g_hInpParams, WM_SETFONT, (WPARAM)g_hFontSegoe, TRUE);

    g_hBtnBrowse = CreateWindowA("BUTTON", "Browse", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 0, 0, 0, 0, g_hFilter, (HMENU)IDC_BTN_BROWSE, hInstance, NULL);
    SendMessage(g_hBtnBrowse, WM_SETFONT, (WPARAM)g_hFontSegoeB, TRUE);

    g_hBtnSave = CreateWindowA("BUTTON", "Save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               0, 0, 0, 0, g_hFilter, (HMENU)IDC_BTN_SAVE, hInstance, NULL);
    SendMessage(g_hBtnSave, WM_SETFONT, (WPARAM)g_hFontSegoeB, TRUE);

    g_hBtnDelete = CreateWindowA("BUTTON", "Delete", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 0, 0, 0, 0, g_hFilter, (HMENU)IDC_BTN_DELETE, hInstance, NULL);
    SendMessage(g_hBtnDelete, WM_SETFONT, (WPARAM)g_hFontSegoeB, TRUE);

    g_hBtnClose = CreateWindowA("BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                0, 0, 0, 0, g_hFilter, (HMENU)IDC_BTN_CLOSE, hInstance, NULL);
    SendMessage(g_hBtnClose, WM_SETFONT, (WPARAM)g_hFontSegoeB, TRUE);

    g_hOutputEdit = CreateWindowExA(0, "EDIT", "",
                                    WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
                                    0, 0, 100, 100, g_hWorkspace, (HMENU)IDC_OUTPUT_EDIT, hInstance, NULL);
    SendMessage(g_hOutputEdit, WM_SETFONT, (WPARAM)g_hFontTahoma, TRUE);

    _LoadTabsFromIni();

    RECT rcClient;
    GetClientRect(g_hMainWnd, &rcClient);
    _UpdateLayout(rcClient.right, rcClient.bottom);

    int nSavedActive = GetPrivateProfileIntA("Window", "ActiveTab", 0, g_szIniPath);
    if (nSavedActive < 0 || nSavedActive >= g_nTabCount) nSavedActive = 0;
    _SelectNav(nSavedActive);

    ShowWindow(g_hMainWnd, SW_SHOW);
    UpdateWindow(g_hMainWnd);

    if (g_bFullScreen) {
        _SetStatus("Started in full screen mode (-f)");
    }

    SetTimer(g_hMainWnd, IDT_SYNC_WATCHDOG, 100, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    for (int i = 0; i < g_nTabCount; i++) {
        if (g_Tabs[i].pszOutput) {
            free(g_Tabs[i].pszOutput);
            g_Tabs[i].pszOutput = NULL;
        }
    }

    DeleteObject(g_hFontSegoe);
    DeleteObject(g_hFontSegoeB);
    DeleteObject(g_hFontTahoma);
    DeleteObject(g_hBrushNav);
    DeleteObject(g_hBrushFilter);
    DeleteObject(g_hBrushStatus);
    DeleteObject(g_hBrushWork);

    OleUninitialize();
    return (int)msg.wParam;
}

/* ==================== MAIN WINDOW PROCEDURE ==================== */
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_BROWSE: {
                    OPENFILENAMEA ofn;
                    char szFile[MAX_PATH] = "";
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner   = hWnd;
                    ofn.lpstrFilter = "All Files (*.*)\0*.*\0Executables (*.exe;*.bat;*.cmd)\0*.exe;*.bat;*.cmd\0";
                    ofn.lpstrFile   = szFile;
                    ofn.nMaxFile    = sizeof(szFile);
                    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

                    if (GetOpenFileNameA(&ofn)) {
                        char szProg[MAX_PATH], szParams[1024];
                        _ResolveDropTarget(szFile, szProg, szParams);
                        SetWindowTextA(g_hInpProg, szProg);
                        SetWindowTextA(g_hInpParams, szParams);
                        _ApplyAndExecuteCurrentTab();
                    }
                    return 0;
                }
                case IDC_BTN_SAVE:
                    _ApplyAndExecuteCurrentTab();
                    return 0;
                case IDC_BTN_DELETE:
                    _DeleteCurrentTab();
                    return 0;
                case IDC_BTN_CLOSE:
                    if (g_nActiveTab >= 0) {
                        _CloseEmbeddedTask(g_nActiveTab);
                        char szLog[64];
                        snprintf(szLog, sizeof(szLog), "Closed task for Tab %d", g_nActiveTab + 1);
                        _SetStatus(szLog);
                    }
                    return 0;
            }
            break;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char szDropped[MAX_PATH];
            if (DragQueryFileA(hDrop, 0, szDropped, MAX_PATH)) {
                POINT pt;
                DragQueryPoint(hDrop, &pt);
                ClientToScreen(hWnd, &pt);
                ScreenToClient(g_hHeader, &pt);
                int nDropTab = _GetTabAtPoint(pt.x, pt.y);

                char szProg[MAX_PATH], szParams[1024];
                _ResolveDropTarget(szDropped, szProg, szParams);

                if (nDropTab == -1) {
                    _AddNewTab(szProg, szParams);
                } else {
                    strncpy(g_Tabs[nDropTab].szProgram, szProg, MAX_PATH - 1);
                    strncpy(g_Tabs[nDropTab].szParams, szParams, sizeof(g_Tabs[nDropTab].szParams) - 1);
                    g_Tabs[nDropTab].bUseParamTitle = FALSE;
                    _GetCleanName(szProg, g_Tabs[nDropTab].szTitle, sizeof(g_Tabs[nDropTab].szTitle));
                    _CloseEmbeddedTask(nDropTab);
                    _SaveAllTabsToIni();
                    if (nDropTab == g_nActiveTab) {
                        SetWindowTextA(g_hInpProg, szProg);
                        SetWindowTextA(g_hInpParams, szParams);
                        _StartOrShowTask(nDropTab);
                    } else {
                        _SelectNav(nDropTab);
                    }
                }
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_MOVE:
            _SyncActiveWindowPos();
            return 0;

        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                if (g_nActiveTab >= 0 && g_Tabs[g_nActiveTab].hWndTask && IsWindow(g_Tabs[g_nActiveTab].hWndTask)) {
                    ShowWindow(g_Tabs[g_nActiveTab].hWndTask, SW_HIDE);
                }
            } else if (wParam == SIZE_RESTORED) {
                if (g_nActiveTab >= 0 && g_Tabs[g_nActiveTab].hWndTask && IsWindow(g_Tabs[g_nActiveTab].hWndTask)) {
                    ShowWindow(g_Tabs[g_nActiveTab].hWndTask, SW_SHOW);
                }
            }
            int iWidth = LOWORD(lParam);
            int iHeight = HIWORD(lParam);
            if (iWidth >= 100 && iHeight >= 100) {
                _LayoutTabs();
                _UpdateLayout(iWidth, iHeight);
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == IDT_SYNC_WATCHDOG) {
                _SyncActiveWindowPos();

                if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(g_hHeader, &pt);
                    RECT rcH;
                    GetClientRect(g_hHeader, &rcH);
                    if (PtInRect(&rcH, pt)) {
                        int idxUnder = _GetTabAtPoint(pt.x, pt.y);
                        if (idxUnder != -1 && idxUnder != g_nActiveTab) {
                            if (idxUnder == g_nHoverTabIdx) {
                                if (GetTickCount() - g_dwHoverStart >= 1000) {
                                    _SelectNav(idxUnder);
                                    _FocusEmbeddedWindow(g_Tabs[idxUnder].hWndTask);
                                    g_nHoverTabIdx = -1;
                                }
                            } else {
                                g_nHoverTabIdx = idxUnder;
                                g_dwHoverStart = GetTickCount();
                            }
                        } else {
                            g_nHoverTabIdx = -1;
                        }
                    } else {
                        g_nHoverTabIdx = -1;
                    }
                } else {
                    g_nHoverTabIdx = -1;
                }
            }
            return 0;

        case WM_CLOSE:
            for (int i = 0; i < g_nTabCount; i++) _CloseEmbeddedTask(i);
            _SaveWindowState();
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/* ==================== HEADER (TABS) WINDOW PROCEDURE ==================== */
static LRESULT CALLBACK HeaderWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DROPFILES:
            return SendMessage(g_hMainWnd, WM_DROPFILES, wParam, lParam);

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, g_hBrushNav);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COL_TEXT_RGB);
            SelectObject(hdc, g_hFontSegoeB);
            RECT rcTitle = { 10, 10, 95, 10 + TAB_HEIGHT };
            DrawTextA(hdc, "Workspacer", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            for (int i = 0; i < g_nTabCount; i++) {
                HBRUSH hTabBrush = (i == g_nActiveTab) ? CreateSolidBrush(COL_TAB_ACTIVE) : CreateSolidBrush(COL_TAB_INACTIVE);
                COLORREF crText  = (i == g_nActiveTab) ? COL_TAB_TEXT_ACT : COL_TAB_TEXT_IN;

                FillRect(hdc, &g_Tabs[i].rcTab, hTabBrush);
                DeleteObject(hTabBrush);

                SetTextColor(hdc, crText);
                DrawTextA(hdc, g_Tabs[i].szTitle, -1, &g_Tabs[i].rcTab, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            HBRUSH hAddBrush = CreateSolidBrush(COL_TAB_INACTIVE);
            FillRect(hdc, &g_rcAddTab, hAddBrush);
            DeleteObject(hAddBrush);
            SetTextColor(hdc, COL_TAB_TEXT_IN);
            DrawTextA(hdc, "+", -1, &g_rcAddTab, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_RBUTTONUP: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            int idx = _GetTabAtPoint(x, y);
            if (idx != -1) {
                _CloseEmbeddedTask(idx);
                char szLog[128];
                snprintf(szLog, sizeof(szLog), "Terminated: %s", g_Tabs[idx].szTitle);
                _SetStatus(szLog);
            }
            return 0;
        }

        /* NEW: Middle click on a tab toggles it between inline and popped out */
        case WM_MBUTTONUP: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            int idx = _GetTabAtPoint(x, y);
            if (idx != -1) {
                if (idx != g_nActiveTab) _SelectNav(idx);
                _ToggleRestoreTab();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            POINT pt = { x, y };

            if ((x >= 10 && x <= 95 && y >= 10 && y <= 10 + TAB_HEIGHT) || PtInRect(&g_rcAddTab, pt)) {
                _AddNewTab("C:\\Windows\\System32\\cmd.exe", "");
                return 0;
            }

            int idx = _GetTabAtPoint(x, y);
            if (idx != -1) {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    char szProgClean[128], szParamClean[128];
                    _GetCleanName(g_Tabs[idx].szProgram, szProgClean, sizeof(szProgClean));
                    _ExtractParamFileName(g_Tabs[idx].szParams, szParamClean, sizeof(szParamClean));

                    if (szParamClean[0] != '\0') {
                        if (lstrcmpiA(g_Tabs[idx].szTitle, szProgClean) == 0) {
                            strncpy(g_Tabs[idx].szTitle, szParamClean, sizeof(g_Tabs[idx].szTitle) - 1);
                            g_Tabs[idx].szTitle[sizeof(g_Tabs[idx].szTitle) - 1] = '\0';
                            g_Tabs[idx].bUseParamTitle = TRUE;
                        } else {
                            strncpy(g_Tabs[idx].szTitle, szProgClean, sizeof(g_Tabs[idx].szTitle) - 1);
                            g_Tabs[idx].szTitle[sizeof(g_Tabs[idx].szTitle) - 1] = '\0';
                            g_Tabs[idx].bUseParamTitle = FALSE;
                        }
                        _SaveAllTabsToIni();
                        InvalidateRect(hWnd, NULL, TRUE);
                    }
                    if (idx != g_nActiveTab) _SelectNav(idx);
                    return 0;
                }

                g_bMouseDownPrev    = TRUE;
                g_bDragStartedOnTab = TRUE;
                g_nDragSourceTab    = idx;
                g_nDragInitTab      = idx;
                SetCapture(hWnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = (short)LOWORD(lParam);
            int y = (short)HIWORD(lParam);
            if (wParam & MK_LBUTTON) {
                int idxUnder = _GetTabAtPoint(x, y);
                if (idxUnder != -1 && g_bDragStartedOnTab && g_nDragSourceTab != -1 && idxUnder != g_nDragSourceTab) {
                    TAB_ITEM tmp = g_Tabs[g_nDragSourceTab];
                    g_Tabs[g_nDragSourceTab] = g_Tabs[idxUnder];
                    g_Tabs[idxUnder] = tmp;

                    if (g_nActiveTab == g_nDragSourceTab) {
                        g_nActiveTab = idxUnder;
                    } else if (g_nActiveTab == idxUnder) {
                        g_nActiveTab = g_nDragSourceTab;
                    }

                    g_nDragSourceTab = idxUnder;
                    _LayoutTabs();
                    _SaveAllTabsToIni();
                    InvalidateRect(hWnd, NULL, TRUE);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            ReleaseCapture();
            if (g_bDragStartedOnTab && g_nDragInitTab != -1 && g_nDragInitTab == g_nDragSourceTab) {
                int x = (short)LOWORD(lParam);
                int y = (short)HIWORD(lParam);
                int idx = _GetTabAtPoint(x, y);
                if (idx == g_nDragInitTab) _SelectNav(idx);
            }
            g_bMouseDownPrev    = FALSE;
            g_bDragStartedOnTab = FALSE;
            g_nDragSourceTab    = -1;
            g_nDragInitTab      = -1;
            return 0;
        }
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/* ==================== FILTER WINDOW PROCEDURE ==================== */
static LRESULT CALLBACK FilterWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_COMMAND:
            return SendMessage(g_hMainWnd, WM_COMMAND, wParam, lParam);

        case WM_DROPFILES:
            return SendMessage(g_hMainWnd, WM_DROPFILES, wParam, lParam);

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, g_hBrushFilter);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0x1E, 0x29, 0x3B));
            SelectObject(hdc, g_hFontSegoeB);

            RECT rcProg = { 10, 14, 65, 34 };
            DrawTextA(hdc, "Program:", -1, &rcProg, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT rcParam = { 235, 14, 305, 34 };
            DrawTextA(hdc, "Parameters:", -1, &rcParam, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT rcSplit = { 0, rc.bottom - 4, rc.right, rc.bottom };
            FillRect(hdc, &rcSplit, g_hBrushNav);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_SETCURSOR: {
            RECT rc;
            GetClientRect(hWnd, &rc);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            if (pt.y >= rc.bottom - 5) {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return TRUE;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            RECT rc;
            GetClientRect(hWnd, &rc);
            int y = (short)HIWORD(lParam);
            if (y >= rc.bottom - 5) {
                g_bSplitterDragging = TRUE;
                SetCapture(hWnd);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_bSplitterDragging) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(g_hMainWnd, &pt);

                RECT rcMain;
                GetClientRect(g_hMainWnd, &rcMain);

                int iNewH = pt.y - g_iHeaderH;
                if (iNewH < 40) iNewH = 40;
                if (iNewH > (rcMain.bottom - g_iHeaderH - STATUS_HEIGHT - 100))
                    iNewH = rcMain.bottom - g_iHeaderH - STATUS_HEIGHT - 100;

                if (g_iFilterH != iNewH) {
                    g_iFilterH = iNewH;
                    _UpdateLayout(rcMain.right, rcMain.bottom);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_bSplitterDragging) {
                g_bSplitterDragging = FALSE;
                ReleaseCapture();
            }
            return 0;
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/* ==================== WORKSPACE WINDOW PROCEDURE ==================== */
static LRESULT CALLBACK WorkspaceWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DROPFILES:
            return SendMessage(g_hMainWnd, WM_DROPFILES, wParam, lParam);

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, g_hBrushWork);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/* ==================== STATUS BAR PROCEDURE ==================== */
static LRESULT CALLBACK StatusWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static char s_szStatusText[512] = " Ready";
    switch (uMsg) {
        case WM_USER + 1:
            strncpy(s_szStatusText, (const char *)lParam, sizeof(s_szStatusText) - 1);
            s_szStatusText[sizeof(s_szStatusText) - 1] = '\0';
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, g_hBrushStatus);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, COL_TEXT_RGB);
            SelectObject(hdc, g_hFontSegoe);

            RECT rcText = { 10, 0, rc.right - 10, rc.bottom };
            DrawTextA(hdc, s_szStatusText, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

/* ==================== TAB & LAYOUT ENGINE ==================== */
static void _LayoutTabs(void) {
    RECT rc;
    GetClientRect(g_hMainWnd, &rc);
    int iW = rc.right;

    int iStartX = 105;
    int iStartY = 10;
    int iGapX   = 5;
    int iGapY   = 3;
    int iCurX   = iStartX;
    int iCurY   = iStartY;

    for (int i = 0; i < g_nTabCount; i++) {
        if (iCurX + TAB_WIDTH > iW - 10) {
            iCurY += TAB_HEIGHT + iGapY;
            iCurX = 10;
        }
        SetRect(&g_Tabs[i].rcTab, iCurX, iCurY, iCurX + TAB_WIDTH, iCurY + TAB_HEIGHT);
        iCurX += TAB_WIDTH + iGapX;
    }

    if (iCurX + 32 > iW - 10) {
        iCurY += TAB_HEIGHT + iGapY;
        iCurX = 10;
    }
    SetRect(&g_rcAddTab, iCurX, iCurY, iCurX + 32, iCurY + TAB_HEIGHT);

    int iNewHeaderH = iCurY + TAB_HEIGHT;
    if (iNewHeaderH != g_iHeaderH) {
        g_iHeaderH = iNewHeaderH;
        _UpdateLayout(rc.right, rc.bottom);
    }

    InvalidateRect(g_hHeader, NULL, TRUE);
}

static void _UpdateLayout(int iW, int iH) {
    if (iW <= 0 || iH <= 0) return;

    MoveWindow(g_hHeader, 0, 0, iW, g_iHeaderH, TRUE);
    MoveWindow(g_hStatus, 0, iH - STATUS_HEIGHT, iW, STATUS_HEIGHT, TRUE);

    int iCalH = iH - g_iHeaderH - g_iFilterH - STATUS_HEIGHT;
    if (iCalH < 50) iCalH = 50;

    MoveWindow(g_hFilter, 0, g_iHeaderH, iW, g_iFilterH, TRUE);
    MoveWindow(g_hWorkspace, 0, g_iHeaderH + g_iFilterH, iW, iCalH, TRUE);

    int iCloseW  = 52;
    int iDeleteW = 52;
    int iSaveW   = 48;
    int iBrowseW = 55;
    int iGap     = 5;

    int iCloseX  = iW - iCloseW - 10;
    int iDeleteX = iCloseX - iDeleteW - iGap;
    int iSaveX   = iDeleteX - iSaveW - iGap;
    int iBrowseX = iSaveX - iBrowseW - iGap;

    MoveWindow(g_hBtnClose,  iCloseX,  10, iCloseW,  26, TRUE);
    MoveWindow(g_hBtnDelete, iDeleteX, 10, iDeleteW, 26, TRUE);
    MoveWindow(g_hBtnSave,   iSaveX,   10, iSaveW,   26, TRUE);
    MoveWindow(g_hBtnBrowse, iBrowseX, 10, iBrowseW, 26, TRUE);

    int iAvailW = (iBrowseX - 10) - 68;
    int iHalfW  = iAvailW / 2;
    int iProgInputW = iHalfW - 35;
    if (iProgInputW < 60) iProgInputW = 60;
    MoveWindow(g_hInpProg, 68, 11, iProgInputW, 24, TRUE);

    int iParamLabelX = 68 + iProgInputW + 8;
    int iParamInputX = iParamLabelX + 75;
    int iParamInputW = (iBrowseX - 8) - iParamInputX;
    if (iParamInputW < 60) iParamInputW = 60;
    MoveWindow(g_hInpParams, iParamInputX, 11, iParamInputW, 24, TRUE);

    MoveWindow(g_hOutputEdit, 0, 0, iW, iCalH, TRUE);
    _SyncActiveWindowPos();
}

static int _GetTabAtPoint(int x, int y) {
    POINT pt = { x, y };
    for (int i = 0; i < g_nTabCount; i++) {
        if (PtInRect(&g_Tabs[i].rcTab, pt)) return i;
    }
    return -1;
}

/* ==================== TASK & EMBEDDING MANAGEMENT ==================== */
static void _SelectNav(int nIndex) {
    if (nIndex < 0 || nIndex >= g_nTabCount) return;
    if (nIndex == g_nActiveTab) return;

    if (g_nActiveTab >= 0 && g_nActiveTab < g_nTabCount) {
        GetWindowTextA(g_hInpProg, g_Tabs[g_nActiveTab].szProgram, MAX_PATH);
        GetWindowTextA(g_hInpParams, g_Tabs[g_nActiveTab].szParams, sizeof(g_Tabs[g_nActiveTab].szParams));
        if (g_Tabs[g_nActiveTab].hWndTask && IsWindow(g_Tabs[g_nActiveTab].hWndTask)) {
            ShowWindow(g_Tabs[g_nActiveTab].hWndTask, SW_HIDE);
        }
        ShowWindow(g_hOutputEdit, SW_HIDE);
    }

    g_nActiveTab = nIndex;
    InvalidateRect(g_hHeader, NULL, TRUE);

    SetWindowTextA(g_hInpProg, g_Tabs[g_nActiveTab].szProgram);
    SetWindowTextA(g_hInpParams, g_Tabs[g_nActiveTab].szParams);

    _StartOrShowTask(g_nActiveTab);
}

static void _StartOrShowTask(int nIndex) {
    if (nIndex < 0 || nIndex >= g_nTabCount) return;

    const char *pszProg   = g_Tabs[nIndex].szProgram;
    const char *pszParams = g_Tabs[nIndex].szParams;
    if (pszProg[0] == '\0') pszProg = "C:\\Windows\\System32\\cmd.exe";

    if (g_Tabs[nIndex].hWndTask && IsWindow(g_Tabs[nIndex].hWndTask)) {
        ShowWindow(g_hOutputEdit, SW_HIDE);
        ShowWindow(g_Tabs[nIndex].hWndTask, SW_SHOW);
        _SyncActiveWindowPos();
        _FocusEmbeddedWindow(g_Tabs[nIndex].hWndTask);
        char szLog[128];
        snprintf(szLog, sizeof(szLog), "Switched to: %s", g_Tabs[nIndex].szTitle);
        _SetStatus(szLog);
        return;
    }

    if (g_Tabs[nIndex].dwPID != 0 && !g_Tabs[nIndex].bCaptureOutput) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, g_Tabs[nIndex].dwPID);
        if (hProcess) {
            DWORD dwExit = 0;
            if (GetExitCodeProcess(hProcess, &dwExit) && dwExit == STILL_ACTIVE) {
                CloseHandle(hProcess);
                FIND_WND_CTX ctx = { g_Tabs[nIndex].dwPID, FALSE, FALSE, NULL };
                EnumWindows(FindNewWindowProc, (LPARAM)&ctx);
                if (ctx.hWndFound) {
                    g_Tabs[nIndex].hWndTask = ctx.hWndFound;
                    if (g_Tabs[nIndex].bPoppedOut) {
                        _PopOutWindow(ctx.hWndFound);
                    } else {
                        _EmbedWindow(ctx.hWndFound);
                    }
                    ShowWindow(g_hOutputEdit, SW_HIDE);
                    char szLog[128];
                    snprintf(szLog, sizeof(szLog), "Re-attached: %s", g_Tabs[nIndex].szTitle);
                    _SetStatus(szLog);
                    return;
                }
            } else {
                CloseHandle(hProcess);
            }
        }
    }

    g_Tabs[nIndex].dwPID = 0;
    g_Tabs[nIndex].hWndTask = NULL;

    if (g_Tabs[nIndex].bCaptureOutput || (StrStrIA(pszProg, "cmd") && StrStrIA(pszParams, "/c"))) {
        _RunQuickConsoleCapture(nIndex, pszProg, pszParams);
        return;
    }

    g_nPreSnapCount = 0;
    EnumWindows(SnapshotEnumProc, 0);

    BOOL bIsExplorer = (StrStrIA(pszProg, "explorer") != NULL);
    BOOL bIsCmd = (StrStrIA(pszProg, "cmd.exe") != NULL || lstrcmpiA(pszProg, "cmd") == 0 || StrStrIA(pszProg, "cmd") != NULL);
    char szCmdLine[2048];

    if (bIsExplorer) {
        char szClean[1024];
        strncpy(szClean, pszParams, sizeof(szClean) - 1);
        szClean[sizeof(szClean) - 1] = '\0';
        PathUnquoteSpacesA(szClean);
        if (szClean[0] == '\0') strcpy(szClean, "C:\\");
        snprintf(szCmdLine, sizeof(szCmdLine), "explorer.exe /n,\"%s\"", szClean);
    } else {
        if (pszParams[0] != '\0') snprintf(szCmdLine, 2048, "\"%s\" %s", pszProg, pszParams);
        else snprintf(szCmdLine, 2048, "\"%s\"", pszProg);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOW;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, szCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        char szLog[MAX_PATH + 32];
        snprintf(szLog, sizeof(szLog), "Failed to execute: %s", pszProg);
        _SetStatus(szLog);
        return;
    }

    g_Tabs[nIndex].dwPID = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    FIND_WND_CTX findCtx = { pi.dwProcessId, bIsExplorer, bIsCmd, NULL };
    DWORD dwStart = GetTickCount();

    while (GetTickCount() - dwStart < 3500) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        EnumWindows(FindNewWindowProc, (LPARAM)&findCtx);
        if (findCtx.hWndFound) break;
        Sleep(30);
    }

    if (findCtx.hWndFound && IsWindow(findCtx.hWndFound)) {
        HWND hWndTarget = findCtx.hWndFound;
        GetWindowThreadProcessId(hWndTarget, &g_Tabs[nIndex].dwPID);

        DWORD dwVisStart = GetTickCount();
        while (!IsWindowVisible(hWndTarget) && (GetTickCount() - dwVisStart < 1000)) {
            Sleep(25);
        }
        Sleep(60);

        g_Tabs[nIndex].hWndTask = hWndTarget;
        
        if (g_Tabs[nIndex].bPoppedOut) {
            _PopOutWindow(hWndTarget);
        } else {
            _EmbedWindow(hWndTarget);
        }
        
        ShowWindow(g_hOutputEdit, SW_HIDE);
        char szLog[128];
        snprintf(szLog, sizeof(szLog), "Ready: %s", g_Tabs[nIndex].szTitle);
        _SetStatus(szLog);
    } else {
        char szLog[128];
        snprintf(szLog, sizeof(szLog), "Task started (PID: %lu)", pi.dwProcessId);
        _SetStatus(szLog);
    }
}

static void _EmbedWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) return;

    SetParent(hWnd, g_hWorkspace);
    LONG_PTR style = GetWindowLongPtrA(hWnd, GWL_STYLE);

    if (GetMenu(hWnd) != NULL) {
        style |= WS_POPUP;
        style &= ~WS_CHILD;
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME);
        SetWindowLongPtrA(hWnd, GWL_STYLE, style);
        SetWindowLongPtrA(hWnd, GWLP_HWNDPARENT, (LONG_PTR)g_hMainWnd);
        DrawMenuBar(hWnd);
    } else {
        char szClass[64];
        GetClassNameA(hWnd, szClass, sizeof(szClass));
        BOOL bIsExplorer = (lstrcmpiA(szClass, "CabinetWClass") == 0);

        if (bIsExplorer) {
            style |= (WS_CHILD | WS_CAPTION);
        } else {
            style |= WS_CHILD;
        }
        style &= ~WS_POPUP;
        style &= ~(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME);
        SetWindowLongPtrA(hWnd, GWL_STYLE, style);
    }

    SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    _SyncActiveWindowPos();
    ShowWindow(hWnd, SW_SHOW);
    _FocusEmbeddedWindow(hWnd);
}

static void _PopOutWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) return;

    SetParent(hWnd, NULL);
    SetWindowLongPtrA(hWnd, GWLP_HWNDPARENT, (LONG_PTR)g_hMainWnd);

    LONG_PTR style = GetWindowLongPtrA(hWnd, GWL_STYLE);
    style |= WS_POPUP;
    style &= ~WS_CHILD;
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_BORDER | WS_DLGFRAME);
    SetWindowLongPtrA(hWnd, GWL_STYLE, style);

    HMENU hMenu = GetMenu(hWnd);
    if (hMenu) DrawMenuBar(hWnd);

    ShowWindow(hWnd, SW_RESTORE);
    _SyncActiveWindowPos();
    ShowWindow(hWnd, SW_SHOW);
    _FocusEmbeddedWindow(hWnd);
}

static void _ToggleRestoreTab(void) {
    if (g_nActiveTab < 0) return;
    HWND hWnd = g_Tabs[g_nActiveTab].hWndTask;
    if (!hWnd || !IsWindow(hWnd)) return;

    if (g_Tabs[g_nActiveTab].bPoppedOut) {
        g_Tabs[g_nActiveTab].bPoppedOut = FALSE;
        _EmbedWindow(hWnd);
        char szLog[128];
        snprintf(szLog, sizeof(szLog), "Inline: %s", g_Tabs[g_nActiveTab].szTitle);
        _SetStatus(szLog);
    } else {
        g_Tabs[g_nActiveTab].bPoppedOut = TRUE;
        _PopOutWindow(hWnd);
        char szLog[128];
        snprintf(szLog, sizeof(szLog), "Popped out: %s", g_Tabs[g_nActiveTab].szTitle);
        _SetStatus(szLog);
    }
    _SaveAllTabsToIni();
}

static void _SyncActiveWindowPos(void) {
    if (g_nActiveTab < 0) return;
    HWND hWnd = g_Tabs[g_nActiveTab].hWndTask;
    if (!hWnd || !IsWindow(hWnd)) return;

    RECT rcWork;
    GetClientRect(g_hWorkspace, &rcWork);
    int iW = rcWork.right;
    int iH = rcWork.bottom;
    if (iW <= 10 || iH <= 10) return;

    char szClass[64];
    GetClassNameA(hWnd, szClass, sizeof(szClass));
    BOOL bIsExplorer = (lstrcmpiA(szClass, "CabinetWClass") == 0);
    HMENU hMenu = GetMenu(hWnd);

    if (g_Tabs[g_nActiveTab].bPoppedOut) {
        POINT pt = { 0, 0 };
        ClientToScreen(g_hWorkspace, &pt);
        RECT rcCur;
        GetWindowRect(hWnd, &rcCur);
        
        if (rcCur.left != pt.x || rcCur.top != pt.y || (rcCur.right - rcCur.left) != iW || (rcCur.bottom - rcCur.top) != iH) {
            SetWindowPos(hWnd, HWND_TOP, pt.x, pt.y, iW, iH, SWP_NOACTIVATE | SWP_FRAMECHANGED);
            if (hMenu) DrawMenuBar(hWnd);
            _FocusEmbeddedWindow(hWnd);
        }
    } else {
        if (hMenu != NULL && (GetWindowLongPtrA(hWnd, GWL_STYLE) & WS_CHILD)) {
            _EmbedWindow(hWnd);
            return;
        }

        /* Apply custom user INI offsets */
        int iTargetY = g_Tabs[g_nActiveTab].iOffsetTop;
        int iTargetH = iH + g_Tabs[g_nActiveTab].iOffsetBottom - g_Tabs[g_nActiveTab].iOffsetTop;

        /* Apply systemic window margin offsets for menus/Explorer ribbons */
        if (bIsExplorer || hMenu != NULL) {
            int iOffset = _GetCaptionOffset();
            iTargetY -= iOffset;
            iTargetH += iOffset;
        }

        RECT rcCur;
        GetWindowRect(hWnd, &rcCur);
        POINT pt = { rcCur.left, rcCur.top };
        ScreenToClient(g_hWorkspace, &pt);

        if (pt.x != 0 || pt.y != iTargetY || (rcCur.right - rcCur.left) != iW || (rcCur.bottom - rcCur.top) != iTargetH) {
            SetWindowPos(hWnd, HWND_TOP, 0, iTargetY, iW, iTargetH, SWP_NOACTIVATE | SWP_FRAMECHANGED);
            if (hMenu) DrawMenuBar(hWnd);
        }
    }
}

static void _FocusEmbeddedWindow(HWND hWnd) {
    if (!IsWindow(hWnd)) {
        if (IsWindowVisible(g_hOutputEdit)) SetFocus(g_hOutputEdit);
        return;
    }

    BringWindowToTop(hWnd);
    SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    DWORD dwTargetThread = GetWindowThreadProcessId(hWnd, NULL);
    DWORD dwCurrentThread = GetCurrentThreadId();

    if (dwTargetThread != 0 && dwTargetThread != dwCurrentThread) {
        AttachThreadInput(dwCurrentThread, dwTargetThread, TRUE);
        SetFocus(hWnd);
        SetActiveWindow(hWnd);
        AttachThreadInput(dwCurrentThread, dwTargetThread, FALSE);
    } else {
        SetFocus(hWnd);
        SetActiveWindow(hWnd);
    }

    if (g_nActiveTab >= 0 && g_Tabs[g_nActiveTab].bPoppedOut) {
        SetForegroundWindow(hWnd);
    }
}

static void _CloseEmbeddedTask(int nIndex) {
    if (nIndex < 0 || nIndex >= g_nTabCount) return;

    if (g_Tabs[nIndex].dwPID != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, g_Tabs[nIndex].dwPID);
        if (hProcess) {
            TerminateProcess(hProcess, 0);
            WaitForSingleObject(hProcess, 500);
            CloseHandle(hProcess);
        }
        g_Tabs[nIndex].dwPID = 0;
    }

    if (g_Tabs[nIndex].hWndTask && IsWindow(g_Tabs[nIndex].hWndTask)) {
        DestroyWindow(g_Tabs[nIndex].hWndTask);
        g_Tabs[nIndex].hWndTask = NULL;
    }

    if (g_Tabs[nIndex].pszOutput) {
        free(g_Tabs[nIndex].pszOutput);
        g_Tabs[nIndex].pszOutput = NULL;
    }

    g_Tabs[nIndex].bPoppedOut = FALSE;

    if (nIndex == g_nActiveTab) {
        ShowWindow(g_hOutputEdit, SW_HIDE);
    }
}

static void _RunQuickConsoleCapture(int nIndex, const char *pszProg, const char *pszParams) {
    _SetStatus("Capturing output...");

    char szCmdLine[2048];
    snprintf(szCmdLine, sizeof(szCmdLine), "cmd.exe /c \"%s\" %s", pszProg, pszParams);

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, szCmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return;
    }

    CloseHandle(hWritePipe);

    DWORD dwAlloc = 8192;
    DWORD dwTotal = 0;
    char *pBuffer = (char *)malloc(dwAlloc);
    pBuffer[0] = '\0';

    DWORD dwStart = GetTickCount();
    char szChunk[4096];

    while (GetTickCount() - dwStart < 3000) {
        DWORD dwAvail = 0;
        if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &dwAvail, NULL) && dwAvail > 0) {
            DWORD dwRead = 0;
            if (ReadFile(hReadPipe, szChunk, sizeof(szChunk) - 1, &dwRead, NULL) && dwRead > 0) {
                szChunk[dwRead] = '\0';
                if (dwTotal + dwRead + 1 > dwAlloc) {
                    dwAlloc = (dwTotal + dwRead + 1) * 2;
                    pBuffer = (char *)realloc(pBuffer, dwAlloc);
                }
                memcpy(pBuffer + dwTotal, szChunk, dwRead);
                dwTotal += dwRead;
                pBuffer[dwTotal] = '\0';
            }
        }

        DWORD dwExit = 0;
        if (GetExitCodeProcess(pi.hProcess, &dwExit) && dwExit != STILL_ACTIVE) {
            DWORD dwRead = 0;
            while (ReadFile(hReadPipe, szChunk, sizeof(szChunk) - 1, &dwRead, NULL) && dwRead > 0) {
                szChunk[dwRead] = '\0';
                if (dwTotal + dwRead + 1 > dwAlloc) {
                    dwAlloc = (dwTotal + dwRead + 1) * 2;
                    pBuffer = (char *)realloc(pBuffer, dwAlloc);
                }
                memcpy(pBuffer + dwTotal, szChunk, dwRead);
                dwTotal += dwRead;
                pBuffer[dwTotal] = '\0';
            }
            break;
        }

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(20);
    }

    CloseHandle(hReadPipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (dwTotal == 0) {
        strcpy(pBuffer, "[Command completed with no output]");
    }

    if (g_Tabs[nIndex].pszOutput) free(g_Tabs[nIndex].pszOutput);
    g_Tabs[nIndex].pszOutput = pBuffer;
    g_Tabs[nIndex].dwPID     = 0;
    g_Tabs[nIndex].hWndTask  = NULL;

    SetWindowTextA(g_hOutputEdit, pBuffer);
    ShowWindow(g_hOutputEdit, SW_SHOW);
    SetFocus(g_hOutputEdit);

    char szLog[64];
    snprintf(szLog, sizeof(szLog), "Output received for Tab %d", nIndex + 1);
    _SetStatus(szLog);
}

/* ==================== CRUD, DROP & PERSISTENCE ==================== */
static void _AddNewTab(const char *pszProg, const char *pszParams) {
    if (g_nTabCount >= MAX_TABS) {
        _SetStatus("Maximum tab limit reached.");
        return;
    }

    int idx = g_nTabCount;
    strncpy(g_Tabs[idx].szProgram, pszProg, MAX_PATH - 1);
    strncpy(g_Tabs[idx].szParams, pszParams, sizeof(g_Tabs[idx].szParams) - 1);
    g_Tabs[idx].hWndTask       = NULL;
    g_Tabs[idx].dwPID          = 0;
    g_Tabs[idx].bPoppedOut     = FALSE;
    g_Tabs[idx].bUseParamTitle = FALSE;
    g_Tabs[idx].bCaptureOutput = FALSE;
    g_Tabs[idx].iOffsetTop     = 0;   /* Default offset */
    g_Tabs[idx].iOffsetBottom  = 0;   /* Default offset */
    g_Tabs[idx].pszOutput      = NULL;

    _GetCleanName(pszProg, g_Tabs[idx].szTitle, sizeof(g_Tabs[idx].szTitle));

    g_nTabCount++;
    _LayoutTabs();
    _SelectNav(idx);
    _SaveAllTabsToIni();

    char szLog[128];
    snprintf(szLog, sizeof(szLog), "Created tab: %s", g_Tabs[idx].szTitle);
    _SetStatus(szLog);
}

static void _DeleteCurrentTab(void) {
    if (g_nActiveTab < 0 || g_nTabCount <= 0) return;

    int nDelIdx = g_nActiveTab;
    char szDeleted[128];
    strncpy(szDeleted, g_Tabs[nDelIdx].szTitle, sizeof(szDeleted) - 1);
    szDeleted[sizeof(szDeleted) - 1] = '\0';

    _CloseEmbeddedTask(nDelIdx);

    for (int i = nDelIdx; i < g_nTabCount - 1; i++) {
        g_Tabs[i] = g_Tabs[i + 1];
    }
    g_nTabCount--;

    if (g_nTabCount == 0) {
        g_nActiveTab = -1;
        _AddNewTab("C:\\Windows\\System32\\cmd.exe", "");
        return;
    }

    if (nDelIdx >= g_nTabCount) {
        g_nActiveTab = g_nTabCount - 1;
    } else {
        g_nActiveTab = nDelIdx;
    }

    _SaveAllTabsToIni();
    _LayoutTabs();

    SetWindowTextA(g_hInpProg, g_Tabs[g_nActiveTab].szProgram);
    SetWindowTextA(g_hInpParams, g_Tabs[g_nActiveTab].szParams);
    InvalidateRect(g_hHeader, NULL, TRUE);
    _StartOrShowTask(g_nActiveTab);

    char szLog[128];
    snprintf(szLog, sizeof(szLog), "Deleted tab: %s", szDeleted);
    _SetStatus(szLog);
}

static void _ApplyAndExecuteCurrentTab(void) {
    if (g_nActiveTab < 0) return;

    GetWindowTextA(g_hInpProg, g_Tabs[g_nActiveTab].szProgram, MAX_PATH);
    GetWindowTextA(g_hInpParams, g_Tabs[g_nActiveTab].szParams, sizeof(g_Tabs[g_nActiveTab].szParams));

    if (g_Tabs[g_nActiveTab].szProgram[0] == '\0') {
        strcpy(g_Tabs[g_nActiveTab].szProgram, "C:\\Windows\\System32\\cmd.exe");
    }

    if (g_Tabs[g_nActiveTab].bUseParamTitle) {
        char szParamClean[128];
        _ExtractParamFileName(g_Tabs[g_nActiveTab].szParams, szParamClean, sizeof(szParamClean));
        if (szParamClean[0] != '\0') {
            strncpy(g_Tabs[g_nActiveTab].szTitle, szParamClean, sizeof(g_Tabs[g_nActiveTab].szTitle) - 1);
            g_Tabs[g_nActiveTab].szTitle[sizeof(g_Tabs[g_nActiveTab].szTitle) - 1] = '\0';
        } else {
            g_Tabs[g_nActiveTab].bUseParamTitle = FALSE;
            _GetCleanName(g_Tabs[g_nActiveTab].szProgram, g_Tabs[g_nActiveTab].szTitle, sizeof(g_Tabs[g_nActiveTab].szTitle));
        }
    } else {
        _GetCleanName(g_Tabs[g_nActiveTab].szProgram, g_Tabs[g_nActiveTab].szTitle, sizeof(g_Tabs[g_nActiveTab].szTitle));
    }

    _SaveAllTabsToIni();
    InvalidateRect(g_hHeader, NULL, TRUE);

    _CloseEmbeddedTask(g_nActiveTab);
    _StartOrShowTask(g_nActiveTab);

    char szLog[128];
    snprintf(szLog, sizeof(szLog), "Saved and executed: %s", g_Tabs[g_nActiveTab].szTitle);
    _SetStatus(szLog);
}

static void _LoadTabsFromIni(void) {
    g_nTabCount = 0;
    for (int i = 1; i <= MAX_TABS; i++) {
        char szKey[32];
        char szProg[MAX_PATH];
        snprintf(szKey, sizeof(szKey), "Program_%d", i);
        GetPrivateProfileStringA("Parameters", szKey, "", szProg, MAX_PATH, g_szIniPath);

        if (szProg[0] == '\0' && i > 1) break;
        if (szProg[0] == '\0') strcpy(szProg, "C:\\Windows\\System32\\cmd.exe");

        char szParams[1024];
        snprintf(szKey, sizeof(szKey), "CommandLine_%d", i);
        GetPrivateProfileStringA("Parameters", szKey, "", szParams, sizeof(szParams), g_szIniPath);

        snprintf(szKey, sizeof(szKey), "UseParamTitle_%d", i);
        BOOL bUseParam = (GetPrivateProfileIntA("Parameters", szKey, 0, g_szIniPath) == 1);

        snprintf(szKey, sizeof(szKey), "PoppedOut_%d", i);
        BOOL bPoppedOut = (GetPrivateProfileIntA("Parameters", szKey, 0, g_szIniPath) == 1);

        snprintf(szKey, sizeof(szKey), "CaptureOutput_%d", i);
        BOOL bCapture = (GetPrivateProfileIntA("Parameters", szKey, 0, g_szIniPath) == 1);

        snprintf(szKey, sizeof(szKey), "OffsetTop_%d", i);
        int iOffTop = GetPrivateProfileIntA("Parameters", szKey, 0, g_szIniPath);

        snprintf(szKey, sizeof(szKey), "OffsetBottom_%d", i);
        int iOffBot = GetPrivateProfileIntA("Parameters", szKey, 0, g_szIniPath);

        char szTitle[128];
        snprintf(szKey, sizeof(szKey), "TabTitle_%d", i);
        GetPrivateProfileStringA("Parameters", szKey, "", szTitle, sizeof(szTitle), g_szIniPath);

        strncpy(g_Tabs[g_nTabCount].szProgram, szProg, MAX_PATH - 1);
        strncpy(g_Tabs[g_nTabCount].szParams, szParams, sizeof(g_Tabs[g_nTabCount].szParams) - 1);
        g_Tabs[g_nTabCount].hWndTask       = NULL;
        g_Tabs[g_nTabCount].dwPID          = 0;
        g_Tabs[g_nTabCount].bPoppedOut     = bPoppedOut;
        g_Tabs[g_nTabCount].bUseParamTitle = bUseParam;
        g_Tabs[g_nTabCount].bCaptureOutput = bCapture;
        g_Tabs[g_nTabCount].iOffsetTop     = iOffTop;
        g_Tabs[g_nTabCount].iOffsetBottom  = iOffBot;
        g_Tabs[g_nTabCount].pszOutput      = NULL;

        if (szTitle[0] != '\0') {
            strncpy(g_Tabs[g_nTabCount].szTitle, szTitle, sizeof(g_Tabs[g_nTabCount].szTitle) - 1);
            g_Tabs[g_nTabCount].szTitle[sizeof(g_Tabs[g_nTabCount].szTitle) - 1] = '\0';
        } else if (bUseParam) {
            _ExtractParamFileName(szParams, g_Tabs[g_nTabCount].szTitle, sizeof(g_Tabs[g_nTabCount].szTitle));
            if (g_Tabs[g_nTabCount].szTitle[0] == '\0') {
                _GetCleanName(szProg, g_Tabs[g_nTabCount].szTitle, sizeof(g_Tabs[g_nTabCount].szTitle));
            }
        } else {
            _GetCleanName(szProg, g_Tabs[g_nTabCount].szTitle, sizeof(g_Tabs[g_nTabCount].szTitle));
        }

        g_nTabCount++;
    }

    if (g_nTabCount == 0) {
        strcpy(g_Tabs[0].szProgram, "C:\\Windows\\System32\\cmd.exe");
        g_Tabs[0].szParams[0] = '\0';
        strcpy(g_Tabs[0].szTitle, "cmd");
        g_Tabs[0].hWndTask       = NULL;
        g_Tabs[0].dwPID          = 0;
        g_Tabs[0].bPoppedOut     = FALSE;
        g_Tabs[0].bUseParamTitle = FALSE;
        g_Tabs[0].bCaptureOutput = FALSE;
        g_Tabs[0].iOffsetTop     = 0;
        g_Tabs[0].iOffsetBottom  = 0;
        g_Tabs[0].pszOutput      = NULL;
        g_nTabCount = 1;
    }

    _LayoutTabs();
}

static void _SaveAllTabsToIni(void) {
    WritePrivateProfileSectionA("Parameters", "", g_szIniPath);
    for (int i = 0; i < g_nTabCount; i++) {
        char szKey[32];
        char szVal[32];

        snprintf(szKey, sizeof(szKey), "Program_%d", i + 1);
        WritePrivateProfileStringA("Parameters", szKey, g_Tabs[i].szProgram, g_szIniPath);

        snprintf(szKey, sizeof(szKey), "CommandLine_%d", i + 1);
        WritePrivateProfileStringA("Parameters", szKey, g_Tabs[i].szParams, g_szIniPath);

        snprintf(szKey, sizeof(szKey), "UseParamTitle_%d", i + 1);
        WritePrivateProfileStringA("Parameters", szKey, g_Tabs[i].bUseParamTitle ? "1" : "0", g_szIniPath);

        snprintf(szKey, sizeof(szKey), "PoppedOut_%d", i + 1);
        WritePrivateProfileStringA("Parameters", szKey, g_Tabs[i].bPoppedOut ? "1" : "0", g_szIniPath);

        snprintf(szKey, sizeof(szKey), "CaptureOutput_%d", i + 1);
        WritePrivateProfileStringA("Parameters", szKey, g_Tabs[i].bCaptureOutput ? "1" : "0", g_szIniPath);

        snprintf(szKey, sizeof(szKey), "OffsetTop_%d", i + 1);
        snprintf(szVal, sizeof(szVal), "%d", g_Tabs[i].iOffsetTop);
        WritePrivateProfileStringA("Parameters", szKey, szVal, g_szIniPath);

        snprintf(szKey, sizeof(szKey), "OffsetBottom_%d", i + 1);
        snprintf(szVal, sizeof(szVal), "%d", g_Tabs[i].iOffsetBottom);
        WritePrivateProfileStringA("Parameters", szKey, szVal, g_szIniPath);

        snprintf(szKey, sizeof(szKey), "TabTitle_%d", i + 1);
        WritePrivateProfileStringA("Parameters", szKey, g_Tabs[i].szTitle, g_szIniPath);
    }
    char szActive[16];
    snprintf(szActive, sizeof(szActive), "%d", g_nActiveTab);
    WritePrivateProfileStringA("Window", "ActiveTab", szActive, g_szIniPath);
}

static void _SaveWindowState(void) {
    if (!g_bFullScreen) {
        RECT rc;
        GetWindowRect(g_hMainWnd, &rc);
        char szVal[32];
        snprintf(szVal, sizeof(szVal), "%ld", rc.right - rc.left);
        WritePrivateProfileStringA("Window", "Width", szVal, g_szIniPath);
        snprintf(szVal, sizeof(szVal), "%ld", rc.bottom - rc.top);
        WritePrivateProfileStringA("Window", "Height", szVal, g_szIniPath);
        snprintf(szVal, sizeof(szVal), "%ld", rc.left);
        WritePrivateProfileStringA("Window", "X", szVal, g_szIniPath);
        snprintf(szVal, sizeof(szVal), "%ld", rc.top);
        WritePrivateProfileStringA("Window", "Y", szVal, g_szIniPath);
    }
    char szActive[16], szFilterH[16];
    snprintf(szActive, sizeof(szActive), "%d", g_nActiveTab);
    WritePrivateProfileStringA("Window", "ActiveTab", szActive, g_szIniPath);
    snprintf(szFilterH, sizeof(szFilterH), "%d", g_iFilterH);
    WritePrivateProfileStringA("Layout", "FilterH", szFilterH, g_szIniPath);
}

/* ==================== UTILITIES ==================== */
static void _GetCleanName(const char *pszPath, char *pszOut, size_t nMax) {
    const char *pSlash = strrchr(pszPath, '\\');
    const char *pName = pSlash ? (pSlash + 1) : pszPath;
    strncpy(pszOut, pName, nMax - 1);
    pszOut[nMax - 1] = '\0';
    PathRemoveExtensionA(pszOut);
    if (pszOut[0] == '\0') strcpy(pszOut, "cmd");
}

static void _ExtractParamFileName(const char *pszParams, char *pszOut, size_t nMax) {
    pszOut[0] = '\0';
    if (!pszParams || pszParams[0] == '\0') return;

    char szTemp[1024];
    strncpy(szTemp, pszParams, sizeof(szTemp) - 1);
    szTemp[sizeof(szTemp) - 1] = '\0';

    char *pStart = szTemp;
    while (*pStart == ' ' || *pStart == '\t') pStart++;
    if (*pStart == '"') {
        pStart++;
        char *pEnd = strchr(pStart, '"');
        if (pEnd) *pEnd = '\0';
    } else {
        char *pSpace = strchr(pStart, ' ');
        if (pSpace) *pSpace = '\0';
    }

    _GetCleanName(pStart, pszOut, nMax);
}

static void _ResolveDropTarget(const char *pszInput, char *pszProgOut, char *pszParamsOut) {
    pszProgOut[0] = '\0';
    pszParamsOut[0] = '\0';

    if (PathIsDirectoryA(pszInput)) {
        strcpy(pszProgOut, "explorer.exe");
        snprintf(pszParamsOut, 1024, "\"%s\"", pszInput);
        return;
    }

    char szExt[32];
    const char *pExt = PathFindExtensionA(pszInput);
    strncpy(szExt, pExt, sizeof(szExt) - 1);

    if (lstrcmpiA(szExt, ".lnk") == 0) {
        IShellLinkA *pShellLink = NULL;
        if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&pShellLink))) {
            IPersistFile *pPersistFile = NULL;
            if (SUCCEEDED(pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile, (void **)&pPersistFile))) {
                WCHAR wszPath[MAX_PATH];
                MultiByteToWideChar(CP_ACP, 0, pszInput, -1, wszPath, MAX_PATH);
                if (SUCCEEDED(pPersistFile->lpVtbl->Load(pPersistFile, wszPath, STGM_READ))) {
                    pShellLink->lpVtbl->GetPath(pShellLink, pszProgOut, MAX_PATH, NULL, 0);
                    pShellLink->lpVtbl->GetArguments(pShellLink, pszParamsOut, 1024);
                }
                pPersistFile->lpVtbl->Release(pPersistFile);
            }
            pShellLink->lpVtbl->Release(pShellLink);
        }
        if (pszProgOut[0] != '\0') return;
    }

    if (lstrcmpiA(szExt, ".exe") == 0 || lstrcmpiA(szExt, ".bat") == 0 || lstrcmpiA(szExt, ".cmd") == 0) {
        strcpy(pszProgOut, pszInput);
    } else {
        char szAssoc[MAX_PATH];
        DWORD dwAssoc = MAX_PATH;
        if (SUCCEEDED(AssocQueryStringA(0, ASSOCSTR_EXECUTABLE, szExt, "open", szAssoc, &dwAssoc)) && szAssoc[0] != '\0') {
            strcpy(pszProgOut, szAssoc);
        } else {
            strcpy(pszProgOut, "C:\\Windows\\System32\\notepad.exe");
        }
        snprintf(pszParamsOut, 1024, "\"%s\"", pszInput);
    }
}

static int _GetCaptionOffset(void) {
    int iCap   = GetSystemMetrics(SM_CYCAPTION);
    int iFrame = GetSystemMetrics(SM_CYSIZEFRAME);
    int iPadded = 0;

    iPadded = GetSystemMetrics(SM_CXPADDEDBORDER);
    if (iCap <= 0) iCap = 23;
    
    return iCap + iFrame + iPadded;
}

static void _SetStatus(const char *pszText) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char szBuf[600];
    snprintf(szBuf, sizeof(szBuf), " %02d:%02d:%02d - %s", st.wHour, st.wMinute, st.wSecond, pszText);
    SendMessage(g_hStatus, WM_USER + 1, 0, (LPARAM)szBuf);
}