/*
 * cbak_standalone.c - Standalone CBAK Backup and Restore
 * Implements Physical Drive Hybrid Backup and Restore with Exclusions.
 * 
 * Compile: gcc -Os -s -mwindows -o cbak_standalone.exe cbak_standalone.c -lcomctl32 -lcomdlg32 -ladvapi32
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <windowsx.h>
#include <aclapi.h>
#include <sddl.h>

#define APP_NAME        "CBAK Utility"
#define APP_VERSION     "1.0"
#define WINDOW_WIDTH    400
#define WINDOW_HEIGHT   200

#define ID_BTN_BACKUP   1001
#define ID_BTN_RESTORE  1002
#define ID_BTN_CANCEL   2010

#define COMPRESS_ALGORITHM_XPRESS 3

/* ============================================================ TYPEDEFS */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed long long   s64;

typedef struct {
    char prefix[128];
    char suffix[128];
    int has_wildcard;
    int is_all;
} CloneExclusion;

typedef PVOID COMPRESSOR_HANDLE;
typedef PVOID DECOMPRESSOR_HANDLE;
typedef BOOL (WINAPI *CreateCompressor_t)(DWORD, PVOID, COMPRESSOR_HANDLE*);
typedef BOOL (WINAPI *Compress_t)(COMPRESSOR_HANDLE, LPCVOID, SIZE_T, PVOID, SIZE_T, PSIZE_T);
typedef BOOL (WINAPI *CloseCompressor_t)(COMPRESSOR_HANDLE);
typedef BOOL (WINAPI *CreateDecompressor_t)(DWORD, PVOID, DECOMPRESSOR_HANDLE*);
typedef BOOL (WINAPI *Decompress_t)(DECOMPRESSOR_HANDLE, LPCVOID, SIZE_T, PVOID, SIZE_T, PSIZE_T);
typedef BOOL (WINAPI *CloseDecompressor_t)(DECOMPRESSOR_HANDLE);

typedef BOOL (WINAPI *ConvertSecurityDescriptorToStringSecurityDescriptorW_t)(
    PSECURITY_DESCRIPTOR, DWORD, SECURITY_INFORMATION, LPWSTR *, PULONG);
typedef HANDLE (WINAPI *FindFirstStreamW_t)(LPCWSTR, STREAM_INFO_LEVELS, LPVOID, DWORD);
typedef BOOL (WINAPI *FindNextStreamW_t)(HANDLE, LPVOID);

/* ============================================================ GLOBALS */
HWND g_hMainWnd = NULL;
HWND g_hStatusBar = NULL, g_hProgressBar = NULL, g_hCancelBtn = NULL;
HINSTANCE g_hInstance = NULL;

HWND g_hCombo;
HWND g_hExclusionEdit = NULL;
HWND g_hCheckCompress = NULL;

int g_combo_sel_data = -1;
int g_combo_compress = 0;
char g_clone_exclusions[1024] = {0};

volatile BOOL g_cancel_operation = FALSE;
static int g_last_percent = -1;

static CreateCompressor_t pCreateCompressor = NULL;
static Compress_t pCompress = NULL;
static CloseCompressor_t pCloseCompressor = NULL;
static CreateDecompressor_t pCreateDecompressor = NULL;
static Decompress_t pDecompress = NULL;
static CloseDecompressor_t pCloseDecompressor = NULL;

static ConvertSecurityDescriptorToStringSecurityDescriptorW_t pConvertSDToStringSD = NULL;
static FindFirstStreamW_t pFindFirstStreamW = NULL;
static FindNextStreamW_t pFindNextStreamW = NULL;

/* ============================================================ HELPERS */
static u32 rd32le(const u8 *p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }

static char* stristr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (const char* p = haystack; *p; p++) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)*needle)) {
            const char* h = p;
            const char* n = needle;
            while (*n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
            if (!*n) return (char*)p;
        }
    }
    return NULL;
}

static void format_size(u64 bytes, char* buffer, int buf_size) {
    if (bytes >= 1073741824ULL)      snprintf(buffer, buf_size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576ULL)    snprintf(buffer, buf_size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024ULL)       snprintf(buffer, buf_size, "%.2f KB", bytes / 1024.0);
    else                             snprintf(buffer, buf_size, "%I64u B", bytes);
}

static long long FileTimeToEpoch(FILETIME ft) {
    LARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return (li.QuadPart / 10000000ULL) - 11644473600ULL;
}

void PumpMessages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
}

