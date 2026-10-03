/* ============================================================================
 * Calmira Taskbar v0.8 - Win32 GCC Port
 *
 * FEATURES:
 * - Single-file C architecture with clean CALLBACK calling conventions
 * - Flawless 64-bit and 32-bit Window Enumeration (WS_EX_APPWINDOW)
 * - Drag-and-Drop Task Reordering on the Taskbar
 * - Dynamic INI-based Right-Click Menus (Taskbar & WinX Start Menu)
 * - Fallback System Tray for environments without Explorer.exe
 * - Dynamic Search/Autocomplete ListBox with 250ms delayed filtering
 *
 * COMPILATION INSTRUCTIONS:
 *   gcc -Os -s -mwindows -o taskbar.exe taskbar.c -lcomdlg32 -lshell32 -lgdi32 -luser32
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef GWLP_WNDPROC
#define GWLP_WNDPROC GWL_WNDPROC
#define GWLP_HINSTANCE GWL_HINSTANCE
#define GWLP_ID GWL_ID
#define GetWindowLongPtr GetWindowLong
#define SetWindowLongPtr SetWindowLong
#define LONG_PTR LONG
#define INT_PTR int
#endif
#ifndef GCLP_HICONSM
#define GCLP_HICONSM (-34)
#endif
#ifndef GCLP_HICON
#define GCLP_HICON (-14)
#endif
#ifndef GetClassLongPtr
#define GetClassLongPtr GetClassLong
#endif
#ifndef MAX_PATH
#define MAX_PATH 128
#endif
#ifndef VK_LWIN
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#endif
#ifndef BST_PUSHED
#define BST_PUSHED 0x0004
#endif
#ifndef WS_EX_TOPMOST
#define WS_EX_TOPMOST 0x00000008L
#endif
#ifndef SPI_SETWORKAREA
#define SPI_SETWORKAREA 0x002F
#endif
#ifndef SPIF_SENDCHANGE
#define SPIF_SENDCHANGE 0x0002
#endif
#ifndef ICON_SMALL2
#define ICON_SMALL2 2
#endif
#ifndef SMTO_ABORTIFHUNG
#define SMTO_ABORTIFHUNG 0x0002
#endif
/* OS Version Bypass for Win 8.1+ */
typedef struct _NTDLL_OSVERSIONINFOW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} NTDLL_OSVERSIONINFOW, *PNTDLL_OSVERSIONINFOW;
typedef LONG (WINAPI *RTLGETVERSION)(PNTDLL_OSVERSIONINFOW);

/* --------------------------------------------------------------------------
   Constants & IDs
   -------------------------------------------------------------------------- */
#define START_BTN_WIDTH     74
#define QUICK_LAUNCH_COUNT  4
#define CLOCK_WIDTH         50
#define CLOCK_HEIGHT        22
#define MAX_TASKS           64
#define MAX_TRAY_ICONS      32
#define MAX_TRAY_APPS       32
#define MAX_START_ITEMS     512
#define MAX_OD_ITEMS        4096  /* Expanded to allow massive mapped drives */
#define MAX_INI_SHORTCUTS   512   /* Expanded to allow huge custom menus */
#define TIMER_CLOCK         1001
#define TIMER_REFRESH       1002
#define TIMER_HOTKEY        1003
#define TIMER_SEARCH        1004
#define ID_START_BUTTON     2001
#define ID_TASK_BASE        3000
#define ID_QUICK_BASE       4000
#define ID_CLOCK            5001
#define IDM_START_BASE      8000
#define IDM_SEARCH_EDIT     4999
#define IDM_SEARCH_LIST     4998
#define IDM_TASK_MINIMIZE   9003
#define IDM_TASK_MAXIMIZE   9004
#define IDM_TASK_CLOSE      9005
#define IDM_TASK_TWOPANE    9006
#define IDM_TASK_MINTOTRAY  9007
#define IDM_WINX_BASE       10000
#define IDM_TBMENU_BASE     11000
#define WM_USER_CONTEXTMENU   (WM_USER + 100)
#define WM_USER_APPLYLAYOUT   (WM_USER + 101)
#define IDM_CTX_PROPS       7101
#define IDM_CTX_NEWFOLDER   7102
#define IDM_CTX_NEWSHORTCUT 7103
#define IDM_CTX_RENAME      7104
#define IDM_CTX_DELETE      7105
#define IDM_CLOCK_ADJUST    7007
#define POS_BOTTOM 0
#define POS_TOP    1
#define POS_LEFT   2
#define POS_RIGHT  3
#define PROMPT_NEWFOLDER    1
#define PROMPT_RENAME       2
#define IDM_FS_BASE 15000
/* --------------------------------------------------------------------------
   Data Structures & Globals
   -------------------------------------------------------------------------- */
typedef struct { HWND hWnd; char title[128]; HWND hBtn; } TaskEntry;
typedef struct { char name[16]; char exe[MAX_PATH]; } QuickLaunchDef;

typedef struct { 
    BOOL active; 
    HWND hwnd; 
    HICON hIcon; 
    char tooltip[64]; 
} TrayIcon;

typedef struct {
    char name[64];
    char exe[MAX_PATH];
    char params[128];
    BOOL isSeparator;
} ContextMenuItem;

typedef struct {
    char text[64];
    BOOL isRoot;
    BOOL isSeparator;
    int polyIcon;
    int yOffset;
    int totalHeight;
    char targetPath[MAX_PATH];
    BOOL isFsFolder;
    UINT cmdId;
} ODMenuItem;

typedef struct {
    char id[12];
    char name[64];
    char exe[MAX_PATH];
    char params[64];
    char icon[16];
    char minimizedStr[4];
    char parentId[12];
    BOOL minimized;
    BOOL isFolder;
    HMENU hMenu;
    char hotkey[16];
} IniShortcut;

static char g_DropTarget[MAX_PATH] = "";
static char g_DropName[MAX_PATH] = "";

static HINSTANCE g_hInst;
static HWND g_hTaskbar = NULL;
static HWND g_hStartBtn = NULL;
static HWND g_hTaskList = NULL;
static HWND g_hClock = NULL;
static HWND g_hQuickLaunch[QUICK_LAUNCH_COUNT];
static HWND g_ContextTargetWnd = NULL;
static HWND g_LastClickedTaskWnd = NULL;
static HWND g_hSearchBox = NULL;
static HWND g_hSearchList = NULL;

static WNDPROC OldTaskListProc = NULL;
static WNDPROC OldTaskBtnProc = NULL;
static WNDPROC OldStartBtnProc = NULL;
static WNDPROC OldClockProc = NULL;
static WNDPROC OldSearchBoxProc = NULL;
static WNDPROC OldSearchListProc = NULL;
static WNDPROC OldHotkeyProc = NULL;

static TaskEntry g_Tasks[MAX_TASKS];
static HWND s_EnumTasks[MAX_TASKS];
static int g_TaskCount = 0;
static int s_EnumCount = 0;

static char g_TrayAppList[MAX_TRAY_APPS][MAX_PATH];
static int g_TrayAppCount = 0;

static HWND s_EnumTrayTasks[MAX_TRAY_ICONS];
static int s_EnumTrayCount = 0;
static TrayIcon g_TrayIcons[MAX_TRAY_ICONS];
static int g_TrayIconCount = 0;

static ContextMenuItem g_WinXMenu[32];
static int g_WinXCount = 0;

static ContextMenuItem g_TbMenu[32];
static int g_TbMenuCount = 0;

static ODMenuItem* g_ODItems = NULL;
static int g_ODCount = 0;

static IniShortcut* g_IniShortcuts = NULL;
static int g_IniShortcutCount = 0;

static BOOL g_bIniChanged = FALSE;
static char g_RunHistory[512] = "";

static char g_ContextId[16] = "";
static BOOL g_ContextIsFolder = FALSE;
static char g_EditShortcutId[16] = "";
static BOOL g_MenuOpen = FALSE;

static char g_PromptLabel[64] = "";
static char g_PromptValue[MAX_PATH] = "";
static int g_PromptMode = PROMPT_NEWFOLDER;

static QuickLaunchDef g_QL[QUICK_LAUNCH_COUNT];
static int g_QLActiveCount = 0;

static char g_szWinVer[32] = "Windows";
static char g_szIniPath[MAX_PATH];

static int g_TbPosition = POS_BOTTOM;
static int g_TbHeight = 30;
static int g_TbWidthVert = 72;
static BOOL g_bDragging = FALSE;
static BOOL g_bResizing = FALSE;
static POINT g_DragStartPt;

/* Drag and Drop Task Reordering Globals */
static HWND g_DragHwnd = NULL;
static POINT g_DragTaskStartPt;
static int g_DragStartIndex = -1;
static BOOL g_DidDrag = FALSE;

static HFONT g_hFontMenu = NULL;
static HFONT g_hFontSidebar = NULL;
static HHOOK g_hMsgHook = NULL;

static HGLOBAL g_hMemODItems = NULL;
static HGLOBAL g_hMemIniShortcuts = NULL;

/* Prototypes */
static void ApplyLayout(void);
static void ShowFolderMenu(HWND hAnchor, const char* folderId);
static void LaunchShortcut(HWND hwnd, IniShortcut* sh);
static ODMenuItem* AddODItem(const char* text, BOOL isRoot, BOOL isSeparator, int polyIcon);
static void CreateCenteredDialog(HINSTANCE hInst, HWND hParent, LPCSTR className, LPCSTR title, int w, int h);
static void ParseContextMenuItem(const char* val, ContextMenuItem* out);
static void LoadMenusFromIni(void);
static void HandleContextItem(ContextMenuItem* item, HWND hwnd);
static void DoShowDesktop(void);

