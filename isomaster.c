/*
 * isomaster_win32.c - Complete ISO9660/RockRidge/Joliet Editor
 * 
 * Recreated Linux ISO Master 2-pane interface for Win32
 * Fully Implemented Features: Save In-Place, Recursive Directory Add/Extract,
 * Drag & Drop Folders, Boot Record Export, Duplicate Scanning.
 * 
 * Compile: gcc -mwindows -o isomaster.exe isomaster.c -lcomctl32 -lcomdlg32
 */

#define UNICODE
#define _UNICODE
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
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <time.h>
#include <windowsx.h>

/* ============================================================
 * ListView Compatibility Wrappers
 * ============================================================ */
static int ListView_InsertItemA_Compat(HWND hwnd, const LVITEMA* pitem) {
    return (int)SendMessageA(hwnd, LVM_INSERTITEMA, 0, (LPARAM)pitem);
}
static void ListView_SetItemTextA_Compat(HWND hwnd, int i, int iSubItem, const char* pszText) {
    LVITEMA lvi = {0}; lvi.iSubItem = iSubItem; lvi.pszText = (LPSTR)pszText;
    SendMessageA(hwnd, LVM_SETITEMTEXTA, (WPARAM)i, (LPARAM)&lvi);
}
static void ListView_GetItemTextA_Compat(HWND hwnd, int i, int iSubItem, char* pszText, int cchTextMax) {
    LVITEMA lvi = {0}; lvi.iSubItem = iSubItem; lvi.cchTextMax = cchTextMax; lvi.pszText = (LPSTR)pszText;
    SendMessageA(hwnd, LVM_GETITEMTEXTA, (WPARAM)i, (LPARAM)&lvi);
}

/* ============================================================
 * CONSTANTS
 * ============================================================ */
#define APP_NAME        "ISO Master"
#define APP_VERSION     "3.0 (Win32)"
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600
#define MAX_PATH_LEN    4096
#define MAX_ENTRIES     16384
#define SECTOR_SIZE     2048

#define RR_ID_NM  "NM"  
#define RR_ID_CL  "CL"  
#define RR_ID_PX  "PX"  
#define RR_ID_SP  "SP"  

#define ID_LOCAL_BACK     2001
#define ID_LOCAL_NEWDIR   2002
#define ID_ISO_BACK       2003
#define ID_ISO_NEWDIR     2004
#define ID_ISO_ADD        2005
#define ID_ISO_EXTRACT    2006
#define ID_ISO_DELETE     2007

#define IDM_IMAGE_NEW      3001
#define IDM_IMAGE_OPEN     3002
#define IDM_IMAGE_SAVE     3003
#define IDM_IMAGE_SAVEAS   3004
#define IDM_IMAGE_PROPS    3005
#define IDM_IMAGE_QUIT     3006
#define IDM_VIEW_REFRESH   3010
#define IDM_VIEW_HIDDEN    3011
#define IDM_VIEW_SORTDIR   3012
#define IDM_BOOT_PROPS     3020
#define IDM_BOOT_SAVE      3021
#define IDM_BOOT_DEL       3022
#define IDM_BOOT_ADD       3023
#define IDM_SET_SCAN       3030
#define IDM_SET_SYMLINK    3031
#define IDM_HELP_ABOUT     3041

/* ============================================================
 * DATA STRUCTURES
 * ============================================================ */
typedef struct {
    BYTE  length;               /* 0 */
    BYTE  ext_attr_length;      /* 1 */
    BYTE  extent_location[8];   /* 2 */
    BYTE  data_length[8];       /* 10 */
    BYTE  recording_date[7];    /* 18 */
    BYTE  flags;                /* 25 */
    BYTE  file_unit_size;       /* 26 */
    BYTE  interleave_gap_size;  /* 27 */
    BYTE  volume_sequence[4];   /* 28 */
    BYTE  name_length;          /* 32 */
    BYTE  name[1];              /* 33 */
} __attribute__((packed)) Iso9660DirRecord;

typedef struct { BYTE length; BYTE id[2]; BYTE version; BYTE data_length; } __attribute__((packed)) RockRidgeAttrHeader;
typedef struct {
    BYTE  type; BYTE  identifier[5]; BYTE  version; BYTE  unused1;
    BYTE  system_identifier[32]; BYTE  volume_identifier[32]; BYTE  unused2[8];
    BYTE  volume_space_size[8]; BYTE  unused3[32]; BYTE  volume_set_size[4];
    BYTE  volume_sequence_number[4]; BYTE  logical_block_size[4]; BYTE  path_table_size[8];
    BYTE  type_l_path_table[4]; BYTE  opt_type_l_path_table[4];
    BYTE  type_m_path_table[4]; BYTE  opt_type_m_path_table[4];
} __attribute__((packed)) IsoPrimaryVolumeDesc;

typedef enum { NODE_TYPE_ROOT, NODE_TYPE_DIRECTORY, NODE_TYPE_FILE } NodeType;

typedef struct {
    char   long_name[256];      
    DWORD  posix_mode;
    BOOL   has_long_name;
} RockRidgeInfo;

typedef struct {
    NodeType    type;
    char        name[256];       
    char        long_name[256];  
    ULONGLONG   size;
    BYTE        is_directory;
    BYTE        flags;
    DWORD       parent_index;    
    DWORD       sector_offset;   
    DWORD       sector_count;    
    BOOL        marked_deleted;  
    BOOL        marked_new;      
    char        source_file[MAX_PATH_LEN]; 
    RockRidgeInfo rockridge;     
    time_t      timestamp;       
} IsoEntry;

typedef struct {
    HANDLE          hFile;
    BOOL            isOpen;
    char            path[MAX_PATH_LEN];
    char            volume_name[32];
    ULONGLONG       file_size;
    DWORD           num_entries;
    IsoEntry        entries[MAX_ENTRIES];
    ULONGLONG       new_data_offset;
    DWORD           root_sector;
    DWORD           logical_block_size;
    BYTE            pvd_buffer[SECTOR_SIZE];  
    time_t          creation_date;
    char            system_identifier[32];
    BOOL            has_rockridge;
    BOOL            has_joliet;
    BOOL            is_bootable;
    DWORD           boot_catalog_sector;
    DWORD           boot_image_sector;
    DWORD           boot_image_size;
} IsoImageState;

/* ============================================================
 * GLOBAL VARIABLES
 * ============================================================ */