void ShowProgress(BOOL show) {
    ShowWindow(g_hProgressBar, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hCancelBtn, show ? SW_SHOW : SW_HIDE);
    if (show) {
        SetWindowPos(g_hProgressBar, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowPos(g_hCancelBtn, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SendMessageA(g_hProgressBar, PBM_SETPOS, 0, 0);
    }
    g_cancel_operation = FALSE;
    g_last_percent = -1;
}

void UpdateProgress(int percent) {
    if (percent != g_last_percent) {
        SendMessageA(g_hProgressBar, PBM_SETPOS, percent, 0);
        g_last_percent = percent;
    }
    PumpMessages();
}

static void init_hybrid_apis(void) {
    if (!pConvertSDToStringSD) {
        HMODULE hAdvapi = LoadLibraryA("advapi32.dll");
        if (hAdvapi) pConvertSDToStringSD = (ConvertSecurityDescriptorToStringSecurityDescriptorW_t)GetProcAddress(hAdvapi, "ConvertSecurityDescriptorToStringSecurityDescriptorW");
        
        HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
        if (hKernel32) {
            pFindFirstStreamW = (FindFirstStreamW_t)GetProcAddress(hKernel32, "FindFirstStreamW");
            pFindNextStreamW = (FindNextStreamW_t)GetProcAddress(hKernel32, "FindNextStreamW");
        }
    }
}

static int init_compression(void) {
    if (!pCreateCompressor) {
        HMODULE hCab = LoadLibraryA("cabinet.dll");
        if (hCab) {
            pCreateCompressor = (CreateCompressor_t)GetProcAddress(hCab, "CreateCompressor");
            pCompress = (Compress_t)GetProcAddress(hCab, "Compress");
            pCloseCompressor = (CloseCompressor_t)GetProcAddress(hCab, "CloseCompressor");
            pCreateDecompressor = (CreateDecompressor_t)GetProcAddress(hCab, "CreateDecompressor");
            pDecompress = (Decompress_t)GetProcAddress(hCab, "Decompress");
            pCloseDecompressor = (CloseDecompressor_t)GetProcAddress(hCab, "CloseDecompressor");
        }
    }
    return pCreateCompressor != NULL;
}

/* ============================================================ DIALOGS */
LRESULT CALLBACK ComboDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == 1) { 
                int sel = SendMessageA(g_hCombo, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_combo_sel_data = SendMessageA(g_hCombo, CB_GETITEMDATA, sel, 0);
                    if (g_hExclusionEdit) {
                        GetWindowTextA(g_hExclusionEdit, g_clone_exclusions, sizeof(g_clone_exclusions));
                    }
                    if (g_hCheckCompress) {
                        g_combo_compress = (SendMessageA(g_hCheckCompress, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    }
                } else g_combo_sel_data = -1;
                DestroyWindow(hwnd); 
            }
            else if (LOWORD(wp) == 2) { g_combo_sel_data = -1; DestroyWindow(hwnd); }
            break;
        case WM_CLOSE: g_combo_sel_data = -1; DestroyWindow(hwnd); break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

BOOL ShowDriveSelectBox(HWND parent, char* out_drive, char* out_exclusions, int* out_compress) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ComboDlgProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "CbakComboDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "CbakComboDlgClass", "Select Physical Drive",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 350, 270,
        parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", "Select source drive (Requires Admin):", WS_CHILD | WS_VISIBLE, 10, 10, 310, 20, hDlg, NULL, g_hInstance, NULL);
    
    g_hCombo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, 30, 310, 200, hDlg, NULL, g_hInstance, NULL);
        
    CreateWindowExA(0, "STATIC", "Exclusions CSV (e.g. \\Windows\\, *.log, *.*):", WS_CHILD | WS_VISIBLE, 10, 60, 310, 20, hDlg, NULL, g_hInstance, NULL);
    
    g_hExclusionEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", 
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL,
        10, 80, 310, 100, hDlg, NULL, g_hInstance, NULL);
    
    g_hCheckCompress = NULL; 
    
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 160, 190, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 245, 190, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);

    int count = 0;
    for (int i = 0; i < 32; i++) {
        char path[64]; snprintf(path, 64, "\\\\.\\PhysicalDrive%d", i);
        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            GET_LENGTH_INFORMATION gli; DWORD ret;
            if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &gli, sizeof(gli), &ret, NULL)) {
                char display[128]; char sz[64];
                format_size(gli.Length.QuadPart, sz, sizeof(sz));
                snprintf(display, sizeof(display), "PhysicalDrive%d (%s)", i, sz);
                int idx = SendMessageA(g_hCombo, CB_ADDSTRING, 0, (LPARAM)display);
                SendMessageA(g_hCombo, CB_SETITEMDATA, idx, i);
                count++;
            }
            CloseHandle(h);
        }
    }
    if (count == 0) {
        int idx = SendMessageA(g_hCombo, CB_ADDSTRING, 0, (LPARAM)"No drives found (Run as Admin?)");
        SendMessageA(g_hCombo, CB_SETITEMDATA, idx, -1);
    }
    SendMessageA(g_hCombo, CB_SETCURSEL, 0, 0);

    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    
    if (g_combo_sel_data != -1) {
        snprintf(out_drive, 64, "\\\\.\\PhysicalDrive%d", g_combo_sel_data);
        if (out_exclusions) strcpy(out_exclusions, g_clone_exclusions);
        if (out_compress) *out_compress = 1; 
        return TRUE;
    }
    return FALSE;
}

