/*
 * vhdmaster.c - VHD Master 0.1
 * Two-pane VHD image editor for Win32 (converted from ISO Master).
 * Implemented: VHD fixed-disk container (footer/checksum/open/save/resize/convert),
 *              MBR partition table (create/delete/properties/active/resize/mbr/vbr),
 *              local filesystem browser, FAT16/FAT32 read/write/format engine,
 *              Drag & Drop recursive imports, QEMU boot integration,
 *              Intelligent Shrink with Data Loss detection, Secure Zeroing, Compacting.
 *
 * Compile: gcc -Os -s -mwindows -o vhdmaster.exe vhdmaster.c -lcomctl32 -lcomdlg32
 *
 * PUBLIC DOMAIN. NO WARRANTY.
 * ============================================================================ */

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <windowsx.h>

/* ============================================================ CONSTANTS */
#define APP_NAME        "VHD Master"
#define APP_VERSION     "0.1"
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600
#define SECTOR_SIZE     512
#define MAX_MBR_PARTS   4

#define ID_LOCAL_BACK     2001
#define ID_LOCAL_NEWDIR   2002
#define ID_VHD_BACK       2003
#define ID_VHD_ADD        2005
#define ID_VHD_EXTRACT    2006
#define ID_VHD_DELETE     2007

#define IDM_IMAGE_NEW      3001
#define IDM_IMAGE_OPEN     3002
#define IDM_IMAGE_SAVE     3003
#define IDM_IMAGE_CONVERT  3007
#define IDM_IMAGE_QEMU_BOOT 3008
#define IDM_IMAGE_QUIT     3006

#define IDM_PART_LIST      3020
#define IDM_PART_DELETE    3022
#define IDM_PART_FORMAT    3023
#define IDM_DISK_RESIZE    3024
#define IDM_PART_ACTIVE    3025
#define IDM_PART_RESIZE    3026
#define IDM_PART_VBR_FILE  3027
#define IDM_PART_COMPACT   3028
#define IDM_PART_REPLACE_BOOT 3029

#define IDM_DISK_MBR_STD     3030
#define IDM_DISK_TRIM        3031
#define IDM_DISK_EXTRACT_MBR 3032
#define IDM_DISK_EXTRACT_VBR 3033

#define IDM_PART_CREATE_FAT12   3051
#define IDM_PART_CREATE_FAT16_S 3052
#define IDM_PART_CREATE_FAT16   3053
#define IDM_PART_CREATE_FAT32   3054
#define IDM_PART_CREATE_FAT32L  3055
#define IDM_PART_CREATE_FAT16L  3056

#define IDM_HELP_ABOUT     3041

#define IDM_MRU_1          3101
#define IDM_MRU_SEP        3100

/* ============================================================ TYPEDEFS */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef struct {
    int used;
    u8  type;         /* MBR partition type byte        */
    u8  boot;         /* 0x80 bootable, 0x00 not        */
    u32 lba_begin;    /* first sector (relative to 0)   */
    u32 lba_count;    /* size in sectors                */
} MbrPart;

typedef struct {
    BOOL        isOpen;
    char        path[MAX_PATH];
    u8         *img;          /* entire VHD file in RAM */
    long long   img_bytes;    /* file size incl. footer */
    u64         cap;          /* virtual disk size      */
    long long   data_offset;  /* byte offset of LBA 0   */
    MbrPart     parts[MAX_MBR_PARTS];
    int         fs_mounted;   /* FAT engine attached?   */
    u32         fs_part_lba, fs_part_nsec;
} VhdState;

/* ============================================================ GLOBALS */
HWND g_hMainWnd = NULL, g_hLocalListView = NULL, g_hVhdListView = NULL;
HWND g_hStatusBar = NULL, g_hProgressBar = NULL, g_hCancelBtn = NULL;
HINSTANCE g_hInstance = NULL;

VhdState g_vhd = {0};
char g_current_local_path[MAX_PATH];
char g_mru[5][MAX_PATH] = {0};
BOOL g_show_hidden = FALSE;
volatile BOOL g_cancel_operation = FALSE;
static int g_last_percent = -1;

static int g_view_mode = 0; // 0 = MBR Partitions, 1 = FAT Files

static void populate_vhd_listview(void);
static void set_local_path(const char* path);
static int fs_list(u32 dir_cluster);
static void update_mbr_in_ram(void);
static int import_recursive(const char* host_path, u32 parent_cluster);
static int vhd_open(const char* path);
static void format_83_name(const u8 *src, char *dst);
static void make_83_name(const char *in, u8 *out);

/* ============================================================ BYTE HELPERS */
static u16 rd16le(const u8 *p) { return (u16)p[0] | ((u16)p[1]<<8); }
static void wr16le(u8 *p, u16 v) { p[0]=(u8)v; p[1]=(u8)(v>>8); }
static u32 rd32le(const u8 *p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static void wr32le(u8 *p, u32 v) { p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static u32 rd32be(const u8 *p) { return ((u32)p[0]<<24) | ((u32)p[1]<<16) | ((u32)p[2]<<8) | p[3]; }
static void wr32be(u8 *p, u32 v) { p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
static u64 rd64be(const u8 *p) { u64 v=0; int i; for(i=0;i<8;i++) v=(v<<8)|p[i]; return v; }
static void wr64be(u8 *p, u64 v) { int i; for(i=0;i<8;i++) p[i]=(u8)(v>>(56-8*i)); }
static void wr16be(u8 *p, u16 v) { p[0]=(u8)(v>>8); p[1]=(u8)v; }

static const char* get_basename(const char* path) {
    const char* slash = strrchr(path, '/'); if (!slash) slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

static void format_size(u64 bytes, char* buffer, int buf_size) {
    if (bytes >= 1073741824ULL)      snprintf(buffer, buf_size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576ULL)    snprintf(buffer, buf_size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024ULL)       snprintf(buffer, buf_size, "%.2f KB", bytes / 1024.0);
    else                             snprintf(buffer, buf_size, "%I64u B", bytes);
}

void UpdateWindowTitle(void) {
    char title[MAX_PATH + 64];
    if (g_vhd.isOpen && strlen(g_vhd.path) > 0)
        snprintf(title, sizeof(title), "%s %s - [%s]", APP_NAME, APP_VERSION, get_basename(g_vhd.path));
    else if (g_vhd.isOpen)
        snprintf(title, sizeof(title), "%s %s - [New VHD]", APP_NAME, APP_VERSION);
    else
        snprintf(title, sizeof(title), "%s %s", APP_NAME, APP_VERSION);
    SetWindowTextA(g_hMainWnd, title);
}

/* ============================================================ PROGRESS HELPERS */
void PumpMessages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
}

void ShowProgress(BOOL show) {
    ShowWindow(g_hProgressBar, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hCancelBtn, show ? SW_SHOW : SW_HIDE);
    g_cancel_operation = FALSE;
    g_last_percent = -1;
    if (show) SendMessageA(g_hProgressBar, PBM_SETPOS, 0, 0);
}

void UpdateProgress(int percent) {
    if (percent != g_last_percent) {
        SendMessageA(g_hProgressBar, PBM_SETPOS, percent, 0);
        g_last_percent = percent;
    }
    PumpMessages();
}

/* ============================================================ DIALOGS */
char g_input_result[MAX_PATH];
HWND g_hInputEdit;

LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == 1)      { GetWindowTextA(g_hInputEdit, g_input_result, MAX_PATH); DestroyWindow(hwnd); }
            else if (LOWORD(wp) == 2) { g_input_result[0] = '\0'; DestroyWindow(hwnd); }
            break;
        case WM_CLOSE: g_input_result[0] = '\0'; DestroyWindow(hwnd); break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

BOOL ShowInputBox(HWND parent, const char* title, const char* prompt, char* out_buf) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = InputWndProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "VhdInputBoxClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "VhdInputBoxClass", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 300, 140,
        parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", prompt, WS_CHILD | WS_VISIBLE, 10, 10, 260, 20, hDlg, NULL, g_hInstance, NULL);
    g_hInputEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", out_buf, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        10, 35, 260, 22, hDlg, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 110, 70, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 195, 70, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);

    SetFocus(g_hInputEdit); EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    if (g_input_result[0] != '\0') { strcpy(out_buf, g_input_result); return TRUE; }
    return FALSE;
}

char g_lost_log[65536];
int g_lost_action = 0;
LRESULT CALLBACK LostFilesProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == 1)      { g_lost_action = 1; DestroyWindow(hwnd); }
            else if (LOWORD(wp) == 2) { g_lost_action = 0; DestroyWindow(hwnd); }
            else if (LOWORD(wp) == 3) {
                OPENFILENAMEA sfn = {0};
                char path[MAX_PATH] = "lost_files.txt";
                sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
                sfn.lpstrFile = path; sfn.nMaxFile = MAX_PATH;
                sfn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files\0*.*\0";
                sfn.lpstrDefExt = "txt";
                if (GetSaveFileNameA(&sfn)) {
                    FILE* f = fopen(path, "w");
                    if (f) { fputs(g_lost_log, f); fclose(f); MessageBoxA(hwnd, "Exported successfully.", "Success", MB_OK); }
                }
            }
            break;
        case WM_CLOSE: g_lost_action = 0; DestroyWindow(hwnd); break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

BOOL ShowLostFilesDialog(HWND parent) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = LostFilesProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "VhdLostFilesClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "VhdLostFilesClass", "Data Loss Warning",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", "The following items exceed the new partition bounds and will be deleted:", WS_CHILD | WS_VISIBLE, 10, 10, 360, 20, hDlg, NULL, g_hInstance, NULL);
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_lost_log, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        10, 30, 360, 180, hDlg, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Proceed", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 210, 220, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 295, 220, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Export...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 220, 75, 23, hDlg, (HMENU)3, g_hInstance, NULL);

    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    return g_lost_action;
}

/* ============================================================ SETTINGS & MRU */
void SaveSettings(void) {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* p = strrchr(iniPath, '\\');
    if (p) strcpy(p + 1, "vhdmaster.ini"); else strcpy(iniPath, "vhdmaster.ini");

    WINDOWPLACEMENT wp = {0}; wp.length = sizeof(WINDOWPLACEMENT);
    if (GetWindowPlacement(g_hMainWnd, &wp)) {
        char buf[32];
        snprintf(buf, 32, "%d", (int)wp.rcNormalPosition.left);   WritePrivateProfileStringA("Window", "X", buf, iniPath);
        snprintf(buf, 32, "%d", (int)wp.rcNormalPosition.top);    WritePrivateProfileStringA("Window", "Y", buf, iniPath);
        snprintf(buf, 32, "%d", (int)(wp.rcNormalPosition.right - wp.rcNormalPosition.left));  WritePrivateProfileStringA("Window", "Width", buf, iniPath);
        snprintf(buf, 32, "%d", (int)(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top));  WritePrivateProfileStringA("Window", "Height", buf, iniPath);
    }
    for (int i = 0; i < 5; i++) {
        char key[16]; snprintf(key, 16, "MRU%d", i + 1);
        WritePrivateProfileStringA("MRU", key, g_mru[i], iniPath);
    }
}