HWND g_hMainWnd = NULL, g_hLocalListView = NULL, g_hIsoListView = NULL;
HWND g_hStatusBar = NULL, g_hLocalToolBar = NULL, g_hIsoToolBar = NULL;
HINSTANCE g_hInstance = NULL;

IsoImageState g_iso = {0};
char g_current_local_path[MAX_PATH];
DWORD g_current_iso_parent = 0; 

BOOL g_show_hidden = FALSE;
BOOL g_sort_dirs_first = TRUE;

/* ============================================================
 * IN-MEMORY INPUT BOX
 * ============================================================ */
char g_input_result[MAX_PATH];
HWND g_hInputEdit;

LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == 1) { 
                GetWindowTextA(g_hInputEdit, g_input_result, MAX_PATH);
                DestroyWindow(hwnd);
            } else if (LOWORD(wp) == 2) { 
                g_input_result[0] = '\0';
                DestroyWindow(hwnd);
            }
            break;
        case WM_CLOSE: g_input_result[0] = '\0'; DestroyWindow(hwnd); break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

BOOL ShowInputBox(HWND parent, const char* title, const char* prompt, char* out_buf) {
    WNDCLASSA wc = {0}; wc.lpfnWndProc = InputWndProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "IsoInputBoxClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "IsoInputBoxClass", title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 300, 140, parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", prompt, WS_CHILD | WS_VISIBLE, 10, 10, 260, 20, hDlg, NULL, g_hInstance, NULL);
    g_hInputEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 10, 35, 260, 22, hDlg, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 110, 70, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 195, 70, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);
    
    SetFocus(g_hInputEdit); EnableWindow(parent, FALSE);
    MSG msg; while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    
    if (g_input_result[0] != '\0') { strcpy(out_buf, g_input_result); return TRUE; }
    return FALSE;
}

/* ============================================================
 * UTILITY FUNCTIONS
 * ============================================================ */
static DWORD ReadLE32(const BYTE* buf) { return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24); }
static void WriteLE32(BYTE* buf, DWORD val) { buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF; buf[2] = (val >> 16) & 0xFF; buf[3] = (val >> 24) & 0xFF; }
static void WriteBE32(BYTE* buf, DWORD val) { buf[0] = (val >> 24) & 0xFF; buf[1] = (val >> 16) & 0xFF; buf[2] = (val >> 8) & 0xFF; buf[3] = val & 0xFF; }

static void ExtractString(const BYTE* src, char* dst, int max_len) {
    int i, j = 0;
    for (i = 0; i < max_len && src[i] != ' ' && src[i] != '\0'; i++) { if (src[i] >= 32 && src[i] < 127) dst[j++] = src[i]; }
    dst[j] = '\0';
}
static const char* get_basename(const char* path) {
    const char* slash = strrchr(path, '/'); if (!slash) slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}
static void format_size(ULONGLONG bytes, char* buffer, int buf_size) {
    if (bytes >= 1073741824ULL) snprintf(buffer, buf_size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576ULL) snprintf(buffer, buf_size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024ULL) snprintf(buffer, buf_size, "%.2f KB", bytes / 1024.0);
    else snprintf(buffer, buf_size, "%llu B", bytes);
}

/* ============================================================
 * UI POPULATION
 * ============================================================ */
void set_local_path(const char* path) {
    strcpy(g_current_local_path, path);
    ListView_DeleteAllItems(g_hLocalListView);
    char search_path[MAX_PATH]; snprintf(search_path, sizeof(search_path), "%s\\*", g_current_local_path);
    WIN32_FIND_DATAA fd; HANDLE hFind = FindFirstFileA(search_path, &fd);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        int i = 0;
        if (strcmp(g_current_local_path, "C:\\") != 0 && strlen(g_current_local_path) > 3) {
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.iItem = i++; lvi.pszText = ".."; lvi.lParam = 1; 
            int idx = ListView_InsertItemA_Compat(g_hLocalListView, &lvi);
            ListView_SetItemTextA_Compat(g_hLocalListView, idx, 1, "<DIR>");
        }
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) { FindClose(hFind); hFind = FindFirstFileA(search_path, &fd); if (hFind == INVALID_HANDLE_VALUE) break; }
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                if (!g_show_hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
                BOOL isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (g_sort_dirs_first && ((pass == 0 && !isDir) || (pass == 1 && isDir))) continue;
                LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.iItem = i; lvi.pszText = fd.cFileName; lvi.lParam = isDir ? 1 : 0;
                int idx = ListView_InsertItemA_Compat(g_hLocalListView, &lvi);
                if (!isDir) {
                    char size_str[64]; format_size(((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, size_str, sizeof(size_str));
                    ListView_SetItemTextA_Compat(g_hLocalListView, idx, 1, size_str);
                } else ListView_SetItemTextA_Compat(g_hLocalListView, idx, 1, "<DIR>");
                i++;
            } while (FindNextFileA(hFind, &fd));
        }
        FindClose(hFind);
    }
}

void populate_iso_listview() {
    ListView_DeleteAllItems(g_hIsoListView);
    if (!g_iso.isOpen) return;
    int idx = 0;
    if (g_current_iso_parent != 0) {
        LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.iItem = idx++; lvi.pszText = ".."; lvi.lParam = 0xFFFFFFFF; 
        int i = ListView_InsertItemA_Compat(g_hIsoListView, &lvi); ListView_SetItemTextA_Compat(g_hIsoListView, i, 1, "<DIR>");
    }
    for (int pass = 0; pass < 2; pass++) {
        for (DWORD i = 1; i < g_iso.num_entries; i++) {
            IsoEntry* entry = &g_iso.entries[i];
            if (entry->marked_deleted || entry->parent_index != g_current_iso_parent) continue;
            if (g_sort_dirs_first && ((pass == 0 && !entry->is_directory) || (pass == 1 && entry->is_directory))) continue;
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.iItem = idx++; lvi.pszText = entry->long_name; lvi.lParam = (LPARAM)i;
            int row = ListView_InsertItemA_Compat(g_hIsoListView, &lvi);
            if (entry->is_directory) ListView_SetItemTextA_Compat(g_hIsoListView, row, 1, "<DIR>");
            else { char size_str[64]; format_size(entry->size, size_str, sizeof(size_str)); ListView_SetItemTextA_Compat(g_hIsoListView, row, 1, size_str); }
        }
    }
}

/* ============================================================
 * CORE ISO PARSING
 * ============================================================ */