BOOL ShowRestoreDriveSelectBox(HWND parent, char* out_drive) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ComboDlgProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "CbakComboDlgClass"; 
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "CbakComboDlgClass", "Select Target Physical Drive",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 350, 140,
        parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", "Select DESTINATION drive (WARNING: Overwritten):", WS_CHILD | WS_VISIBLE, 10, 10, 320, 20, hDlg, NULL, g_hInstance, NULL);
    
    g_hCombo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, 35, 310, 200, hDlg, NULL, g_hInstance, NULL);
        
    g_hExclusionEdit = NULL;
    g_hCheckCompress = NULL;
    
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 160, 70, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 245, 70, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);

    int count = 0;
    for (int i = 0; i < 32; i++) {
        char path[64]; snprintf(path, 64, "\\\\.\\PhysicalDrive%d", i);
        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            GET_LENGTH_INFORMATION gli; DWORD ret;
            if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &gli, sizeof(gli), &ret, NULL)) {
                char display[128]; char sz[64];
                format_size(gli.Length.QuadPart, sz, sizeof(sz));
                snprintf(display, sizeof(display), "PhysicalDrive%d (%s)", i, sz);
                int idx = SendMessageA(g_hCombo, CB_ADDSTRING, 0, (LPARAM)display);
                SendMessageA(g_hCombo, CB_SETITEMDATA, idx, i);
                count++;
            }
            CloseHandle(h);
        }
    }
    if (count == 0) {
        int idx = SendMessageA(g_hCombo, CB_ADDSTRING, 0, (LPARAM)"No drives found.");
        SendMessageA(g_hCombo, CB_SETITEMDATA, idx, -1);
    }
    SendMessageA(g_hCombo, CB_SETCURSEL, 0, 0);

    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    
    if (g_combo_sel_data != -1) {
        snprintf(out_drive, 64, "\\\\.\\PhysicalDrive%d", g_combo_sel_data);
        return TRUE;
    }
    return FALSE;
}