void UpdateMRUMenu(void) {
    HMENU hMenu = GetMenu(g_hMainWnd);
    if (!hMenu) return;
    HMENU hFile = GetSubMenu(hMenu, 0);
    DeleteMenu(hFile, IDM_MRU_SEP, MF_BYCOMMAND);
    for (int i = 0; i < 5; i++) DeleteMenu(hFile, IDM_MRU_1 + i, MF_BYCOMMAND);

    int count = 0;
    for (int i = 0; i < 5; i++) if (strlen(g_mru[i]) > 0) count++;

    if (count > 0) {
        InsertMenuA(hFile, IDM_IMAGE_QUIT, MF_BYCOMMAND | MF_SEPARATOR, IDM_MRU_SEP, NULL);
        for (int i = 0; i < 5; i++) {
            if (strlen(g_mru[i]) > 0) {
                char text[MAX_PATH + 10];
                snprintf(text, sizeof(text), "&%d %s", i + 1, g_mru[i]);
                InsertMenuA(hFile, IDM_IMAGE_QUIT, MF_BYCOMMAND | MF_STRING, IDM_MRU_1 + i, text);
            }
        }
    }
    DrawMenuBar(g_hMainWnd);
}

void LoadSettings(void) {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* p = strrchr(iniPath, '\\');
    if (p) strcpy(p + 1, "vhdmaster.ini"); else strcpy(iniPath, "vhdmaster.ini");

    char buf[32];
    int x = -9999, y = -9999, w = WINDOW_WIDTH, h = WINDOW_HEIGHT;
    if (GetPrivateProfileStringA("Window", "X", "", buf, 32, iniPath) && strlen(buf) > 0) x = atoi(buf);
    if (GetPrivateProfileStringA("Window", "Y", "", buf, 32, iniPath) && strlen(buf) > 0) y = atoi(buf);
    if (GetPrivateProfileStringA("Window", "Width", "", buf, 32, iniPath) && strlen(buf) > 0) w = atoi(buf);
    if (GetPrivateProfileStringA("Window", "Height", "", buf, 32, iniPath) && strlen(buf) > 0) h = atoi(buf);
    if (x != -9999 && y != -9999) SetWindowPos(g_hMainWnd, NULL, x, y, w, h, SWP_NOZORDER);

    for (int i = 0; i < 5; i++) {
        char key[16]; snprintf(key, 16, "MRU%d", i + 1);
        GetPrivateProfileStringA("MRU", key, "", g_mru[i], MAX_PATH, iniPath);
    }
    UpdateMRUMenu();
}

void UpdateMRU(const char* path) {
    int existing = -1;
    for (int i = 0; i < 5; i++) if (strcmp(g_mru[i], path) == 0) existing = i;
    if (existing != -1) {
        char temp[MAX_PATH]; strcpy(temp, g_mru[existing]);
        for (int i = existing; i > 0; i--) strcpy(g_mru[i], g_mru[i-1]);
        strcpy(g_mru[0], temp);
    } else {
        for (int i = 4; i > 0; i--) strcpy(g_mru[i], g_mru[i-1]);
        strcpy(g_mru[0], path);
    }
    UpdateMRUMenu(); SaveSettings();
}

/* ============================================================ VHD CONTAINER */
static void vhd_build_footer(u8 *foot, u64 cap) {
    u32 sum; int i;
    memset(foot, 0, 512);
    memcpy(foot + 0,  "conectix", 8);
    wr32be(foot + 8,  0x00000002);
    wr32be(foot + 12, 0x00010000);
    wr32be(foot + 16, 0xFFFFFFFF);
    wr32be(foot + 24, (u32)time(NULL));
    memcpy(foot + 28, "vhdm", 4);
    wr32be(foot + 32, 0x00010000);
    memcpy(foot + 36, "Wi2k", 4);
    wr64be(foot + 40, cap);
    wr64be(foot + 48, cap);
    {
        u64 secs = cap / 512;
        u32 cyl = (u32)(secs / (255ULL * 63));
        if (cyl < 1)     cyl = 1;
        if (cyl > 65535) cyl = 65535;
        wr16be(foot + 56, (u16)cyl);
        foot[58] = 255;
        foot[59] = 63;
    }
    wr32be(foot + 60, 2);
    for (i = 0; i < 16; i++) foot[68 + i] = (u8)(rand() & 0xFF);
    foot[84] = 0;
    memset(foot + 64, 0, 4);
    sum = 0;
    for (i = 0; i < 512; i++) sum += foot[i];
    wr32be(foot + 64, ~sum);
}

static int vhd_validate(const u8 *img, long long bytes, long long *data_offset, u64 *cap) {
    const u8 *foot = img + bytes - 512;
    if (bytes < 1024)                                             return -1;
    if (memcmp(img, "conectix", 8) == 0) *data_offset = 512;
    else if (memcmp(foot, "conectix", 8) == 0) *data_offset = 0;
    else return -2;
    if (rd32be(foot + 60) != 2) return -3;
    *cap = rd64be(foot + 48);
    if (*cap == 0 || (long long)(*cap) + 512 + *data_offset > bytes) return -4;
    return 0;
}

static void vhd_parse_mbr(void) {
    int i;
    memset(g_vhd.parts, 0, sizeof(g_vhd.parts));
    if (!g_vhd.img) return;
    const u8 *mbr = g_vhd.img + g_vhd.data_offset;
    for (i = 0; i < MAX_MBR_PARTS; i++) {
        const u8 *e = mbr + 0x1BE + i * 16;
        if (e[4] == 0) continue;
        g_vhd.parts[i].used      = 1;
        g_vhd.parts[i].type      = e[4];
        g_vhd.parts[i].boot      = e[0];
        g_vhd.parts[i].lba_begin = rd32le(e + 8);
        g_vhd.parts[i].lba_count = rd32le(e + 12);
    }
}

static void update_mbr_in_ram(void) {
    if (!g_vhd.isOpen || !g_vhd.img) return;
    u8 *mbr = g_vhd.img + g_vhd.data_offset;
    for (int i = 0; i < MAX_MBR_PARTS; i++) {
        u8 *e = mbr + 0x1BE + i * 16;
        memset(e, 0, 16);
        if (g_vhd.parts[i].used) {
            e[0] = g_vhd.parts[i].boot;
            e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;
            e[4] = g_vhd.parts[i].type;
            e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
            wr32le(e + 8, g_vhd.parts[i].lba_begin);
            wr32le(e + 12, g_vhd.parts[i].lba_count);
        }
    }
}

static void vhd_close(void) {
    if (g_vhd.img) { free(g_vhd.img); g_vhd.img = NULL; }
    g_vhd.isOpen = FALSE; g_vhd.img_bytes = 0; g_vhd.data_offset = 0; g_vhd.cap = 0;
    g_vhd.fs_mounted = 0; g_view_mode = 0;
    UpdateWindowTitle();
}

static int vhd_open(const char* path) {
    HANDLE h; LARGE_INTEGER sz; u8 *buf; DWORD got; int rc;
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return -1; }
    buf = (u8*)malloc((size_t)sz.QuadPart);
    if (!buf) { CloseHandle(h); return -5; }
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart) {
        free(buf); CloseHandle(h); return -2;
    }
    CloseHandle(h);

    long long doff; u64 cap;
    rc = vhd_validate(buf, (long long)sz.QuadPart, &doff, &cap);
    if (rc != 0) { free(buf); return rc; }

    vhd_close();
    g_vhd.img         = buf;
    g_vhd.img_bytes   = (long long)sz.QuadPart;
    g_vhd.data_offset = doff;
    g_vhd.cap         = cap;
    g_vhd.isOpen      = TRUE;
    strncpy(g_vhd.path, path, MAX_PATH - 1);
    vhd_parse_mbr();
    UpdateWindowTitle();
    return 0;
}

static int vhd_create(const char* path, u32 size_mb) {
    u8 *buf; u64 cap = (u64)size_mb * 1024 * 1024;
    long long total = 512 + (long long)cap + 512;
    buf = (u8*)calloc(1, (size_t)total);
    if (!buf) return -1;
    vhd_build_footer(buf, cap);
    vhd_build_footer(buf + total - 512, cap);
    {
        u8 *mbr = buf + 512;
        mbr[510] = 0x55; mbr[511] = 0xAA;
    }
    {
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD w;
        if (h == INVALID_HANDLE_VALUE) { free(buf); return -2; }
        WriteFile(h, buf, (DWORD)total, &w, NULL);
        CloseHandle(h);
    }
    free(buf);
    return 0;
}