LRESULT CALLBACK HotkeyEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK DateTimeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK TaskbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK RunDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK StartBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ClockProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TaskBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK SearchListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CALLBACK MsgFilterProc(int code, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK    TaskbarEnumWindowsProc(HWND hwnd, LPARAM lParam);
BOOL CALLBACK    MinimizeEnumProc(HWND hwnd, LPARAM lParam);

static void SetFont(HWND hwnd, HFONT font);
static void PositionDialogNearStart(HWND hwnd);
static BOOL BrowseFile(HWND hwnd, char* outPath, const char* filter);
static void GetNewIniId(char* outId);
static void SaveIniEntry(IniShortcut* item);
static void LoadIniShortcuts(void);
static void SaveAllToIni(void);
static void SaveConfig(void);
/* --------------------------------------------------------------------------
   Sorting Engine (Fixed Root Menu Tiers)
   -------------------------------------------------------------------------- */
int CompareIni(const void* a, const void* b);
/* --------------------------------------------------------------------------
   Utility Functions
   -------------------------------------------------------------------------- */
static void GetAppFilePath(const char* filename, char* outPath) {
    char* p; GetModuleFileName(NULL, outPath, MAX_PATH);
    p = strrchr(outPath, '\\'); if (p) lstrcpy(p + 1, filename); else lstrcpy(outPath, filename);
}
/* --------------------------------------------------------------------------
   Corrected Save Engine (With flush override & zero disk access if untouched)
   -------------------------------------------------------------------------- */
static void SaveAllToIni(void) {
    char buf[16]; int i;
    
    /* Strict check: Abort immediately if nothing changed in memory */
    if (!g_bIniChanged) return;
    
    /* Tell OS INI Cache to safely clear existing sections without deleting the file */
    WritePrivateProfileString("Systray", NULL, NULL, g_szIniPath);
    WritePrivateProfileString("QuickLaunch", NULL, NULL, g_szIniPath);
    WritePrivateProfileString("WinXMenu", NULL, NULL, g_szIniPath);
    WritePrivateProfileString("TaskbarMenu", NULL, NULL, g_szIniPath);
    WritePrivateProfileString("Run", NULL, NULL, g_szIniPath);
    WritePrivateProfileString("Shortcut", NULL, NULL, g_szIniPath);
    
    sprintf(buf, "%d", g_TbPosition); WritePrivateProfileString("Taskbar", "Position", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbHeight); WritePrivateProfileString("Taskbar", "Height", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbWidthVert); WritePrivateProfileString("Taskbar", "WidthVert", buf, g_szIniPath);
    
    char trayApps[512] = "";
    for (i=0; i<g_TrayAppCount; i++) { lstrcat(trayApps, g_TrayAppList[i]); lstrcat(trayApps, "|"); }
    WritePrivateProfileString("Systray", "Apps", trayApps, g_szIniPath);
    
    if (g_QLActiveCount > 0) {
        sprintf(buf, "%d", g_QLActiveCount); WritePrivateProfileString("QuickLaunch", "Count", buf, g_szIniPath);
        WritePrivateProfileString("QuickLaunch", "Enabled", "1", g_szIniPath);
        for (i=0; i<g_QLActiveCount; i++) {
            char keyN[16], keyE[16]; sprintf(keyN, "Name%d", i); sprintf(keyE, "Exe%d", i);
            WritePrivateProfileString("QuickLaunch", keyN, g_QL[i].name, g_szIniPath);
            WritePrivateProfileString("QuickLaunch", keyE, g_QL[i].exe, g_szIniPath);
        }
    }
    
    /* Write MRU from Memory */
    WritePrivateProfileString("Run", "History", g_RunHistory, g_szIniPath);
    
    for (i=0; i<g_WinXCount; i++) {
        char val[512]; char key[8]; sprintf(key, "%02d", i+1);
        sprintf(val, "%s|%s|%s", g_WinXMenu[i].name, g_WinXMenu[i].exe, g_WinXMenu[i].params);
        WritePrivateProfileString("WinXMenu", key, val, g_szIniPath);
    }
    for (i=0; i<g_TbMenuCount; i++) {
        char val[512]; char key[8]; sprintf(key, "%02d", i+1);
        sprintf(val, "%s|%s|%s", g_TbMenu[i].name, g_TbMenu[i].exe, g_TbMenu[i].params);
        WritePrivateProfileString("TaskbarMenu", key, val, g_szIniPath);
    }
    
    /* Ensure [Shortcut] section is physically written last */
    for (i=0; i<g_IniShortcutCount; i++) {
        char val[1024];
        int flags = (g_IniShortcuts[i].minimized ? 1 : 0) | (g_IniShortcuts[i].isFolder ? 2 : 0);
        sprintf(val, "%s|%s|%s|%s|%d|%s|%s", g_IniShortcuts[i].name, g_IniShortcuts[i].exe, g_IniShortcuts[i].params, g_IniShortcuts[i].icon, flags, g_IniShortcuts[i].parentId, g_IniShortcuts[i].hotkey);
        WritePrivateProfileString("Shortcut", g_IniShortcuts[i].id, val, g_szIniPath);
    }

    /* Force a synchronous cache flush so nothing is lost upon immediate process termination */
    WritePrivateProfileString(NULL, NULL, NULL, g_szIniPath);
}

/* --------------------------------------------------------------------------
   Shortcut Dialog Proc (Pre-fills inputs based on the Drag-and-Drop data)
   -------------------------------------------------------------------------- */
LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hName, hTarget, hParams, hIcon, hMinCheck, hFolderCheck, hMapDirCheck, hParentFolder, hHotkey, hTargetLbl;
    switch(msg) {
        case WM_CREATE: {
            char name[MAX_PATH] = "", target[MAX_PATH] = "", params[MAX_PATH] = "", iconF[MAX_PATH] = "", parentId[16] = "0"; 
            int minimized = 0, isFolder = 0, bMapDir = 0, vkCode = 0, i;
            
            /* If launched from a drag-and-drop file event */
            if (g_DropTarget[0] != '\0') {
                lstrcpy(name, g_DropName);
                lstrcpy(target, g_DropTarget);
                lstrcpy(parentId, g_ContextId);
                
                DWORD attr = GetFileAttributes(target);
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    isFolder = 1;
                    bMapDir = 1;
                }
                
                g_DropTarget[0] = '\0';
                g_DropName[0] = '\0';
            } 
            /* Editing existing shortcut */
            else if (g_EditShortcutId[0] != '\0') {
                for (i = 0; i < g_IniShortcutCount; i++) {
                    if (lstrcmp(g_IniShortcuts[i].id, g_EditShortcutId) == 0) {
                        lstrcpy(name, g_IniShortcuts[i].name); lstrcpy(target, g_IniShortcuts[i].exe); lstrcpy(params, g_IniShortcuts[i].params); lstrcpy(iconF, g_IniShortcuts[i].icon);
                        lstrcpy(parentId, g_IniShortcuts[i].parentId); 
                        vkCode = atoi(g_IniShortcuts[i].hotkey);
                        minimized = g_IniShortcuts[i].minimized; 
                        isFolder = g_IniShortcuts[i].isFolder; 
                        bMapDir = (isFolder && target[0] != '\0');
                        break; 
                    } 
                }
            } else if (g_ContextId[0] != '\0') {
                lstrcpy(parentId, g_ContextId);
            }

            CreateWindow("STATIC", "Name:", WS_CHILD|WS_VISIBLE, 10, 10, 120, 20, hwnd, NULL, g_hInst, NULL);
            hName = CreateWindowEx(0, "EDIT", name, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 130, 10, 210, 22, hwnd, NULL, g_hInst, NULL);
            
            hTargetLbl = CreateWindow("STATIC", isFolder ? (bMapDir ? "Dir Path:" : "Target:") : "Target (File):", WS_CHILD|WS_VISIBLE, 10, 40, 120, 20, hwnd, NULL, g_hInst, NULL);
            hTarget = CreateWindowEx(0, "EDIT", target, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 130, 40, 150, 22, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 290, 40, 50, 22, hwnd, (HMENU)101, g_hInst, NULL);
            
            CreateWindow("STATIC", "Parameters:", WS_CHILD|WS_VISIBLE, 10, 70, 120, 20, hwnd, NULL, g_hInst, NULL);
            hParams = CreateWindowEx(0, "EDIT", params, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 130, 70, 210, 22, hwnd, NULL, g_hInst, NULL);
            
            CreateWindow("STATIC", "Icon ID (0-6):", WS_CHILD|WS_VISIBLE, 10, 100, 120, 20, hwnd, NULL, g_hInst, NULL);
            hIcon = CreateWindowEx(0, "EDIT", iconF, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 130, 100, 50, 22, hwnd, NULL, g_hInst, NULL);
            
            CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 130, 120, 20, hwnd, NULL, g_hInst, NULL);
            hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL, 130, 130, 210, 150, hwnd, NULL, g_hInst, NULL);
            
            CreateWindow("STATIC", "Hotkey (press key):", WS_CHILD|WS_VISIBLE, 10, 160, 120, 20, hwnd, NULL, g_hInst, NULL);
            hHotkey = CreateWindowEx(0, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 130, 160, 80, 22, hwnd, NULL, g_hInst, NULL);
            OldHotkeyProc = (WNDPROC)SetWindowLongPtr(hHotkey, GWLP_WNDPROC, (LONG_PTR)HotkeyEditProc);
            
            if (vkCode) {
                char kname[64];
                UINT scanCode = MapVirtualKey(vkCode, 0);
                SetProp(hHotkey, "VK", (HANDLE)(INT_PTR)vkCode);
                GetKeyNameText(MAKELONG(0, scanCode), kname, sizeof(kname));
                SetWindowText(hHotkey, kname);
            }

            hMinCheck = CreateWindow("BUTTON", "Start Minimized", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 130, 190, 120, 20, hwnd, NULL, g_hInst, NULL);
            hFolderCheck = CreateWindow("BUTTON", "Is Folder", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 260, 190, 80, 20, hwnd, (HMENU)103, g_hInst, NULL);
            hMapDirCheck = CreateWindow("BUTTON", "Map Dir to Menu", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 130, 215, 140, 20, hwnd, (HMENU)104, g_hInst, NULL);
            
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 90, 250, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 190, 250, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            
            SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)"0 (Root)");
            for (i = 0; i < g_IniShortcutCount; i++) {
                if (g_IniShortcuts[i].isFolder) {
                    char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i].id, g_IniShortcuts[i].name);
                    SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)buf);
                }
            }
            
            for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) {
                char buf[128]; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)buf);
                if (strncmp(buf, parentId, lstrlen(parentId)) == 0 && buf[lstrlen(parentId)] == ' ') {
                    SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break;
                }
            }
            if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0);

            if (minimized) SendMessage(hMinCheck, BM_SETCHECK, 1, 0); 
            SendMessage(hFolderCheck, BM_SETCHECK, isFolder, 0);
            SendMessage(hMapDirCheck, BM_SETCHECK, bMapDir, 0);
            
            if (isFolder) {
                EnableWindow(hParams, FALSE);
                EnableWindow(hMinCheck, FALSE);
                EnableWindow(hHotkey, FALSE);
                EnableWindow(hMapDirCheck, TRUE);
                if (bMapDir) {
                    EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                } else {
                    EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE);
                }
            } else {
                EnableWindow(hMapDirCheck, FALSE);
            }

            SetFont(hwnd, NULL); SetFocus(hName); PositionDialogNearStart(hwnd); return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == 101) { 
                char path[MAX_PATH]; 
                if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) {
                    MessageBox(hwnd, "Please select any file inside the directory you wish to map.", "Select Directory", MB_OK|MB_ICONINFORMATION);
                    if (BrowseFile(hwnd, path, "All Files (*.*)\0*.*\0")) {
                        char* pDir = strrchr(path, '\\');
                        if (pDir) {
                            if (pDir == path || *(pDir - 1) == ':') *(pDir + 1) = '\0'; 
                            else *pDir = '\0';
                        }
                        SetWindowText(hTarget, path);
                    }
                } else {
                    if (BrowseFile(hwnd, path, "Programs (*.exe;*.com;*.pif;*.bat)\0*.exe;*.com;*.pif;*.bat\0All Files (*.*)\0*.*\0")) { 
                        SetWindowText(hTarget, path); 
                        if (GetWindowTextLength(hName) == 0) {
                            char base[MAX_PATH], *p, *dot;
                            lstrcpy(base, path);
                            p = strrchr(base, '\\');
                            if (p) lstrcpy(base, p + 1);
                            dot = strrchr(base, '.');
                            if (dot) *dot = '\0';
                            if (base[0]) {
                                AnsiLower((LPSTR)base);
                                if (base[0] >= 'a' && base[0] <= 'z') base[0] -= 32; 
                            }
                            SetWindowText(hName, base);
                        }
                    } 
                }
            } 
            else if (LOWORD(wp) == 103) {
                if (SendMessage(hFolderCheck, BM_GETCHECK, 0, 0)) {
                    EnableWindow(hParams, FALSE); SetWindowText(hParams, "");
                    EnableWindow(hMinCheck, FALSE); SendMessage(hMinCheck, BM_SETCHECK, 0, 0);
                    EnableWindow(hHotkey, FALSE); SetWindowText(hHotkey, ""); RemoveProp(hHotkey, "VK");
                    EnableWindow(hMapDirCheck, TRUE);
                    if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) {
                        SetWindowText(hTargetLbl, "Dir Path:");
                        EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                    } else {
                        SetWindowText(hTargetLbl, "Target:");
                        EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE);
                    }
                } else {
                    EnableWindow(hMapDirCheck, FALSE); SendMessage(hMapDirCheck, BM_SETCHECK, 0, 0);
                    SetWindowText(hTargetLbl, "Target (File):");
                    EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                    EnableWindow(hParams, TRUE);
                    EnableWindow(hMinCheck, TRUE);
                    EnableWindow(hHotkey, TRUE);
                }
            }
            else if (LOWORD(wp) == 104) {
                if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) {
                    SetWindowText(hTargetLbl, "Dir Path:");
                    EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE);
                } else {
                    SetWindowText(hTargetLbl, "Target:");
                    SetWindowText(hTarget, "");
                    EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE);
                }
            }
            else if (LOWORD(wp) == IDOK) {
                char name[MAX_PATH], target[MAX_PATH], params[MAX_PATH], iconF[MAX_PATH], parentSel[128], *space;
                int vkCode = (int)(INT_PTR)GetProp(hHotkey, "VK");
                GetWindowText(hName, name, MAX_PATH); GetWindowText(hTarget, target, MAX_PATH); GetWindowText(hParams, params, MAX_PATH); GetWindowText(hIcon, iconF, MAX_PATH);
                GetWindowText(hParentFolder, parentSel, sizeof(parentSel));
                space = strchr(parentSel, ' '); if (space) *space = '\0';

                if (name[0] && (target[0] || SendMessage(hFolderCheck, BM_GETCHECK, 0, 0))) {
                    IniShortcut sh; memset(&sh, 0, sizeof(IniShortcut)); 
                    int i, maxId = 0, foundIdx = -1;
                    
                    for (i = 0; i < g_IniShortcutCount; i++) { 
                        int idNum = atoi(g_IniShortcuts[i].id);
                        if (idNum > maxId) maxId = idNum;
                        if (g_EditShortcutId[0] != '\0' && lstrcmp(g_IniShortcuts[i].id, g_EditShortcutId) == 0) foundIdx = i;
                    }
                    
                    if (foundIdx != -1) lstrcpy(sh.id, g_EditShortcutId);
                    else sprintf(sh.id, "%08d", maxId + 1);

                    lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0");
                    lstrcpy(sh.name, name); lstrcpy(sh.exe, target); lstrcpy(sh.params, params); lstrcpy(sh.icon, iconF);
                    if (vkCode) sprintf(sh.hotkey, "%d", vkCode); else sh.hotkey[0] = '\0';
                    sh.minimized = SendMessage(hMinCheck, BM_GETCHECK, 0, 0) ? 1 : 0; 
                    sh.isFolder = SendMessage(hFolderCheck, BM_GETCHECK, 0, 0) ? 1 : 0;
                    if (sh.isFolder) { 
                        sh.params[0] = '\0'; sh.hotkey[0] = '\0';
                        if (!SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) sh.exe[0] = '\0';
                    }
                    
                    if (foundIdx != -1) g_IniShortcuts[foundIdx] = sh;
                    else if (g_IniShortcutCount < MAX_INI_SHORTCUTS) g_IniShortcuts[g_IniShortcutCount++] = sh;
                    
                    qsort(g_IniShortcuts, g_IniShortcutCount, sizeof(IniShortcut), CompareIni);
                    g_bIniChanged = TRUE;
                }
                g_EditShortcutId[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); ShowWindow(hwnd, SW_HIDE); DestroyWindow(hwnd);
            } else if (LOWORD(wp) == IDCANCEL) { g_EditShortcutId[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); ShowWindow(hwnd, SW_HIDE); DestroyWindow(hwnd); }
            return 0;
        case WM_CLOSE: g_EditShortcutId[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); ShowWindow(hwnd, SW_HIDE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
static void SetFont(HWND hwnd, HFONT font) {
    if (!font) font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    if (!font) font = (HFONT)GetStockObject(SYSTEM_FONT);
    SendMessage(hwnd, WM_SETFONT, (WPARAM)font, MAKELONG(TRUE, 0));
}
static void TrimCaption(const char* src, char* dst, int maxLen) {
    int len = lstrlen(src);
    if (len > maxLen) { memcpy(dst, src, maxLen - 3); dst[maxLen-3] = '.'; dst[maxLen-2] = '.'; dst[maxLen-1] = '.'; dst[maxLen] = '\0'; } 
    else lstrcpy(dst, src);
}
static void UpdateClock(void) {
    if (g_hClock) InvalidateRect(g_hClock, NULL, FALSE);
}

static BOOL BrowseFile(HWND hwnd, char* outPath, const char* filter) {
    OPENFILENAME ofn; char file[MAX_PATH] = ""; 
    memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileName(&ofn)) { lstrcpy(outPath, file); return TRUE; }
    return FALSE;
}

void HideMinimizedIcon(HWND hwnd) {
    if (IsIconic(hwnd)) {
        RECT rc; GetWindowRect(hwnd, &rc);
        if (rc.top < 30000) SetWindowPos(hwnd, 0, rc.left, 32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

BOOL CALLBACK HideIconsEnumProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd) || hwnd == (HWND)lParam) return TRUE;
    HideMinimizedIcon(hwnd); return TRUE;
}

void SweepDesktopIcons(HINSTANCE hInstance, HWND hShellWnd) {
    EnumWindows(HideIconsEnumProc, (LPARAM)hShellWnd);
}

static void DetectWindowsVersion(void) {
    HMODULE hMod = GetModuleHandle("ntdll.dll");
    if (hMod) {
        RTLGETVERSION pRtlGetVersion = (RTLGETVERSION)GetProcAddress(hMod, "RtlGetVersion");
        if (pRtlGetVersion) {
            NTDLL_OSVERSIONINFOW rovi; memset(&rovi, 0, sizeof(rovi)); rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (pRtlGetVersion(&rovi) == 0) {
                if (rovi.dwMajorVersion >= 10) lstrcpy(g_szWinVer, "Windows 10 / 11");
                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 3) lstrcpy(g_szWinVer, "Windows 8.1");
                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 2) lstrcpy(g_szWinVer, "Windows 8");
                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 1) lstrcpy(g_szWinVer, "Windows 7");
                else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 0) lstrcpy(g_szWinVer, "Windows Vista");
                else sprintf(g_szWinVer, "Windows %lu.%lu", rovi.dwMajorVersion, rovi.dwMinorVersion);
                return;
            }
        }
    }
    OSVERSIONINFO osvi; ZeroMemory(&osvi, sizeof(OSVERSIONINFO)); osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);
    if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0) lstrcpy(g_szWinVer, "Windows 95");
    else if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 10) lstrcpy(g_szWinVer, "Windows 98");
    else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0) lstrcpy(g_szWinVer, "Windows 2000");
    else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 1) lstrcpy(g_szWinVer, "Windows XP");
    else sprintf(g_szWinVer, "Windows %lu.%lu", osvi.dwMajorVersion, osvi.dwMinorVersion);
}

static void InitFonts(void) {
    g_hFontMenu = CreateFont(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_SWISS, "Arial");
    g_hFontSidebar = CreateFont(-22, 0, 900, 900, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_SWISS, "Arial");
}

static void PositionDialogNearStart(HWND hwnd) {
    RECT rcStart, rcDlg; int x, y, w, h, screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN);
    GetWindowRect(g_hStartBtn, &rcStart); GetWindowRect(hwnd, &rcDlg);
    w = rcDlg.right - rcDlg.left; h = rcDlg.bottom - rcDlg.top;
    if (g_TbPosition == POS_BOTTOM) { x = rcStart.left; y = rcStart.top - h; } 
    else if (g_TbPosition == POS_TOP) { x = rcStart.left; y = rcStart.bottom; } 
    else if (g_TbPosition == POS_LEFT) { x = rcStart.right; y = rcStart.top; } 
    else { x = rcStart.left - w; y = rcStart.top; }
    if (x < 0) x = 0; if (y < 0) y = 0; if (x + w > screenW) x = screenW - w; if (y + h > screenH) y = screenH - h;
    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static void CreateCenteredDialog(HINSTANCE hInst, HWND hParent, LPCSTR className, LPCSTR title, int w, int h) {
    RECT rcStart; int x = 100, y = 100, screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN); HWND hDlg;
    if (g_hStartBtn && IsWindow(g_hStartBtn)) { GetWindowRect(g_hStartBtn, &rcStart); x = rcStart.left; y = rcStart.top - h; if (y < 0) y = rcStart.bottom; } 
    else { x = (screenW - w) / 2; y = (screenH - h) / 2; }
    if (x + w > screenW) x = screenW - w; if (y + h > screenH) y = screenH - h; if (x < 0) x = 0; if (y < 0) y = 0;
    hDlg = CreateWindowEx(0, className, title, WS_VISIBLE | WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, hParent, NULL, hInst, NULL);
    ShowWindow(hDlg, SW_SHOWNORMAL); BringWindowToTop(hDlg); EnableWindow(hParent, FALSE);
}

/* --------------------------------------------------------------------------
   GDI Polygons scaled dynamically
   -------------------------------------------------------------------------- */
static void DrawScaledPolygon(HDC hdc, const POINT* pts, int count, int dx, int dy, int scale) {
    POINT* temp = (POINT*)malloc(sizeof(POINT) * count); int i;
    if (!temp) return;
    for(i = 0; i < count; ++i) { temp[i].x = (pts[i].x / scale) + dx; temp[i].y = (pts[i].y / scale) + dy; }
    Polygon(hdc, temp, count); free(temp);
}

#define DRAW_POLY(pts, count, r, g, b) \
do { \
        COLORREF color = RGB(r, g, b); \
        HBRUSH hBr = CreateSolidBrush(color); \
        HPEN hPn = CreatePen(PS_SOLID, 1, color); \
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr); \
        HPEN hOldPn = (HPEN)SelectObject(hdc, hPn); \
        DrawScaledPolygon(hdc, pts, count, x, y, scale); \
        SelectObject(hdc, hOldBr); SelectObject(hdc, hOldPn); \
        DeleteObject(hBr); DeleteObject(hPn); \
    } while(0);