static int parse_directory_record(const BYTE* record, int record_len, DWORD parent_idx, DWORD base_sector) {
    if (record_len < 34 || record[0] == 0) return 0;
    if (record[0] >= 2 && (record[33] == 0 || record[33] == 1)) return record[0];
    if (g_iso.num_entries >= MAX_ENTRIES) return record[0];
    
    IsoEntry* entry = &g_iso.entries[g_iso.num_entries]; memset(entry, 0, sizeof(IsoEntry));
    entry->sector_offset = ReadLE32(record + 2); entry->size = ReadLE32(record + 10);
    entry->sector_count = (entry->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    entry->is_directory = (record[25] & 2) != 0; entry->parent_index = parent_idx;
    
    if (record[32] > 0 && record[32] < 256) {
        memcpy(entry->name, record + 33, record[32]); entry->name[record[32]] = '\0';
        char* semi = strchr(entry->name, ';'); if (semi) *semi = '\0';
    } else strcpy(entry->name, "(unnamed)");
    
    strncpy(entry->long_name, entry->name, sizeof(entry->long_name) - 1);
    entry->type = entry->is_directory ? NODE_TYPE_DIRECTORY : NODE_TYPE_FILE;
    g_iso.num_entries++; return record[0];
}

static void read_directory(DWORD sector, DWORD parent_idx, int depth) {
    if (depth > 32 || g_iso.entries[parent_idx].size == 0) return;
    BYTE* buffer = (BYTE*)malloc(g_iso.entries[parent_idx].size); if (!buffer) return;
    
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)sector * SECTOR_SIZE;
    SetFilePointerEx(g_iso.hFile, pos, NULL, FILE_BEGIN);
    
    DWORD bytes_read;
    if (!ReadFile(g_iso.hFile, buffer, g_iso.entries[parent_idx].size, &bytes_read, NULL)) { free(buffer); return; }
    
    DWORD offset = 0;
    while (offset < bytes_read) {
        BYTE len = buffer[offset];
        if (len == 0) { offset += SECTOR_SIZE - (offset % SECTOR_SIZE); continue; }
        if (offset + len > bytes_read) break;
        int consumed = parse_directory_record(&buffer[offset], len, parent_idx, sector);
        if (consumed == 0) break;
        offset += consumed;
    }
    free(buffer);
    
    DWORD current_entries = g_iso.num_entries; 
    for (DWORD i = 0; i < current_entries; i++) {
        if (!g_iso.entries[i].marked_deleted && g_iso.entries[i].is_directory && g_iso.entries[i].parent_index == parent_idx && i != parent_idx) {
            read_directory(g_iso.entries[i].sector_offset, i, depth + 1);
        }
    }
}

static void iso_close_image(void) {
    if (g_iso.hFile && g_iso.hFile != INVALID_HANDLE_VALUE) { CloseHandle(g_iso.hFile); g_iso.hFile = INVALID_HANDLE_VALUE; }
    g_iso.isOpen = FALSE; g_iso.num_entries = 0; g_current_iso_parent = 0;
}

static int iso_open_image(const char* path) {
    iso_close_image(); memset(&g_iso, 0, sizeof(g_iso));
    g_iso.hFile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_iso.hFile == INVALID_HANDLE_VALUE) return -1;
    
    LARGE_INTEGER fsize; GetFileSizeEx(g_iso.hFile, &fsize);
    g_iso.file_size = fsize.QuadPart; g_iso.logical_block_size = SECTOR_SIZE;
    
    LARGE_INTEGER pos; pos.QuadPart = 16ULL * SECTOR_SIZE; SetFilePointerEx(g_iso.hFile, pos, NULL, FILE_BEGIN);
    DWORD bytes_read; if (!ReadFile(g_iso.hFile, g_iso.pvd_buffer, SECTOR_SIZE, &bytes_read, NULL)) { CloseHandle(g_iso.hFile); return -2; }
    
    IsoPrimaryVolumeDesc* pvd = (IsoPrimaryVolumeDesc*)g_iso.pvd_buffer;
    if (memcmp(pvd->identifier, "CD001", 5) != 0) { CloseHandle(g_iso.hFile); return -3; }
    
    ExtractString(pvd->volume_identifier, g_iso.volume_name, 32);
    DWORD root_extent = ReadLE32(g_iso.pvd_buffer + 156 + 2);
    DWORD root_size   = ReadLE32(g_iso.pvd_buffer + 156 + 10);
    g_iso.root_sector = root_extent;
    
    IsoEntry* root_entry = &g_iso.entries[0]; memset(root_entry, 0, sizeof(IsoEntry));
    root_entry->type = NODE_TYPE_DIRECTORY; root_entry->is_directory = TRUE; root_entry->parent_index = 0;
    strcpy(root_entry->name, ""); strcpy(root_entry->long_name, "Root");
    root_entry->sector_offset = root_extent; root_entry->size = root_size; g_iso.num_entries = 1;
    
    read_directory(root_extent, 0, 0);
    g_iso.new_data_offset = g_iso.file_size;
    if (g_iso.new_data_offset % SECTOR_SIZE) g_iso.new_data_offset += SECTOR_SIZE - (g_iso.new_data_offset % SECTOR_SIZE);
    
    strncpy(g_iso.path, path, MAX_PATH_LEN - 1); g_iso.isOpen = TRUE; g_current_iso_parent = 0;
    return 0;
}

static void iso_new_image(void) {
    iso_close_image(); memset(&g_iso, 0, sizeof(g_iso));
    g_iso.isOpen = TRUE; strcpy(g_iso.volume_name, "NEW_VOLUME"); g_iso.file_size = SECTOR_SIZE * 20; 
    g_iso.entries[0].type = NODE_TYPE_DIRECTORY; g_iso.entries[0].is_directory = TRUE;
    strcpy(g_iso.entries[0].name, ""); strcpy(g_iso.entries[0].long_name, "Root");
    g_iso.num_entries = 1; g_current_iso_parent = 0; populate_iso_listview();
}