static int vhd_save(void) {
    HANDLE h; DWORD w;
    if (!g_vhd.isOpen || !g_vhd.img) return -1;
    vhd_build_footer(g_vhd.img + g_vhd.img_bytes - 512, g_vhd.cap);
    if (g_vhd.data_offset >= 512) vhd_build_footer(g_vhd.img, g_vhd.cap);
    h = CreateFileA(g_vhd.path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -2;
    WriteFile(h, g_vhd.img, (DWORD)g_vhd.img_bytes, &w, NULL);
    CloseHandle(h);
    return (w == (DWORD)g_vhd.img_bytes) ? 0 : -3;
}

static int vhd_resize(u32 new_mb) {
    u8 *nbuf; u64 newcap = (u64)new_mb * 1024 * 1024;
    long long new_total;
    int i;
    if (!g_vhd.isOpen) return -1;
    new_total = 512 + (long long)newcap + 512;

    for (i = 0; i < MAX_MBR_PARTS; i++)
        if (g_vhd.parts[i].used &&
            (u64)(g_vhd.parts[i].lba_begin + g_vhd.parts[i].lba_count) * 512 > newcap)
            return -2;

    nbuf = (u8*)calloc(1, (size_t)new_total);
    if (!nbuf) return -3;
    if (new_total >= g_vhd.img_bytes)
        memcpy(nbuf + 512, g_vhd.img + 512, (size_t)g_vhd.img_bytes - 1024);
    else
        memcpy(nbuf + 512, g_vhd.img + 512, (size_t)(g_vhd.img_bytes - 1024 - (g_vhd.img_bytes - new_total)));

    free(g_vhd.img);
    g_vhd.img = nbuf;
    g_vhd.img_bytes = new_total;
    g_vhd.cap = newcap;
    return 0;
}

/* ============================================================ PARTITIONS */
static const char* part_type_name(u8 t) {
    switch (t) {
        case 0x01: return "FAT12";
        case 0x04: case 0x06: case 0x0E: return "FAT16";
        case 0x05: case 0x0F: return "Extended";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: case 0x0C: return "FAT32";
        case 0x83: return "Linux";
        default:   return "Other";
    }
}

static void part_show_properties(HWND hwnd) {
    char msg[1024], sz1[32], sz2[32], line[128];
    int i; u64 total_used = 0;
    strcpy(msg, "");
    for (i = 0; i < MAX_MBR_PARTS; i++) {
        if (!g_vhd.parts[i].used) { snprintf(line, sizeof(line), "Slot %d: (empty)\n", i + 1); }
        else {
            format_size((u64)g_vhd.parts[i].lba_count * 512, sz1, sizeof(sz1));
            total_used += (u64)g_vhd.parts[i].lba_count * 512;
            snprintf(line, sizeof(line), "Slot %d: %s, %s, start LBA %lu%s\n",
                     i + 1, part_type_name(g_vhd.parts[i].type), sz1,
                     (unsigned long)g_vhd.parts[i].lba_begin,
                     g_vhd.parts[i].boot == 0x80 ? ", bootable" : "");
        }
        strcat(msg, line);
    }
    format_size(total_used, sz1, sizeof(sz1));
    format_size(g_vhd.cap, sz2, sizeof(sz2));
    snprintf(line, sizeof(line), "\nCapacity: %s\nUsed by partitions: %s\nFree: %.1f%%",
             sz2, sz1, g_vhd.cap ? 100.0 * (1.0 - (double)total_used / (double)g_vhd.cap) : 0.0);
    strcat(msg, line);
    MessageBoxA(hwnd, msg, "Disk Properties", MB_ICONINFORMATION);
}

static int part_create_fat(HWND hwnd, u8 force_type) {
    u64 cap_secs = g_vhd.cap / 512;
    u8 used_end[4] = {0,0,0,0};
    int i, best = -1, slot = -1;
    u64 best_sz = 0, gap_begin = 0;

    { u8 flags[4] = {0,0,0,0};
      for (i = 0; i < MAX_MBR_PARTS; i++) if (g_vhd.parts[i].used) {
          u64 s = g_vhd.parts[i].lba_begin, e = s + g_vhd.parts[i].lba_count;
          if (s < 4) used_end[0] = 1; (void)e;
      } }

    {
        u64 ranges[5][2]; int nr = 0, j;
        u32 starts[4], counts[4]; int n = 0;
        for (i = 0; i < MAX_MBR_PARTS; i++) if (g_vhd.parts[i].used) { starts[n] = g_vhd.parts[i].lba_begin; counts[n] = g_vhd.parts[i].lba_count; n++; }
        for (i = 0; i < n - 1; i++) for (j = i + 1; j < n; j++)
            if (starts[j] < starts[i]) { u32 t = starts[i]; starts[i] = starts[j]; starts[j] = t;
                                         t = counts[i]; counts[i] = counts[j]; counts[j] = t; }
        u64 cursor = 63;
        for (i = 0; i < n && nr < 5; i++) {
            if (starts[i] > cursor) { ranges[nr][0] = cursor; ranges[nr][1] = starts[i]; nr++; }
            if ((u64)starts[i] + counts[i] > cursor) cursor = (u64)starts[i] + counts[i];
        }
        if (cursor < cap_secs) { ranges[nr][0] = cursor; ranges[nr][1] = cap_secs; nr++; }

        for (i = 0; i < nr; i++) {
            u64 len = ranges[i][1] - ranges[i][0];
            if (len > best_sz) { best_sz = len; best = i; gap_begin = ranges[i][0]; }
        }
    }
    if (best < 0 || best_sz < 4200) {
        MessageBoxA(hwnd, "No free space large enough for a partition.", "Create Partition", MB_ICONWARNING);
        return -1;
    }
    for (i = 0; i < MAX_MBR_PARTS; i++) if (!g_vhd.parts[i].used) { slot = i; break; }
    if (slot < 0) { MessageBoxA(hwnd, "MBR partition table is full (4/4 used).", "Create Partition", MB_ICONWARNING); return -1; }

    {
        u64 len = best_sz;
        if (len > 0xFFFFFFFFu) len = 0xFFFFFFFFu;
        
        u8 type = 0x06;
        if (force_type != 0) {
            type = force_type;
        } else {
            type = (len >= 65528 * 63) ? 0x0B : 0x06;
        }

        g_vhd.parts[slot].used = 1;
        g_vhd.parts[slot].type = type;
        g_vhd.parts[slot].boot = 0;
        g_vhd.parts[slot].lba_begin = (u32)gap_begin;
        g_vhd.parts[slot].lba_count = (u32)len;
        
        update_mbr_in_ram();

        char szs[32]; format_size(len * 512, szs, sizeof(szs));
        char msg[128];
        snprintf(msg, sizeof(msg), "Created %s partition (%s) in MBR slot %d.", part_type_name(type), szs, slot + 1);
        SetWindowTextA(g_hStatusBar, msg);
        
        return slot;
    }
}

static void part_delete(HWND hwnd, int slot) {
    if (slot < 0 || slot >= MAX_MBR_PARTS || !g_vhd.parts[slot].used) return;
    g_vhd.parts[slot].used = 0;
    if (g_vhd.fs_mounted && g_vhd.fs_part_lba == g_vhd.parts[slot].lba_begin) {
        g_vhd.fs_mounted = 0;
        g_view_mode = 0;
    }
    update_mbr_in_ram();
    populate_vhd_listview();
    SetWindowTextA(g_hStatusBar, "Partition deleted from MBR.");
}

/* ============================================================ FAT ENGINE */
int g_fat_type = 0;
u32 g_fat_lba = 0, g_root_lba = 0, g_data_lba = 0;
u32 g_sec_per_clus = 0, g_fat_size = 0, g_root_secs = 0;
u32 g_root_cluster = 0, g_total_clusters = 0;
u32 g_current_dir_cluster = 0;

#define FS_MAX_ENTRIES 4096
typedef struct {
    char name[256];
    int  is_directory;
    u64  size;
    u32  first_cluster;
} FsEntry;
static FsEntry g_fs_entries[FS_MAX_ENTRIES];
static int     g_fs_entry_count = 0;

static int read_sec(u32 lba, u8 *buf, u32 count) {
    if (!g_vhd.isOpen) return -1;
    long long offset = g_vhd.data_offset + (long long)lba * 512;
    if (offset + count * 512 > g_vhd.img_bytes - 512) return -1;
    memcpy(buf, g_vhd.img + offset, count * 512);
    return 0;
}
static int write_sec(u32 lba, const u8 *buf, u32 count) {
    if (!g_vhd.isOpen) return -1;
    long long offset = g_vhd.data_offset + (long long)lba * 512;
    if (offset + count * 512 > g_vhd.img_bytes - 512) return -1;
    memcpy(g_vhd.img + offset, buf, count * 512);
    return 0;
}

static u32 cluster_to_lba(u32 cluster) {
    if (cluster >= 2) return g_data_lba + (cluster - 2) * g_sec_per_clus;
    return g_root_lba;
}

static u32 read_fat(u32 cluster) {
    if (cluster < 2 || cluster > g_total_clusters + 1) return 0x0FFFFFFF;
    u8 sec[512];
    if (g_fat_type == 32) {
        u32 sec_off = (cluster * 4) / 512;
        u32 ent_off = (cluster * 4) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        return rd32le(sec + ent_off) & 0x0FFFFFFF;
    } else if (g_fat_type == 16) {
        u32 sec_off = (cluster * 2) / 512;
        u32 ent_off = (cluster * 2) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        u32 val = rd16le(sec + ent_off);
        if (val >= 0xFFF8) val = 0x0FFFFFFF;
        return val;
    }
    return 0x0FFFFFFF;
}

static void write_fat(u32 cluster, u32 val) {
    if (cluster < 2 || cluster > g_total_clusters + 1) return;
    u8 sec[512];
    if (g_fat_type == 32) {
        u32 sec_off = (cluster * 4) / 512;
        u32 ent_off = (cluster * 4) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        u32 cur = rd32le(sec + ent_off);
        cur = (cur & 0xF0000000) | (val & 0x0FFFFFFF);
        wr32le(sec + ent_off, cur);
        write_sec(g_fat_lba + sec_off, sec, 1);
    } else if (g_fat_type == 16) {
        u32 sec_off = (cluster * 2) / 512;
        u32 ent_off = (cluster * 2) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        wr16le(sec + ent_off, (u16)val);
        write_sec(g_fat_lba + sec_off, sec, 1);
    }
}

static u32 alloc_cluster(void) {
    for (u32 i = 2; i <= g_total_clusters + 1; i++) {
        if (read_fat(i) == 0) {
            write_fat(i, 0x0FFFFFFF);
            u8 z[512] = {0};
            for(u32 k=0; k<g_sec_per_clus; k++) write_sec(cluster_to_lba(i)+k, z, 1);
            return i;
        }
    }
    return 0;
}

static int fs_mount(int part_slot) {
    if (!g_vhd.isOpen || !g_vhd.parts[part_slot].used) return -1;
    g_vhd.fs_part_lba = g_vhd.parts[part_slot].lba_begin;
    g_vhd.fs_part_nsec = g_vhd.parts[part_slot].lba_count;

    u8 bpb[512];
    if (read_sec(g_vhd.fs_part_lba, bpb, 1) != 0) return -2;
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) return -3;

    u16 bytsPerSec = rd16le(bpb + 11);
    if (bytsPerSec != 512) return -4;
    g_sec_per_clus = bpb[13];
    u16 rsvdSecCnt = rd16le(bpb + 14);
    u8 numFATs = bpb[16];
    u16 rootEntCnt = rd16le(bpb + 17);
    u16 totSec16 = rd16le(bpb + 19);
    u32 totSec32 = rd32le(bpb + 32);
    u16 fatSz16 = rd16le(bpb + 22);

    u32 fatSz = fatSz16 ? fatSz16 : rd32le(bpb + 36);
    u32 totSec = totSec16 ? totSec16 : totSec32;
    g_fat_size = fatSz;

    g_root_secs = ((rootEntCnt * 32) + 511) / 512;
    g_fat_lba = g_vhd.fs_part_lba + rsvdSecCnt;
    g_root_lba = g_fat_lba + (numFATs * fatSz);
    g_data_lba = g_root_lba + g_root_secs;

    u32 dataSecs = totSec - (rsvdSecCnt + (numFATs * fatSz) + g_root_secs);
    g_total_clusters = dataSecs / g_sec_per_clus;

    if (g_total_clusters < 4085) return -5; 
    else if (g_total_clusters < 65525) g_fat_type = 16;
    else g_fat_type = 32;

    if (g_fat_type == 32) g_root_cluster = rd32le(bpb + 44);
    else g_root_cluster = 0;

    g_vhd.fs_mounted = 1;
    g_current_dir_cluster = g_root_cluster;
    return 0;
}

static void format_83_name(const u8 *src, char *dst) {
    int i, j = 0;
    for (i = 0; i < 8 && src[i] != ' '; i++) dst[j++] = src[i];
    if (src[8] != ' ') {
        dst[j++] = '.';
        for (i = 8; i < 11 && src[i] != ' '; i++) dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static int fs_list(u32 dir_cluster) {
    g_fs_entry_count = 0;
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;

        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0x00) goto done; 
            if (ent[0] == 0xE5) continue; 
            if (ent[11] == 0x0F) continue; 
            if (ent[11] & 0x08) continue; 

            FsEntry *fse = &g_fs_entries[g_fs_entry_count];
            format_83_name(ent, fse->name);
            fse->is_directory = (ent[11] & 0x10) ? 1 : 0;
            fse->size = rd32le(ent + 28);
            fse->first_cluster = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            g_fs_entry_count++;
            if (g_fs_entry_count >= FS_MAX_ENTRIES) goto done;
        }

        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
done:
    return 0;
}