static void DrawIconFolder(HDC hdc, int x, int y, int scale) {
    static const POINT back[] = { {6, 12}, {24, 12}, {30, 20}, {58, 20}, {58, 56}, {6, 56} }; DRAW_POLY(back, 6, 128, 128, 0)
    static const POINT front[] = { {6, 24}, {58, 24}, {58, 56}, {6, 56} }; DRAW_POLY(front, 4, 255, 255, 0)
}
static void DrawIconSearch(HDC hdc, int x, int y, int scale) {
    static const POINT p0[] = { {38,22}, {37,28}, {33,33}, {28,37}, {22,38}, {16,37}, {11,33}, {7,28}, {6,22}, {7,16}, {11,11}, {16,7}, {22,6}, {28,7}, {33,11}, {37,16} }; DRAW_POLY(p0, 16, 0, 128, 128)
    static const POINT p2[] = { {33,34}, {37,28}, {58,45}, {54,51} }; DRAW_POLY(p2, 4, 0, 0, 128)
}
static void DrawIconSettings(HDC hdc, int x, int y, int scale) {
    static const POINT bg[] = { {8, 8}, {56, 8}, {56, 56}, {8, 56} }; DRAW_POLY(bg, 4, 192, 192, 192)
    static const POINT top[] = { {12, 12}, {52, 12}, {52, 20}, {12, 20} }; DRAW_POLY(top, 4, 128, 128, 128)
    static const POINT b1[] = { {14, 26}, {24, 26}, {24, 34}, {14, 34} }; DRAW_POLY(b1, 4, 0, 0, 128)
    static const POINT b2[] = { {28, 38}, {38, 38}, {38, 46}, {28, 46} }; DRAW_POLY(b2, 4, 255, 0, 0)
    static const POINT b3[] = { {42, 30}, {52, 30}, {52, 38}, {42, 38} }; DRAW_POLY(b3, 4, 0, 128, 0)
}
static void DrawIconHelp(HDC hdc, int x, int y, int scale) {
    static const POINT bL[] = { {6,24}, {30,28}, {30,56}, {6,52} }; DRAW_POLY(bL, 4, 0, 0, 128)
    static const POINT bR[] = { {34,28}, {58,24}, {58,52}, {34,56} }; DRAW_POLY(bR, 4, 0, 0, 128)
    static const POINT pL[] = { {10,26}, {30,30}, {30,54}, {10,50} }; DRAW_POLY(pL, 4, 255, 255, 255)
    static const POINT pR[] = { {34,30}, {54,26}, {54,50}, {34,54} }; DRAW_POLY(pR, 4, 255, 255, 255)
    static const POINT q1[] = { {24,8}, {40,8}, {40,16}, {24,16} }; DRAW_POLY(q1, 4, 255, 255, 0)
    static const POINT q2[] = { {32,16}, {40,16}, {40,24}, {32,24} }; DRAW_POLY(q2, 4, 255, 255, 0)
    static const POINT q3[] = { {28,24}, {40,24}, {40,32}, {28,32} }; DRAW_POLY(q3, 4, 255, 255, 0)
    static const POINT q4[] = { {28,32}, {36,32}, {36,38}, {28,38} }; DRAW_POLY(q4, 4, 255, 255, 0)
    static const POINT qd[] = { {28,42}, {36,42}, {36,50}, {28,50} }; DRAW_POLY(qd, 4, 255, 255, 0)
}
static void DrawIconRun(HDC hdc, int x, int y, int scale) {
    static const POINT appbg[] = { {8,12}, {56,12}, {56,52}, {8,52} }; DRAW_POLY(appbg, 4, 192, 192, 192)
    static const POINT apptop[] = { {8,12}, {56,12}, {56,20}, {8,20} }; DRAW_POLY(apptop, 4, 0, 0, 128)
    static const POINT appin[] = { {12,24}, {52,24}, {52,48}, {12,48} }; DRAW_POLY(appin, 4, 255, 255, 255)
}
static void DrawIconMonitor(HDC hdc, int x, int y, int scale) {
    static const POINT bzl[] = { {6, 4}, {58, 4}, {58, 40}, {6, 40} }; DRAW_POLY(bzl, 4, 0, 0, 0)
    static const POINT scr[] = { {10, 8}, {54, 8}, {54, 36}, {10, 36} }; DRAW_POLY(scr, 4, 0, 128, 128)
    static const POINT nck[] = { {26, 40}, {38, 40}, {38, 52}, {26, 52} }; DRAW_POLY(nck, 4, 224, 224, 224)
    static const POINT bas[] = { {18, 52}, {46, 52}, {46, 58}, {18, 58} }; DRAW_POLY(bas, 4, 224, 224, 224)
}
static void DrawIconVolume(HDC hdc, int x, int y, int scale) {
    static const POINT box[] = { {12, 24}, {20, 24}, {20, 40}, {12, 40} }; DRAW_POLY(box, 4, 255, 255, 128)
    static const POINT cone[] = { {20, 24}, {32, 12}, {32, 52}, {20, 40} }; DRAW_POLY(cone, 4, 255, 200, 0)
    static const POINT w1p[] = { {38, 24}, {42, 32}, {38, 40}, {36, 40}, {40, 32}, {36, 24} }; DRAW_POLY(w1p, 6, 0, 0, 0)
    static const POINT w2p[] = { {46, 16}, {52, 32}, {46, 48}, {44, 48}, {50, 32}, {44, 16} }; DRAW_POLY(w2p, 6, 0, 0, 0)
}
static void DrawIconNotepad(HDC hdc, int x, int y, int scale) {
    static const POINT cover[] = { {16, 8}, {48, 8}, {48, 56}, {16, 56} }; DRAW_POLY(cover, 4, 255, 255, 255)
    static const POINT bind[] = { {12, 8}, {20, 8}, {20, 56}, {12, 56} }; DRAW_POLY(bind, 4, 0, 0, 128)
    static const POINT l1[] = { {24, 16}, {44, 16}, {44, 18}, {24, 18} }; DRAW_POLY(l1, 4, 192, 192, 192)
    static const POINT l2[] = { {24, 24}, {44, 24}, {44, 26}, {24, 26} }; DRAW_POLY(l2, 4, 192, 192, 192)
    static const POINT l3[] = { {24, 32}, {44, 32}, {44, 34}, {24, 34} }; DRAW_POLY(l3, 4, 192, 192, 192)
}
static void DrawIconCmd(HDC hdc, int x, int y, int scale) {
    static const POINT bg[] = { {8, 12}, {56, 12}, {56, 52}, {8, 52} }; DRAW_POLY(bg, 4, 0, 0, 0)
    static const POINT tb[] = { {8, 12}, {56, 12}, {56, 20}, {8, 20} }; DRAW_POLY(tb, 4, 128, 128, 128)
    static const POINT p1[] = { {12, 24}, {18, 28}, {12, 32}, {14, 32}, {20, 28}, {14, 24} }; DRAW_POLY(p1, 6, 192, 192, 192)
    static const POINT p2[] = { {22, 30}, {28, 30}, {28, 32}, {22, 32} }; DRAW_POLY(p2, 4, 192, 192, 192)
}
static void DrawIconWrite(HDC hdc, int x, int y, int scale) {
    static const POINT doc[] = { {12, 8}, {44, 8}, {44, 56}, {12, 56} }; DRAW_POLY(doc, 4, 255, 255, 255)
    static const POINT fold[] = { {36, 8}, {44, 16}, {36, 16} }; DRAW_POLY(fold, 3, 192, 192, 192)
    static const POINT pen1[] = { {48, 12}, {54, 18}, {36, 44}, {30, 38} }; DRAW_POLY(pen1, 4, 255, 255, 0)
    static const POINT pen2[] = { {30, 38}, {36, 44}, {28, 48} }; DRAW_POLY(pen2, 3, 0, 0, 0)
}
static void DrawIconCalc(HDC hdc, int x, int y, int scale) {
    static const POINT bg[] = { {12, 8}, {52, 8}, {52, 56}, {12, 56} }; DRAW_POLY(bg, 4, 192, 192, 192)
    static const POINT screen[] = { {16, 12}, {48, 12}, {48, 24}, {16, 24} }; DRAW_POLY(screen, 4, 0, 128, 128)
    static const POINT b1[] = { {16, 28}, {24, 28}, {24, 36}, {16, 36} }; DRAW_POLY(b1, 4, 128, 128, 128)
    static const POINT b2[] = { {28, 28}, {36, 28}, {36, 36}, {28, 36} }; DRAW_POLY(b2, 4, 128, 128, 128)
    static const POINT b3[] = { {40, 28}, {48, 28}, {48, 36}, {40, 36} }; DRAW_POLY(b3, 4, 128, 128, 128)
    static const POINT b4[] = { {16, 40}, {24, 40}, {24, 48}, {16, 48} }; DRAW_POLY(b4, 4, 128, 128, 128)
    static const POINT b5[] = { {28, 40}, {36, 40}, {36, 48}, {28, 48} }; DRAW_POLY(b5, 4, 128, 128, 128)
    static const POINT b6[] = { {40, 40}, {48, 40}, {48, 48}, {40, 48} }; DRAW_POLY(b6, 4, 255, 128, 0)
}
static void DrawIconPaint(HDC hdc, int x, int y, int scale) {
    static const POINT pal[] = { {16,24}, {24,12}, {40,12}, {52,24}, {48,44}, {32,56}, {16,44} }; DRAW_POLY(pal, 7, 222, 184, 135)
    static const POINT c1[] = { {24,20}, {28,20}, {28,24}, {24,24} }; DRAW_POLY(c1, 4, 255, 0, 0)
    static const POINT c2[] = { {36,20}, {40,20}, {40,24}, {36,24} }; DRAW_POLY(c2, 4, 0, 255, 0)
    static const POINT c3[] = { {28,32}, {32,32}, {32,36}, {28,36} }; DRAW_POLY(c3, 4, 0, 0, 255)
    static const POINT brsh[] = { {48,8}, {56,16}, {36,40}, {28,32} }; DRAW_POLY(brsh, 4, 160, 82, 45)
}
static void DrawGdiIcon(HDC hdc, int x, int y, int polyIcon, int scale) {
    switch(polyIcon) {
        case 0: case 1: DrawIconFolder(hdc, x, y, scale); break;
        case 2: DrawIconSearch(hdc, x, y, scale); break;
        case 3: DrawIconSettings(hdc, x, y, scale); break;
        case 4: DrawIconHelp(hdc, x, y, scale); break;
        case 5: DrawIconRun(hdc, x, y, scale); break;
        case 6: DrawIconMonitor(hdc, x, y, scale); break;
        case 7: DrawIconVolume(hdc, x, y, scale); break;
        case 8: DrawIconNotepad(hdc, x, y, scale); break;
        case 9: DrawIconCmd(hdc, x, y, scale); break;
        case 10: DrawIconWrite(hdc, x, y, scale); break;
        case 11: DrawIconCalc(hdc, x, y, scale); break;
        case 12: DrawIconPaint(hdc, x, y, scale); break;
        default: DrawIconRun(hdc, x, y, scale); break;
    }
}

/* --------------------------------------------------------------------------
   Configuration Engine & Dynamic Menu Parsing
   -------------------------------------------------------------------------- */
static void ParseContextMenuItem(const char* val, ContextMenuItem* out) {
    const char* p = val; int i; char* dests[3];
    dests[0] = out->name; dests[1] = out->exe; dests[2] = out->params;
    for (i = 0; i < 3; i++) {
        char* d = dests[i]; int c = 0;
        int maxL = (i == 0) ? 63 : (i == 1 ? MAX_PATH - 1 : 127);
        while (*p && *p != '|') { if (c < maxL) d[c++] = *p; p++; }
        d[c] = '\0'; if (*p == '|') p++;
    }
    out->isSeparator = (lstrcmp(out->name, "-") == 0);
}

/* --------------------------------------------------------------------------
   2. Menu Loader (e1plorer rename)
   -------------------------------------------------------------------------- */
static void LoadMenusFromIni(void) {
    char keys[4096], *pKey;
    g_WinXCount = 0;
    
    GetPrivateProfileString("WinXMenu", NULL, "", keys, sizeof(keys), g_szIniPath);
    if (keys[0] == '\0') {
        ParseContextMenuItem("Power Options|control.exe|/name Microsoft.PowerOptions", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Event Viewer|eventvwr.msc|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("System|control.exe|/name Microsoft.System", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Device Manager|devmgmt.msc|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Network Connections|control.exe|/name Microsoft.NetworkAndSharingCenter", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Disk Management|diskmgmt.msc|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Computer Management|compmgmt.msc|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("-||", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("New Folder|*NEWFOLDER*|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("New Shortcut|*NEWSHORTCUT*|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Task Manager|taskmgr.exe|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Settings|ms-settings:|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("File Explorer|e1plorer.exe|", &g_WinXMenu[g_WinXCount++]); /* Changed */
        ParseContextMenuItem("Search|e1plorer.exe|-search:c:\\", &g_WinXMenu[g_WinXCount++]); /* Changed */
        ParseContextMenuItem("Run...|*RUN*|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("-||", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Shutdown or Sign out|*SHUTDOWN*|", &g_WinXMenu[g_WinXCount++]);
        ParseContextMenuItem("Desktop|*DESKTOP*|", &g_WinXMenu[g_WinXCount++]);
        g_bIniChanged = TRUE;
    } else {
        pKey = keys;
        while (*pKey) {
            char val[512];
            if (g_WinXCount < 32) {
                GetPrivateProfileString("WinXMenu", pKey, "", val, sizeof(val), g_szIniPath);
                ParseContextMenuItem(val, &g_WinXMenu[g_WinXCount++]);
            }
            pKey += lstrlen(pKey) + 1;
        }
    }

    g_TbMenuCount = 0;
    GetPrivateProfileString("TaskbarMenu", NULL, "", keys, sizeof(keys), g_szIniPath);
    if (keys[0] == '\0') {
        ParseContextMenuItem("Show Desktop|*DESKTOP*|", &g_TbMenu[g_TbMenuCount++]);
        ParseContextMenuItem("-||", &g_TbMenu[g_TbMenuCount++]);
        ParseContextMenuItem("Task Manager|taskmgr.exe|", &g_TbMenu[g_TbMenuCount++]);
        g_bIniChanged = TRUE;
    } else {
        pKey = keys;
        while (*pKey) {
            char val[512];
            if (g_TbMenuCount < 32) {
                GetPrivateProfileString("TaskbarMenu", pKey, "", val, sizeof(val), g_szIniPath);
                ParseContextMenuItem(val, &g_TbMenu[g_TbMenuCount++]);
            }
            pKey += lstrlen(pKey) + 1;
        }
    }
}

static void HandleContextItem(ContextMenuItem* item, HWND hwnd) {
    if (item->isSeparator) return;
    
    if (lstrcmpi(item->exe, "*NEWFOLDER*") == 0) {
        lstrcpy(g_ContextId, "0"); g_ContextIsFolder = TRUE; lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0'; g_PromptMode = PROMPT_NEWFOLDER; 
        CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 170);
    }
    else if (lstrcmpi(item->exe, "*NEWSHORTCUT*") == 0) {
        lstrcpy(g_ContextId, "0"); g_ContextIsFolder = TRUE; g_EditShortcutId[0] = '\0'; 
        CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 360, 300);
    }
    else if (lstrcmpi(item->exe, "*RUN*") == 0) { CreateCenteredDialog(g_hInst, hwnd, "RunDlgClass", "Run", 290, 160); }
    else if (lstrcmpi(item->exe, "*SHUTDOWN*") == 0) { ExitWindows(0, 0); }
    else if (lstrcmpi(item->exe, "*DESKTOP*") == 0) { DoShowDesktop(); }
    else { ShellExecute(NULL, "open", item->exe, item->params[0] ? item->params : NULL, NULL, SW_SHOWNORMAL); }
}

static void BuildMenuFromFileSystemFolder(HMENU hMenu, const char* dirPath) {
    WIN32_FIND_DATA fd; HANDLE hFind;
    char searchPath[MAX_PATH]; int totalItems = 0, currentItem = 0, itemsPerCol;
    
    lstrcpy(searchPath, dirPath);
    if (searchPath[lstrlen(searchPath) - 1] != '\\') lstrcat(searchPath, "\\");
    lstrcat(searchPath, "*.*");
    
    hFind = FindFirstFile(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (lstrcmp(fd.cFileName, ".") != 0 && lstrcmp(fd.cFileName, "..") != 0) totalItems++;
        } while (FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
    
    if (totalItems == 0) return;
    itemsPerCol = (totalItems + 4) / 5; if (itemsPerCol < 1) itemsPerCol = 1;
    
    hFind = FindFirstFile(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (lstrcmp(fd.cFileName, ".") != 0 && lstrcmp(fd.cFileName, "..") != 0) {
                char fullPath[MAX_PATH]; ODMenuItem* pItem; UINT flags = MF_OWNERDRAW | MF_STRING;
                if (currentItem > 0 && (currentItem % itemsPerCol) == 0) flags |= MF_MENUBARBREAK;
                
                lstrcpy(fullPath, dirPath);
                if (fullPath[lstrlen(fullPath) - 1] != '\\') lstrcat(fullPath, "\\");
                lstrcat(fullPath, fd.cFileName);
                
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    pItem = AddODItem(fd.cFileName, FALSE, FALSE, 0); 
                    if (pItem) { lstrcpy(pItem->targetPath, fullPath); pItem->isFsFolder = TRUE; AppendMenu(hMenu, flags, pItem->cmdId, (LPSTR)pItem); }
                } else {
                    pItem = AddODItem(fd.cFileName, FALSE, FALSE, 5); 
                    if (pItem) { lstrcpy(pItem->targetPath, fullPath); pItem->isFsFolder = FALSE; AppendMenu(hMenu, flags, pItem->cmdId, (LPSTR)pItem); }
                }
                currentItem++;
            }
        } while (FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
}

static void ParseIniEntry(const char* id, const char* val, IniShortcut* out) {
    const char* p = val; int i; char* dests[7];
    dests[0] = out->name; dests[1] = out->exe; dests[2] = out->params; dests[3] = out->icon; dests[4] = out->minimizedStr; dests[5] = out->parentId; dests[6] = out->hotkey;
    lstrcpy(out->id, id);
    for (i = 0; i < 7; i++) {
        char* d = dests[i]; int c = 0;
        int maxL = (i == 3) ? 15 : (i == 4 ? 3 : (i == 5 ? 11 : (i == 6 ? 15 : 63)));
        if (i == 1) maxL = MAX_PATH - 1;
        while (*p && *p != '|') { if (c < maxL) d[c++] = *p; p++; }
        d[c] = '\0'; if (*p == '|') p++;
    }
    {
        int flags = atoi(out->minimizedStr);
        out->minimized = (flags & 1) != 0; out->isFolder = (flags & 2) != 0;
        if (out->exe[0] == '\0' && flags == 0 && lstrcmp(out->minimizedStr, "0") == 0) out->isFolder = TRUE;
    }
    out->hMenu = NULL;
}

static void GetNewIniId(char* outId) {
    char keys[4096], *pKey; int maxId = 0;
    GetPrivateProfileString("Shortcut", NULL, "", keys, sizeof(keys), g_szIniPath); pKey = keys;
    while (*pKey) { int id = atoi(pKey); if (id > maxId) maxId = id; pKey += lstrlen(pKey) + 1; }
    sprintf(outId, "%08d", maxId + 1);
}

static void SaveIniEntry(IniShortcut* item) {
    char val[1024]; int flags = (item->minimized ? 1 : 0) | (item->isFolder ? 2 : 0);
    sprintf(val, "%s|%s|%s|%s|%d|%s|%s", item->name, item->exe, item->params, item->icon, flags, item->parentId, item->hotkey);
    WritePrivateProfileString("Shortcut", item->id, val, g_szIniPath);
}

/* --------------------------------------------------------------------------
   Sorting Engine (Fixed Root Menu Tiers)
   -------------------------------------------------------------------------- */
int CompareIni(const void* a, const void* b) {
    IniShortcut* sa = (IniShortcut*)a; 
    IniShortcut* sb = (IniShortcut*)b; 
    int cmp = lstrcmp(sa->parentId, sb->parentId);
    
    if (cmp == 0) {
        /* Custom sorting tiers for the Start Menu Root */
        if (lstrcmp(sa->parentId, "0") == 0) {
            int idA = atoi(sa->id), idB = atoi(sb->id);
            
            /* Group 0: Top items (C: Drive & Programs, IDs 1-2)
             * Group 1: Custom User Items (IDs >= 10)
             * Group 2: Bottom System Items (Settings -> Shut Down, IDs 3-9) */
            int grpA = (idA <= 2) ? 0 : ((idA <= 9) ? 2 : 1);
            int grpB = (idB <= 2) ? 0 : ((idB <= 9) ? 2 : 1);
            
            if (grpA != grpB) return grpA - grpB;
            
            /* If they are custom user items in the middle, sort them nicely */
            if (grpA == 1) {
                if (sa->isFolder != sb->isFolder) return sb->isFolder - sa->isFolder;
                return lstrcmpi(sa->name, sb->name);
            }
            
            /* Otherwise, preserve the exact hardcoded system ID order */
            return idA - idB;
        }
        
        /* Standard alphabetical sorting for all sub-folders */
        if (sa->isFolder != sb->isFolder) return sb->isFolder - sa->isFolder;
        cmp = lstrcmpi(sa->name, sb->name);
    }
    return cmp;
}
/* --------------------------------------------------------------------------
   3. Shortcut Loader (e1plorer rename & Removed Startup item)
   -------------------------------------------------------------------------- */
static void LoadIniShortcuts(void) {
    char keys[4096], *pKey; int i; g_IniShortcutCount = 0;
    GetPrivateProfileString("Shortcut", NULL, "", keys, sizeof(keys), g_szIniPath);
    if (keys[0] == '\0') {
        ParseIniEntry("00000001", "C: Drive|C:\\||0|2|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000002", "Programs||||2|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000003", "-||||0|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000004", "Settings|control.exe||3|0|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000005", "Search|e1plorer.exe|-search:c:\\|2|0|0|", &g_IniShortcuts[g_IniShortcutCount++]); /* Changed */
        ParseIniEntry("00000006", "Help|winhelp.exe||4|0|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000007", "Run...|run||5|0|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000008", "-||||0|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000009", "Shut Down...|shutdown||6|0|0|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000010", "Startup||||2|00000002|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000011", "File Explorer|e1plorer.exe||0|0|00000002|", &g_IniShortcuts[g_IniShortcutCount++]); /* Changed */
        ParseIniEntry("00000012", "Notepad|notepad.exe||8|0|00000002|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000013", "Command Prompt|cmd.exe||9|0|00000002|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000014", "Write|write.exe||10|0|00000002|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000015", "Calculator|calc.exe||11|0|00000002|", &g_IniShortcuts[g_IniShortcutCount++]);
        ParseIniEntry("00000016", "Paint Brush|mspaint.exe||12|0|00000002|", &g_IniShortcuts[g_IniShortcutCount++]);
        /* ID 17 (Explorer in Startup) intentionally removed */
        g_bIniChanged = TRUE;
    } else {
        pKey = keys;
        while (*pKey) {
            char val[512];
            if (g_IniShortcutCount < MAX_INI_SHORTCUTS) {
                GetPrivateProfileString("Shortcut", pKey, "", val, sizeof(val), g_szIniPath);
                ParseIniEntry(pKey, val, &g_IniShortcuts[g_IniShortcutCount]); g_IniShortcutCount++;
            }
            pKey += lstrlen(pKey) + 1;
        }
    }
    qsort(g_IniShortcuts, g_IniShortcutCount, sizeof(IniShortcut), CompareIni);
}

static void RunStartupItems(void) {
    int i, j; char startupId[16] = "";
    for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmpi(g_IniShortcuts[i].name, "Startup") == 0) { lstrcpy(startupId, g_IniShortcuts[i].id); break; } }
    if (startupId[0] != '\0') {
        for (j = 0; j < g_IniShortcutCount; j++) {
            if (lstrcmp(g_IniShortcuts[j].parentId, startupId) == 0 && !g_IniShortcuts[j].isFolder) {
                ShellExecute(NULL, "open", g_IniShortcuts[j].exe, g_IniShortcuts[j].params, NULL, g_IniShortcuts[j].minimized ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
            }
        }
    }
}

/* --------------------------------------------------------------------------
   Corrected Config Loader (Loads MRU History into Memory)
   -------------------------------------------------------------------------- */
static void LoadConfig(void) {
    int i; char trayApps[512]; char* token;
    GetAppFilePath("taskbar.ini", g_szIniPath);
    g_TbPosition = GetPrivateProfileInt("Taskbar", "Position", POS_BOTTOM, g_szIniPath);
    g_TbHeight = GetPrivateProfileInt("Taskbar", "Height", 30, g_szIniPath);
    g_TbWidthVert = GetPrivateProfileInt("Taskbar", "WidthVert", 72, g_szIniPath);
    if (g_TbHeight < 24) g_TbHeight = 24; if (g_TbWidthVert < 48) g_TbWidthVert = 48;
    g_TrayAppCount = 0;
    
    GetPrivateProfileString("Systray", "Apps", "", trayApps, sizeof(trayApps), g_szIniPath);
    if (trayApps[0] == '\0') { lstrcpy(g_TrayAppList[g_TrayAppCount++], "taskmgr.exe"); g_bIniChanged = TRUE; }
    else {
        token = strtok(trayApps, "|");
        while(token && g_TrayAppCount < MAX_TRAY_APPS) { lstrcpy(g_TrayAppList[g_TrayAppCount++], token); token = strtok(NULL, "|"); }
    }
    
    g_QLActiveCount = GetPrivateProfileInt("QuickLaunch", "Enabled", 0, g_szIniPath) ? GetPrivateProfileInt("QuickLaunch", "Count", QUICK_LAUNCH_COUNT, g_szIniPath) : 0;
    if (g_QLActiveCount > QUICK_LAUNCH_COUNT) g_QLActiveCount = QUICK_LAUNCH_COUNT;
    for (i = 0; i < g_QLActiveCount; i++) {
        char keyN[16], keyE[16]; sprintf(keyN, "Name%d", i); sprintf(keyE, "Exe%d", i);
        GetPrivateProfileString("QuickLaunch", keyN, "", g_QL[i].name, 16, g_szIniPath); GetPrivateProfileString("QuickLaunch", keyE, "", g_QL[i].exe, MAX_PATH, g_szIniPath);
    }
    
    /* Safely load history directly into the memory variable */
    GetPrivateProfileString("Run", "History", "", g_RunHistory, sizeof(g_RunHistory), g_szIniPath);

    LoadMenusFromIni(); 
    LoadIniShortcuts();
}

static void SaveConfig(void) {
    char buf[16]; sprintf(buf, "%d", g_TbPosition); WritePrivateProfileString("Taskbar", "Position", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbHeight); WritePrivateProfileString("Taskbar", "Height", buf, g_szIniPath);
    sprintf(buf, "%d", g_TbWidthVert); WritePrivateProfileString("Taskbar", "WidthVert", buf, g_szIniPath);
}

/* --------------------------------------------------------------------------
   Owner-Drawn Win95-Style Start Menu Engine
   -------------------------------------------------------------------------- */
static ODMenuItem* AddODItem(const char* text, BOOL isRoot, BOOL isSeparator, int polyIcon) {
    ODMenuItem* item;
    if (g_ODCount >= MAX_OD_ITEMS) return NULL;
    item = &g_ODItems[g_ODCount++]; memset(item, 0, sizeof(ODMenuItem));
    if (text) lstrcpyn(item->text, text, 63);
    item->isRoot = isRoot; item->isSeparator = isSeparator;
    item->polyIcon = polyIcon; item->cmdId = IDM_FS_BASE + g_ODCount;
    return item;
}

static void BuildMenuFromIni(HMENU hMenu, const char* parentId, BOOL isRoot) {
    int i;
    for (i = 0; i < g_IniShortcutCount; i++) {
        if (lstrcmp(g_IniShortcuts[i].parentId, parentId) == 0) {
            BOOL isSep = (lstrcmp(g_IniShortcuts[i].name, "-") == 0); int poly = 5;
            if (g_IniShortcuts[i].icon[0] >= '0' && g_IniShortcuts[i].icon[0] <= '9') poly = atoi(g_IniShortcuts[i].icon);
            else if (g_IniShortcuts[i].isFolder) poly = 0; 
            
            if (isSep) {
                ODMenuItem* pItem = AddODItem("", isRoot, TRUE, -1);
                if (pItem) AppendMenu(hMenu, MF_OWNERDRAW, 0, (LPSTR)pItem);
            } else if (g_IniShortcuts[i].isFolder) {
                HMENU hSub = CreatePopupMenu(); ODMenuItem* pItem = AddODItem(g_IniShortcuts[i].name, isRoot, FALSE, poly); g_IniShortcuts[i].hMenu = hSub;
                if (g_IniShortcuts[i].exe[0] != '\0') BuildMenuFromFileSystemFolder(hSub, g_IniShortcuts[i].exe); else BuildMenuFromIni(hSub, g_IniShortcuts[i].id, FALSE);
                if (pItem) AppendMenu(hMenu, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)hSub, (LPSTR)pItem);
            } else {
                ODMenuItem* pItem = AddODItem(g_IniShortcuts[i].name, isRoot, FALSE, poly);
                if (pItem) AppendMenu(hMenu, MF_OWNERDRAW | MF_STRING, IDM_START_BASE + i, (LPSTR)pItem);
            }
        }
    }
}