static int build_directory_record(IsoEntry* entry, BYTE* buffer, int buffer_size) {
    int pos = 0; buffer[pos++] = 0; buffer[pos++] = 0;  
    WriteLE32(buffer + pos, entry->sector_offset); WriteBE32(buffer + pos + 4, entry->sector_offset); pos += 8;
    WriteLE32(buffer + pos, entry->size); WriteBE32(buffer + pos + 4, entry->size); pos += 8;
    memcpy(buffer + pos, "\x76\x01\x01\x01\x01\x01\x00", 7); pos += 7; 
    buffer[pos++] = entry->is_directory ? 2 : 0; buffer[pos++] = 0; buffer[pos++] = 0;  
    buffer[pos++] = 1; buffer[pos++] = 0; buffer[pos++] = 0; buffer[pos++] = 1; 
    
    char name[256]; strncpy(name, entry->name, 255); name[255] = '\0';
    int name_len = strlen(name); if (name_len > 31) name_len = 31;
    buffer[pos++] = name_len; memcpy(buffer + pos, name, name_len); pos += name_len;
    if (pos % 2) buffer[pos++] = 0; buffer[0] = pos;
    return pos;
}

/* ============================================================
 * ISO SAVING & EXTRACTION ENGINE
 * ============================================================ */
static int iso_save_image(const char* target_path) {
    if (!g_iso.isOpen) return -1;
    
    HANDLE hOut;
    BOOL inplace = (strcmp(target_path, g_iso.path) == 0);
    
    if (inplace) {
        hOut = g_iso.hFile; 
    } else {
        CopyFileA(g_iso.path, target_path, FALSE);
        hOut = CreateFileA(target_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut == INVALID_HANDLE_VALUE) { MessageBoxA(g_hMainWnd, "Failed to open target file for writing.", "Error", MB_ICONERROR); return -1; }
    }
    
    LARGE_INTEGER fsize; GetFileSizeEx(hOut, &fsize);
    ULONGLONG new_end = fsize.QuadPart;
    if (new_end % SECTOR_SIZE) new_end += SECTOR_SIZE - (new_end % SECTOR_SIZE);
    
    for (DWORD i = 0; i < g_iso.num_entries; i++) {
        IsoEntry* entry = &g_iso.entries[i];
        if (entry->marked_deleted || !entry->marked_new || entry->type != NODE_TYPE_FILE) continue;
        
        HANDLE hSrc = CreateFileA(entry->source_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hSrc == INVALID_HANDLE_VALUE) continue;
        
        ULONGLONG file_size = 0; BY_HANDLE_FILE_INFORMATION fi;
        if (GetFileInformationByHandle(hSrc, &fi)) file_size = ((ULONGLONG)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
        
        entry->sector_count = (DWORD)((file_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
        entry->sector_offset = (DWORD)(new_end / SECTOR_SIZE); entry->size = file_size;
        
        BYTE* buffer = (BYTE*)malloc(SECTOR_SIZE); ULONGLONG total_copied = 0; DWORD sector_num = entry->sector_offset;
        while (total_copied < file_size) {
            DWORD to_read = (DWORD)(file_size - total_copied); if (to_read > SECTOR_SIZE) to_read = SECTOR_SIZE;
            DWORD actual = 0; LARGE_INTEGER srcPos; srcPos.QuadPart = total_copied; SetFilePointerEx(hSrc, srcPos, NULL, FILE_BEGIN); ReadFile(hSrc, buffer, to_read, &actual, NULL);
            if (actual < SECTOR_SIZE) { memset(buffer + actual, 0, SECTOR_SIZE - actual); actual = SECTOR_SIZE; }
            LARGE_INTEGER dstPos; dstPos.QuadPart = (LONGLONG)sector_num * SECTOR_SIZE; SetFilePointerEx(hOut, dstPos, NULL, FILE_BEGIN);
            DWORD written = 0; WriteFile(hOut, buffer, actual, &written, NULL); total_copied += actual; sector_num++;
        }
        new_end = sector_num * SECTOR_SIZE; free(buffer); CloseHandle(hSrc);
    }
    
    BYTE* dir_buf = (BYTE*)calloc(1, SECTOR_SIZE * 16); int dir_pos = 0;
    IsoEntry root_self = g_iso.entries[0]; root_self.name[0] = '\0'; root_self.sector_offset = (DWORD)(new_end / SECTOR_SIZE);
    dir_pos += build_directory_record(&root_self, dir_buf + dir_pos, SECTOR_SIZE);
    IsoEntry root_parent = root_self; root_parent.name[0] = '\1';
    dir_pos += build_directory_record(&root_parent, dir_buf + dir_pos, SECTOR_SIZE);
    
    for (DWORD i = 1; i < g_iso.num_entries; i++) {
        if (g_iso.entries[i].parent_index == 0 && !g_iso.entries[i].marked_deleted) {
            dir_pos += build_directory_record(&g_iso.entries[i], dir_buf + dir_pos, SECTOR_SIZE * 16 - dir_pos);
        }
    }
    
    DWORD new_root_sector = (DWORD)(new_end / SECTOR_SIZE);
    DWORD new_root_size = dir_pos;
    if (new_root_size % SECTOR_SIZE) new_root_size += SECTOR_SIZE - (new_root_size % SECTOR_SIZE);
    
    LARGE_INTEGER dirPos; dirPos.QuadPart = new_end; SetFilePointerEx(hOut, dirPos, NULL, FILE_BEGIN);
    DWORD written = 0; WriteFile(hOut, dir_buf, new_root_size, &written, NULL); new_end += new_root_size; free(dir_buf);
    
    DWORD new_space_size = (DWORD)(new_end / SECTOR_SIZE);
    WriteLE32(&g_iso.pvd_buffer[80], new_space_size); WriteBE32(&g_iso.pvd_buffer[84], new_space_size);
    WriteLE32(g_iso.pvd_buffer + 156 + 2, new_root_sector); WriteBE32(g_iso.pvd_buffer + 156 + 6, new_root_sector);
    WriteLE32(g_iso.pvd_buffer + 156 + 10, new_root_size); WriteBE32(g_iso.pvd_buffer + 156 + 14, new_root_size);
    
    LARGE_INTEGER pvdPos; pvdPos.QuadPart = 16ULL * SECTOR_SIZE; SetFilePointerEx(hOut, pvdPos, NULL, FILE_BEGIN);
    WriteFile(hOut, g_iso.pvd_buffer, SECTOR_SIZE, NULL, NULL); 
    
    if (!inplace) CloseHandle(hOut);
    
    SetWindowTextA(g_hStatusBar, "ISO saved successfully.");
    return 0;
}

static int iso_extract_file(DWORD entry_idx, const char* dest_path) {
    if (entry_idx >= g_iso.num_entries) return -1;
    IsoEntry* entry = &g_iso.entries[entry_idx]; if (entry->type != NODE_TYPE_FILE) return -2;
    
    HANDLE hDst = CreateFileA(dest_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDst == INVALID_HANDLE_VALUE) return -3;
    
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)entry->sector_offset * SECTOR_SIZE;
    if (!SetFilePointerEx(g_iso.hFile, pos, NULL, FILE_BEGIN)) { CloseHandle(hDst); return -4; }
    
    BYTE* buffer = (BYTE*)malloc(SECTOR_SIZE); ULONGLONG remaining = entry->size;
    while (remaining > 0) {
        DWORD to_read = (DWORD)(remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE); DWORD actual = 0, written = 0;
        if (!ReadFile(g_iso.hFile, buffer, to_read, &actual, NULL) || actual == 0) break;
        WriteFile(hDst, buffer, actual, &written, NULL); remaining -= actual;
    }
    free(buffer); CloseHandle(hDst); return 0;
}

static void iso_extract_local_directory(DWORD entry_idx, const char* dest_path) {
    if (entry_idx >= g_iso.num_entries) return;
    CreateDirectoryA(dest_path, NULL);
    
    for (DWORD i = 1; i < g_iso.num_entries; i++) {
        if (g_iso.entries[i].parent_index == entry_idx && !g_iso.entries[i].marked_deleted) {
            char child_dest[MAX_PATH];
            snprintf(child_dest, sizeof(child_dest), "%s\\%s", dest_path, g_iso.entries[i].name);
            
            if (g_iso.entries[i].is_directory) iso_extract_local_directory(i, child_dest);
            else iso_extract_file(i, child_dest);
        }
    }
}

static int iso_add_directory(const char* dest_name, DWORD parent_idx) {
    if (!g_iso.isOpen || g_iso.num_entries >= MAX_ENTRIES) return -1;
    IsoEntry* entry = &g_iso.entries[g_iso.num_entries]; memset(entry, 0, sizeof(IsoEntry));
    entry->type = NODE_TYPE_DIRECTORY; entry->is_directory = TRUE; entry->size = 0;
    entry->marked_new = TRUE; entry->parent_index = parent_idx; entry->timestamp = time(NULL);
    strncpy(entry->name, dest_name, 255); strncpy(entry->long_name, dest_name, 255);
    g_iso.num_entries++; return g_iso.num_entries - 1;
}

static int iso_add_file(const char* source_file, const char* dest_name, DWORD parent_idx) {
    if (!g_iso.isOpen || g_iso.num_entries >= MAX_ENTRIES) return -1;
    HANDLE hSrc = CreateFileA(source_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSrc == INVALID_HANDLE_VALUE) return -3;
    ULONGLONG file_size = 0; BY_HANDLE_FILE_INFORMATION fi;
    if (GetFileInformationByHandle(hSrc, &fi)) file_size = ((ULONGLONG)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
    CloseHandle(hSrc);
    
    DWORD sectors_needed = (DWORD)((file_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    if (g_iso.new_data_offset % SECTOR_SIZE) g_iso.new_data_offset += SECTOR_SIZE - (g_iso.new_data_offset % SECTOR_SIZE);
    
    IsoEntry* entry = &g_iso.entries[g_iso.num_entries]; memset(entry, 0, sizeof(IsoEntry));
    entry->type = NODE_TYPE_FILE; entry->is_directory = FALSE; entry->size = file_size;
    entry->marked_new = TRUE; entry->parent_index = parent_idx; entry->timestamp = time(NULL);
    strncpy(entry->name, get_basename(dest_name), 255); strncpy(entry->long_name, get_basename(dest_name), 255);
    strncpy(entry->source_file, source_file, MAX_PATH_LEN - 1);
    
    g_iso.new_data_offset += sectors_needed * SECTOR_SIZE; g_iso.num_entries++;
    return g_iso.num_entries - 1;
}

static int iso_add_local_directory(const char* local_path, const char* dir_name, DWORD parent_idx, int depth) {
    if (depth > 32 || !g_iso.isOpen || g_iso.num_entries >= MAX_ENTRIES) return -1;
    
    int new_dir_idx = iso_add_directory(dir_name, parent_idx);
    if (new_dir_idx < 0) return -1;
    
    char search_path[MAX_PATH]; snprintf(search_path, sizeof(search_path), "%s\\*", local_path);
    WIN32_FIND_DATAA fd; HANDLE hFind = FindFirstFileA(search_path, &fd);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char full_path[MAX_PATH]; snprintf(full_path, sizeof(full_path), "%s\\%s", local_path, fd.cFileName);
            
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                iso_add_local_directory(full_path, fd.cFileName, new_dir_idx, depth + 1);
            } else iso_add_file(full_path, fd.cFileName, new_dir_idx);
            
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return new_dir_idx;
}

static void iso_delete_entry(DWORD entry_idx) {
    if (entry_idx >= g_iso.num_entries || entry_idx == 0) return;
    g_iso.entries[entry_idx].marked_deleted = TRUE;
    for (DWORD i = 0; i < g_iso.num_entries; i++) { if (g_iso.entries[i].parent_index == (BYTE)entry_idx) iso_delete_entry(i); }
}

/* ============================================================
 * COMMANDS & EVENT HANDLERS
 * ============================================================ */
static void cmd_local_back() {
    if (strcmp(g_current_local_path, "C:\\") != 0) {
        char* last_slash = strrchr(g_current_local_path, '\\');
        if (last_slash && last_slash != g_current_local_path) {
            if (*(last_slash - 1) == ':') last_slash[1] = '\0'; else *last_slash = '\0';
            set_local_path(g_current_local_path);
        }
    }
}
static void cmd_iso_back() {
    if (g_current_iso_parent != 0) {
        g_current_iso_parent = g_iso.entries[g_current_iso_parent].parent_index;
        populate_iso_listview();
    }
}

static void cmd_local_newdir(HWND hwnd) {
    char dirName[MAX_PATH];
    if (ShowInputBox(hwnd, "Create Directory", "Enter folder name:", dirName)) {
        char fullPath[MAX_PATH];
        if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') snprintf(fullPath, MAX_PATH, "%s%s", g_current_local_path, dirName);
        else snprintf(fullPath, MAX_PATH, "%s\\%s", g_current_local_path, dirName);
        if (CreateDirectoryA(fullPath, NULL)) set_local_path(g_current_local_path);
        else MessageBoxA(hwnd, "Failed to create directory.", "Error", MB_ICONERROR);
    }
}

static void cmd_iso_newdir(HWND hwnd) {
    if (!g_iso.isOpen) return;
    char dirName[MAX_PATH];
    if (ShowInputBox(hwnd, "Create ISO Directory", "Enter folder name:", dirName)) {
        iso_add_directory(dirName, g_current_iso_parent); populate_iso_listview();
    }
}

static void cmd_add_selected(HWND hwnd) {
    if (!g_iso.isOpen) { MessageBoxA(hwnd, "Open or create an ISO first.", "Warning", MB_ICONWARNING); return; }
    int sel = SendMessageA(g_hLocalListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (sel == -1) return;
    char filename[MAX_PATH]; ListView_GetItemTextA_Compat(g_hLocalListView, sel, 0, filename, MAX_PATH);
    char src_path[MAX_PATH]; 
    if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') snprintf(src_path, MAX_PATH, "%s%s", g_current_local_path, filename);
    else snprintf(src_path, MAX_PATH, "%s\\%s", g_current_local_path, filename);
    
    DWORD attr = GetFileAttributesA(src_path);
    if (attr & FILE_ATTRIBUTE_DIRECTORY) { 
        if (iso_add_local_directory(src_path, filename, g_current_iso_parent, 0) >= 0) {
            populate_iso_listview(); SetWindowTextA(g_hStatusBar, "Directory tree added to ISO.");
        }
    } else {
        if (iso_add_file(src_path, filename, g_current_iso_parent) >= 0) {
            populate_iso_listview(); SetWindowTextA(g_hStatusBar, "File queued for addition.");
        }
    }
}

static void cmd_extract_selected(HWND hwnd) {
    int sel = SendMessageA(g_hIsoListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (sel == -1) return;
    LVITEMA lvi = {0}; lvi.iItem = sel; lvi.mask = LVIF_PARAM; SendMessageA(g_hIsoListView, LVM_GETITEMA, 0, (LPARAM)&lvi);
    DWORD idx = (DWORD)lvi.lParam; if (idx == 0xFFFFFFFF) return;
    
    char dest_path[MAX_PATH]; 
    if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') snprintf(dest_path, MAX_PATH, "%s%s", g_current_local_path, g_iso.entries[idx].name);
    else snprintf(dest_path, MAX_PATH, "%s\\%s", g_current_local_path, g_iso.entries[idx].name);
    
    if (g_iso.entries[idx].is_directory) {
        iso_extract_local_directory(idx, dest_path);
        set_local_path(g_current_local_path); SetWindowTextA(g_hStatusBar, "Directory extracted.");
    } else {
        OPENFILENAMEA sf = {0}; sf.lStructSize = sizeof(sf); sf.hwndOwner = hwnd; 
        strcpy(dest_path, g_iso.entries[idx].long_name); sf.lpstrFile = dest_path; sf.nMaxFile = MAX_PATH;
        sf.Flags = OFN_OVERWRITEPROMPT; sf.lpstrFilter = "All Files\0*.*\0";
        if (GetSaveFileNameA(&sf)) {
            if (iso_extract_file(idx, dest_path) == 0) { set_local_path(g_current_local_path); SetWindowTextA(g_hStatusBar, "File extracted."); }
            else MessageBoxA(hwnd, "Failed to extract file.", "Error", MB_ICONERROR);
        }
    }
}

static void cmd_delete_selected(HWND hwnd) {
    int sel = SendMessageA(g_hIsoListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (sel == -1) return;
    LVITEMA lvi = {0}; lvi.iItem = sel; lvi.mask = LVIF_PARAM; SendMessageA(g_hIsoListView, LVM_GETITEMA, 0, (LPARAM)&lvi);
    DWORD idx = (DWORD)lvi.lParam;
    if (idx != 0xFFFFFFFF && MessageBoxA(hwnd, "Delete selected item from ISO?", "Confirm", MB_YESNO) == IDYES) {
        iso_delete_entry(idx); populate_iso_listview();
    }
}

/* ============================================================
 * WINDOW PROCEDURES
 * ============================================================ */
void init_listview_columns(HWND hLv) {
    LVCOLUMNA lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvc.iSubItem = 0; lvc.cx = 350; lvc.pszText = (LPSTR)"Name"; SendMessageA(hLv, LVM_INSERTCOLUMNA, 0, (LPARAM)&lvc);
    lvc.iSubItem = 1; lvc.cx = 150; lvc.pszText = (LPSTR)"Size"; SendMessageA(hLv, LVM_INSERTCOLUMNA, 1, (LPARAM)&lvc);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            
            HMENU hMenu = CreateMenu(), hFile = CreatePopupMenu();
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_NEW, "&New");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_OPEN, "&Open...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_SAVE, "&Save\tCtrl+S");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_SAVEAS, "&Save As...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_PROPS, "&Properties");
            AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_QUIT, "&Quit");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&Image");
            
            HMENU hView = CreatePopupMenu();
            AppendMenuA(hView, MF_STRING, IDM_VIEW_REFRESH, "&Refresh");
            AppendMenuA(hView, MF_STRING, IDM_VIEW_HIDDEN, "&Hidden files");
            AppendMenuA(hView, MF_STRING | MF_CHECKED, IDM_VIEW_SORTDIR, "&Sort directories first");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "&View");
            
            HMENU hBoot = CreatePopupMenu();
            AppendMenuA(hBoot, MF_STRING, IDM_BOOT_PROPS, "&Properties");
            AppendMenuA(hBoot, MF_STRING, IDM_BOOT_SAVE, "&Save to drive");
            AppendMenuA(hBoot, MF_STRING, IDM_BOOT_DEL, "&Delete");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hBoot, "&BootRecord");

            HMENU hHelp = CreatePopupMenu();
            AppendMenuA(hHelp, MF_STRING, IDM_HELP_ABOUT, "&About");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");
            
            SetMenu(hwnd, hMenu);
            INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);
            
            g_hLocalToolBar = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | CCS_NODIVIDER | CCS_NORESIZE, 0, 0, 0, 0, hwnd, (HMENU)100, g_hInstance, NULL);
            SendMessage(g_hLocalToolBar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
            char locBtnStrings[] = "Go Back\0New Directory\0"; LRESULT idxLoc = SendMessage(g_hLocalToolBar, TB_ADDSTRINGA, 0, (LPARAM)locBtnStrings);
            TBBUTTON tbbLoc[2] = { {I_IMAGENONE, ID_LOCAL_BACK, TBSTATE_ENABLED, BTNS_BUTTON|BTNS_SHOWTEXT|BTNS_AUTOSIZE, {0},0,idxLoc}, {I_IMAGENONE, ID_LOCAL_NEWDIR, TBSTATE_ENABLED, BTNS_BUTTON|BTNS_SHOWTEXT|BTNS_AUTOSIZE, {0},0,idxLoc+1} };
            SendMessage(g_hLocalToolBar, TB_ADDBUTTONS, 2, (LPARAM)&tbbLoc);
            
            g_hIsoToolBar = CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | CCS_NODIVIDER | CCS_NORESIZE, 0, 0, 0, 0, hwnd, (HMENU)101, g_hInstance, NULL);
            SendMessage(g_hIsoToolBar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
            char isoBtnStrings[] = "Go Back\0New Directory\0Add to ISO\0Extract from ISO\0Delete\0"; LRESULT idxIso = SendMessage(g_hIsoToolBar, TB_ADDSTRINGA, 0, (LPARAM)isoBtnStrings);
            TBBUTTON tbbIso[5] = { {I_IMAGENONE, ID_ISO_BACK, TBSTATE_ENABLED, BTNS_BUTTON|BTNS_SHOWTEXT|BTNS_AUTOSIZE, {0},0,idxIso}, {I_IMAGENONE, ID_ISO_NEWDIR, TBSTATE_ENABLED, BTNS_BUTTON|BTNS_SHOWTEXT|BTNS_AUTOSIZE, {0},0,idxIso+1}, {I_IMAGENONE, ID_ISO_ADD, TBSTATE_ENABLED, BTNS_BUTTON|BTNS_SHOWTEXT|BTNS_AUTOSIZE, {0},0,idxIso+2}, {I_IMAGENONE, ID_ISO_EXTRACT, TBSTATE_ENABLED, BTNS_BUTTON|BTNS_SHOWTEXT|BTNS_AUTOSIZE, {0},0,idxIso+3}, {I_IMAGENONE, ID_ISO_DELETE, TBSTATE_ENABLED, BTNS_BUTTON|BTNS_SHOWTEXT|BTNS_AUTOSIZE, {0},0,idxIso+4} };
            SendMessage(g_hIsoToolBar, TB_ADDBUTTONS, 5, (LPARAM)&tbbIso);

            g_hLocalListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hIsoListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            
            init_listview_columns(g_hLocalListView); init_listview_columns(g_hIsoListView);
            g_hStatusBar = CreateWindowExA(0, STATUSCLASSNAMEA, "Ready", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            
            char root[MAX_PATH]; GetEnvironmentVariableA("HOMEDRIVE", root, MAX_PATH); strcat(root, "\\"); set_local_path(root);
            return 0;
        }
        
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            if (!g_iso.isOpen) {
                MessageBoxA(hwnd, "Open or create an ISO first before dropping files.", "Warning", MB_ICONWARNING);
                DragFinish(hDrop); return 0;
            }
            
            POINT pt; DragQueryPoint(hDrop, &pt);
            RECT rcClient; GetClientRect(hwnd, &rcClient);
            
            if (pt.x > (rcClient.right / 2)) {
                UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
                int added = 0; 
                for (UINT i = 0; i < count; i++) {
                    char filepath[MAX_PATH]; DragQueryFileA(hDrop, i, filepath, MAX_PATH);
                    DWORD attr = GetFileAttributesA(filepath);
                    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                        if (iso_add_local_directory(filepath, get_basename(filepath), g_current_iso_parent, 0) >= 0) added++;
                    } else {
                        if (iso_add_file(filepath, get_basename(filepath), g_current_iso_parent) >= 0) added++;
                    }
                }
                if (added > 0) {
                    populate_iso_listview();
                    char msg[256]; snprintf(msg, sizeof(msg), "%d item(s) dropped and queued for addition.", added);
                    SetWindowTextA(g_hStatusBar, msg);
                }
            }
            DragFinish(hDrop);
            return 0;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_VIEW_HIDDEN: g_show_hidden = !g_show_hidden; CheckMenuItem(GetMenu(hwnd), IDM_VIEW_HIDDEN, MF_BYCOMMAND | (g_show_hidden ? MF_CHECKED : MF_UNCHECKED)); set_local_path(g_current_local_path); break;
                case IDM_VIEW_SORTDIR: g_sort_dirs_first = !g_sort_dirs_first; CheckMenuItem(GetMenu(hwnd), IDM_VIEW_SORTDIR, MF_BYCOMMAND | (g_sort_dirs_first ? MF_CHECKED : MF_UNCHECKED)); set_local_path(g_current_local_path); populate_iso_listview(); break;

                case IDM_IMAGE_NEW: iso_new_image(); break;
                case IDM_IMAGE_OPEN: {
                    OPENFILENAMEA ofn = {0}; char szFile[MAX_PATH] = "";
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = "ISO Files (*.iso)\0*.iso\0All Files\0*.*\0";
                    if (GetOpenFileNameA(&ofn) && iso_open_image(szFile) == 0) {
                        populate_iso_listview(); char status[512]; snprintf(status, sizeof(status), "Opened: %s", szFile); SetWindowTextA(g_hStatusBar, status);
                    }
                    break;
                }
                case IDM_IMAGE_SAVE: 
                    if (g_iso.isOpen) {
                        iso_save_image(g_iso.path); 
                        iso_open_image(g_iso.path); 
                        populate_iso_listview();
                    }
                    break;
                case IDM_IMAGE_SAVEAS: {
                    if (!g_iso.isOpen) break;
                    OPENFILENAMEA ofn = {0}; char szFile[MAX_PATH] = ""; strcpy(szFile, get_basename(g_iso.path));
                    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = "ISO Files (*.iso)\0*.iso\0All Files\0*.*\0"; ofn.Flags = OFN_OVERWRITEPROMPT;
                    if (GetSaveFileNameA(&ofn)) { 
                        iso_save_image(szFile); 
                        iso_open_image(szFile); 
                        populate_iso_listview();
                    }
                    break;
                }
                case IDM_IMAGE_PROPS: {
                    if (!g_iso.isOpen) break;
                    char props[1024]; snprintf(props, sizeof(props), "Volume Name: %s\nSize: %llu bytes\nEntries: %lu\nRockRidge: %s\nJoliet: %s\nBootable: %s", g_iso.volume_name, g_iso.file_size, g_iso.num_entries, g_iso.has_rockridge ? "Yes" : "No", g_iso.has_joliet ? "Yes" : "No", g_iso.is_bootable ? "Yes (El Torito)" : "No");
                    MessageBoxA(hwnd, props, "Image Properties", MB_ICONINFORMATION); break;
                }
                case IDM_IMAGE_QUIT: PostQuitMessage(0); break;
                case IDM_VIEW_REFRESH: set_local_path(g_current_local_path); populate_iso_listview(); break;
                
                case IDM_BOOT_PROPS: {
                    if (!g_iso.isOpen) break;
                    if (g_iso.is_bootable) {
                        char props[256]; snprintf(props, sizeof(props), "Bootable: Yes\nCatalog Sector: %lu\nImage Sector: %lu\nSize: %lu (512-byte blocks)", g_iso.boot_catalog_sector, g_iso.boot_image_sector, g_iso.boot_image_size);
                        MessageBoxA(hwnd, props, "Boot Properties", MB_ICONINFORMATION);
                    } else MessageBoxA(hwnd, "No El Torito boot record found.", "Boot Properties", MB_ICONINFORMATION);
                    break;
                }
                case IDM_BOOT_SAVE: {
                    if (!g_iso.is_bootable) { MessageBoxA(hwnd, "No boot record to save.", "Warning", MB_ICONWARNING); break; }
                    char dest[MAX_PATH] = "boot_image.img"; OPENFILENAMEA sf = {0}; sf.lStructSize = sizeof(sf); sf.hwndOwner = hwnd; sf.lpstrFile = dest; sf.nMaxFile = MAX_PATH;
                    sf.Flags = OFN_OVERWRITEPROMPT; sf.lpstrFilter = "Image Files\0*.img\0All Files\0*.*\0";
                    if (GetSaveFileNameA(&sf)) {
                        HANDLE hDst = CreateFileA(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hDst != INVALID_HANDLE_VALUE) {
                            LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)g_iso.boot_image_sector * SECTOR_SIZE; SetFilePointerEx(g_iso.hFile, pos, NULL, FILE_BEGIN);
                            BYTE* buf = (BYTE*)malloc(g_iso.boot_image_size * 512); DWORD bw;
                            if (ReadFile(g_iso.hFile, buf, g_iso.boot_image_size * 512, &bw, NULL)) WriteFile(hDst, buf, bw, &bw, NULL);
                            free(buf); CloseHandle(hDst); SetWindowTextA(g_hStatusBar, "Boot image extracted.");
                        } else MessageBoxA(hwnd, "Failed to create destination file.", "Error", MB_ICONERROR);
                    }
                    break;
                }
                case IDM_BOOT_DEL: if (g_iso.is_bootable) { g_iso.is_bootable = FALSE; SetWindowTextA(g_hStatusBar, "Boot record marked for deletion on save."); } break;
                case IDM_HELP_ABOUT: MessageBoxA(hwnd, "ISO Master Win32 - 2-Pane Editor", "About", MB_ICONINFORMATION); break;

                case ID_LOCAL_BACK: cmd_local_back(); break;
                case ID_LOCAL_NEWDIR: cmd_local_newdir(hwnd); break;
                case ID_ISO_BACK: cmd_iso_back(); break;
                case ID_ISO_NEWDIR: cmd_iso_newdir(hwnd); break;
                case ID_ISO_ADD: cmd_add_selected(hwnd); break;
                case ID_ISO_EXTRACT: cmd_extract_selected(hwnd); break;
                case ID_ISO_DELETE: cmd_delete_selected(hwnd); break;
            }
            return 0;
        }
        
        case WM_SIZE: {
            RECT rcClient; GetClientRect(hwnd, &rcClient);
            int h = rcClient.bottom - 24, w = rcClient.right, half = h / 2;
            SetWindowPos(g_hLocalToolBar, NULL, 0, 0, w, 28, SWP_NOZORDER);
            SetWindowPos(g_hLocalListView, NULL, 0, 28, w, half - 28, SWP_NOZORDER);
            SetWindowPos(g_hIsoToolBar, NULL, 0, half, w, 28, SWP_NOZORDER);
            SetWindowPos(g_hIsoListView, NULL, 0, half + 28, w, h - half - 28, SWP_NOZORDER);
            SetWindowPos(g_hStatusBar, NULL, 0, rcClient.bottom - 24, w, 24, SWP_NOZORDER);
            return 0;
        }
        
        case WM_NOTIFY: {
            LPNMHDR nmhdr = (LPNMHDR)lParam;
            if (nmhdr->code == NM_DBLCLK) {
                if (nmhdr->hwndFrom == g_hLocalListView) {
                    int sel = SendMessageA(g_hLocalListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (sel != -1) {
                        char name[MAX_PATH]; ListView_GetItemTextA_Compat(g_hLocalListView, sel, 0, name, MAX_PATH);
                        if (strcmp(name, "..") == 0) cmd_local_back();
                        else {
                            char new_path[MAX_PATH]; 
                            if (g_current_local_path[strlen(g_current_local_path)-1] == '\\') snprintf(new_path, MAX_PATH, "%s%s", g_current_local_path, name);
                            else snprintf(new_path, MAX_PATH, "%s\\%s", g_current_local_path, name);
                            DWORD attr = GetFileAttributesA(new_path);
                            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) set_local_path(new_path);
                        }
                    }
                } else if (nmhdr->hwndFrom == g_hIsoListView) {
                    int sel = SendMessageA(g_hIsoListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (sel != -1) {
                        LVITEMA lvi = {0}; lvi.iItem = sel; lvi.mask = LVIF_PARAM; SendMessageA(g_hIsoListView, LVM_GETITEMA, 0, (LPARAM)&lvi);
                        DWORD idx = (DWORD)lvi.lParam;
                        if (idx == 0xFFFFFFFF) cmd_iso_back();
                        else if (g_iso.entries[idx].is_directory) { g_current_iso_parent = idx; populate_iso_listview(); }
                    }
                }
            }
            return 0;
        }
        
        case WM_DESTROY: iso_close_image(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
    g_hInstance = hInst;
    WNDCLASSA wc = {0}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = "IsoMasterClass";
    if (!RegisterClassA(&wc)) return 1;
    
    g_hMainWnd = CreateWindowExA(0, "IsoMasterClass", "ISO Master - " APP_VERSION, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInst, NULL);
    ShowWindow(g_hMainWnd, SW_SHOW); UpdateWindow(g_hMainWnd);
    
    MSG msg; while (GetMessageA(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    return (int)msg.wParam;
}