static int find_free_dir_entry(u32 dir_cluster, u32 *out_lba, u32 *out_offset) {
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        read_sec(lba, sec, 1);
        for (int i = 0; i < 512; i += 32) {
            if (sec[i] == 0x00 || sec[i] == 0xE5) {
                *out_lba = lba;
                *out_offset = i;
                return 0;
            }
        }
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) return -1;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            u32 next = read_fat(cur);
            if (next >= 0x0FFFFFF8) {
                u32 nclus = alloc_cluster();
                if (!nclus) return -1;
                write_fat(cur, nclus);
                cur = nclus;
            } else {
                cur = next;
            }
        }
    }
    return -1;
}

static void make_83_name(const char *in, u8 *out) {
    memset(out, ' ', 11);
    int i=0, j=0;
    while(in[i] && in[i] != '.' && j < 8) { out[j++] = toupper(in[i++]); }
    while(in[i] && in[i] != '.') i++;
    if (in[i] == '.') {
        i++; j=8;
        while(in[i] && j < 11) { out[j++] = toupper(in[i++]); }
    }
}

static int fs_add_file(const char* host_path, const char* name, u32 parent_cluster) {
    FILE *f = fopen(host_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    u32 sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    u32 first_clus = 0;
    if (sz > 0) {
        first_clus = alloc_cluster();
        if (!first_clus) { fclose(f); return -2; }
        u32 cur_clus = first_clus;
        u32 rem = sz;
        u8 buf[512];
        while (rem > 0) {
            u32 lba = cluster_to_lba(cur_clus);
            for (u32 i = 0; i < g_sec_per_clus && rem > 0; i++) {
                u32 chunk = rem > 512 ? 512 : rem;
                memset(buf, 0, 512);
                fread(buf, 1, chunk, f);
                write_sec(lba + i, buf, 1);
                rem -= chunk;
            }
            if (rem > 0) {
                u32 nclus = alloc_cluster();
                if (!nclus) { fclose(f); return -3; }
                write_fat(cur_clus, nclus);
                cur_clus = nclus;
            }
        }
    }
    fclose(f);

    u32 lba, off;
    if (find_free_dir_entry(parent_cluster, &lba, &off) != 0) return -4;

    u8 sec[512];
    read_sec(lba, sec, 1);
    u8 *ent = sec + off;
    memset(ent, 0, 32);
    make_83_name(name, ent);
    ent[11] = 0x20;
    wr16le(ent + 20, first_clus >> 16);
    wr16le(ent + 26, first_clus & 0xFFFF);
    wr32le(ent + 28, sz);
    write_sec(lba, sec, 1);
    return 0;
}

static u32 fs_mkdir(const char* name, u32 parent_cluster) {
    u32 dclus = alloc_cluster();
    if (!dclus) return 0;
    u32 lba, off;
    if (find_free_dir_entry(parent_cluster, &lba, &off) != 0) return 0;

    u8 sec[512];
    read_sec(lba, sec, 1);
    u8 *ent = sec + off;
    memset(ent, 0, 32);
    make_83_name(name, ent);
    ent[11] = 0x10;
    wr16le(ent + 20, dclus >> 16);
    wr16le(ent + 26, dclus & 0xFFFF);
    write_sec(lba, sec, 1);

    u8 z[512] = {0};
    memset(z, ' ', 11); z[0] = '.'; z[11] = 0x10;
    wr16le(z + 20, dclus >> 16); wr16le(z + 26, dclus & 0xFFFF);
    
    memset(z+32, ' ', 11); z[32] = '.'; z[33] = '.'; z[43] = 0x10;
    u32 pclus = parent_cluster;
    if (g_fat_type == 32 && pclus == g_root_cluster) pclus = 0;
    wr16le(z + 32 + 20, pclus >> 16); wr16le(z + 32 + 26, pclus & 0xFFFF);

    write_sec(cluster_to_lba(dclus), z, 1);
    return dclus;
}

static int import_recursive(const char* host_path, u32 parent_cluster) {
    DWORD attr = GetFileAttributesA(host_path);
    if (attr == INVALID_FILE_ATTRIBUTES) return -1;
    char basename[MAX_PATH];
    const char* slash = strrchr(host_path, '\\');
    if (!slash) slash = strrchr(host_path, '/');
    strcpy(basename, slash ? slash + 1 : host_path);

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        u32 new_clus = fs_mkdir(basename, parent_cluster);
        if (!new_clus) return -1;
        char search[MAX_PATH];
        snprintf(search, sizeof(search), "%s\\*", host_path);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                char child[MAX_PATH];
                snprintf(child, sizeof(child), "%s\\%s", host_path, fd.cFileName);
                import_recursive(child, new_clus);
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    } else {
        fs_add_file(host_path, basename, parent_cluster);
    }
    return 0;
}

static int fs_extract(u32 entry_idx, const char* dest_path) {
    if (entry_idx >= g_fs_entry_count) return -1;
    FsEntry *fse = &g_fs_entries[entry_idx];
    if (fse->is_directory) return -2;

    FILE *f = fopen(dest_path, "wb");
    if (!f) return -3;

    u32 clus = fse->first_cluster;
    u32 rem = fse->size;
    u8 buf[512];

    while (rem > 0 && clus >= 2 && clus < 0x0FFFFFF0) {
        u32 lba = cluster_to_lba(clus);
        for (u32 i = 0; i < g_sec_per_clus && rem > 0; i++) {
            read_sec(lba + i, buf, 1);
            u32 chunk = rem > 512 ? 512 : rem;
            fwrite(buf, 1, chunk, f);
            rem -= chunk;
        }
        clus = read_fat(clus);
    }
    fclose(f);
    return 0;
}

static int fs_delete(u32 entry_idx) {
    if (entry_idx >= g_fs_entry_count) return -1;
    FsEntry *fse = &g_fs_entries[entry_idx];
    u8 sec[512];
    u32 cur = g_current_dir_cluster;
    int is_root16 = (g_fat_type == 16 && cur == 0);
    u32 sec_idx = 0, found = 0;

    while (!found) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        read_sec(lba, sec, 1);
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0) goto end_search;
            if (ent[0] == 0xE5) continue;
            u32 eclus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            if (eclus == fse->first_cluster) {
                ent[0] = 0xE5;
                memset(ent + 1, 0, 31); /* Securely wipe entire directory entry, excluding marker */
                write_sec(lba, sec, 1);
                found = 1; break;
            }
        }
        if (found) break;
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
end_search:
    if (found && fse->first_cluster) {
        u32 c = fse->first_cluster;
        u8 z[512] = {0};
        while(c >= 2 && c < 0x0FFFFFF0) {
            u32 n = read_fat(c);
            u32 lba = cluster_to_lba(c);
            for(u32 i=0; i<g_sec_per_clus; i++) write_sec(lba+i, z, 1);
            write_fat(c, 0);
            c = n;
        }
    }
    return 0;
}

static int fs_format_partition(int part_idx, int fat32) {
    if (!g_vhd.isOpen || !g_vhd.parts[part_idx].used) return -1;
    u64 secs = (u64)g_vhd.parts[part_idx].lba_count;
    if (secs < 2048) return -2;

    u32 spc = 8;
    if (secs > 65536) spc = 16;
    if (secs > 524288) spc = 32;
    if (secs > 1048576) spc = 64; 

    fat32 = fat32 || (secs > 65525 * spc);
    u8 bpb[512] = {0};
    
    bpb[0] = 0xEB; bpb[1] = 0x58; bpb[2] = 0x90;
    memcpy(bpb + 3, "MSWIN4.1", 8);
    wr16le(bpb + 11, 512); 
    bpb[13] = (u8)spc;
    wr16le(bpb + 14, fat32 ? 32 : 1);
    bpb[16] = 2;
    wr16le(bpb + 17, fat32 ? 0 : 512);
    wr16le(bpb + 19, (secs < 65536) ? (u16)secs : 0);
    bpb[21] = 0xF8;
    
    u32 root_secs = fat32 ? 0 : ((512 * 32) / 512);
    u32 tmp_data = secs - (fat32 ? 32 : 1) - root_secs;
    u32 fat_sz = (tmp_data / spc * (fat32 ? 4 : 2) / 512) + 1;
    
    wr16le(bpb + 22, fat32 ? 0 : (u16)fat_sz);
    wr16le(bpb + 24, 63); 
    wr16le(bpb + 26, 255); 
    wr32le(bpb + 28, g_vhd.parts[part_idx].lba_begin);
    wr32le(bpb + 32, (secs >= 65536) ? (u32)secs : 0);

    if (fat32) {
        wr32le(bpb + 36, fat_sz);
        wr16le(bpb + 40, 0); 
        wr16le(bpb + 42, 0); 
        wr32le(bpb + 44, 2); 
        wr16le(bpb + 48, 1); 
        wr16le(bpb + 50, 6); 
        bpb[64] = 0x80; 
        bpb[66] = 0x29; 
        wr32le(bpb + 67, 0x12345678); 
        memcpy(bpb + 71, "NO NAME    ", 11);
        memcpy(bpb + 82, "FAT32   ", 8);
    } else {
        bpb[36] = 0x80; 
        bpb[38] = 0x29; 
        wr32le(bpb + 39, 0x12345678); 
        memcpy(bpb + 43, "NO NAME    ", 11);
        memcpy(bpb + 54, "FAT16   ", 8);
    }
    bpb[510] = 0x55; bpb[511] = 0xAA;
    
    write_sec(g_vhd.parts[part_idx].lba_begin, bpb, 1);
    
    u8 z[512] = {0};
    u32 rsvd = fat32 ? 32 : 1;
    u32 fat_start = g_vhd.parts[part_idx].lba_begin + rsvd;
    
    z[0] = 0xF8; z[1] = 0xFF; z[2] = 0xFF;
    if (fat32) {
        z[3] = 0x0F; z[4] = 0xFF; z[5] = 0xFF; z[6] = 0xFF; z[7] = 0x0F; 
    } else {
        z[3] = 0xFF;
    }
    write_sec(fat_start, z, 1);
    write_sec(fat_start + fat_sz, z, 1);
    
    memset(z, 0, 512);
    for(u32 i=1; i<fat_sz; i++) {
        write_sec(fat_start + i, z, 1);
        write_sec(fat_start + fat_sz + i, z, 1);
    }
    
    if (!fat32) {
        u32 rd_start = fat_start + fat_sz * 2;
        for(u32 i=0; i<root_secs; i++) write_sec(rd_start + i, z, 1);
    } else {
        u32 rd_start = fat_start + fat_sz * 2; 
        for(u32 i=0; i<spc; i++) write_sec(rd_start + i, z, 1);
    }

    g_vhd.parts[part_idx].type = fat32 ? 0x0B : 0x06;
    update_mbr_in_ram();
    return 0;
}