/* ============================================================ BACKUP / RESTORE ENGINE */
static void TraverseAndBackup(LPCWSTR rootPath, LPCWSTR currentDir, HANDLE hArchiveOut, COMPRESSOR_HANDLE hCompressor, 
                              HANDLE hMetadataOut, CloneExclusion* exclusions, int ex_count, u64* total_copied,
                              PUCHAR file_buf, PUCHAR comp_buf) {
    
    WCHAR searchPath[MAX_PATH];
    swprintf(searchPath, MAX_PATH, L"%ls%ls\\*", rootPath, currentDir);
    
    WIN32_FIND_DATAW fdw;
    HANDLE hFind = FindFirstFileW(searchPath, &fdw);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (g_cancel_operation) break;
        if (wcscmp(fdw.cFileName, L".") == 0 || wcscmp(fdw.cFileName, L"..") == 0) continue;
        
        WCHAR relPath[MAX_PATH];
        if (wcslen(currentDir) > 0) swprintf(relPath, MAX_PATH, L"%ls\\%ls", currentDir, fdw.cFileName);
        else swprintf(relPath, MAX_PATH, L"\\%ls", fdw.cFileName); 
        
        WCHAR fullPath[MAX_PATH];
        swprintf(fullPath, MAX_PATH, L"%ls%ls", rootPath, relPath);
        
        /* Apply Exclusions */
        int exclude = 0;
        char mbRelPath[1024]; 
        WideCharToMultiByte(CP_UTF8, 0, relPath, -1, mbRelPath, 1024, NULL, NULL);
        
        for (int i = 0; i < ex_count; i++) {
            if (exclusions[i].is_all) { exclude = 1; break; }
            if (exclusions[i].has_wildcard) {
                char* p1 = stristr(mbRelPath, exclusions[i].prefix);
                char* p2 = stristr(mbRelPath, exclusions[i].suffix);
                if (p1 && p2 && p2 >= p1) { exclude = 1; break; }
            } else {
                if (stristr(mbRelPath, exclusions[i].prefix)) { exclude = 1; break; }
            }
        }
        if (exclude) continue;
        
        int can_read = 1;
        HANDLE hFile = CreateFileW(fullPath, GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            hFile = CreateFileW(fullPath, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            can_read = 0;
        }

        if (hFile != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION bhfi;
            if (GetFileInformationByHandle(hFile, &bhfi)) {
                LARGE_INTEGER mftId;
                mftId.LowPart = bhfi.nFileIndexLow;
                mftId.HighPart = bhfi.nFileIndexHigh;
                
                LPWSTR sddl = NULL;
                PSECURITY_DESCRIPTOR pSD = NULL;
                if (GetNamedSecurityInfoW(fullPath, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &pSD) == ERROR_SUCCESS) {
                    if (pConvertSDToStringSD) pConvertSDToStringSD(pSD, SDDL_REVISION_1, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &sddl, NULL);
                }
                
                WCHAR streams[128] = L"";
                if (pFindFirstStreamW) {
                    WIN32_FIND_STREAM_DATA fsd;
                    HANDLE hStream = pFindFirstStreamW(fullPath, FindStreamInfoStandard, &fsd, 0);
                    if (hStream != INVALID_HANDLE_VALUE) {
                        do {
                            if (wcscmp(fsd.cStreamName, L"::$DATA") != 0) {
                                WCHAR streamEntry[64];
                                swprintf(streamEntry, 64, L"%ls=%llu;", fsd.cStreamName, fsd.StreamSize.QuadPart);
                                if (wcslen(streams) + wcslen(streamEntry) < 127) wcscat(streams, streamEntry);
                            }
                        } while (pFindNextStreamW(hStream, &fsd));
                        FindClose(hStream);
                    }
                }
                
                LPCWSTR outPath = fullPath;
                if (wcslen(fullPath) >= 2 && fullPath[1] == L':') outPath = fullPath + 2;

                WCHAR metaLine[1024];
                int lineLen = swprintf(metaLine, 1024, L"%016llu|%011lld|%011lld|%011lld|%010lu|%04lu|%-40.40ls|%-100.100ls|%-260.260ls\r\n",
                    mftId.QuadPart, 
                    FileTimeToEpoch(bhfi.ftCreationTime), 
                    FileTimeToEpoch(bhfi.ftLastWriteTime), 
                    FileTimeToEpoch(bhfi.ftLastAccessTime),
                    bhfi.dwFileAttributes, 
                    bhfi.nNumberOfLinks, 
                    wcslen(streams) > 0 ? streams : L"NONE", 
                    sddl ? sddl : L"NO_SDDL", 
                    outPath
                );
                
                char utf8Line[2048];
                int mbLen = WideCharToMultiByte(CP_UTF8, 0, metaLine, lineLen, utf8Line, sizeof(utf8Line), NULL, NULL);
                DWORD bwMeta;
                WriteFile(hMetadataOut, utf8Line, mbLen, &bwMeta, NULL);
                
                if (sddl) LocalFree(sddl);
                if (pSD) LocalFree(pSD);
                
                if (!(bhfi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    u64 fileSize = ((u64)bhfi.nFileSizeHigh << 32) | bhfi.nFileSizeLow;
                    if (!can_read) fileSize = 0; 
                    
                    u16 pathLen = (u16)(strlen(mbRelPath) + 1);
                    DWORD bw;
                    WriteFile(hArchiveOut, "FILE", 4, &bw, NULL);
                    WriteFile(hArchiveOut, &pathLen, 2, &bw, NULL);
                    WriteFile(hArchiveOut, mbRelPath, pathLen, &bw, NULL);
                    WriteFile(hArchiveOut, &fileSize, 8, &bw, NULL);
                    
                    if (can_read && fileSize > 0) {
                        DWORD bytesRead;
                        while (ReadFile(hFile, file_buf, 1048576, &bytesRead, NULL) && bytesRead > 0) {
                            if (g_cancel_operation) break;
                            SIZE_T comp_size = 0;
                            BOOL success = pCompress(hCompressor, file_buf, bytesRead, comp_buf, bytesRead + 4096, &comp_size);
                            
                            if (success && comp_size < bytesRead) {
                                u32 cSize = (u32)comp_size;
                                WriteFile(hArchiveOut, &cSize, 4, &bw, NULL);
                                WriteFile(hArchiveOut, comp_buf, cSize, &bw, NULL);
                            } else {
                                u32 cSize = (u32)bytesRead;
                                WriteFile(hArchiveOut, &cSize, 4, &bw, NULL);
                                WriteFile(hArchiveOut, file_buf, bytesRead, &bw, NULL);
                            }
                            *total_copied += bytesRead;
                            
                            char status_buf[256];
                            snprintf(status_buf, sizeof(status_buf), "Zipping: %s", mbRelPath);
                            SetWindowTextA(g_hStatusBar, status_buf);
                            PumpMessages();
                        }
                        u32 eof = 0; WriteFile(hArchiveOut, &eof, 4, &bw, NULL);
                    }
                }
            }
            CloseHandle(hFile);
        }
        
        if ((fdw.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fdw.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            TraverseAndBackup(rootPath, relPath, hArchiveOut, hCompressor, hMetadataOut, exclusions, ex_count, total_copied, file_buf, comp_buf);
        }
        
    } while (FindNextFileW(hFind, &fdw));
    FindClose(hFind);
}

static void cmd_backup_hybrid_cbak(HWND hwnd) {
    char drive_path[64];
    char exclusions_csv[1024] = {0};
    if (!ShowDriveSelectBox(hwnd, drive_path, exclusions_csv, NULL)) return;
    
    init_hybrid_apis();
    if (!init_compression()) {
        MessageBoxA(hwnd, "Compression API (cabinet.dll) not found. Requires Windows 8+.", "Error", MB_ICONERROR);
        return;
    }

    CloneExclusion exclusions[64];
    memset(exclusions, 0, sizeof(exclusions));
    int ex_count = 0;
    
    char *token = strtok(exclusions_csv, ",");
    while (token && ex_count < 64) {
        while (*token == ' ') token++;
        char* end = token + strlen(token) - 1;
        while (end > token && *end == ' ') { *end = '\0'; end--; }
        if (*token) {
            char* search_str = token;
            if (strlen(search_str) >= 3 && search_str[1] == ':' && (search_str[2] == '\\' || search_str[2] == '/')) search_str += 2;
            if (strcmp(search_str, "*.*") == 0 || strcmp(search_str, "*") == 0) {
                exclusions[ex_count].is_all = 1;
            } else {
                exclusions[ex_count].is_all = 0;
                char* star = strchr(search_str, '*');
                if (star) {
                    exclusions[ex_count].has_wildcard = 1;
                    int pre_len = (int)(star - search_str);
                    if (pre_len > 127) pre_len = 127;
                    strncpy(exclusions[ex_count].prefix, search_str, pre_len);
                    exclusions[ex_count].prefix[pre_len] = '\0';
                    
                    strncpy(exclusions[ex_count].suffix, star + 1, 127);
                    char* end_star = strchr(exclusions[ex_count].suffix, '*');
                    if (end_star) *end_star = '\0';
                } else {
                    exclusions[ex_count].has_wildcard = 0;
                    strncpy(exclusions[ex_count].prefix, search_str, 127);
                    exclusions[ex_count].prefix[127] = '\0';
                }
            }
            ex_count++;
        }
        token = strtok(NULL, ",");
    }

    OPENFILENAMEA sfn = {0};
    char szCbak[MAX_PATH] = "";
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szCbak; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "Compressed Backup (*.cbak)\0*.cbak\0";
    sfn.lpstrDefExt = "cbak";
    sfn.Flags = OFN_OVERWRITEPROMPT;
    sfn.lpstrTitle = "Save Hybrid Backup as...";
    if (!GetSaveFileNameA(&sfn)) return;

    char szMeta[MAX_PATH];
    strcpy(szMeta, szCbak);
    char* lastDot = strrchr(szMeta, '.');
    if (lastDot) strcpy(lastDot, ".txt"); else strcat(szMeta, ".txt");

    HANDLE hPhys = CreateFileA(drive_path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hPhys == INVALID_HANDLE_VALUE) { MessageBoxA(hwnd, "Cannot open physical drive.", "Error", MB_ICONERROR); return; }

    int target_disk_num = atoi(drive_path + strlen("\\\\.\\PhysicalDrive"));

    u8 mbr[512] = {0};
    DWORD br;
    ReadFile(hPhys, mbr, 512, &br, NULL);
    
    u32 active_lba = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr[0x1BE + i * 16] == 0x80) { active_lba = rd32le(mbr + 0x1BE + i * 16 + 8); break; }
    }
    
    WCHAR targetVolume[4] = L"";
    u64 largest_size = 0;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            char vol[8]; snprintf(vol, sizeof(vol), "\\\\.\\%c:", 'A' + i);
            HANDLE hVol = CreateFileA(vol, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hVol != INVALID_HANDLE_VALUE) {
                VOLUME_DISK_EXTENTS vde; DWORD ret;
                if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &vde, sizeof(vde), &ret, NULL)) {
                    if (vde.Extents[0].DiskNumber == (DWORD)target_disk_num) {
                        u32 vol_lba = (u32)(vde.Extents[0].StartingOffset.QuadPart / 512);
                        u64 vol_len = vde.Extents[0].ExtentLength.QuadPart;
                        if (active_lba > 0 && vol_lba == active_lba) { swprintf(targetVolume, 4, L"%c:", L'A' + i); CloseHandle(hVol); break; }
                        if (vol_len > largest_size) { largest_size = vol_len; swprintf(targetVolume, 4, L"%c:", L'A' + i); if (active_lba == 0) active_lba = vol_lba; }
                    }
                }
                CloseHandle(hVol);
            }
        }
    }

    if (wcslen(targetVolume) == 0) {
        CloseHandle(hPhys);
        MessageBoxA(hwnd, "Could not map partition to a mounted drive letter. Ensure the drive is formatted and mounted.", "Error", MB_ICONERROR);
        return;
    }

    u8 vbr[8192] = {0}; 
    if (active_lba > 0) {
        LARGE_INTEGER li; li.QuadPart = (u64)active_lba * 512;
        SetFilePointerEx(hPhys, li, NULL, FILE_BEGIN);
        ReadFile(hPhys, vbr, 8192, &br, NULL);
    }
    CloseHandle(hPhys);

    HANDLE hOut = CreateFileA(szCbak, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hOut == INVALID_HANDLE_VALUE) { MessageBoxA(hwnd, "Cannot create output archive.", "Error", MB_ICONERROR); return; }
    
    HANDLE hMetaOut = CreateFileA(szMeta, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hMetaOut == INVALID_HANDLE_VALUE) { CloseHandle(hOut); MessageBoxA(hwnd, "Cannot create metadata file.", "Error", MB_ICONERROR); return; }

    COMPRESSOR_HANDLE hCompressor = NULL;
    pCreateCompressor(COMPRESS_ALGORITHM_XPRESS, NULL, &hCompressor);

    DWORD bw;
    WriteFile(hOut, "CBAK", 4, &bw, NULL);
    u32 mbrSz = 512, vbrSz = 8192;
    WriteFile(hOut, &mbrSz, 4, &bw, NULL); WriteFile(hOut, mbr, 512, &bw, NULL);
    WriteFile(hOut, &vbrSz, 4, &bw, NULL); WriteFile(hOut, vbr, 8192, &bw, NULL);

    PUCHAR file_buf = (PUCHAR)malloc(1048576);
    PUCHAR comp_buf = (PUCHAR)malloc(1048576 + 4096);

    ShowProgress(TRUE);
    u64 total_copied = 0;
    TraverseAndBackup(targetVolume, L"", hOut, hCompressor, hMetaOut, exclusions, ex_count, &total_copied, file_buf, comp_buf);

    if (hCompressor) pCloseCompressor(hCompressor);
    free(file_buf);
    free(comp_buf);
    CloseHandle(hOut);
    CloseHandle(hMetaOut);
    ShowProgress(FALSE);
    
    SetWindowTextA(g_hStatusBar, g_cancel_operation ? "Hybrid Backup (.cbak) Cancelled." : "Hybrid Backup (.cbak) and Metadata complete.");
}