/* --------------------------------------------------------------------------
   Start Menu Alignment Fix
   -------------------------------------------------------------------------- */
static void ShowStartMenu(HWND hBtn) {
    HMENU hMenu = CreatePopupMenu(); POINT pt; RECT rc; int i, curY = 0; g_ODCount = 0; BuildMenuFromIni(hMenu, "0", TRUE);
    for (i = 0; i < g_ODCount; i++) { if (g_ODItems[i].isRoot) { g_ODItems[i].yOffset = curY; curY += g_ODItems[i].isSeparator ? 8 : 36; } }
    for (i = 0; i < g_ODCount; i++) { if (g_ODItems[i].isRoot) g_ODItems[i].totalHeight = curY; }
    GetWindowRect(hBtn, &rc);
    
    UINT flags = TPM_LEFTALIGN;
    
    /* Fix: TPM_BOTTOMALIGN automatically handles the menu height. Removing double-subtraction. */
    if (g_TbPosition == POS_BOTTOM) { pt.x = rc.left; pt.y = rc.top; flags |= TPM_BOTTOMALIGN; } 
    else if (g_TbPosition == POS_TOP) { pt.x = rc.left; pt.y = rc.bottom; flags |= TPM_TOPALIGN; } 
    else if (g_TbPosition == POS_LEFT) { pt.x = rc.right; pt.y = rc.top; flags |= TPM_TOPALIGN; } 
    else { pt.x = rc.left; pt.y = rc.top; flags = TPM_RIGHTALIGN | TPM_TOPALIGN; }
    
    g_ContextId[0] = '\0'; g_ContextIsFolder = FALSE;
    g_hMsgHook = SetWindowsHookEx(WH_MSGFILTER, MsgFilterProc, NULL, GetCurrentThreadId());
    
    SetForegroundWindow(g_hTaskbar);
    g_MenuOpen = TRUE; 
    TrackPopupMenu(hMenu, flags, pt.x, pt.y, 0, g_hTaskbar, NULL); 
    PostMessage(g_hTaskbar, WM_NULL, 0, 0);
    g_MenuOpen = FALSE;
    
    UnhookWindowsHookEx(g_hMsgHook); 
    DestroyMenu(hMenu);
}
static void ShowFolderMenu(HWND hAnchor, const char* folderId) {
    HMENU hMenu = CreatePopupMenu(); POINT pt; RECT rc;
    BuildMenuFromIni(hMenu, folderId, FALSE);

    GetWindowRect(hAnchor, &rc);
    UINT flags = TPM_LEFTALIGN;
    if (g_TbPosition == POS_BOTTOM) { pt.x = rc.left; pt.y = rc.top; flags |= TPM_BOTTOMALIGN; } 
    else if (g_TbPosition == POS_TOP) { pt.x = rc.left; pt.y = rc.bottom; flags |= TPM_TOPALIGN; } 
    else if (g_TbPosition == POS_LEFT) { pt.x = rc.right; pt.y = rc.top; flags |= TPM_TOPALIGN; } 
    else { pt.x = rc.left; pt.y = rc.top; flags = TPM_RIGHTALIGN | TPM_TOPALIGN; }

    g_ContextId[0] = '\0'; g_ContextIsFolder = FALSE;
    g_hMsgHook = SetWindowsHookEx(WH_MSGFILTER, MsgFilterProc, NULL, GetCurrentThreadId());
    
    SetForegroundWindow(g_hTaskbar);
    g_MenuOpen = TRUE;
    TrackPopupMenu(hMenu, flags, pt.x, pt.y, 0, g_hTaskbar, NULL);
    PostMessage(g_hTaskbar, WM_NULL, 0, 0);
    g_MenuOpen = FALSE;
    
    UnhookWindowsHookEx(g_hMsgHook); DestroyMenu(hMenu);
}

static void LaunchShortcut(HWND hwnd, IniShortcut* sh) {
    if (lstrcmpi(sh->exe, "shutdown") == 0) {
        ExitWindows(0, 0);
    } else if (lstrcmpi(sh->exe, "run") == 0) {
        CreateCenteredDialog(g_hInst, hwnd, "RunDlgClass", "Run", 290, 160);
    } else if (sh->exe[0] != '\0') {
        char dir[MAX_PATH]; char* pDir; 
        lstrcpy(dir, sh->exe);
        pDir = strrchr(dir, '\\');
        if (pDir) *pDir = '\0'; else dir[0] = '\0';
        ShellExecute(NULL, "open", sh->exe, sh->params[0] ? sh->params : NULL, dir[0] ? dir : NULL, sh->minimized ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
    }
}

/* --------------------------------------------------------------------------
   Subclass Procedures
   -------------------------------------------------------------------------- */
LRESULT CALLBACK HotkeyEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        int vk = (int)wp;
        if (vk == VK_ESCAPE || vk == VK_BACK || vk == VK_DELETE) { SetWindowText(hwnd, ""); RemoveProp(hwnd, "VK"); return 0; }
        if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
            char name[64]; SetProp(hwnd, "VK", (HANDLE)(INT_PTR)vk); GetKeyNameText(lp, name, sizeof(name)); SetWindowText(hwnd, name);
        }
        return 0;
    }
    if (msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_KEYUP || msg == WM_SYSKEYUP) return 0;
    return CallWindowProc(OldHotkeyProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_DOWN || wp == VK_UP) { SetFocus(g_hSearchList); SendMessage(g_hSearchList, WM_KEYDOWN, wp, lp); return 0; }
        if (wp == VK_RETURN) { PostMessage(g_hTaskbar, WM_COMMAND, MAKEWPARAM(IDM_SEARCH_LIST, LBN_DBLCLK), (LPARAM)g_hSearchList); return 0; }
        if (wp == VK_ESCAPE) { ShowWindow(g_hSearchList, SW_HIDE); SetWindowText(hwnd, ""); return 0; }
    }
    if (msg == WM_KEYUP && wp != VK_DOWN && wp != VK_UP && wp != VK_RETURN && wp != VK_ESCAPE) {
        KillTimer(hwnd, TIMER_SEARCH); SetTimer(hwnd, TIMER_SEARCH, 250, NULL);
    }
    if (msg == WM_TIMER && wp == TIMER_SEARCH) {
        KillTimer(hwnd, TIMER_SEARCH);
        char buf[64]; GetWindowText(hwnd, buf, sizeof(buf)); SendMessage(g_hSearchList, LB_RESETCONTENT, 0, 0);
        if (buf[0]) {
            int i; char bufUpper[64]; lstrcpy(bufUpper, buf); AnsiUpper((LPSTR)bufUpper);
            for (i=0; i<g_IniShortcutCount; i++) {
                if (lstrcmp(g_IniShortcuts[i].name, "-") != 0) {
                    char nameUpper[64]; lstrcpy(nameUpper, g_IniShortcuts[i].name); AnsiUpper((LPSTR)nameUpper);
                    if (strstr(nameUpper, bufUpper)) {
                        int pos = SendMessage(g_hSearchList, LB_ADDSTRING, 0, (LPARAM)g_IniShortcuts[i].name);
                        SendMessage(g_hSearchList, LB_SETITEMDATA, pos, i);
                    }
                }
            }
            if (SendMessage(g_hSearchList, LB_GETCOUNT, 0, 0) > 0) {
                RECT rc; int lbH; GetWindowRect(hwnd, &rc);
                lbH = SendMessage(g_hSearchList, LB_GETCOUNT, 0, 0) * 16 + 2; if (lbH > 200) lbH = 200;
                SendMessage(g_hSearchList, LB_SETCURSEL, 0, 0);
                int screenH = GetSystemMetrics(SM_CYSCREEN), screenW = GetSystemMetrics(SM_CXSCREEN);
                int sx = rc.left, sy = rc.top;
                if (g_TbPosition == POS_BOTTOM) sy = rc.top - lbH;
                else if (g_TbPosition == POS_TOP) sy = rc.bottom;
                else if (g_TbPosition == POS_RIGHT) sx = rc.left - 150;
                else sx = rc.right;
                if (sy + lbH > screenH) sy = screenH - lbH;
                if (sy < 0) sy = 0; if (sx + 150 > screenW) sx = screenW - 150; if (sx < 0) sx = 0;
                SetWindowPos(g_hSearchList, HWND_TOPMOST, sx, sy, 150, lbH, SWP_SHOWWINDOW);
            } else ShowWindow(g_hSearchList, SW_HIDE);
        } else ShowWindow(g_hSearchList, SW_HIDE);
        return 0; 
    }
    if (msg == WM_KILLFOCUS && (HWND)wp != g_hSearchList) ShowWindow(g_hSearchList, SW_HIDE);
    return CallWindowProc(OldSearchBoxProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK SearchListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { PostMessage(g_hTaskbar, WM_COMMAND, MAKEWPARAM(IDM_SEARCH_LIST, LBN_DBLCLK), (LPARAM)hwnd); return 0; }
    if (msg == WM_LBUTTONUP) {
        RECT rc; POINT pt; LRESULT ret = CallWindowProc(OldSearchListProc, hwnd, msg, wp, lp); GetClientRect(hwnd, &rc); pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
        if (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom) PostMessage(g_hTaskbar, WM_COMMAND, MAKEWPARAM(IDM_SEARCH_LIST, LBN_DBLCLK), (LPARAM)hwnd);
        return ret;
    }
    if (msg == WM_LBUTTONDBLCLK) {
        LRESULT ret = CallWindowProc(OldSearchListProc, hwnd, msg, wp, lp);
        PostMessage(g_hTaskbar, WM_COMMAND, MAKEWPARAM(IDM_SEARCH_LIST, LBN_DBLCLK), (LPARAM)hwnd); return ret;
    }
    if (msg == WM_KILLFOCUS && (HWND)wp != g_hSearchBox) ShowWindow(hwnd, SW_HIDE);
    return CallWindowProc(OldSearchListProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK MsgFilterProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == MSGF_MENU) {
        MSG* pMsg = (MSG*)lParam;
        if (pMsg->message == WM_RBUTTONUP) { PostMessage(g_hTaskbar, WM_USER_CONTEXTMENU, 0, MAKELONG(pMsg->pt.x, pMsg->pt.y)); SendMessage(pMsg->hwnd, WM_CANCELMODE, 0, 0); return 1; }
    }
    return CallNextHookEx(g_hMsgHook, code, wParam, lParam);
}