/* ============================================================ DATA LOSS EVALUATION */
static void scan_dir_for_lost(u32 dir_cluster, u32 max_cluster, const char* path, char* log, int* count) {
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;
        
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0x00) return;
            if (ent[0] == 0xE5) continue;
            if (ent[11] == 0x0F) continue;
            
            char fname[13]; format_83_name(ent, fname);
            if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) continue;
            
            u32 fclus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            int is_dir = (ent[11] & 0x10);
            
            int is_lost = 0;
            u32 c = fclus;
            while (c >= 2 && c < 0x0FFFFFF0) {
                if (c >= max_cluster) { is_lost = 1; break; }
                c = read_fat(c);
            }
            
            char full_path[MAX_PATH];
            snprintf(full_path, sizeof(full_path), "%s%s%s", path, strcmp(path, "\\") == 0 ? "" : "\\", fname);
            
            if (is_lost) {
                if (strlen(log) < 65000) {
                    strcat(log, full_path); strcat(log, "\r\n");
                }
                (*count)++;
            } else if (is_dir && fclus >= 2) {
                scan_dir_for_lost(fclus, max_cluster, full_path, log, count);
            }
        }
        
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
}

static void delete_lost_items(u32 dir_cluster, u32 max_cluster) {
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;
        
        int modified = 0;
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0x00) goto write_back;
            if (ent[0] == 0xE5 || ent[11] == 0x0F) continue;
            
            char fname[13]; format_83_name(ent, fname);
            if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) continue;
            
            u32 fclus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            int is_dir = (ent[11] & 0x10);
            
            int is_lost = 0;
            u32 c = fclus;
            while (c >= 2 && c < 0x0FFFFFF0) {
                if (c >= max_cluster) { is_lost = 1; break; }
                c = read_fat(c);
            }
            
            if (is_lost) {
                c = fclus;
                u8 z[512] = {0};
                while (c >= 2 && c < 0x0FFFFFF0) {
                    u32 n = read_fat(c);
                    u32 clba = cluster_to_lba(c);
                    for(u32 j=0; j<g_sec_per_clus; j++) write_sec(clba+j, z, 1);
                    write_fat(c, 0);
                    c = n;
                }
                ent[0] = 0xE5;
                memset(ent + 1, 0, 31); /* Wipe entry metadata */
                modified = 1;
            } else if (is_dir && fclus >= 2) {
                if (modified) { write_sec(lba, sec, 1); modified = 0; }
                delete_lost_items(fclus, max_cluster);
            }
        }
    write_back:
        if (modified) write_sec(lba, sec, 1);
        
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
}