static void cmd_restore_cbak(HWND hwnd) {
    OPENFILENAMEA ofn = {0};
    char szCbak[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szCbak; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Compressed Backup Archive (*.cbak)\0*.cbak\0All Files\0*.*\0";
    ofn.lpstrTitle = "Select CBAK to Restore";
    if (!GetOpenFileNameA(&ofn)) return;

    char drive_path[64];
    if (!ShowRestoreDriveSelectBox(hwnd, drive_path)) return;

    if (MessageBoxA(hwnd, "WARNING: This will completely overwrite the target physical drive's boot structures and files! Are you sure?", "Confirm Restore", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    if (!init_compression()) {
        MessageBoxA(hwnd, "Compression API (cabinet.dll) not found. Requires Windows 8+.", "Error", MB_ICONERROR);
        return;
    }

    HANDLE hIn = CreateFileA(szCbak, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hIn == INVALID_HANDLE_VALUE) { MessageBoxA(hwnd, "Cannot open CBAK file.", "Error", MB_ICONERROR); return; }

    u8 header[4];
    DWORD br, bw;
    if (!ReadFile(hIn, header, 4, &br, NULL) || br != 4 || memcmp(header, "CBAK", 4) != 0) {
        CloseHandle(hIn);
        MessageBoxA(hwnd, "Invalid CBAK format.", "Error", MB_ICONERROR);
        return;
    }

    u32 mbrSz = 0, vbrSz = 0;
    u8 mbr[512] = {0}, vbr[8192] = {0};
    
    ReadFile(hIn, &mbrSz, 4, &br, NULL);
    if (mbrSz == 512) ReadFile(hIn, mbr, 512, &br, NULL);
    else SetFilePointer(hIn, mbrSz, NULL, FILE_CURRENT);

    ReadFile(hIn, &vbrSz, 4, &br, NULL);
    if (vbrSz == 8192) ReadFile(hIn, vbr, 8192, &br, NULL);
    else SetFilePointer(hIn, vbrSz, NULL, FILE_CURRENT);

    HANDLE hPhys = CreateFileA(drive_path, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hPhys == INVALID_HANDLE_VALUE) { 
        CloseHandle(hIn); 
        MessageBoxA(hwnd, "Cannot open physical drive for writing. Ensure it is accessible.", "Error", MB_ICONERROR); 
        return; 
    }

    WriteFile(hPhys, mbr, 512, &bw, NULL);

    u32 active_lba = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr[0x1BE + i * 16] == 0x80) {
            active_lba = rd32le(mbr + 0x1BE + i * 16 + 8);
            break;
        }
    }

    if (active_lba > 0 && vbrSz == 8192) {
        LARGE_INTEGER li; li.QuadPart = (u64)active_lba * 512;
        SetFilePointerEx(hPhys, li, NULL, FILE_BEGIN);
        WriteFile(hPhys, vbr, 8192, &bw, NULL);
    }
    
    DeviceIoControl(hPhys, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &br, NULL);
    CloseHandle(hPhys);

    Sleep(2000); 

    int target_disk_num = atoi(drive_path + strlen("\\\\.\\PhysicalDrive"));
    WCHAR targetVolume[MAX_PATH] = L"";
    
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            char vol[8]; snprintf(vol, sizeof(vol), "\\\\.\\%c:", 'A' + i);
            HANDLE hVol = CreateFileA(vol, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hVol != INVALID_HANDLE_VALUE) {
                VOLUME_DISK_EXTENTS vde;
                if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &vde, sizeof(vde), &br, NULL)) {
                    if (vde.Extents[0].DiskNumber == (DWORD)target_disk_num) {
                        u32 vol_lba = (u32)(vde.Extents[0].StartingOffset.QuadPart / 512);
                        if (active_lba > 0 && vol_lba == active_lba) {
                            swprintf(targetVolume, MAX_PATH, L"%c:", L'A' + i);
                            CloseHandle(hVol);
                            break;
                        } else if (wcslen(targetVolume) == 0) {
                            swprintf(targetVolume, MAX_PATH, L"%c:", L'A' + i);
                        }
                    }
                }
                CloseHandle(hVol);
            }
        }
    }

    if (wcslen(targetVolume) == 0) {
        CloseHandle(hIn);
        MessageBoxA(hwnd, "Could not map restored partition to a mounted drive letter. (Assign one using Disk Management).", "Error", MB_ICONERROR);
        return;
    }

    DECOMPRESSOR_HANDLE hDecompressor = NULL;
    pCreateDecompressor(COMPRESS_ALGORITHM_XPRESS, NULL, &hDecompressor);

    PUCHAR comp_buf = (PUCHAR)malloc(1048576 + 4096);
    PUCHAR file_buf = (PUCHAR)malloc(1048576);

    ShowProgress(TRUE);
    SetWindowTextA(g_hStatusBar, "Restoring files from CBAK...");

    while (!g_cancel_operation) {
        PumpMessages();
        u8 marker[4];
        if (!ReadFile(hIn, marker, 4, &br, NULL) || br != 4) break;
        
        if (memcmp(marker, "FILE", 4) == 0 || memcmp(marker, "DEL ", 4) == 0) {
            u16 pathLen = 0; ReadFile(hIn, &pathLen, 2, &br, NULL);
            char mbRelPath[1024] = {0}; ReadFile(hIn, mbRelPath, pathLen, &br, NULL);
            u64 fileSize = 0; ReadFile(hIn, &fileSize, 8, &br, NULL);

            if (memcmp(marker, "DEL ", 4) == 0) {
                u32 cSize = 0;
                while (ReadFile(hIn, &cSize, 4, &br, NULL) && br == 4 && cSize > 0) {
                    SetFilePointer(hIn, cSize, NULL, FILE_CURRENT);
                }
                continue;
            }

            WCHAR relPath[1024];
            MultiByteToWideChar(CP_UTF8, 0, mbRelPath, -1, relPath, 1024);
            
            WCHAR fullPath[MAX_PATH];
            swprintf(fullPath, MAX_PATH, L"\\\\?\\%ls%ls", targetVolume, relPath);
            
            WCHAR dirPath[MAX_PATH];
            wcscpy(dirPath, fullPath);
            WCHAR* lastSlash = wcsrchr(dirPath, L'\\');
            if (lastSlash) {
                *lastSlash = L'\0';
                WCHAR* p = dirPath;
                if (p[0] == L'\\' && p[1] == L'\\' && p[2] == L'?' && p[3] == L'\\') p += 4;
                while (*p) {
                    if (*p == L'\\') {
                        *p = L'\0';
                        CreateDirectoryW(dirPath, NULL);
                        *p = L'\\';
                    }
                    p++;
                }
                CreateDirectoryW(dirPath, NULL);
            }

            if (fileSize > 0 || fullPath[wcslen(fullPath)-1] != L'\\') {
                HANDLE hFileOut = CreateFileW(fullPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                
                while (1) {
                    PumpMessages();
                    if (g_cancel_operation) break;
                    
                    u32 cSize = 0;
                    ReadFile(hIn, &cSize, 4, &br, NULL);
                    if (cSize == 0) break;
                    
                    ReadFile(hIn, comp_buf, cSize, &br, NULL);
                    
                    if (hFileOut != INVALID_HANDLE_VALUE) {
                        SIZE_T final_uncomp = 0;
                        if (cSize < 1048576) {
                            BOOL success = pDecompress(hDecompressor, comp_buf, cSize, file_buf, 1048576, &final_uncomp);
                            if (success && final_uncomp > 0) {
                                WriteFile(hFileOut, file_buf, (DWORD)final_uncomp, &bw, NULL);
                            } else {
                                WriteFile(hFileOut, comp_buf, cSize, &bw, NULL);
                            }
                        } else {
                            WriteFile(hFileOut, comp_buf, cSize, &bw, NULL);
                        }
                    }
                }
                if (hFileOut != INVALID_HANDLE_VALUE) CloseHandle(hFileOut);
            }
        } else {
            break; 
        }
    }

    if (hDecompressor) pCloseDecompressor(hDecompressor);
    free(comp_buf);
    free(file_buf);
    CloseHandle(hIn);
    
    ShowProgress(FALSE);
    SetWindowTextA(g_hStatusBar, g_cancel_operation ? "CBAK Restore cancelled." : "CBAK Restore complete.");
}