LRESULT CALLBACK ClockProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) return HTCLIENT;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc, textRc; int isHorz, w, h, rows, cols, trayW, trayH, startX, startY, i;
        GetClientRect(hwnd, &rc); isHorz = (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP);
        w = rc.right - rc.left; h = rc.bottom - rc.top; rows = 1; cols = 1;
        if (isHorz) { rows = h / 21; if (rows < 1) rows = 1; cols = (g_TrayIconCount + rows - 1) / rows; if (cols < 1) cols = 1; } 
        else { cols = w / 21; if (cols < 1) cols = 1; rows = (g_TrayIconCount + cols - 1) / cols; if (rows < 1) rows = 1; }
        trayW = cols * 21; trayH = rows * 21; textRc = rc;
        
        if (isHorz) {
            int totalW = (g_TrayIconCount > 0 ? trayW : 0) + CLOCK_WIDTH;
            startX = rc.left + (w - totalW) / 2; if (startX < rc.left + 2) startX = rc.left + 2;
            startY = rc.top + (h - trayH) / 2; if (startY < rc.top + 2) startY = rc.top + 2;
            textRc.left = startX + (g_TrayIconCount > 0 ? trayW : 0); textRc.right = textRc.left + CLOCK_WIDTH;
        } else {
            int totalH = (g_TrayIconCount > 0 ? trayH : 0) + CLOCK_HEIGHT;
            startY = rc.top + (h - totalH) / 2; if (startY < rc.top + 2) startY = rc.top + 2;
            startX = rc.left + (w - trayW) / 2; if (startX < rc.left + 2) startX = rc.left + 2;
            textRc.top = startY + (g_TrayIconCount > 0 ? trayH : 0); textRc.bottom = textRc.top + CLOCK_HEIGHT;
        }
        
        { HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &rc, hBr); DeleteObject(hBr); }
        
        {
            HPEN hShadow = CreatePen(PS_SOLID, 1, RGB(128, 128, 128)), hHighlight = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HPEN hOld = SelectObject(hdc, hShadow);
            MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.right, rc.top);
            MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.left, rc.bottom);
            SelectObject(hdc, hHighlight);
            MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.right, rc.bottom - 1);
            MoveToEx(hdc, rc.right - 1, rc.top, NULL); LineTo(hdc, rc.right - 1, rc.bottom);
            SelectObject(hdc, hOld); DeleteObject(hShadow); DeleteObject(hHighlight);
        }
        
        for (i = 0; i < g_TrayIconCount; i++) { 
            int c, r, ix, iy;
            if (isHorz) { c = i / rows; r = i % rows; } else { c = i % cols; r = i / cols; }
            ix = startX + c * 21; iy = startY + r * 21;
            if (g_TrayIcons[i].hIcon) {
                DrawIconEx(hdc, ix + 2, iy + 2, g_TrayIcons[i].hIcon, 16, 16, 0, NULL, DI_NORMAL);
            }
        }
        
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        {
            char timeStr[32]; SYSTEMTIME st; GetLocalTime(&st);
            sprintf(timeStr, "%02d:%02d", st.wHour, st.wMinute);
            DrawText(hdc, timeStr, -1, &textRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        int ptX = (short)LOWORD(lParam), ptY = (short)HIWORD(lParam), isHorz, w, h, rows, cols, trayW, trayH, startX, startY; BOOL inTray; RECT rc; 
        GetClientRect(hwnd, &rc); isHorz = (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP);
        w = rc.right - rc.left; h = rc.bottom - rc.top; rows = 1; cols = 1;
        if (isHorz) { rows = h / 21; if (rows < 1) rows = 1; cols = (g_TrayIconCount + rows - 1) / rows; if (cols < 1) cols = 1; } 
        else { cols = w / 21; if (cols < 1) cols = 1; rows = (g_TrayIconCount + cols - 1) / cols; if (rows < 1) rows = 1; }
        trayW = cols * 21; trayH = rows * 21;
        if (isHorz) {
            int totalW = (g_TrayIconCount > 0 ? trayW : 0) + CLOCK_WIDTH;
            startX = rc.left + (w - totalW) / 2; if (startX < rc.left + 2) startX = rc.left + 2;
            startY = rc.top + (h - trayH) / 2; if (startY < rc.top + 2) startY = rc.top + 2;
        } else {
            int totalH = (g_TrayIconCount > 0 ? trayH : 0) + CLOCK_HEIGHT;
            startY = rc.top + (h - totalH) / 2; if (startY < rc.top + 2) startY = rc.top + 2;
            startX = rc.left + (w - trayW) / 2; if (startX < rc.left + 2) startX = rc.left + 2;
        }
        inTray = (ptX >= startX && ptX < startX + trayW && ptY >= startY && ptY < startY + trayH);
        SweepDesktopIcons(g_hInst, NULL);
        if (inTray && g_TrayIconCount > 0) {
            int idx = -1;
            if (isHorz) {
                int c = (ptX - startX) / 21, r = (ptY - startY) / 21;
                if (c >= 0 && c < cols && r >= 0 && r < rows) idx = c * rows + r;
            } else {
                int c = (ptX - startX) / 21, r = (ptY - startY) / 21;
                if (c >= 0 && c < cols && r >= 0 && r < rows) idx = r * cols + c;
            }
            if (idx >= 0 && idx < g_TrayIconCount) {
                HWND tHwnd = g_TrayIcons[idx].hwnd;
                if (IsWindow(tHwnd)) { 
                    if (IsIconic(tHwnd)) ShowWindow(tHwnd, SW_RESTORE); 
                    SetForegroundWindow(tHwnd); 
                }
            }
        }
        return 0;
    }

    if (msg == WM_RBUTTONUP || msg == WM_RBUTTONDOWN) {
        int ptX = (short)LOWORD(lParam), ptY = (short)HIWORD(lParam), isHorz, w, h, rows, cols, trayW, trayH, startX, startY; BOOL inTray; RECT rc; 
        GetClientRect(hwnd, &rc); isHorz = (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP);
        w = rc.right - rc.left; h = rc.bottom - rc.top; rows = 1; cols = 1;
        if (isHorz) { rows = h / 21; if (rows < 1) rows = 1; cols = (g_TrayIconCount + rows - 1) / rows; if (cols < 1) cols = 1; } 
        else { cols = w / 21; if (cols < 1) cols = 1; rows = (g_TrayIconCount + cols - 1) / cols; if (rows < 1) rows = 1; }
        trayW = cols * 21; trayH = rows * 21;
        if (isHorz) {
            int totalW = (g_TrayIconCount > 0 ? trayW : 0) + CLOCK_WIDTH;
            startX = rc.left + (w - totalW) / 2; if (startX < rc.left + 2) startX = rc.left + 2;
            startY = rc.top + (h - trayH) / 2; if (startY < rc.top + 2) startY = rc.top + 2;
        } else {
            int totalH = (g_TrayIconCount > 0 ? trayH : 0) + CLOCK_HEIGHT;
            startY = rc.top + (h - totalH) / 2; if (startY < rc.top + 2) startY = rc.top + 2;
            startX = rc.left + (w - trayW) / 2; if (startX < rc.left + 2) startX = rc.left + 2;
        }
        inTray = (ptX >= startX && ptX < startX + trayW && ptY >= startY && ptY < startY + trayH);
        
        if (inTray && g_TrayIconCount > 0) {
            if (msg == WM_RBUTTONUP) {
                int idx = -1;
                if (isHorz) {
                    int c = (ptX - startX) / 21, r = (ptY - startY) / 21;
                    if (c >= 0 && c < cols && r >= 0 && r < rows) idx = c * rows + r;
                } else {
                    int c = (ptX - startX) / 21, r = (ptY - startY) / 21;
                    if (c >= 0 && c < cols && r >= 0 && r < rows) idx = r * cols + c;
                }
                if (idx >= 0 && idx < g_TrayIconCount) {
                    HWND tHwnd = g_TrayIcons[idx].hwnd;
                    if (IsWindow(tHwnd)) {
                        HMENU hSysMenu = GetSystemMenu(tHwnd, FALSE);
                        if (hSysMenu) { 
                            POINT pt; pt.x = ptX; pt.y = ptY; ClientToScreen(hwnd, &pt); 
                            SetForegroundWindow(tHwnd);
                            TrackPopupMenu(hSysMenu, 0, pt.x, pt.y, 0, tHwnd, NULL); 
                            PostMessage(tHwnd, WM_NULL, 0, 0);
                        }
                    }
                }
            }
        } else if (msg == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu(); POINT pt; pt.x = ptX; pt.y = ptY; ClientToScreen(hwnd, &pt);
            AppendMenu(hMenu, MF_STRING, IDM_CLOCK_ADJUST, "Adjust Date/Time");
            
            SetForegroundWindow(g_hTaskbar);
            g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL); 
            PostMessage(g_hTaskbar, WM_NULL, 0, 0); g_MenuOpen = FALSE;
            DestroyMenu(hMenu); 
        }
        return 0;
    }
    return CallWindowProc(OldClockProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK TaskBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) return HTCLIENT; 
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_SETTEXT) {
        LRESULT res = CallWindowProc(OldTaskBtnProc, hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, TRUE); UpdateWindow(hwnd);
        return res;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; HBRUSH hGray; HPEN hOldPen, hShadow, hHighlight, hTL, hBR; 
        BOOL isPushed; char title[128];
        GetClientRect(hwnd, &rc); isPushed = (BOOL)(INT_PTR)GetProp(hwnd, "P");
        hGray = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &rc, hGray); DeleteObject(hGray);
        hShadow = CreatePen(PS_SOLID, 1, RGB(128, 128, 128)); hHighlight = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        if (isPushed) { hTL = hShadow; hBR = hHighlight; } else { hTL = hHighlight; hBR = hShadow; }
        hOldPen = SelectObject(hdc, hTL);
        MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.right, rc.top);
        MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.left, rc.bottom);
        SelectObject(hdc, hBR);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.right, rc.bottom - 1);
        MoveToEx(hdc, rc.right - 1, rc.top, NULL); LineTo(hdc, rc.right - 1, rc.bottom);
        SelectObject(hdc, hOldPen); DeleteObject(hShadow); DeleteObject(hHighlight);
        GetWindowText(hwnd, title, sizeof(title)); SetBkMode(hdc, TRANSPARENT); SetBkColor(hdc, GetSysColor(COLOR_BTNFACE)); SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        {
            HFONT hOldFont = SelectObject(hdc, (HFONT)GetStockObject(ANSI_VAR_FONT)); 
            if (isPushed) { rc.left += 1; rc.top += 1; }
            DrawText(hdc, title, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            SelectObject(hdc, hOldFont);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        SweepDesktopIcons(g_hInst, NULL); 
        SetProp(hwnd, "P", (HANDLE)(INT_PTR)1); 
        g_DragHwnd = hwnd;
        GetCursorPos(&g_DragTaskStartPt);
        g_DragStartIndex = GetWindowLongPtr(hwnd, GWLP_ID) - ID_TASK_BASE;
        g_DidDrag = FALSE;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE); UpdateWindow(hwnd); return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        if (GetCapture() == hwnd) {
            POINT pt; GetCursorPos(&pt);
            RECT rc;
            if (g_DragHwnd == hwnd) {
                int dx = pt.x - g_DragTaskStartPt.x;
                GetWindowRect(hwnd, &rc);
                int btnW = rc.right - rc.left;
                if (abs(dx) > btnW / 2) {
                    g_DidDrag = TRUE;
                    int dir = (dx > 0) ? 1 : -1;
                    int newIdx = g_DragStartIndex + dir;
                    if (newIdx >= 0 && newIdx < g_TaskCount) {
                        TaskEntry tmp = g_Tasks[g_DragStartIndex];
                        g_Tasks[g_DragStartIndex] = g_Tasks[newIdx];
                        g_Tasks[newIdx] = tmp;
                        SetWindowLongPtr(g_Tasks[g_DragStartIndex].hBtn, GWLP_ID, ID_TASK_BASE + g_DragStartIndex);
                        SetWindowLongPtr(g_Tasks[newIdx].hBtn, GWLP_ID, ID_TASK_BASE + newIdx);
                        g_DragStartIndex = newIdx;
                        g_DragTaskStartPt.x += dir * btnW;
                        PostMessage(g_hTaskbar, WM_USER_APPLYLAYOUT, 0, 0);
                    }
                }
            } else {
                BOOL wasPushed = (BOOL)(INT_PTR)GetProp(hwnd, "P"), isPushed;
                GetClientRect(hwnd, &rc);
                isPushed = PtInRect(&rc, pt);
                if (isPushed != wasPushed) { 
                    SetProp(hwnd, "P", (HANDLE)(INT_PTR)isPushed); 
                    InvalidateRect(hwnd, NULL, FALSE); 
                    UpdateWindow(hwnd); 
                }
            }
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        if (GetCapture() == hwnd) {
            BOOL wasPushed = (BOOL)(INT_PTR)GetProp(hwnd, "P"); 
            ReleaseCapture(); SetProp(hwnd, "P", (HANDLE)(INT_PTR)0);
            g_DragHwnd = NULL;
            InvalidateRect(hwnd, NULL, FALSE); UpdateWindow(hwnd);
            if (wasPushed && !g_DidDrag) PostMessage(g_hTaskbar, WM_COMMAND, MAKEWPARAM(GetWindowLongPtr(hwnd, GWLP_ID), 0), (LPARAM)hwnd);
        }
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        int ti = GetWindowLongPtr(hwnd, GWLP_ID) - ID_TASK_BASE; SweepDesktopIcons(g_hInst, NULL);
        if (ti >= 0 && ti < g_TaskCount) {
            HMENU hMenu = CreatePopupMenu(); POINT pt; g_ContextTargetWnd = g_Tasks[ti].hWnd;
            AppendMenu(hMenu, MF_STRING, IDM_TASK_MINIMIZE, "Mi&nimize");
            AppendMenu(hMenu, MF_STRING, IDM_TASK_MAXIMIZE, "Ma&ximize");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, IDM_TASK_TWOPANE, "Two Pane");
            AppendMenu(hMenu, MF_STRING, IDM_TASK_MINTOTRAY, "Minimize to Tray");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, IDM_TASK_CLOSE, "&Close");
            pt.x = (short)LOWORD(lParam); pt.y = (short)HIWORD(lParam); ClientToScreen(hwnd, &pt);
            
            SetForegroundWindow(g_hTaskbar);
            g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL); 
            PostMessage(g_hTaskbar, WM_NULL, 0, 0); g_MenuOpen = FALSE;
            DestroyMenu(hMenu);
        }
        return 0;
    }
    if (msg == WM_DESTROY) RemoveProp(hwnd, "P");
    return CallWindowProc(OldTaskBtnProc, hwnd, msg, wParam, lParam);
}

/* --------------------------------------------------------------------------
   Start Button Procedure (Fixed window re-enabling after Drag and Drop)
   -------------------------------------------------------------------------- */
LRESULT CALLBACK StartBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DROPFILES) {
        HDROP hDrop = (HDROP)wParam;
        char path[MAX_PATH];
        if (DragQueryFile(hDrop, 0, path, MAX_PATH)) {
            SweepDesktopIcons(g_hInst, NULL);
            lstrcpy(g_DropTarget, path);
            
            /* Extract base name and strip extension */
            char base[MAX_PATH], *p, *dot;
            lstrcpy(base, path);
            p = strrchr(base, '\\');
            if (p) lstrcpy(base, p + 1);
            dot = strrchr(base, '.');
            if (dot) *dot = '\0';
            if (base[0]) {
                AnsiLower((LPSTR)base);
                if (base[0] >= 'a' && base[0] <= 'z') base[0] -= 32; 
            }
            lstrcpy(g_DropName, base);

            /* Default to the Programs folder */
            lstrcpy(g_ContextId, "0");
            int i;
            for (i = 0; i < g_IniShortcutCount; i++) {
                if (g_IniShortcuts[i].isFolder && lstrcmpi(g_IniShortcuts[i].name, "Programs") == 0) {
                    lstrcpy(g_ContextId, g_IniShortcuts[i].id);
                    break;
                }
            }
            
            g_ContextIsFolder = TRUE;
            g_EditShortcutId[0] = '\0';
            SetForegroundWindow(g_hTaskbar);
            
            /* Fix: Explicitly pass g_hTaskbar so the dialog re-enables the correct UI layer */
            CreateCenteredDialog(g_hInst, g_hTaskbar, "ShortcutDlgClass", "Create Shortcut", 360, 300);
        }
        DragFinish(hDrop);
        return 0;
    }
    
    if (msg == WM_ERASEBKGND) return 1; 
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; HBRUSH hBlue, hRed, hGreen, hYellow, hOldBrush, hGray; HPEN hBlack, hOldPen, hShadow, hHighlight, hTL, hBR; BOOL isPushed;
        GetClientRect(hwnd, &rc); isPushed = (SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
        hGray = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &rc, hGray); DeleteObject(hGray);
        hShadow = CreatePen(PS_SOLID, 1, RGB(128, 128, 128)); hHighlight = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        if (isPushed) { hTL = hShadow; hBR = hHighlight; } else { hTL = hHighlight; hBR = hShadow; }
        hOldPen = SelectObject(hdc, hTL);
        MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.right, rc.top);
        MoveToEx(hdc, rc.left, rc.top, NULL); LineTo(hdc, rc.left, rc.bottom);
        SelectObject(hdc, hBR);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL); LineTo(hdc, rc.right, rc.bottom - 1);
        MoveToEx(hdc, rc.right - 1, rc.top, NULL); LineTo(hdc, rc.right - 1, rc.bottom);
        SelectObject(hdc, hOldPen); DeleteObject(hShadow); DeleteObject(hHighlight);
        {
            int lx = 6 + (isPushed ? 1 : 0), ly = ((rc.bottom - rc.top - 16) / 2) + (isPushed ? 1 : 0), half = 8;
            hBlue = CreateSolidBrush(RGB(0, 0, 255)); hRed = CreateSolidBrush(RGB(255, 0, 0)); hGreen = CreateSolidBrush(RGB(0, 128, 0)); hYellow = CreateSolidBrush(RGB(255, 255, 0)); hBlack = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            hOldPen = SelectObject(hdc, hBlack); hOldBrush = SelectObject(hdc, hBlue); Rectangle(hdc, lx, ly, lx + half + 1, ly + half + 1);
            SelectObject(hdc, hRed); Rectangle(hdc, lx + half, ly, lx + (half*2) + 1, ly + half + 1);
            SelectObject(hdc, hGreen); Rectangle(hdc, lx, ly + half, lx + half + 1, ly + (half*2) + 1);
            SelectObject(hdc, hYellow); Rectangle(hdc, lx + half, ly + half, lx + (half*2) + 1, ly + (half*2) + 1);
            SelectObject(hdc, hOldBrush); SelectObject(hdc, hOldPen);
            DeleteObject(hBlue); DeleteObject(hRed); DeleteObject(hGreen); DeleteObject(hYellow); DeleteObject(hBlack);
            { HFONT hOldFont = SelectObject(hdc, g_hFontMenu); SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT)); TextOut(hdc, lx + 22, ly + 1, "Start", 5); SelectObject(hdc, hOldFont); }
        }
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK || msg == BM_SETSTATE) {
        LRESULT res; if (msg == WM_LBUTTONDOWN) SweepDesktopIcons(g_hInst, NULL);
        res = CallWindowProc(OldStartBtnProc, hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE); UpdateWindow(hwnd); return res;
    }
    
    if (msg == WM_RBUTTONUP) {
        HMENU hMenu = CreatePopupMenu(); POINT pt; SweepDesktopIcons(g_hInst, NULL);
        pt.x = (short)LOWORD(lParam); pt.y = (short)HIWORD(lParam); ClientToScreen(hwnd, &pt);
        
        int i;
        for (i = 0; i < g_WinXCount; i++) {
            if (g_WinXMenu[i].isSeparator) AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            else AppendMenu(hMenu, MF_STRING, IDM_WINX_BASE + i, g_WinXMenu[i].name);
        }
        
        SetForegroundWindow(g_hTaskbar);
        g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, g_hTaskbar, NULL); 
        PostMessage(g_hTaskbar, WM_NULL, 0, 0); g_MenuOpen = FALSE; 
        DestroyMenu(hMenu); return 0;
    }
    return CallWindowProc(OldStartBtnProc, hwnd, msg, wParam, lParam);
}
/* --------------------------------------------------------------------------
   Window Layout and Enum
   -------------------------------------------------------------------------- */