/* ============================================================ UI LISTVIEW */
static void set_local_path(const char* path) {
    strcpy(g_current_local_path, path);
    ListView_DeleteAllItems(g_hLocalListView);
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", g_current_local_path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        int i = 0;
        if (strlen(g_current_local_path) > 3) {
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i++; lvi.pszText = "..";
            int idx = (int)SendMessageA(g_hLocalListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
            (void)idx;
            LVITEMA s = {0}; s.iSubItem = 1; s.pszText = "<DIR>";
            SendMessageA(g_hLocalListView, LVM_SETITEMTEXTA, 0, (LPARAM)&s);
        }
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (!g_show_hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.pszText = fd.cFileName;
            SendMessageA(g_hLocalListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
            char sz[64];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) strcpy(sz, "<DIR>");
            else format_size(((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, sz, sizeof(sz));
            LVITEMA s = {0}; s.iSubItem = 1; s.pszText = sz;
            SendMessageA(g_hLocalListView, LVM_SETITEMTEXTA, i, (LPARAM)&s);
            i++;
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

static void populate_vhd_listview(void) {
    ListView_DeleteAllItems(g_hVhdListView);
    if (!g_vhd.isOpen) return;

    if (g_view_mode == 0) {
        int i;
        for (i = 0; i < MAX_MBR_PARTS; i++) {
            char name[128], sz[64];
            if (g_vhd.parts[i].used) {
                format_size((u64)g_vhd.parts[i].lba_count * 512, sz, sizeof(sz));
                snprintf(name, sizeof(name), "Partition %d (%s)%s", i + 1, part_type_name(g_vhd.parts[i].type),
                         g_vhd.parts[i].boot == 0x80 ? " [Active]" : "");
            } else {
                strcpy(sz, "-");
                snprintf(name, sizeof(name), "Partition %d (empty)", i + 1);
            }
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.pszText = name;
            SendMessageA(g_hVhdListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
            LVITEMA s = {0}; s.iSubItem = 1; s.pszText = sz;
            SendMessageA(g_hVhdListView, LVM_SETITEMTEXTA, i, (LPARAM)&s);
        }
    } else {
        int i = 0;
        int is_root = (g_current_dir_cluster == g_root_cluster || (g_fat_type == 16 && g_current_dir_cluster == 0));
        
        LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.pszText = "..";
        SendMessageA(g_hVhdListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
        LVITEMA s = {0}; s.iSubItem = 1; s.pszText = is_root ? "<UNMOUNT>" : "<DIR>";
        SendMessageA(g_hVhdListView, LVM_SETITEMTEXTA, i, (LPARAM)&s);
        i++;

        for (int k = 0; k < g_fs_entry_count; k++) {
            if (strcmp(g_fs_entries[k].name, ".") == 0 || strcmp(g_fs_entries[k].name, "..") == 0) continue;
            LVITEMA lvi2 = {0}; lvi2.mask = LVIF_TEXT; lvi2.iItem = i; lvi2.pszText = g_fs_entries[k].name;
            SendMessageA(g_hVhdListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi2);
            char sz[64];
            if (g_fs_entries[k].is_directory) strcpy(sz, "<DIR>");
            else format_size(g_fs_entries[k].size, sz, sizeof(sz));
            LVITEMA s2 = {0}; s2.iSubItem = 1; s2.pszText = sz;
            SendMessageA(g_hVhdListView, LVM_SETITEMTEXTA, i, (LPARAM)&s2);
            i++;
        }
    }
}

/* ============================================================ CONVERSION, BOOT, EXTRACT */
static void cmd_convert_img(HWND hwnd) {
    OPENFILENAMEA ofn = {0};
    char szImg[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szImg; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Raw Images (*.img;*.bin;*.iso)\0*.img;*.bin;*.iso\0All Files\0*.*\0";
    ofn.lpstrTitle = "Select source .img to convert";
    if (!GetOpenFileNameA(&ofn)) return;

    char szVhd[MAX_PATH] = "";
    OPENFILENAMEA sfn = {0};
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szVhd; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "VHD Files (*.vhd)\0*.vhd\0";
    sfn.lpstrDefExt = "vhd";
    sfn.Flags = OFN_OVERWRITEPROMPT;
    sfn.lpstrTitle = "Save converted VHD as...";
    if (!GetSaveFileNameA(&sfn)) return;

    FILE *fin = fopen(szImg, "rb");
    if (!fin) { MessageBoxA(hwnd, "Cannot open source file.", "Error", MB_ICONERROR); return; }
    
    FILE *fout = fopen(szVhd, "wb");
    if (!fout) { fclose(fin); MessageBoxA(hwnd, "Cannot create VHD file.", "Error", MB_ICONERROR); return; }

    fseek(fin, 0, SEEK_END);
    u64 fileSize = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    u8 buf[8192];
    size_t bytes;
    ShowProgress(TRUE);
    u64 copied = 0;

    while ((bytes = fread(buf, 1, sizeof(buf), fin)) > 0) {
        fwrite(buf, 1, bytes, fout);
        copied += bytes;
        if (copied % (1024 * 1024) == 0) UpdateProgress((int)((copied * 100) / fileSize));
    }
    
    u8 footer[512];
    vhd_build_footer(footer, fileSize);
    fwrite(footer, 1, 512, fout);
    
    fclose(fin);
    fclose(fout);
    ShowProgress(FALSE);
    
    if (vhd_open(szVhd) == 0) {
        UpdateMRU(szVhd);
        populate_vhd_listview();
        SetWindowTextA(g_hStatusBar, "Conversion complete. VHD Opened.");
    }
}

static void cmd_qemu_boot(HWND hwnd) {
    if (!g_vhd.isOpen) return;
    
    char bootFile[MAX_PATH] = "";
    if (MessageBoxA(hwnd, "Do you want to attach a bootable CD/Floppy image as well?", "QEMU Boot", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        OPENFILENAMEA ofn = {0};
        ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFile = bootFile; ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "Bootable Images (*.iso;*.img)\0*.iso;*.img\0All Files\0*.*\0";
        GetOpenFileNameA(&ofn);
    }

    char args[1024];
    if (strlen(bootFile) > 0) {
        snprintf(args, sizeof(args), "-hda \"%s\" -cdrom \"%s\" -boot d -m 512", g_vhd.path, bootFile);
    } else {
        snprintf(args, sizeof(args), "-hda \"%s\" -m 512", g_vhd.path);
    }

    if ((INT_PTR)ShellExecuteA(hwnd, "open", "qemu-system-i386", args, NULL, SW_SHOW) <= 32) {
        MessageBoxA(hwnd, "Failed to launch QEMU. Ensure 'qemu-system-i386' is in your system PATH.", "QEMU Error", MB_ICONERROR);
    }
}

static void cmd_extract_mbr(HWND hwnd) {
    if (!g_vhd.isOpen || !g_vhd.img) return;
    OPENFILENAMEA sfn = {0};
    char szFile[MAX_PATH] = "mbr.bin";
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szFile; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "Bin Files (*.bin)\0*.bin\0All Files\0*.*\0";
    sfn.lpstrDefExt = "bin";
    if (GetSaveFileNameA(&sfn)) {
        FILE *f = fopen(szFile, "wb");
        if (f) {
            fwrite(g_vhd.img + g_vhd.data_offset, 1, 512, f);
            fclose(f);
            SetWindowTextA(g_hStatusBar, "MBR extracted successfully.");
        }
    }
}

static void cmd_extract_vbr(HWND hwnd) {
    if (!g_vhd.isOpen || !g_vhd.img) return;
    int active_slot = -1;
    for (int i = 0; i < MAX_MBR_PARTS; i++) {
        if (g_vhd.parts[i].used && g_vhd.parts[i].boot == 0x80) { active_slot = i; break; }
    }
    if (active_slot == -1) {
        MessageBoxA(hwnd, "No active (bootable) partition found to extract VBR from.", "Error", MB_ICONWARNING);
        return;
    }
    OPENFILENAMEA sfn = {0};
    char szFile[MAX_PATH] = "vbr.bin";
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szFile; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "Bin Files (*.bin)\0*.bin\0All Files\0*.*\0";
    sfn.lpstrDefExt = "bin";
    if (GetSaveFileNameA(&sfn)) {
        FILE *f = fopen(szFile, "wb");
        if (f) {
            fwrite(g_vhd.img + g_vhd.data_offset + (g_vhd.parts[active_slot].lba_begin * 512), 1, 512, f);
            fclose(f);
            SetWindowTextA(g_hStatusBar, "Active VBR extracted successfully.");
        }
    }
}

static void cmd_replace_os_boot(HWND hwnd) {
    if (g_view_mode != 1) {
        MessageBoxA(hwnd, "Please mount a FAT partition first.", "Error", MB_ICONWARNING);
        return;
    }
    char target_name[32] = "IO.SYS";
    if (!ShowInputBox(hwnd, "Replace OS Boot File", "Target filename in current directory:", target_name)) return;

    u8 target_83[11];
    make_83_name(target_name, target_83);

    u8 sec[512];
    u32 cur = g_current_dir_cluster;
    int is_root16 = (g_fat_type == 16 && cur == 0);
    u32 sec_idx = 0, found_lba = 0, found_off = 0, old_clus = 0;

    while (!found_lba) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0) break;
            if (ent[0] == 0xE5 || (ent[11] & 0x0F) == 0x0F) continue;
            if (memcmp(ent, target_83, 11) == 0) {
                found_lba = lba;
                found_off = i;
                old_clus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
                break;
            }
        }
        if (found_lba) break;
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }

    if (!found_lba) {
        MessageBoxA(hwnd, "Target file not found in current directory.", "Error", MB_ICONERROR);
        return;
    }

    OPENFILENAMEA ofn = {0};
    char szHost[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szHost; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.lpstrTitle = "Select replacement file";
    if (!GetOpenFileNameA(&ofn)) return;

    FILE *f = fopen(szHost, "rb");
    if (!f) { MessageBoxA(hwnd, "Cannot open host file.", "Error", MB_ICONERROR); return; }
    fseek(f, 0, SEEK_END);
    u32 sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Free old clusters securely */
    if (old_clus >= 2) {
        u32 c = old_clus;
        u8 z[512] = {0};
        while (c >= 2 && c < 0x0FFFFFF0) {
            u32 n = read_fat(c);
            u32 clba = cluster_to_lba(c);
            for(u32 j=0; j<g_sec_per_clus; j++) write_sec(clba+j, z, 1);
            write_fat(c, 0);
            c = n;
        }
    }

    /* Write new file data */
    u32 first_clus = 0;
    if (sz > 0) {
        first_clus = alloc_cluster();
        if (!first_clus) { fclose(f); return; }
        u32 cur_clus = first_clus;
        u32 rem = sz;
        u8 buf[512];
        while (rem > 0) {
            u32 lba = cluster_to_lba(cur_clus);
            for (u32 i = 0; i < g_sec_per_clus && rem > 0; i++) {
                u32 chunk = rem > 512 ? 512 : rem;
                memset(buf, 0, 512);
                fread(buf, 1, chunk, f);
                write_sec(lba + i, buf, 1);
                rem -= chunk;
            }
            if (rem > 0) {
                u32 nclus = alloc_cluster();
                if (!nclus) break;
                write_fat(cur_clus, nclus);
                cur_clus = nclus;
            }
        }
    }
    fclose(f);

    /* Update existing directory entry precisely in place */
    read_sec(found_lba, sec, 1);
    u8 *ent = sec + found_off;
    wr16le(ent + 20, first_clus >> 16);
    wr16le(ent + 26, first_clus & 0xFFFF);
    wr32le(ent + 28, sz);
    write_sec(found_lba, sec, 1);

    fs_list(g_current_dir_cluster);
    populate_vhd_listview();
    SetWindowTextA(g_hStatusBar, "OS Boot file replaced successfully.");
}

static void cmd_write_mbr(HWND hwnd) {
    if (!g_vhd.isOpen || !g_vhd.img) return;
    if (MessageBoxA(hwnd, "Write standard Windows/DOS MBR? This will overwrite existing bootloader code, but preserve partitions.", "Write MBR", MB_YESNO | MB_ICONWARNING) != IDYES) return;

    static const u8 std_mbr[424] = {
        0xFA, 0x33, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8B, 0xF4, 0x50, 0x07, 0x50, 0x1F, 0xFB, 0xFC,
        0xBF, 0x00, 0x06, 0xB9, 0x00, 0x01, 0xF2, 0xA5, 0xEA, 0x1D, 0x06, 0x00, 0x00, 0xBE, 0xBE, 0x07,
        0xB3, 0x04, 0x80, 0x3C, 0x80, 0x74, 0x0E, 0x83, 0xC6, 0x10, 0xFE, 0xCB, 0x75, 0xF4, 0xCD, 0x18,
        0x8B, 0x14, 0x8B, 0x4C, 0x02, 0x8B, 0xEE, 0x83, 0xC6, 0x10, 0xFE, 0xCB, 0x74, 0x1A, 0x80, 0x3C,
        0x00, 0x74, 0xF4, 0xBE, 0x8B, 0x06, 0xAC, 0x3C, 0x00, 0x74, 0x0B, 0x56, 0xBB, 0x07, 0x00, 0xB4,
        0x0E, 0xCD, 0x10, 0x5E, 0xEB, 0xF0, 0xEB, 0xFE, 0xBF, 0x05, 0x00, 0xBB, 0x00, 0x7C, 0xB8, 0x01,
        0x02, 0xCD, 0x13, 0x73, 0x0C, 0x33, 0xC0, 0xCD, 0x13, 0x4F, 0x75, 0xED, 0xBE, 0xA3, 0x06, 0xEB,
        0xD3, 0xBE, 0xC2, 0x06, 0xBF, 0xFE, 0x7D, 0x81, 0x3D, 0x55, 0xAA, 0x75, 0xC7, 0x8B, 0xF5, 0xEA,
        0x00, 0x7C, 0x00, 0x00, 0x49, 0x6E, 0x76, 0x61, 0x6C, 0x69, 0x64, 0x20, 0x70, 0x61, 0x72, 0x74,
        0x69, 0x74, 0x69, 0x6F, 0x6E, 0x20, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x00, 0x45, 0x72, 0x72, 0x6F,
        0x72, 0x20, 0x6C, 0x6F, 0x61, 0x64, 0x69, 0x6E, 0x67, 0x20, 0x6F, 0x70, 0x65, 0x72, 0x61, 0x74,
        0x69, 0x6E, 0x67, 0x20, 0x73, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x00, 0x4D, 0x69, 0x73, 0x73, 0x69,
        0x6E, 0x67, 0x20, 0x6F, 0x70, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6E, 0x67, 0x20, 0x73, 0x79, 0x73,
        0x74, 0x65, 0x6D, 0x00
    };

    u8 *mbr = g_vhd.img + g_vhd.data_offset;
    memcpy(mbr, std_mbr, sizeof(std_mbr));
    SetWindowTextA(g_hStatusBar, "Standard MBR written. Save VHD to commit.");
}

static void cmd_write_vbr(HWND hwnd) {
    if (!g_vhd.isOpen || g_view_mode != 0) return;
    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= MAX_MBR_PARTS || !g_vhd.parts[sel].used) {
        MessageBoxA(hwnd, "Select a valid partition to inject the VBR into.", "Error", MB_ICONWARNING);
        return;
    }

    OPENFILENAMEA ofn = {0};
    char szBin[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szBin; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Bootsectors (*.bin)\0*.bin\0All Files\0*.*\0";
    if (!GetOpenFileNameA(&ofn)) return;

    FILE *f = fopen(szBin, "rb");
    if (!f) return;
    u8 vbr[512] = {0};
    fread(vbr, 1, 512, f);
    fclose(f);

    u8 *part_boot = g_vhd.img + g_vhd.data_offset + (g_vhd.parts[sel].lba_begin * 512);
    
    memcpy(part_boot, vbr, 11);
    
    if (g_vhd.parts[sel].type == 0x0B || g_vhd.parts[sel].type == 0x0C) { 
        memcpy(part_boot + 90, vbr + 90, 512 - 90 - 2); 
    } else { 
        memcpy(part_boot + 62, vbr + 62, 512 - 62 - 2); 
    }
    
    part_boot[510] = 0x55; part_boot[511] = 0xAA;
    SetWindowTextA(g_hStatusBar, "VBR injected successfully. Save VHD to commit.");
}

/* ============================================================ STANDARD COMMANDS */
static void cmd_vhd_open_dialog(HWND hwnd) {
    OPENFILENAMEA ofn = {0};
    char szFile[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "VHD Files (*.vhd)\0*.vhd\0All Files\0*.*\0";
    if (!GetOpenFileNameA(&ofn)) return;
    int res = vhd_open(szFile);
    if (res == 0) {
        UpdateMRU(szFile);
        populate_vhd_listview();
        char s[512]; snprintf(s, sizeof(s), "Opened: %s (%.1f MB)", szFile, g_vhd.cap / 1048576.0);
        SetWindowTextA(g_hStatusBar, s);
    } else if (res == -3) {
        MessageBoxA(hwnd, "Only fixed-size VHD images are supported.", "Unsupported VHD", MB_ICONERROR);
    } else {
        MessageBoxA(hwnd, "Failed to open a valid fixed VHD image.", "Error", MB_ICONERROR);
    }
}

static void cmd_new_vhd(HWND hwnd) {
    char buf[32] = "100";
    if (!ShowInputBox(hwnd, "New VHD", "Size in megabytes:", buf)) return;
    int mb = atoi(buf);
    if (mb < 1 || mb > 2040) { MessageBoxA(hwnd, "Size must be 1-2040 MB.", "New VHD", MB_ICONWARNING); return; }

    OPENFILENAMEA ofn = {0};
    char path[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "VHD Files (*.vhd)\0*.vhd\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "vhd";
    
    if (!GetSaveFileNameA(&ofn)) return;

    if (vhd_create(path, (u32)mb) != 0) { MessageBoxA(hwnd, "Failed to create VHD file.", "Error", MB_ICONERROR); return; }
    if (vhd_open(path) != 0) { MessageBoxA(hwnd, "Created but failed to reopen VHD.", "Error", MB_ICONERROR); return; }
    UpdateMRU(path);
    populate_vhd_listview();
    SetWindowTextA(g_hStatusBar, "New empty VHD created. Use Partition > Create to add one.");
}

static void cmd_save(HWND hwnd) {
    if (!g_vhd.isOpen) return;
    if (vhd_save() == 0) SetWindowTextA(g_hStatusBar, "VHD saved (footer checksum rebuilt).");
    else MessageBoxA(hwnd, "Failed to save VHD.", "Error", MB_ICONERROR);
}

static void cmd_resize(HWND hwnd) {
    char buf[32];
    if (!g_vhd.isOpen) return;
    snprintf(buf, sizeof(buf), "%u", (u32)(g_vhd.cap / 1048576));
    if (!ShowInputBox(hwnd, "Resize VHD Container", "New size in megabytes:", buf)) return;
    int mb = atoi(buf);
    int rc = vhd_resize((u32)mb);
    if (rc == 0) { populate_vhd_listview(); SetWindowTextA(g_hStatusBar, "VHD container resized. Data preserved."); }
    else if (rc == -2) MessageBoxA(hwnd, "Shrink refused: a partition extends beyond the new bounds.", "Resize", MB_ICONWARNING);
    else MessageBoxA(hwnd, "Resize failed (out of memory?).", "Resize", MB_ICONERROR);
}

static void cmd_extract_selected(HWND hwnd) {
    if (g_view_mode != 1) return;
    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
    if (sel < 0) return;
    char name[256];
    ListView_GetItemText(g_hVhdListView, sel, 0, name, sizeof(name));
    if (strcmp(name, "..") == 0) return;
    int eidx = -1;
    for(int k=0; k<g_fs_entry_count; k++) {
        if (strcmp(g_fs_entries[k].name, name) == 0) { eidx = k; break; }
    }
    if (eidx == -1 || g_fs_entries[eidx].is_directory) {
        MessageBoxA(hwnd, "Select a file to extract.", "Error", MB_ICONWARNING);
        return;
    }
    char dest[MAX_PATH];
    snprintf(dest, sizeof(dest), "%s\\%s", g_current_local_path, name);
    if (fs_extract(eidx, dest) == 0) {
        set_local_path(g_current_local_path);
        SetWindowTextA(g_hStatusBar, "Extracted successfully.");
    } else {
        MessageBoxA(hwnd, "Failed to extract file.", "Error", MB_ICONERROR);
    }
}

static void cmd_add_selected(HWND hwnd) {
    if (g_view_mode != 1) {
        MessageBoxA(hwnd, "Navigate into a FAT partition first.", "Error", MB_ICONWARNING);
        return;
    }
    int sel = ListView_GetNextItem(g_hLocalListView, -1, LVNI_SELECTED);
    if (sel < 0) return;
    char name[256];
    ListView_GetItemText(g_hLocalListView, sel, 0, name, sizeof(name));
    if (strcmp(name, "..") == 0) return;
    char src[MAX_PATH];
    snprintf(src, sizeof(src), "%s\\%s", g_current_local_path, name);
    
    if (import_recursive(src, g_current_dir_cluster) == 0) {
        fs_list(g_current_dir_cluster);
        populate_vhd_listview();
        SetWindowTextA(g_hStatusBar, "File(s) imported successfully.");
    } else {
        MessageBoxA(hwnd, "Failed to import some file(s).", "Error", MB_ICONERROR);
    }
}

static void cmd_delete_selected(HWND hwnd) {
    if (g_view_mode != 1) return;
    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
    if (sel < 0) return;
    char name[256];
    ListView_GetItemText(g_hVhdListView, sel, 0, name, sizeof(name));
    if (strcmp(name, "..") == 0) return;
    int eidx = -1;
    for(int k=0; k<g_fs_entry_count; k++) {
        if (strcmp(g_fs_entries[k].name, name) == 0) { eidx = k; break; }
    }
    if (eidx == -1) return;
    if (fs_delete(eidx) == 0) {
        fs_list(g_current_dir_cluster);
        populate_vhd_listview();
        SetWindowTextA(g_hStatusBar, "Deleted successfully (sectors & metadata zeroed).");
    } else {
        MessageBoxA(hwnd, "Failed to delete item.", "Error", MB_ICONERROR);
    }
}

/* ============================================================ WINDOW PROC */
static void init_listview_columns(HWND hLv) {
    LVCOLUMNA lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.cx = 350; lvc.pszText = (LPSTR)"Name";
    SendMessageA(hLv, LVM_INSERTCOLUMNA, 0, (LPARAM)&lvc);
    lvc.cx = 150; lvc.pszText = (LPSTR)"Size";
    SendMessageA(hLv, LVM_INSERTCOLUMNA, 1, (LPARAM)&lvc);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            HMENU hMenu = CreateMenu(), hFile = CreatePopupMenu();
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_NEW,   "&New...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_OPEN,  "&Open...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_SAVE,  "&Save\tCtrl+S");
            AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_CONVERT, "&Convert .img to .vhd...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_QEMU_BOOT, "Boot in &QEMU...");
            AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_QUIT,  "&Quit");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&File");

            HMENU hCreatePart = CreatePopupMenu();
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT12,  "FAT12 (0x01)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16_S,"FAT16 <32MB (0x04)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16,  "FAT16 >32MB (0x06)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32,  "FAT32 CHS (0x0B)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32L, "FAT32 LBA (0x0C)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16L, "FAT16 LBA (0x0E)");

            HMENU hPart = CreatePopupMenu();
            AppendMenuA(hPart, MF_STRING, IDM_PART_LIST,   "&Properties");
            AppendMenuA(hPart, MF_POPUP, (UINT_PTR)hCreatePart, "&Create FAT Partition");
            AppendMenuA(hPart, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hPart, MF_STRING, IDM_PART_COMPACT, "&Compact (Zero Free Space)");
            AppendMenuA(hPart, MF_STRING, IDM_PART_ACTIVE, "Set &Active (Bootable)");
            AppendMenuA(hPart, MF_STRING, IDM_PART_RESIZE, "&Resize Partition...");
            AppendMenuA(hPart, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hPart, MF_STRING, IDM_PART_VBR_FILE, "Write &VBR from File...");
            AppendMenuA(hPart, MF_STRING, IDM_PART_REPLACE_BOOT, "Replace OS &Boot File...");
            AppendMenuA(hPart, MF_STRING, IDM_PART_FORMAT, "&Format (FAT)...");
            AppendMenuA(hPart, MF_STRING, IDM_PART_DELETE, "&Delete Partition...");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hPart, "&Partition");

            HMENU hDisk = CreatePopupMenu();
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_EXTRACT_MBR, "Extract &MBR...");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_EXTRACT_VBR, "Extract Active &VBR...");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_MBR_STD, "Write &Standard MBR");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_RESIZE, "&Resize Disk Container...");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_TRIM,   "&Trim VHD to Last Partition");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hDisk, "&Disk");

            HMENU hHelp = CreatePopupMenu();
            AppendMenuA(hHelp, MF_STRING, IDM_HELP_ABOUT, "&About");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");
            SetMenu(hwnd, hMenu);

            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS };
            InitCommonControlsEx(&icc);

            g_hLocalListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hVhdListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            init_listview_columns(g_hLocalListView);
            init_listview_columns(g_hVhdListView);
            g_hStatusBar = CreateWindowExA(0, STATUSCLASSNAMEA, "Ready. File > New to create a VHD.",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hProgressBar = CreateWindowExA(0, PROGRESS_CLASSA, NULL, WS_CHILD | PBS_SMOOTH,
                0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hCancelBtn = CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, (HMENU)2010, g_hInstance, NULL);

            DragAcceptFiles(hwnd, TRUE);

            char root[MAX_PATH];
            if (!GetEnvironmentVariableA("USERPROFILE", root, MAX_PATH)) strcpy(root, "C:\\");
            set_local_path(root);
            return 0;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right, half = rc.bottom / 2;
            SetWindowPos(g_hLocalListView, NULL, 0, 0, w, half, SWP_NOZORDER);
            SetWindowPos(g_hVhdListView,   NULL, 0, half, w, rc.bottom - half - 24, SWP_NOZORDER);
            SetWindowPos(g_hStatusBar,     NULL, 0, rc.bottom - 24, w, 24, SWP_NOZORDER);
            
            SetWindowPos(g_hProgressBar, NULL, w - 210, rc.bottom - 20, 150, 16, SWP_NOZORDER);
            SetWindowPos(g_hCancelBtn, NULL, w - 55, rc.bottom - 21, 50, 18, SWP_NOZORDER);
            return 0;
        }
        case WM_DROPFILES: {
            if (g_view_mode != 1) {
                MessageBoxA(hwnd, "Please navigate into a FAT partition to drop files.", "Warning", MB_ICONWARNING);
                DragFinish((HDROP)wParam);
                break;
            }
            HDROP hDrop = (HDROP)wParam;
            UINT numFiles = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < numFiles; i++) {
                char path[MAX_PATH];
                DragQueryFileA(hDrop, i, path, MAX_PATH);
                import_recursive(path, g_current_dir_cluster);
            }
            DragFinish(hDrop);
            fs_list(g_current_dir_cluster);
            populate_vhd_listview();
            SetWindowTextA(g_hStatusBar, "Dropped files imported successfully.");
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR nmh = (LPNMHDR)lParam;
            if (nmh->idFrom == GetDlgCtrlID(g_hVhdListView) && nmh->code == NM_DBLCLK) {
                LPNMITEMACTIVATE lpnm = (LPNMITEMACTIVATE)lParam;
                if (lpnm->iItem >= 0) {
                    if (g_view_mode == 0) {
                        if (g_vhd.parts[lpnm->iItem].used) {
                            if (fs_mount(lpnm->iItem) == 0) {
                                g_view_mode = 1;
                                fs_list(g_current_dir_cluster);
                                populate_vhd_listview();
                            } else {
                                MessageBoxA(hwnd, "Failed to mount partition. (Not FAT16/32 or unformatted)", "Error", MB_ICONERROR);
                            }
                        }
                    } else {
                        char name[256];
                        ListView_GetItemText(g_hVhdListView, lpnm->iItem, 0, name, sizeof(name));
                        if (strcmp(name, "..") == 0) {
                            int is_root = (g_current_dir_cluster == g_root_cluster || (g_fat_type == 16 && g_current_dir_cluster == 0));
                            if (is_root) {
                                g_view_mode = 0; g_vhd.fs_mounted = 0;
                                populate_vhd_listview();
                            } else {
                                u32 pclus = 0;
                                for(int k=0; k<g_fs_entry_count; k++) {
                                    if (strcmp(g_fs_entries[k].name, "..") == 0) {
                                        pclus = g_fs_entries[k].first_cluster;
                                        if (pclus == 0 && g_fat_type == 32) pclus = g_root_cluster;
                                        break;
                                    }
                                }
                                g_current_dir_cluster = pclus;
                                fs_list(g_current_dir_cluster);
                                populate_vhd_listview();
                            }
                        } else {
                            for (int k = 0; k < g_fs_entry_count; k++) {
                                if (strcmp(g_fs_entries[k].name, name) == 0 && g_fs_entries[k].is_directory) {
                                    g_current_dir_cluster = g_fs_entries[k].first_cluster;
                                    fs_list(g_current_dir_cluster);
                                    populate_vhd_listview();
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            if (nmh->idFrom == GetDlgCtrlID(g_hLocalListView) && nmh->code == NM_DBLCLK) {
                LPNMITEMACTIVATE lpnm = (LPNMITEMACTIVATE)lParam;
                if (lpnm->iItem >= 0) {
                    char name[256], type[64];
                    ListView_GetItemText(g_hLocalListView, lpnm->iItem, 0, name, sizeof(name));
                    ListView_GetItemText(g_hLocalListView, lpnm->iItem, 1, type, sizeof(type));
                    if (strcmp(type, "<DIR>") == 0) {
                        if (strcmp(name, "..") == 0) {
                            char *p = strrchr(g_current_local_path, '\\');
                            if (p && p != g_current_local_path) {
                                *p = '\0';
                                if (g_current_local_path[0] != '\0' && g_current_local_path[1] == ':' && g_current_local_path[2] == '\0')
                                    strcat(g_current_local_path, "\\");
                            }
                        } else {
                            if (g_current_local_path[strlen(g_current_local_path)-1] != '\\') strcat(g_current_local_path, "\\");
                            strcat(g_current_local_path, name);
                        }
                        set_local_path(g_current_local_path);
                    }
                }
            }
            break;
        }
        case WM_CONTEXTMENU: {
            HWND hwndCtl = (HWND)wParam;
            POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
            if (hwndCtl == g_hVhdListView) {
                HMENU hMenu = CreatePopupMenu();
                if (g_view_mode == 1) {
                    AppendMenuA(hMenu, MF_STRING, ID_VHD_EXTRACT, "Extract File");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_REPLACE_BOOT, "Replace OS Boot File");
                    AppendMenuA(hMenu, MF_STRING, ID_VHD_DELETE, "Delete File");
                } else {
                    HMENU hCreatePart = CreatePopupMenu();
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT12,  "FAT12 (0x01)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16_S,"FAT16 <32MB (0x04)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16,  "FAT16 >32MB (0x06)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32,  "FAT32 CHS (0x0B)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32L, "FAT32 LBA (0x0C)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16L, "FAT16 LBA (0x0E)");
                    
                    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hCreatePart, "Create FAT Partition");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_FORMAT, "Format (FAT)");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_COMPACT, "Compact (Zero Free Space)");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_ACTIVE, "Set Active");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_RESIZE, "Resize Partition");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_VBR_FILE, "Write VBR from File");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_DELETE, "Delete");
                }
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            } else if (hwndCtl == g_hLocalListView) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_VHD_ADD, "Add to VHD");
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) >= IDM_MRU_1 && LOWORD(wParam) < IDM_MRU_1 + 5) {
                int idx = LOWORD(wParam) - IDM_MRU_1;
                if (strlen(g_mru[idx]) > 0 && vhd_open(g_mru[idx]) == 0) {
                    populate_vhd_listview();
                    SetWindowTextA(g_hStatusBar, g_mru[idx]);
                }
                return 0;
            }
            switch (LOWORD(wParam)) {
                case 2010: g_cancel_operation = TRUE; break;
                case IDM_IMAGE_NEW:   cmd_new_vhd(hwnd); break;
                case IDM_IMAGE_OPEN:  cmd_vhd_open_dialog(hwnd); break;
                case IDM_IMAGE_SAVE:  cmd_save(hwnd); break;
                case IDM_IMAGE_QUIT:  PostQuitMessage(0); break;
                
                case IDM_IMAGE_CONVERT:   cmd_convert_img(hwnd); break;
                case IDM_IMAGE_QEMU_BOOT: cmd_qemu_boot(hwnd); break;

                case IDM_PART_LIST:   if (g_vhd.isOpen) part_show_properties(hwnd); break;
                case IDM_PART_CREATE_FAT12:  if (g_vhd.isOpen) { part_create_fat(hwnd, 0x01); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT16_S:if (g_vhd.isOpen) { part_create_fat(hwnd, 0x04); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT16:  if (g_vhd.isOpen) { part_create_fat(hwnd, 0x06); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT32:  if (g_vhd.isOpen) { part_create_fat(hwnd, 0x0B); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT32L: if (g_vhd.isOpen) { part_create_fat(hwnd, 0x0C); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT16L: if (g_vhd.isOpen) { part_create_fat(hwnd, 0x0E); populate_vhd_listview(); } break;
                
                case IDM_PART_DELETE: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        if (MessageBoxA(hwnd, "Are you sure you want to delete this partition?", "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            part_delete(hwnd, sel);
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition to delete.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_FORMAT: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        if (MessageBoxA(hwnd, "Format this partition as FAT? All data will be lost.", "Confirm Format", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            if (fs_format_partition(sel, 0) == 0) {
                                populate_vhd_listview();
                                SetWindowTextA(g_hStatusBar, "Formatted partition successfully.");
                            } else {
                                MessageBoxA(hwnd, "Failed to format partition.", "Error", MB_ICONERROR);
                            }
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition to format.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_ACTIVE: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        for (int i = 0; i < MAX_MBR_PARTS; i++) g_vhd.parts[i].boot = (i == sel) ? 0x80 : 0x00;
                        update_mbr_in_ram();
                        populate_vhd_listview();
                        SetWindowTextA(g_hStatusBar, "Active (Bootable) partition updated.");
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_COMPACT: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        if (fs_mount(sel) == 0) {
                            ShowProgress(TRUE);
                            u8 z[512] = {0};
                            for (u32 i = 2; i <= g_total_clusters + 1; i++) {
                                if (read_fat(i) == 0) {
                                    u32 lba = cluster_to_lba(i);
                                    for(u32 j=0; j<g_sec_per_clus; j++) write_sec(lba+j, z, 1);
                                }
                                if (i % 100 == 0) UpdateProgress((int)((i * 100) / g_total_clusters));
                            }
                            ShowProgress(FALSE);
                            g_vhd.fs_mounted = 0;
                            SetWindowTextA(g_hStatusBar, "Partition free space zeroed (Compacted).");
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition to compact.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_RESIZE: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        u32 max_secs = (u32)(g_vhd.cap / 512) - g_vhd.parts[sel].lba_begin;
                        for (int i = 0; i < MAX_MBR_PARTS; i++) {
                            if (i != sel && g_vhd.parts[i].used && g_vhd.parts[i].lba_begin >= g_vhd.parts[sel].lba_begin) {
                                u32 gap = g_vhd.parts[i].lba_begin - g_vhd.parts[sel].lba_begin;
                                if (gap < max_secs) max_secs = gap;
                            }
                        }
                        
                        char buf[32];
                        u32 max_mb = max_secs / 2048; 
                        u32 cur_mb = g_vhd.parts[sel].lba_count / 2048;
                        snprintf(buf, sizeof(buf), "%u", cur_mb);
                        
                        char prompt[256];
                        snprintf(prompt, sizeof(prompt), "New size in MB (Max %u MB):", max_mb);
                        if (ShowInputBox(hwnd, "Resize Partition", prompt, buf)) {
                            u32 new_mb = atoi(buf);
                            if (new_mb >= 1 && new_mb <= max_mb) {
                                u32 new_lba = new_mb * 2048;
                                
                                if (new_lba < g_vhd.parts[sel].lba_count) {
                                    if (fs_mount(sel) == 0) {
                                        u32 data_sectors_new = 0;
                                        if (new_lba > (g_data_lba - g_vhd.fs_part_lba)) {
                                            data_sectors_new = new_lba - (g_data_lba - g_vhd.fs_part_lba);
                                        }
                                        u32 max_cluster = data_sectors_new / g_sec_per_clus + 2;
                                        if (new_lba <= (g_data_lba - g_vhd.fs_part_lba)) max_cluster = 2;
                                        
                                        g_lost_log[0] = '\0';
                                        int lost_count = 0;
                                        scan_dir_for_lost(g_root_cluster, max_cluster, "\\", g_lost_log, &lost_count);
                                        
                                        if (lost_count > 0) {
                                            if (!ShowLostFilesDialog(hwnd)) {
                                                g_vhd.fs_mounted = 0;
                                                break; 
                                            }
                                            delete_lost_items(g_root_cluster, max_cluster);
                                        }
                                        
                                        u8 bpb[512];
                                        read_sec(g_vhd.parts[sel].lba_begin, bpb, 1);
                                        if (new_lba < 65536) {
                                            wr16le(bpb + 19, (u16)new_lba);
                                            wr32le(bpb + 32, 0);
                                        } else {
                                            wr16le(bpb + 19, 0);
                                            wr32le(bpb + 32, new_lba);
                                        }
                                        write_sec(g_vhd.parts[sel].lba_begin, bpb, 1);
                                    }
                                    g_vhd.fs_mounted = 0; 
                                }
                                
                                g_vhd.parts[sel].lba_count = new_lba;
                                update_mbr_in_ram();
                                populate_vhd_listview();
                                SetWindowTextA(g_hStatusBar, "Partition resized. Save VHD to commit.");
                            } else {
                                MessageBoxA(hwnd, "Invalid size or exceeds available space.", "Error", MB_ICONERROR);
                            }
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a partition to resize.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_VBR_FILE:   cmd_write_vbr(hwnd); break;
                case IDM_PART_REPLACE_BOOT: cmd_replace_os_boot(hwnd); break;
                
                case IDM_DISK_MBR_STD:    cmd_write_mbr(hwnd); break;
                case IDM_DISK_RESIZE:     cmd_resize(hwnd); break;
                case IDM_DISK_EXTRACT_MBR:cmd_extract_mbr(hwnd); break;
                case IDM_DISK_EXTRACT_VBR:cmd_extract_vbr(hwnd); break;

                case IDM_DISK_TRIM: {
                    if (!g_vhd.isOpen) break;
                    u32 highest = 0;
                    for(int i=0; i<MAX_MBR_PARTS; i++) {
                        if(g_vhd.parts[i].used) {
                            u32 end = g_vhd.parts[i].lba_begin + g_vhd.parts[i].lba_count;
                            if(end > highest) highest = end;
                        }
                    }
                    if (highest == 0) highest = 2048;
                    u32 new_mb = (highest / 2048) + 1; 
                    if (vhd_resize(new_mb) == 0) {
                        populate_vhd_listview();
                        SetWindowTextA(g_hStatusBar, "VHD Trimmed to fit partitions exactly.");
                    } else {
                        MessageBoxA(hwnd, "Failed to trim VHD.", "Error", MB_ICONERROR);
                    }
                    break;
                }

                case ID_VHD_EXTRACT:  cmd_extract_selected(hwnd); break;
                case ID_VHD_DELETE:   cmd_delete_selected(hwnd); break;
                case ID_VHD_ADD:      cmd_add_selected(hwnd); break;
                
                case IDM_HELP_ABOUT:
                    MessageBoxA(hwnd, APP_NAME " " APP_VERSION
                        "\n\nImplemented: VHD container, MBR partitions, File Extract/Import/Drop, Resize."
                        "\nImplemented: FAT16/32 basic read, format, and add/extract engine, Bootsector Injection.",
                        "About", MB_ICONINFORMATION);
                    break;
            }
            return 0;
        }
        case WM_DESTROY:
            SaveSettings();
            vhd_close();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
    (void)hPrev; (void)cmdline;
    g_hInstance = hInst;
    WNDCLASSA wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "VhdMasterClass";
    if (!RegisterClassA(&wc)) return 1;

    g_hMainWnd = CreateWindowExA(0, "VhdMasterClass", APP_NAME " " APP_VERSION,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInst, NULL);
    LoadSettings();
    ShowWindow(g_hMainWnd, show);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    return (int)msg.wParam;
}