/* ============================================================ WINDOW PROC */
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
            InitCommonControlsEx(&icc);

            CreateWindowExA(0, "BUTTON", "Backup Physical Drive to CBAK...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, 20, 340, 40, hwnd, (HMENU)ID_BTN_BACKUP, g_hInstance, NULL);
            CreateWindowExA(0, "BUTTON", "Restore CBAK to Physical Drive...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, 70, 340, 40, hwnd, (HMENU)ID_BTN_RESTORE, g_hInstance, NULL);

            g_hStatusBar = CreateWindowExA(0, STATUSCLASSNAMEA, "Ready. CBAK Standalone Utility.",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hProgressBar = CreateWindowExA(0, PROGRESS_CLASSA, NULL, WS_CHILD | PBS_SMOOTH | WS_CLIPSIBLINGS,
                0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hCancelBtn = CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | BS_PUSHBUTTON | WS_CLIPSIBLINGS,
                0, 0, 0, 0, hwnd, (HMENU)ID_BTN_CANCEL, g_hInstance, NULL);

            ShowWindow(g_hProgressBar, SW_HIDE);
            ShowWindow(g_hCancelBtn, SW_HIDE);
            return 0;
        }

        case WM_SIZE: {
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right;
            
            int parts[] = { w - 220, -1 };
            SendMessageA(g_hStatusBar, SB_SETPARTS, 2, (LPARAM)parts);
            
            SetWindowPos(g_hStatusBar, NULL, 0, rc.bottom - 24, w, 24, SWP_NOZORDER);
            SetWindowPos(g_hProgressBar, HWND_TOP, w - 210, rc.bottom - 20, 150, 16, 0);
            SetWindowPos(g_hCancelBtn, HWND_TOP, w - 55, rc.bottom - 21, 50, 18, 0);
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_BTN_BACKUP:  cmd_backup_hybrid_cbak(hwnd); break;
                case ID_BTN_RESTORE: cmd_restore_cbak(hwnd); break;
                case ID_BTN_CANCEL:  g_cancel_operation = TRUE; break;
            }
            return 0;
        }

        case WM_DESTROY:
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "CbakStandaloneClass";
    
    if (!RegisterClassA(&wc)) return 1;

    g_hMainWnd = CreateWindowExA(0, "CbakStandaloneClass", APP_NAME " " APP_VERSION,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInst, NULL);
        
    ShowWindow(g_hMainWnd, show);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) { 
        TranslateMessage(&msg); 
        DispatchMessageA(&msg); 
    }
    return (int)msg.wParam;
}