BOOL CALLBACK TaskbarEnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    
    char title[128], cls[64];
    GetWindowText(hwnd, title, sizeof(title)); 
    if (title[0] == '\0') return TRUE;
    
    GetClassName(hwnd, cls, sizeof(cls));
    if (lstrcmp(cls, "CalmiraTaskbarClass") == 0 || 
        lstrcmp(cls, "Progman") == 0 || 
        lstrcmp(cls, "RunDlgClass") == 0 || 
        lstrcmp(cls, "ShortcutDlgClass") == 0 || 
        lstrcmp(cls, "PromptDlgClass") == 0) return TRUE;

    if (GetProp(hwnd, "TrayMin")) { 
        if (s_EnumTrayCount < MAX_TRAY_ICONS) s_EnumTrayTasks[s_EnumTrayCount++] = hwnd; 
        return TRUE; 
    }

    HWND hwndOwner = GetWindow(hwnd, GW_OWNER);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    
    BOOL isAppWindow = (exStyle & WS_EX_APPWINDOW) != 0;
    BOOL isToolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
    
    if (isToolWindow) return TRUE;
    if (hwndOwner != NULL && !isAppWindow) return TRUE;

    if (s_EnumCount < MAX_TASKS) s_EnumTasks[s_EnumCount++] = hwnd;
    return TRUE;
}

static void RefreshTasks(void) {
    int i, j; RECT rc; int isHorz = (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP);
    int prevTrayCount = g_TrayIconCount;
    
    s_EnumCount = 0; s_EnumTrayCount = 0;
    EnumWindows(TaskbarEnumWindowsProc, 0);
    
    for (i = 0; i < g_TaskCount; ) {
        BOOL found = FALSE;
        for (j = 0; j < s_EnumCount; j++) { if (g_Tasks[i].hWnd == s_EnumTasks[j]) { found = TRUE; break; } }
        if (!found) {
            if (g_Tasks[i].hBtn) DestroyWindow(g_Tasks[i].hBtn);
            for (j = i; j < g_TaskCount - 1; j++) g_Tasks[j] = g_Tasks[j + 1];
            g_TaskCount--;
        } else { GetWindowText(g_Tasks[i].hWnd, g_Tasks[i].title, 128); i++; }
    }
    
    for (i = 0; i < s_EnumCount; i++) {
        BOOL found = FALSE;
        for (j = 0; j < g_TaskCount; j++) { if (g_Tasks[j].hWnd == s_EnumTasks[i]) { found = TRUE; break; } }
        if (!found && g_TaskCount < MAX_TASKS) {
            g_Tasks[g_TaskCount].hWnd = s_EnumTasks[i];
            GetWindowText(s_EnumTasks[i], g_Tasks[g_TaskCount].title, 128);
            g_Tasks[g_TaskCount].hBtn = NULL;
            g_TaskCount++;
        }
    }
    
    for (i = 0; i < g_TrayIconCount; i++) g_TrayIcons[i].active = FALSE;
    for (i = 0; i < s_EnumTrayCount; i++) {
        HWND thwnd = s_EnumTrayTasks[i]; BOOL found = FALSE;
        for (j = 0; j < g_TrayIconCount; j++) {
            if (g_TrayIcons[j].hwnd == thwnd) { g_TrayIcons[j].active = TRUE; found = TRUE; break; }
        }
        if (!found) {
            int slot = -1;
            for (j = 0; j < g_TrayIconCount; j++) { if (!g_TrayIcons[j].active) { slot = j; break; } }
            if (slot == -1 && g_TrayIconCount < MAX_TRAY_ICONS) { slot = g_TrayIconCount++; }
            if (slot != -1) {
                DWORD_PTR res = 0; HICON hIcon = NULL;
                
                if (SendMessageTimeout(thwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 50, &res) && res) hIcon = (HICON)res;
                if (!hIcon && SendMessageTimeout(thwnd, WM_GETICON, 0, 0, SMTO_ABORTIFHUNG, 50, &res) && res) hIcon = (HICON)res;
                if (!hIcon && SendMessageTimeout(thwnd, WM_GETICON, 1, 0, SMTO_ABORTIFHUNG, 50, &res) && res) hIcon = (HICON)res;
                if (!hIcon) hIcon = (HICON)GetClassLongPtr(thwnd, GCLP_HICONSM);
                if (!hIcon) hIcon = (HICON)GetClassLongPtr(thwnd, GCLP_HICON);
                if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);
                
                g_TrayIcons[slot].active = TRUE; g_TrayIcons[slot].hwnd = thwnd;
                g_TrayIcons[slot].hIcon = hIcon;
                
                if (!GetProp(thwnd, "TrayMin")) { SetProp(thwnd, "TrayMin", (HANDLE)(INT_PTR)1); ShowWindow(thwnd, SW_MINIMIZE); }
            }
        }
    }
    for (i = 0; i < g_TrayIconCount; ) {
        if (!g_TrayIcons[i].active) {
            for (j = i; j < g_TrayIconCount - 1; j++) g_TrayIcons[j] = g_TrayIcons[j + 1];
            g_TrayIconCount--;
        } else i++;
    }
    if (g_TrayIconCount != prevTrayCount) PostMessage(g_hTaskbar, WM_USER_APPLYLAYOUT, 0, 0);
    
    GetClientRect(g_hTaskList, &rc);
    if (g_TaskCount > 0) {
        int containerW = rc.right - rc.left, containerH = rc.bottom - rc.top;
        if (isHorz && g_TbHeight > 60 && containerH >= 48) {
            int rowHeight = 24, numRows = containerH / rowHeight; if (numRows < 1) numRows = 1;
            int cols = (g_TaskCount + numRows - 1) / numRows; if (cols < 1) cols = 1;
            int btnWidth = containerW / cols; if (btnWidth > 160) btnWidth = 160; if (btnWidth < 40) btnWidth = 40;
            for (i = 0; i < g_TaskCount; i++) {
                int r = i / cols, c = i % cols; char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 28);
                if (!g_Tasks[i].hBtn) {
                    g_Tasks[i].hBtn = CreateWindow("STATIC", shortTitle, WS_CHILD, c * btnWidth, r * rowHeight, btnWidth - 2, rowHeight - 2, g_hTaskList, (HMENU)(INT_PTR)(ID_TASK_BASE + i), g_hInst, NULL);
                    SetFont(g_Tasks[i].hBtn, NULL);
                    if (!OldTaskBtnProc) OldTaskBtnProc = (WNDPROC)GetWindowLongPtr(g_Tasks[i].hBtn, GWLP_WNDPROC);
                    SetWindowLongPtr(g_Tasks[i].hBtn, GWLP_WNDPROC, (LONG_PTR)TaskBtnProc); ShowWindow(g_Tasks[i].hBtn, SW_SHOW);
                } else { SetWindowText(g_Tasks[i].hBtn, shortTitle); MoveWindow(g_Tasks[i].hBtn, c * btnWidth, r * rowHeight, btnWidth - 2, rowHeight - 2, TRUE); SetWindowLongPtr(g_Tasks[i].hBtn, GWLP_ID, ID_TASK_BASE + i); }
            }
        } else if (isHorz) {
            int btnWidth = containerW / g_TaskCount; if (btnWidth > 160) btnWidth = 160;
            for (i = 0; i < g_TaskCount; i++) {
                char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 28);
                if (!g_Tasks[i].hBtn) {
                    g_Tasks[i].hBtn = CreateWindow("STATIC", shortTitle, WS_CHILD, i * btnWidth, 0, btnWidth - 2, containerH - 2, g_hTaskList, (HMENU)(INT_PTR)(ID_TASK_BASE + i), g_hInst, NULL);
                    SetFont(g_Tasks[i].hBtn, NULL);
                    if (!OldTaskBtnProc) OldTaskBtnProc = (WNDPROC)GetWindowLongPtr(g_Tasks[i].hBtn, GWLP_WNDPROC);
                    SetWindowLongPtr(g_Tasks[i].hBtn, GWLP_WNDPROC, (LONG_PTR)TaskBtnProc); ShowWindow(g_Tasks[i].hBtn, SW_SHOW);
                } else { SetWindowText(g_Tasks[i].hBtn, shortTitle); MoveWindow(g_Tasks[i].hBtn, i * btnWidth, 0, btnWidth - 2, containerH - 2, TRUE); SetWindowLongPtr(g_Tasks[i].hBtn, GWLP_ID, ID_TASK_BASE + i); }
            }
        } else {
            for (i = 0; i < g_TaskCount; i++) {
                char shortTitle[32]; TrimCaption(g_Tasks[i].title, shortTitle, 10);
                if (!g_Tasks[i].hBtn) {
                    g_Tasks[i].hBtn = CreateWindow("STATIC", shortTitle, WS_CHILD, 0, i * 26, containerW, 24, g_hTaskList, (HMENU)(INT_PTR)(ID_TASK_BASE + i), g_hInst, NULL);
                    SetFont(g_Tasks[i].hBtn, NULL);
                    if (!OldTaskBtnProc) OldTaskBtnProc = (WNDPROC)GetWindowLongPtr(g_Tasks[i].hBtn, GWLP_WNDPROC);
                    SetWindowLongPtr(g_Tasks[i].hBtn, GWLP_WNDPROC, (LONG_PTR)TaskBtnProc); ShowWindow(g_Tasks[i].hBtn, SW_SHOW);
                } else { SetWindowText(g_Tasks[i].hBtn, shortTitle); MoveWindow(g_Tasks[i].hBtn, 0, i * 26, containerW, 24, TRUE); SetWindowLongPtr(g_Tasks[i].hBtn, GWLP_ID, ID_TASK_BASE + i); }
            }
        }
    }
}

static void SnapToEdge(POINT pt) {
    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN);
    int edge = POS_BOTTOM, dBot = cy - pt.y, dTop = pt.y, dLeft = pt.x, dRight = cx - pt.x;
    int minD = dBot;
    if (dTop < minD) { minD = dTop; edge = POS_TOP; }
    if (dLeft < minD) { minD = dLeft; edge = POS_LEFT; }
    if (dRight < minD) { minD = dRight; edge = POS_RIGHT; }
    if (edge != g_TbPosition) { g_TbPosition = edge; ApplyLayout(); }
}

BOOL CALLBACK MinimizeEnumProc(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && !IsIconic(hwnd) && IsWindowEnabled(hwnd)) {
        char cls[64], title[128]; 
        GetClassName(hwnd, cls, sizeof(cls));
        GetWindowText(hwnd, title, sizeof(title));
        
        /* Omit windows named "Desktop" from being minimized */
        if (lstrcmp(title, "Desktop") == 0) return TRUE;
        
        if (lstrcmp(cls, "CalmiraTaskbarClass") != 0 && lstrcmp(cls, "Progman") != 0 && lstrcmp(cls, "RunDlgClass") != 0 && lstrcmp(cls, "ShortcutDlgClass") != 0 && lstrcmp(cls, "PromptDlgClass") != 0) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
    }
    return TRUE;
}
static void DoShowDesktop(void) { EnumWindows(MinimizeEnumProc, 0); }

static void ApplyLayout(void) {
    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN), i, qlCount = g_QLActiveCount, qlW = qlCount * 40;
    {
        RECT rcWork; rcWork.left = 0; rcWork.top = 0; rcWork.right = cx; rcWork.bottom = cy;
        switch (g_TbPosition) {
            case POS_BOTTOM: rcWork.bottom -= g_TbHeight; break;
            case POS_TOP:    rcWork.top += g_TbHeight; break;
            case POS_LEFT:   rcWork.left += g_TbWidthVert; break;
            case POS_RIGHT:  rcWork.right -= g_TbWidthVert; break;
        }
        SystemParametersInfo(SPI_SETWORKAREA, 0, &rcWork, SPIF_SENDCHANGE);
    }
    switch (g_TbPosition) {
        case POS_BOTTOM: MoveWindow(g_hTaskbar, 0, cy - g_TbHeight, cx, g_TbHeight, TRUE); break;
        case POS_TOP:    MoveWindow(g_hTaskbar, 0, 0, cx, g_TbHeight, TRUE); break;
        case POS_LEFT:   MoveWindow(g_hTaskbar, 0, 0, g_TbWidthVert, cy, TRUE); break;
        case POS_RIGHT:  MoveWindow(g_hTaskbar, cx - g_TbWidthVert, 0, g_TbWidthVert, cy, TRUE); break;
    }
    if (g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP) {
        int tlX = 6 + START_BTN_WIDTH + 6, yOff = (g_TbHeight - 22) / 2;
        int rows = (g_TbHeight - 6) / 21, cols, trayW, clockW;
        if (rows < 1) rows = 1; cols = (g_TrayIconCount + rows - 1) / rows; if (cols < 1) cols = 1;
        
        trayW = (g_TrayIconCount > 0) ? (cols * 21) : 21; 
        clockW = CLOCK_WIDTH + trayW + 4;
        
        MoveWindow(g_hStartBtn, 4, yOff, START_BTN_WIDTH, 22, TRUE);
        if (qlCount > 0) {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) {
                if (i < qlCount) { MoveWindow(g_hQuickLaunch[i], tlX + i * 40, yOff, 38, 22, TRUE); ShowWindow(g_hQuickLaunch[i], SW_SHOW); } 
                else ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            }
            if (g_hSearchBox) ShowWindow(g_hSearchBox, SW_HIDE);
            tlX += qlW + 6;
        } else {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            if (g_hSearchBox) { MoveWindow(g_hSearchBox, tlX, yOff, 120, 22, TRUE); ShowWindow(g_hSearchBox, SW_SHOW); tlX += 120 + 6; }
        }
        int tlW = cx - clockW - tlX - 8; if (tlW < 50) tlW = 50;
        MoveWindow(g_hTaskList, tlX, 3, tlW, g_TbHeight - 6, TRUE);
        MoveWindow(g_hClock, cx - clockW - 4, 3, clockW, g_TbHeight - 6, TRUE);
    } else {
        int y = 4, cols = (g_TbWidthVert - 8) / 21, rows, trayH, clockH, tlH, xOff;
        if (cols < 1) cols = 1; rows = (g_TrayIconCount + cols - 1) / cols; if (rows < 1) rows = 1;
        
        trayH = (g_TrayIconCount > 0) ? (rows * 21) : 21;
        clockH = CLOCK_HEIGHT + trayH;
        tlH = cy - clockH - y - 10; if (tlH < 50) tlH = 50;
        xOff = (g_TbWidthVert - START_BTN_WIDTH) / 2; if (xOff < 4) xOff = 4;
        
        MoveWindow(g_hStartBtn, xOff, y, START_BTN_WIDTH, 22, TRUE); y += 26;
        if (qlCount > 0) {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) {
                if (i < qlCount) { MoveWindow(g_hQuickLaunch[i], (g_TbWidthVert - 38)/2, y, 38, 22, TRUE); ShowWindow(g_hQuickLaunch[i], SW_SHOW); y += 24; } 
                else ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            }
            if (g_hSearchBox) ShowWindow(g_hSearchBox, SW_HIDE);
        } else {
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) ShowWindow(g_hQuickLaunch[i], SW_HIDE);
            if (g_hSearchBox) { MoveWindow(g_hSearchBox, 4, y, g_TbWidthVert - 8, 22, TRUE); ShowWindow(g_hSearchBox, SW_SHOW); y += 26; }
        }
        y += 4;
        MoveWindow(g_hTaskList, 4, y, g_TbWidthVert - 8, tlH, TRUE);
        MoveWindow(g_hClock, 4, cy - clockH - 4, g_TbWidthVert - 8, clockH, TRUE);
    }
    RefreshTasks();
}

LRESULT CALLBACK TaskListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT; 
    if (msg == WM_COMMAND) { SendMessage(GetParent(hwnd), msg, wp, lp); return 0; }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &rc, hBr); DeleteObject(hBr);
        EndPaint(hwnd, &ps); return 0;
    }
    return CallWindowProc(OldTaskListProc, hwnd, msg, wp, lp);
}

/* --------------------------------------------------------------------------
   Main Taskbar Procedure (Added DragAcceptFiles and WM_DROPFILES router)
   -------------------------------------------------------------------------- */
LRESULT CALLBACK TaskbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            int i; g_hTaskbar = hwnd; InitFonts();
            g_hMemODItems = GlobalAlloc(GPTR, MAX_OD_ITEMS * sizeof(ODMenuItem)); g_ODItems = (ODMenuItem*)GlobalLock(g_hMemODItems);
            g_hMemIniShortcuts = GlobalAlloc(GPTR, MAX_INI_SHORTCUTS * sizeof(IniShortcut)); g_IniShortcuts = (IniShortcut*)GlobalLock(g_hMemIniShortcuts);
            srand((unsigned)time(NULL)); LoadConfig(); DetectWindowsVersion();
            
            g_hStartBtn = CreateWindow("BUTTON", "Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_START_BUTTON, g_hInst, NULL);
            SetFont(g_hStartBtn, g_hFontMenu); 
            OldStartBtnProc = (WNDPROC)SetWindowLongPtr(g_hStartBtn, GWLP_WNDPROC, (LONG_PTR)StartBtnProc);
            DragAcceptFiles(g_hStartBtn, TRUE); /* Allows the Start Button to physically receive drops */
            
            for (i = 0; i < QUICK_LAUNCH_COUNT; i++) g_hQuickLaunch[i] = CreateWindow("BUTTON", "", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)(ID_QUICK_BASE + i), g_hInst, NULL);
            if (g_QLActiveCount == 0) {
                g_hSearchBox = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)IDM_SEARCH_EDIT, g_hInst, NULL);
                SetFont(g_hSearchBox, NULL); 
                OldSearchBoxProc = (WNDPROC)SetWindowLongPtr(g_hSearchBox, GWLP_WNDPROC, (LONG_PTR)SearchBoxProc);
                
                g_hSearchList = CreateWindowEx(WS_EX_TOPMOST, "LISTBOX", "", WS_POPUP | WS_BORDER | LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL, 0, 0, 150, 150, hwnd, NULL, g_hInst, NULL);
                SetFont(g_hSearchList, NULL); 
                OldSearchListProc = (WNDPROC)SetWindowLongPtr(g_hSearchList, GWLP_WNDPROC, (LONG_PTR)SearchListProc);
            }
            g_hTaskList = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)0, g_hInst, NULL);
            OldTaskListProc = (WNDPROC)SetWindowLongPtr(g_hTaskList, GWLP_WNDPROC, (LONG_PTR)TaskListProc);
            
            g_hClock = CreateWindowEx(0, "STATIC", "00:00", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, hwnd, (HMENU)ID_CLOCK, g_hInst, NULL);
            SetFont(g_hClock, NULL); 
            OldClockProc = (WNDPROC)SetWindowLongPtr(g_hClock, GWLP_WNDPROC, (LONG_PTR)ClockProc);
            
            SetTimer(hwnd, TIMER_CLOCK, 1000, NULL); SetTimer(hwnd, TIMER_REFRESH, 2000, NULL); SetTimer(hwnd, TIMER_HOTKEY, 100, NULL);
            ApplyLayout(); SweepDesktopIcons(g_hInst, NULL); RunStartupItems(); return 0;
        }

        /* If files are dropped onto the Taskbar background, route it to the Start Menu logic */
        case WM_DROPFILES:
            return StartBtnProc(g_hStartBtn, msg, wParam, lParam);

        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc);
            { HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); FillRect(hdc, &rc, hBr); DeleteObject(hBr); }
            {
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0,0,0)), hOldPen = SelectObject(hdc, hPen);
                HBRUSH hOldBr = SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, hOldBr); SelectObject(hdc, hOldPen); DeleteObject(hPen);
            }
            EndPaint(hwnd, &ps); return 0;
        }
        
        case WM_SETCURSOR: {
            POINT pt; RECT rc; GetCursorPos(&pt); ScreenToClient(hwnd, &pt); GetClientRect(hwnd, &rc);
            if (g_TbPosition == POS_BOTTOM && pt.y < 5) { SetCursor(LoadCursor(NULL, IDC_SIZENS)); return TRUE; }
            else if (g_TbPosition == POS_TOP && pt.y > rc.bottom - 5) { SetCursor(LoadCursor(NULL, IDC_SIZENS)); return TRUE; }
            else if (g_TbPosition == POS_LEFT && pt.x > rc.right - 5) { SetCursor(LoadCursor(NULL, IDC_SIZEWE)); return TRUE; }
            else if (g_TbPosition == POS_RIGHT && pt.x < 5) { SetCursor(LoadCursor(NULL, IDC_SIZEWE)); return TRUE; }
            break;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
            if (lpdis->CtlType == ODT_BUTTON && lpdis->CtlID >= ID_QUICK_BASE && lpdis->CtlID < ID_QUICK_BASE + QUICK_LAUNCH_COUNT) {
                int idx = lpdis->CtlID - ID_QUICK_BASE; COLORREF colors[4] = { RGB(255, 60, 60), RGB(60, 255, 60), RGB(60, 60, 255), RGB(255, 255, 60) };
                HBRUSH hBr = CreateSolidBrush(colors[idx % 4]); HBRUSH hOldBr = SelectObject(lpdis->hDC, hBr); HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0)); HPEN hOldPen = SelectObject(lpdis->hDC, hPen);
                FillRect(lpdis->hDC, &lpdis->rcItem, (HBRUSH)(COLOR_BTNFACE + 1));
                if (lpdis->itemState & ODS_SELECTED) Ellipse(lpdis->hDC, lpdis->rcItem.left + 5, lpdis->rcItem.top + 5, lpdis->rcItem.right - 3, lpdis->rcItem.bottom - 3);
                else Ellipse(lpdis->hDC, lpdis->rcItem.left + 4, lpdis->rcItem.top + 4, lpdis->rcItem.right - 4, lpdis->rcItem.bottom - 4);
                SelectObject(lpdis->hDC, hOldPen); DeleteObject(hPen); SelectObject(lpdis->hDC, hOldBr); DeleteObject(hBr); return TRUE;
            }
            if (lpdis->CtlType == ODT_MENU) {
                ODMenuItem* item = (ODMenuItem*)lpdis->itemData; HDC hdc = lpdis->hDC; RECT rc = lpdis->rcItem; HBRUSH hBrush; int textX = rc.left + 4;
                if (!item) return TRUE;
                if (lpdis->itemState & ODS_SELECTED && !item->isSeparator) { hBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT)); FillRect(hdc, &rc, hBrush); SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT)); } 
                else { hBrush = CreateSolidBrush(GetSysColor(COLOR_MENU)); FillRect(hdc, &rc, hBrush); SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT)); }
                DeleteObject(hBrush);
                if (item->isRoot) {
                    RECT rcB = rc; HFONT hOldF; rcB.right = rcB.left + 32;
                    hBrush = CreateSolidBrush(RGB(0, 0, 128)); FillRect(hdc, &rcB, hBrush); DeleteObject(hBrush); textX += 32;
                    hOldF = SelectObject(hdc, g_hFontSidebar); SetTextColor(hdc, RGB(255, 255, 255)); SetBkMode(hdc, TRANSPARENT);
                    TextOut(hdc, rc.left + 4, rc.top - item->yOffset + item->totalHeight - 10, g_szWinVer, lstrlen(g_szWinVer)); SelectObject(hdc, hOldF);
                }
                if (item->isSeparator) {
                    HPEN hShadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)), hHighlight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT)), hOldPen = SelectObject(hdc, hShadow); int y = rc.top + (rc.bottom - rc.top) / 2;
                    MoveToEx(hdc, textX, y - 1, NULL); LineTo(hdc, rc.right - 2, y - 1); SelectObject(hdc, hHighlight); MoveToEx(hdc, textX, y, NULL); LineTo(hdc, rc.right - 2, y);
                    SelectObject(hdc, hOldPen); DeleteObject(hShadow); DeleteObject(hHighlight);
                } else {
                    DrawGdiIcon(hdc, textX, rc.top + 2, item->polyIcon, 2); textX += 36; SetBkMode(hdc, TRANSPARENT);
                    if (lpdis->itemState & ODS_SELECTED) SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT)); else SetTextColor(hdc, GetSysColor(COLOR_MENUTEXT));
                    { HFONT hOldMenuFont = SelectObject(hdc, g_hFontMenu); TextOut(hdc, textX, rc.top + 10, item->text, lstrlen(item->text)); SelectObject(hdc, hOldMenuFont); }
                }
            }
            return TRUE;
        }

        case WM_MEASUREITEM: {
            LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lParam;
            if (lpmis->CtlType == ODT_MENU) {
                ODMenuItem* item = (ODMenuItem*)lpmis->itemData;
                if (item) { if (item->isSeparator) { lpmis->itemWidth = 100; lpmis->itemHeight = 8; } else { lpmis->itemWidth = 120 + (item->isRoot ? 32 : 0); lpmis->itemHeight = 36; } }
            } else if (lpmis->CtlType == ODT_BUTTON) { lpmis->itemWidth = 38; lpmis->itemHeight = 22; }
            return TRUE;
        }

        case WM_RBUTTONUP: {
            HMENU hMenu = CreatePopupMenu(); POINT pt;
            pt.x = (short)LOWORD(lParam); pt.y = (short)HIWORD(lParam); ClientToScreen(hwnd, &pt);
            
            int i;
            for (i = 0; i < g_TbMenuCount; i++) {
                if (g_TbMenu[i].isSeparator) AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                else AppendMenu(hMenu, MF_STRING, IDM_TBMENU_BASE + i, g_TbMenu[i].name);
            }
            
            SetForegroundWindow(hwnd);
            g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL); 
            PostMessage(hwnd, WM_NULL, 0, 0); g_MenuOpen = FALSE; 
            DestroyMenu(hMenu); return 0;
        }

        case WM_USER_CONTEXTMENU: {
            HMENU hMenu = CreatePopupMenu(); UINT state = (g_ContextId[0] == '\0') ? MF_GRAYED : 0;
            AppendMenu(hMenu, MF_STRING | state, IDM_CTX_PROPS, "Properties"); AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, IDM_CTX_NEWFOLDER, "New Folder..."); AppendMenu(hMenu, MF_STRING, IDM_CTX_NEWSHORTCUT, "New Shortcut..."); AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING | state, IDM_CTX_RENAME, "Rename..."); AppendMenu(hMenu, MF_STRING | state, IDM_CTX_DELETE, "Delete");
            SetForegroundWindow(hwnd);
            g_MenuOpen = TRUE; TrackPopupMenu(hMenu, 0, LOWORD(lParam), HIWORD(lParam), 0, hwnd, NULL); 
            PostMessage(hwnd, WM_NULL, 0, 0); g_MenuOpen = FALSE; DestroyMenu(hMenu); return 0;
        }
        
        case WM_USER_APPLYLAYOUT: { ApplyLayout(); return 0; }
        
        case WM_MENUSELECT: {
            UINT idItem = LOWORD(wParam), flags = HIWORD(wParam); HMENU hMenu = (HMENU)lParam; 
            if ((flags & 0xFFFF) == 0xFFFF && hMenu == NULL) {} 
            else if (!(flags & MF_SEPARATOR) && !(flags & MF_SYSMENU)) {
                if (flags & MF_POPUP) {
                    int i; for (i = 0; i < g_IniShortcutCount; i++) {
                        if (g_IniShortcuts[i].isFolder && (g_IniShortcuts[i].hMenu == GetSubMenu(hMenu, idItem) || g_IniShortcuts[i].hMenu == (HMENU)(UINT_PTR)idItem)) {
                            lstrcpy(g_ContextId, g_IniShortcuts[i].id); g_ContextIsFolder = TRUE; break;
                        }
                    }
                } else if (idItem >= IDM_START_BASE && idItem < IDM_START_BASE + MAX_START_ITEMS) {
                    int idx = idItem - IDM_START_BASE; if (idx < g_IniShortcutCount) { lstrcpy(g_ContextId, g_IniShortcuts[idx].id); g_ContextIsFolder = FALSE; }
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt; RECT rc; pt.x = (short)LOWORD(lParam); pt.y = (short)HIWORD(lParam); GetClientRect(hwnd, &rc); SweepDesktopIcons(g_hInst, NULL);
            if (IsWindowVisible(g_hSearchList)) ShowWindow(g_hSearchList, SW_HIDE);
            if ((g_TbPosition == POS_BOTTOM || g_TbPosition == POS_TOP) && pt.x > rc.right - 14) { DoShowDesktop(); return 0; }
            
            if (g_TbPosition == POS_BOTTOM && pt.y < 5) g_bResizing = TRUE;
            else if (g_TbPosition == POS_TOP && pt.y > rc.bottom - 5) g_bResizing = TRUE;
            else if (g_TbPosition == POS_LEFT && pt.x > rc.right - 5) g_bResizing = TRUE;
            else if (g_TbPosition == POS_RIGHT && pt.x < 5) g_bResizing = TRUE;
            else g_bDragging = TRUE;
            
            ClientToScreen(hwnd, &pt); g_DragStartPt = pt; SetCapture(hwnd); return 0;
        }

        case WM_MOUSEMOVE: {
            if (GetCapture() == hwnd) {
                POINT pt; pt.x = (short)LOWORD(lParam); pt.y = (short)HIWORD(lParam); ClientToScreen(hwnd, &pt);
                if (g_bResizing) {
                    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN);
                    if (g_TbPosition == POS_BOTTOM) { g_TbHeight = cy - pt.y; if (g_TbHeight < 24) g_TbHeight = 24; }
                    else if (g_TbPosition == POS_TOP) { g_TbHeight = pt.y; if (g_TbHeight < 24) g_TbHeight = 24; }
                    else if (g_TbPosition == POS_LEFT) { g_TbWidthVert = pt.x; if (g_TbWidthVert < 48) g_TbWidthVert = 48; }
                    else if (g_TbPosition == POS_RIGHT) { g_TbWidthVert = cx - pt.x; if (g_TbWidthVert < 48) g_TbWidthVert = 48; }
                    ApplyLayout();
                } else if (g_bDragging) SnapToEdge(pt);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_bDragging || g_bResizing) { g_bDragging = FALSE; g_bResizing = FALSE; ReleaseCapture(); g_bIniChanged = TRUE; }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam); SweepDesktopIcons(g_hInst, NULL);
            if (id == ID_START_BUTTON) ShowStartMenu(g_hStartBtn);
            else if (id >= ID_QUICK_BASE && id < ID_QUICK_BASE + QUICK_LAUNCH_COUNT) {
                IniShortcut sh; memset(&sh, 0, sizeof(sh)); lstrcpy(sh.exe, g_QL[id - ID_QUICK_BASE].exe); LaunchShortcut(hwnd, &sh);
            }
            else if (id >= IDM_FS_BASE && id < IDM_FS_BASE + MAX_OD_ITEMS) {
                int j; for (j = 0; j < g_ODCount; j++) {
                    if (g_ODItems[j].cmdId == id) {
                        if (g_ODItems[j].isFsFolder) ShellExecute(NULL, "open", "e1plorer.exe", g_ODItems[j].targetPath, NULL, SW_SHOWNORMAL);
                        else { IniShortcut tempSh; memset(&tempSh, 0, sizeof(tempSh)); lstrcpy(tempSh.exe, g_ODItems[j].targetPath); LaunchShortcut(hwnd, &tempSh); }
                        break;
                    }
                }
            }
            else if (id >= IDM_WINX_BASE && id < IDM_WINX_BASE + g_WinXCount) { 
                HandleContextItem(&g_WinXMenu[id - IDM_WINX_BASE], hwnd); 
            }
            else if (id >= IDM_TBMENU_BASE && id < IDM_TBMENU_BASE + g_TbMenuCount) { 
                HandleContextItem(&g_TbMenu[id - IDM_TBMENU_BASE], hwnd); 
            }
            else if (id == IDM_SEARCH_LIST && HIWORD(wParam) == LBN_DBLCLK) {
                int sel = SendMessage(g_hSearchList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) {
                    int idx = SendMessage(g_hSearchList, LB_GETITEMDATA, sel, 0);
                    if (idx >= 0 && idx < g_IniShortcutCount) {
                        IniShortcut* sh = &g_IniShortcuts[idx];
                        if (sh->isFolder) { ShowWindow(g_hSearchList, SW_HIDE); SetWindowText(g_hSearchBox, ""); ShowFolderMenu(g_hSearchBox, sh->id); return 0; } 
                        else PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDM_START_BASE + idx, 0), 0);
                    }
                }
                ShowWindow(g_hSearchList, SW_HIDE); SetWindowText(g_hSearchBox, ""); return 0;
            }
            else if (id >= IDM_START_BASE && id < IDM_START_BASE + MAX_START_ITEMS) {
                int idx = id - IDM_START_BASE; if (idx < g_IniShortcutCount) LaunchShortcut(hwnd, &g_IniShortcuts[idx]);
            }
            else if (id >= ID_TASK_BASE && id < ID_TASK_BASE + MAX_TASKS) {
                int ti = id - ID_TASK_BASE;
                if (ti >= 0 && ti < g_TaskCount) {
                    HWND win = g_Tasks[ti].hWnd; g_LastClickedTaskWnd = win;
                    if (IsIconic(win)) ShowWindow(win, SW_RESTORE); 
                    SetForegroundWindow(win);
                    
                    if (IsZoomed(win)) {
                        int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN); RECT rcWork; rcWork.left = 0; rcWork.top = 0; rcWork.right = cx; rcWork.bottom = cy;
                        switch (g_TbPosition) {
                            case POS_BOTTOM: rcWork.bottom -= g_TbHeight; break;
                            case POS_TOP:    rcWork.top += g_TbHeight; break;
                            case POS_LEFT:   rcWork.left += g_TbWidthVert; break;
                            case POS_RIGHT:  rcWork.right -= g_TbWidthVert; break;
                        }
                        ShowWindow(win, SW_RESTORE); SetWindowPos(win, NULL, rcWork.left, rcWork.top, rcWork.right - rcWork.left, rcWork.bottom - rcWork.top, SWP_NOZORDER);
                    }
                }
            }
            else if (id == IDM_TASK_MINIMIZE && g_ContextTargetWnd) SendMessage(g_ContextTargetWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0L);
            else if (id == IDM_TASK_MAXIMIZE && g_ContextTargetWnd) {
                int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN); RECT rcWork; rcWork.left = 0; rcWork.top = 0; rcWork.right = cx; rcWork.bottom = cy;
                switch (g_TbPosition) {
                    case POS_BOTTOM: rcWork.bottom -= g_TbHeight; break;
                    case POS_TOP:    rcWork.top += g_TbHeight; break;
                    case POS_LEFT:   rcWork.left += g_TbWidthVert; break;
                    case POS_RIGHT:  rcWork.right -= g_TbWidthVert; break;
                }
                ShowWindow(g_ContextTargetWnd, SW_RESTORE); SetWindowPos(g_ContextTargetWnd, NULL, rcWork.left, rcWork.top, rcWork.right - rcWork.left, rcWork.bottom - rcWork.top, SWP_NOZORDER);
            }
            else if (id == IDM_TASK_TWOPANE && g_ContextTargetWnd) {
                HWND w1 = g_ContextTargetWnd, w2 = g_LastClickedTaskWnd;
                if (!w2 || !IsWindow(w2) || w1 == w2) {
                    int ti; w2 = NULL;
                    for (ti = 0; ti < g_TaskCount; ti++) { if (g_Tasks[ti].hWnd != w1 && IsWindowVisible(g_Tasks[ti].hWnd)) { w2 = g_Tasks[ti].hWnd; break; } }
                }
                if (w1 && w2 && w1 != w2 && IsWindow(w1) && IsWindow(w2)) {
                    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN); RECT rcWork; rcWork.left = 0; rcWork.top = 0; rcWork.right = cx; rcWork.bottom = cy;
                    switch (g_TbPosition) {
                        case POS_BOTTOM: rcWork.bottom -= g_TbHeight; break;
                        case POS_TOP:    rcWork.top += g_TbHeight; break;
                        case POS_LEFT:   rcWork.left += g_TbWidthVert; break;
                        case POS_RIGHT:  rcWork.right -= g_TbWidthVert; break;
                    }
                    ShowWindow(w1, SW_RESTORE); ShowWindow(w2, SW_RESTORE);
                    SetWindowPos(w1, NULL, rcWork.left, rcWork.top, (rcWork.right - rcWork.left) / 2, rcWork.bottom - rcWork.top, SWP_NOZORDER);
                    SetWindowPos(w2, NULL, rcWork.left + (rcWork.right - rcWork.left) / 2, rcWork.top, (rcWork.right - rcWork.left) / 2, rcWork.bottom - rcWork.top, SWP_NOZORDER);
                }
            }
            else if (id == IDM_TASK_MINTOTRAY && g_ContextTargetWnd) {
                char modPath[MAX_PATH], *pSlash, *mName; int k; BOOL alreadyInList = FALSE;
                if (GetModuleFileName((HINSTANCE)GetWindowLongPtr(g_ContextTargetWnd, GWLP_HINSTANCE), modPath, MAX_PATH) > 0) {
                    pSlash = strrchr(modPath, '\\'); mName = pSlash ? pSlash + 1 : modPath; AnsiLower((LPSTR)mName);
                    for (k = 0; k < g_TrayAppCount; k++) { if (lstrcmpi(mName, g_TrayAppList[k]) == 0) { alreadyInList = TRUE; break; } }
                    if (!alreadyInList && g_TrayAppCount < MAX_TRAY_APPS) {
                        lstrcpy(g_TrayAppList[g_TrayAppCount++], mName);
                        g_bIniChanged = TRUE;
                    }
                }
                SetProp(g_ContextTargetWnd, "TrayMin", (HANDLE)(INT_PTR)1);
                ShowWindow(g_ContextTargetWnd, SW_MINIMIZE); 
                SweepDesktopIcons(g_hInst, NULL); 
                PostMessage(g_hTaskbar, WM_USER_APPLYLAYOUT, 0, 0);
            }
            else if (id == IDM_TASK_CLOSE && g_ContextTargetWnd) SendMessage(g_ContextTargetWnd, WM_SYSCOMMAND, SC_CLOSE, 0L);
            else if (id == IDM_CTX_PROPS) { if (g_ContextId[0] != '\0') { lstrcpy(g_EditShortcutId, g_ContextId); CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Edit Properties", 360, 300); } }
            else if (id == IDM_CTX_NEWFOLDER) { lstrcpy(g_ContextId, "0"); g_ContextIsFolder = TRUE; lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0'; g_PromptMode = PROMPT_NEWFOLDER; CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 170); }
            else if (id == IDM_CTX_NEWSHORTCUT) { lstrcpy(g_ContextId, "0"); g_ContextIsFolder = TRUE; g_EditShortcutId[0] = '\0'; CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 360, 300); }
            else if (id == IDM_CTX_RENAME) { int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i].id, g_ContextId) == 0) { lstrcpy(g_PromptValue, g_IniShortcuts[i].name); break; } } lstrcpy(g_PromptLabel, "New Name:"); g_PromptMode = PROMPT_RENAME; CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "Rename", 290, 130); }
            else if (id == IDM_CTX_DELETE) { 
                if (g_ContextId[0] != '\0' && MessageBox(hwnd, "Delete item?", "Confirm", MB_YESNO) == IDYES) {
                    int k, foundIdx = -1;
                    for (k = 0; k < g_IniShortcutCount; k++) {
                        if (lstrcmp(g_IniShortcuts[k].id, g_ContextId) == 0) { foundIdx = k; break; }
                    }
                    if (foundIdx != -1) {
                        for (k = foundIdx; k < g_IniShortcutCount - 1; k++) g_IniShortcuts[k] = g_IniShortcuts[k+1];
                        g_IniShortcutCount--;
                        g_bIniChanged = TRUE;
                    }
                } 
            }
            else if (id == IDM_CLOCK_ADJUST) { CreateCenteredDialog(g_hInst, hwnd, "DateTimeDlgClass", "Date and Time", 240, 280); }
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_CLOCK) UpdateClock();
            else if (wParam == TIMER_REFRESH && !g_MenuOpen) { SweepDesktopIcons(g_hInst, hwnd); RefreshTasks(); }
            else if (wParam == TIMER_HOTKEY) { 
                if (GetAsyncKeyState(VK_LWIN) & 1 || GetAsyncKeyState(VK_RWIN) & 1) PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_START_BUTTON, 0), 0); 
                {
                    int i;
                    for (i = 0; i < g_IniShortcutCount; i++) {
                        if (g_IniShortcuts[i].hotkey[0] != '\0') {
                            int vk = atoi(g_IniShortcuts[i].hotkey);
                            if (vk && (GetAsyncKeyState(vk) & 1)) LaunchShortcut(hwnd, &g_IniShortcuts[i]);
                        }
                    }
                }
            }
            return 0;

        case WM_DESTROY: {
            int i;
            for (i = 0; i < g_TrayIconCount; i++) {
                if (IsWindow(g_TrayIcons[i].hwnd)) {
                    RemoveProp(g_TrayIcons[i].hwnd, "TrayMin");
                    ShowWindow(g_TrayIcons[i].hwnd, SW_RESTORE);
                }
            }
            
            SaveAllToIni();
            
            if (g_hMemODItems) { GlobalUnlock(g_hMemODItems); GlobalFree(g_hMemODItems); }
            if (g_hMemIniShortcuts) { GlobalUnlock(g_hMemIniShortcuts); GlobalFree(g_hMemIniShortcuts); }
            KillTimer(hwnd, TIMER_CLOCK); KillTimer(hwnd, TIMER_REFRESH); KillTimer(hwnd, TIMER_HOTKEY);
            if (g_hFontMenu) DeleteObject(g_hFontMenu); if (g_hFontSidebar) DeleteObject(g_hFontSidebar);
            PostQuitMessage(0); return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit, hParentFolder;
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", g_PromptLabel, WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            hEdit = CreateWindowEx(0, "EDIT", g_PromptValue, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER, 10, 35, 260, 22, hwnd, NULL, g_hInst, NULL);
            
            if (g_PromptMode == PROMPT_NEWFOLDER) {
                int i; char parentId[16];
                CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 65, 100, 20, hwnd, NULL, g_hInst, NULL);
                hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP, 110, 65, 160, 150, hwnd, NULL, g_hInst, NULL);
                
                SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)"0 (Root)");
                for (i = 0; i < g_IniShortcutCount; i++) {
                    if (g_IniShortcuts[i].isFolder) {
                        char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i].id, g_IniShortcuts[i].name);
                        SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)buf);
                    }
                }
                
                lstrcpy(parentId, g_ContextId[0] ? g_ContextId : "0");
                for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) {
                    char buf[128]; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)buf);
                    if (strncmp(buf, parentId, lstrlen(parentId)) == 0 && buf[lstrlen(parentId)] == ' ') {
                        SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break;
                    }
                }
                if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0);
                
                CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 50, 100, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
                CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 150, 100, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            } else {
                hParentFolder = NULL;
                CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 50, 70, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
                CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 150, 70, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            }
            SetFont(hwnd, NULL); SetFocus(hEdit); PositionDialogNearStart(hwnd); return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                GetWindowText(hEdit, g_PromptValue, MAX_PATH);
                if (g_PromptValue[0] != '\0') {
                    if (g_PromptMode == PROMPT_NEWFOLDER) {
                        char parentSel[128], *space; 
                        
                        /* Fix: Zero out stack memory */
                        IniShortcut sh; memset(&sh, 0, sizeof(IniShortcut)); 
                        int i, maxId = 0;
                        
                        for (i = 0; i < g_IniShortcutCount; i++) { 
                            int idNum = atoi(g_IniShortcuts[i].id);
                            if (idNum > maxId) maxId = idNum;
                        }
                        
                        sprintf(sh.id, "%08d", maxId + 1); lstrcpy(sh.name, g_PromptValue);
                        sh.exe[0] = '\0'; sh.params[0] = '\0'; sh.icon[0] = '\0'; sh.minimized = 0; sh.hotkey[0] = '\0';
                        if (hParentFolder) {
                            GetWindowText(hParentFolder, parentSel, sizeof(parentSel));
                            space = strchr(parentSel, ' '); if (space) *space = '\0';
                            lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0");
                        } else {
                            lstrcpy(sh.parentId, g_ContextId[0] ? g_ContextId : "0");
                        }
                        sh.isFolder = TRUE;
                        
                        if (g_IniShortcutCount < MAX_INI_SHORTCUTS) g_IniShortcuts[g_IniShortcutCount++] = sh;
                        qsort(g_IniShortcuts, g_IniShortcutCount, sizeof(IniShortcut), CompareIni);
                        g_bIniChanged = TRUE;
                        
                    } else if (g_PromptMode == PROMPT_RENAME) {
                        int i; for (i = 0; i < g_IniShortcutCount; i++) { 
                            if (lstrcmp(g_IniShortcuts[i].id, g_ContextId) == 0) { 
                                lstrcpy(g_IniShortcuts[i].name, g_PromptValue); 
                                g_bIniChanged = TRUE; break; 
                            } 
                        }
                    }
                }
                EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd);
            } else if (LOWORD(wp) == IDCANCEL) { g_PromptValue[0] = '\0'; EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); }
            return 0;
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
/* --------------------------------------------------------------------------
   Corrected Run Dialog (Now strictly memory-backed)
   -------------------------------------------------------------------------- */
LRESULT CALLBACK RunDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hCombo;
    switch(msg) {
        case WM_CREATE: {
            char histCopy[512], *token;
            CreateWindow("STATIC", "Type the name of a program to open:", WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL);
            hCombo = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL|WS_BORDER, 10, 35, 260, 120, hwnd, NULL, g_hInst, NULL);
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 10, 70, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 100, 70, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 190, 70, 80, 24, hwnd, (HMENU)101, g_hInst, NULL);
            
            /* Populate from Memory, NOT Disk */
            lstrcpy(histCopy, g_RunHistory);
            token = strtok(histCopy, "|"); 
            while(token) { SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)token); token = strtok(NULL, "|"); }
            
            SendMessage(hCombo, CB_SETCURSEL, 0, 0); SetFont(hwnd, NULL); SetFocus(hCombo); PositionDialogNearStart(hwnd); return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                char cmd[128], fullCmd[128], newHist[512], oldHist[512]; 
                int i, count; char* args;
                memset(newHist, 0, sizeof(newHist)); memset(oldHist, 0, sizeof(oldHist));

                GetWindowText(hCombo, cmd, sizeof(cmd));
                if (cmd[0]) { 
                    lstrcpy(fullCmd, cmd);
                    args = strchr(fullCmd, ' ');
                    if (args) { *args = '\0'; args++; }
                    
                    lstrcpy(newHist, cmd); count = SendMessage(hCombo, CB_GETCOUNT, 0, 0);
                    for (i=0; i<count && i<9; i++) { 
                        SendMessage(hCombo, CB_GETLBTEXT, i, (LPARAM)oldHist); 
                        if (lstrcmpi(oldHist, cmd) != 0) { lstrcat(newHist, "|"); lstrcat(newHist, oldHist); } 
                    }
                    
                    /* Write back to Memory, NOT Disk, and tag as modified */
                    lstrcpy(g_RunHistory, newHist);
                    g_bIniChanged = TRUE;

                    EnableWindow(g_hTaskbar, TRUE); ShowWindow(hwnd, SW_HIDE); DestroyWindow(hwnd);
                    ShellExecute(NULL, "open", fullCmd, args ? args : NULL, NULL, SW_SHOWNORMAL);
                } else { EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); }
            } else if (LOWORD(wp) == IDCANCEL) { EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); } 
            else if (LOWORD(wp) == 101) { char path[MAX_PATH]; if (BrowseFile(hwnd, path, "Programs (*.exe;*.com)\0*.exe;*.com\0All Files (*.*)\0*.*\0")) SetWindowText(hCombo, path); }
            return 0;
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK DateTimeDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static int s_month, s_year, s_day;
    static HWND hPrev, hNext, hLbl;
    switch(msg) {
        case WM_CREATE: {
            SYSTEMTIME st; GetLocalTime(&st);
            s_month = st.wMonth; s_year = st.wYear; s_day = st.wDay;
            
            hPrev = CreateWindow("BUTTON", "<", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 10, 10, 30, 24, hwnd, (HMENU)101, g_hInst, NULL);
            hNext = CreateWindow("BUTTON", ">", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 200, 10, 30, 24, hwnd, (HMENU)102, g_hInst, NULL);
            hLbl = CreateWindow("STATIC", "", WS_CHILD|WS_VISIBLE|SS_CENTER, 45, 14, 150, 20, hwnd, NULL, g_hInst, NULL);
            SetFont(hPrev, NULL); SetFont(hNext, NULL); SetFont(hLbl, g_hFontMenu);
            
            CreateWindow("BUTTON", "Apply", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 35, 220, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 125, 220, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            
            PostMessage(hwnd, WM_USER+1, 0, 0); 
            PositionDialogNearStart(hwnd);
            return 0;
        }
        case WM_USER+1: {
            char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
            char buf[64];
            sprintf(buf, "%s %d", months[s_month-1], s_year);
            SetWindowText(hLbl, buf);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == 101) {
                if (--s_month < 1) { s_month = 12; s_year--; }
                PostMessage(hwnd, WM_USER+1, 0, 0);
            } else if (LOWORD(wp) == 102) {
                if (++s_month > 12) { s_month = 1; s_year++; }
                PostMessage(hwnd, WM_USER+1, 0, 0);
            } else if (LOWORD(wp) == IDOK) {
                SYSTEMTIME st; GetLocalTime(&st);
                st.wYear = s_year; st.wMonth = s_month; st.wDay = s_day;
                SetLocalTime(&st);
                EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd);
            } else if (LOWORD(wp) == IDCANCEL) {
                EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
            int y = s_year - (s_month < 3);
            int firstDay = (y + y/4 - y/100 + y/400 + t[s_month-1] + 1) % 7;
            int daysInMonth = 31;
            int r, c, d = 1, cx = 15, cy = 45, cellW = 30, cellH = 24;
            char* wd[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
            HFONT hOldFont = SelectObject(hdc, GetStockObject(ANSI_VAR_FONT));
            
            SetBkMode(hdc, TRANSPARENT);
            if (s_month==4||s_month==6||s_month==9||s_month==11) daysInMonth=30;
            else if (s_month==2) daysInMonth = (s_year%4==0 && (s_year%100!=0 || s_year%400==0)) ? 29 : 28;
            
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            for(c=0; c<7; c++) {
                TextOut(hdc, cx + c*cellW + 6, cy, wd[c], 2);
            }
            cy += 20;
            
            for(r=0; r<6; r++) {
                for(c=0; c<7; c++) {
                    if (r==0 && c<firstDay) continue;
                    if (d > daysInMonth) break;
                    
                    if (d == s_day) {
                        HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
                        RECT hRc; hRc.left = cx + c*cellW+2; hRc.top = cy + r*cellH; hRc.right = hRc.left + cellW-4; hRc.bottom = hRc.top + cellH;
                        FillRect(hdc, &hRc, hBr);
                        DeleteObject(hBr);
                        SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                    } else {
                        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                    }
                    
                    {
                        char dstr[4]; sprintf(dstr, "%d", d);
                        TextOut(hdc, cx + c*cellW + (d<10?10:6), cy + r*cellH + 4, dstr, lstrlen(dstr));
                    }
                    d++;
                }
            }
            SelectObject(hdc, hOldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int px = LOWORD(lp), py = HIWORD(lp);
            int cx = 15, cy = 65, cellW = 30, cellH = 24;
            if (px >= cx && px < cx + 7*cellW && py >= cy && py < cy + 6*cellH) {
                int c = (px - cx) / cellW;
                int r = (py - cy) / cellH;
                int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
                int y = s_year - (s_month < 3);
                int firstDay = (y + y/4 - y/100 + y/400 + t[s_month-1] + 1) % 7;
                int clickedDay = r * 7 + c - firstDay + 1;
                int daysInMonth = 31;
                if (s_month==4||s_month==6||s_month==9||s_month==11) daysInMonth=30;
                else if (s_month==2) daysInMonth = (s_year%4==0 && (s_year%100!=0 || s_year%400==0)) ? 29 : 28;
                
                if (clickedDay >= 1 && clickedDay <= daysInMonth) {
                    s_day = clickedDay;
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            return 0;
        }
        case WM_CLOSE: EnableWindow(g_hTaskbar, TRUE); DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc; MSG msg; g_hInst = hInstance;

    memset(&wc, 0, sizeof(WNDCLASS)); wc.style = CS_DBLCLKS; wc.lpfnWndProc = TaskbarProc; wc.hInstance = hInstance; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "CalmiraTaskbarClass"; RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = RunDlgProc; wc.hInstance = hInstance; wc.lpszClassName = "RunDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = ShortcutDlgProc; wc.hInstance = hInstance; wc.lpszClassName = "ShortcutDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = PromptDlgProc; wc.hInstance = hInstance; wc.lpszClassName = "PromptDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = DateTimeDlgProc; wc.hInstance = hInstance; wc.lpszClassName = "DateTimeDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    
    g_hTaskbar = CreateWindowEx(WS_EX_ACCEPTFILES | WS_EX_TOPMOST, "CalmiraTaskbarClass", "Calmira Taskbar", WS_POPUP | WS_VISIBLE, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    ShowWindow(g_hTaskbar, nCmdShow); UpdateWindow(g_hTaskbar);
    
    while (GetMessage(&msg, NULL, 0, 0)) { 
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                HWND hwndFocus = GetFocus();
                if (hwndFocus != g_hSearchBox && hwndFocus != g_hSearchList) {
                    HWND hActive = GetActiveWindow();
                    if (hActive && hActive != g_hTaskbar) {
                        PostMessage(hActive, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
                        continue;
                    }
                }
            } else if (msg.wParam == VK_ESCAPE) {
                HWND hwndFocus = GetFocus();
                if (hwndFocus != g_hSearchBox && hwndFocus != g_hSearchList) {
                    HWND hActive = GetActiveWindow();
                    if (hActive && hActive != g_hTaskbar) {
                        PostMessage(hActive, WM_COMMAND, MAKEWPARAM(IDCANCEL, 0), 0);
                        continue;
                    }
                }
            }
        }
        TranslateMessage(&msg); 
        DispatchMessage(&msg); 
    }
    
    return (int)msg.wParam;
}