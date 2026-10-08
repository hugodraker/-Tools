/*
 * ============================================================================
 * Explorer for Win32 - Old Windows Style Shell
 * ============================================================================
 *
* COMPILATION INSTRUCTIONS:
 * Using GCC on Windows (MinGW/MinGW-w64):
 *   gcc -Os -s -mwindows -o e1plorer.exe e1plorer.c -lshell32 -lcomdlg32 -lgdi32 -lcomctl32 -lole32 -luuid
 *
 * PUBLIC DOMAIN NOTICE
 * Free and unencumbered software released into the public domain.
 * ============================================================================
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <tchar.h>
#pragma comment(lib, "comctl32.lib")
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <commdlg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <ctype.h>
#include <malloc.h>
#include <commctrl.h>
#ifndef SS_NOTIFY
#define SS_NOTIFY 0x0100L
#endif
#ifndef DEFAULT_GUI_FONT
#define DEFAULT_GUI_FONT ANSI_VAR_FONT
#endif
#ifndef WS_EX_CLIENTEDGE
#define WS_EX_CLIENTEDGE 0x00000200L
#endif
#ifndef GWL_ID
#define GWL_ID (-12)
#endif
#define MAX_PATH 260
#define MAX_INI_SHORTCUTS 256
#define MAX_EXPANDED_NODES 64
#define MAX_LIST_ITEMS 2048
#define MAX_TREE_LEVEL 16
#define CELL_W (g_IconSizeLarge + 45)
#define CELL_H (g_IconSizeLarge + 60)
#define ID_TREE 101
#define ID_LIST 102
#define ID_TOOLBAR 103
#define ID_HDR_NAME 201
#define ID_HDR_SIZE 202
#define ID_HDR_TYPE 203
#define ID_HDR_DATE 204
#define ID_HDR_INFOLDER 205
#define PROMPT_RENAME 1
#define PROMPT_NEWFOLDER 2
#define SEARCH_MAX_ITEMS 250000
// --- Definitions & Global State ---
#define MAX_RECENTS 30
#define IDC_MYTOOLBAR 1001
#define IDC_MYADDRESSBAR 1002
#define ID_VIEW_TOOLBAR 2001 // Example menu ID to toggle visibility

TCHAR g_recents[MAX_RECENTS][MAX_PATH];
int g_recentCount = 0;

HWND hToolBar = NULL;
HWND hAddressBar = NULL;

typedef struct {
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
} 
MY_DROPFILES;

typedef struct {
    char pathOrId[MAX_PATH];
    BOOL isVirtual;
    int activeTab;
    int viewMode;
} 
WindowState;

typedef struct {
    char id[16];
    char name[64];
    char exe[MAX_PATH];
    char params[MAX_PATH];
    char icon[MAX_PATH];
    char hotkey[16];
    char parentId[16];
    int minimized;
    int isFolder;
    char minimizedStr[8];
} 
IniShortcut;

typedef struct {
    char pathOrId[MAX_PATH];
    char displayName[64];
    int level;
    BOOL hasChildren;
    BOOL expanded;
    BOOL isVirtual;
    BOOL isLastChild[MAX_TREE_LEVEL];
    char expandId[MAX_PATH + 32];
} 
TreeItemData;

typedef struct {
    int type; /* 1 = ListItem */
    char name[64];
    char path[MAX_PATH];
    unsigned long size;
    unsigned date;
    unsigned time;
    BOOL isDir;
    BOOL isVirtual;
    char ext[16];
} 
ListItemData;

typedef struct {
    int type; /* 2 = RowItem */
    int count;
    ListItemData FAR* items[16];
} 
RowItemData;

typedef struct {
    char name[MAX_PATH];
} 
TempSubDir;

/* --- Global State --- */
IniShortcut FAR* g_IniShortcuts[MAX_INI_SHORTCUTS];
int g_IniShortcutCount = 0;
char g_szExplorerIni[MAX_PATH];

char FAR* g_ExpandedNodes[MAX_EXPANDED_NODES];
int g_ExpandedCount = 0;

HINSTANCE g_hInst;
HWND g_hwndMain;
BOOL g_bIsWindowed = FALSE;

HBRUSH g_hbrDesktop = NULL;
HBRUSH g_hbrWindow = NULL;
HBRUSH g_hbrHighlight = NULL;

int g_SplitX = 200;
int g_SortCol = 0;   
int g_SortOrder = 1; 
int g_GlobalViewMode = 3; 
BOOL g_bDraggingSplitter = FALSE;
BOOL g_bShowToolbar = TRUE;
BOOL g_bShowStatusBar = TRUE;
BOOL g_bStopSearch = FALSE;

int g_WinX = CW_USEDEFAULT;
int g_WinY = CW_USEDEFAULT;
int g_WinW = 600;
int g_WinH = 400;

char g_ContextId[MAX_PATH] = "";
BOOL g_ContextIsFolder = FALSE;
BOOL g_ContextIsVirtual = TRUE;
char g_EditShortcutId[16] = "";
int g_PromptMode = 0;
char g_PromptValue[MAX_PATH] = "";
char g_PromptLabel[64] = "";

char g_SelectedListItemPath[MAX_PATH] = "";
char g_SelectedListItemName[64] = "";
BOOL g_SelectedListItemIsVirtual = FALSE;
BOOL g_SelectedListItemIsDir = FALSE;

char g_ClipPath[MAX_PATH] = "";
char g_ClipName[64] = "";
char g_ClipId[16] = "";
BOOL g_ClipIsVirtual = FALSE;
BOOL g_ClipIsDir = FALSE;
int g_ClipOp = 0; 

typedef struct {
    char src[16384];
    char dst[MAX_PATH];
    BOOL isMove;
    BOOL isDir;
} 
CopyJob;

CopyJob g_CurrentJob;

char g_DelPath[MAX_PATH] = "";
BOOL g_DelIsDir = FALSE;
BOOL g_bCancelDel = FALSE;
HWND g_hDelDlg = NULL;

int g_ReplaceMode = 0; 
char g_ReplaceTarget[MAX_PATH] = "";
int g_ProgressTick = 0;
int g_ReplaceResult = 0;

/* --- Forward Declarations --- */
// --- COM OLE Drag & Drop Implementation ---
typedef struct { IDropSourceVtbl *lpVtbl; LONG refCount; } 
CDropSource;
void AddRecent(HWND hComboBox, const char* newPath) 
{
    if (!newPath || lstrlen(newPath) == 0) return;
    
    char normPath[MAX_PATH];
    lstrcpyn(normPath, newPath, MAX_PATH);
    
    // Normalize path to prevent duplicates: strip trailing slash unless it's a root drive (e.g. "C:\")
    int len = lstrlen(normPath);
    if (len > 3 && normPath[len - 1] == '\\') {
        normPath[len - 1] = '\0';
    }

    // 1. Check for duplicates (case-insensitive) and find its index
    int dupIndex = -1;
    for (int i = 0; i < g_recentCount; i++) 
    {
        if (lstrcmpi(g_recents[i], normPath) == 0) 
        {
            dupIndex = i;
            break;
        }
    }

    // 2. If it is already the newest item, do nothing
    if (dupIndex == 0) return;

    // 3. Shift older items down to make room at index 0
    int startShift = (dupIndex != -1) ? dupIndex : 
                     (g_recentCount < MAX_RECENTS ? g_recentCount : MAX_RECENTS - 1);
                     
    for (int i = startShift; i > 0; i--) 
    {
        lstrcpy(g_recents[i], g_recents[i-1]);
    }

    // 4. Insert the new path at the top
    lstrcpy(g_recents[0], normPath);
    if (g_recentCount < MAX_RECENTS && dupIndex == -1) g_recentCount++;

    // 5. Update the ComboBox UI
    SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_recentCount; i++) 
    {
        SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)g_recents[i]);
    }
    SendMessage(hComboBox, CB_SETCURSEL, 0, 0); // Select the top item

    // 6. Save back to recents.csv
    FILE *fp = fopen("recents.csv", "w");
    if (fp) 
    {
        for (int i = 0; i < g_recentCount; i++) 
        {
            fprintf(fp, "%s\n", g_recents[i]);
        }
        fclose(fp);
    }
}
LRESULT CALLBACK AddressBarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) 
{
    // Catch the Enter key in the internal Edit control of the ComboBox
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) 
    {
        HWND hComboBox = GetParent(hWnd);
        HWND hParent = GetParent(hComboBox);
        
        // Forward custom execution command '9999' to the parent FolderWndProc
        SendMessage(hParent, WM_COMMAND, MAKEWPARAM(IDC_MYADDRESSBAR, 9999), (LPARAM)hComboBox);
        
        return 0; // Consume the key so Windows doesn't play a "ding" error sound
    }
    
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}
HRESULT STDMETHODCALLTYPE DS_QueryInterface(IDropSource* This, REFIID riid, void** ppv) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropSource)) { *ppv = This; This->lpVtbl->AddRef(This); return S_OK; }
    *ppv = NULL; return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE DS_AddRef(IDropSource* This) { CDropSource* pDS = (CDropSource*)This; return InterlockedIncrement(&pDS->refCount); }
ULONG STDMETHODCALLTYPE DS_Release(IDropSource* This) { CDropSource* pDS = (CDropSource*)This; ULONG count = InterlockedDecrement(&pDS->refCount); if (count == 0) free(pDS); return count; }
HRESULT STDMETHODCALLTYPE DS_QueryContinueDrag(IDropSource* This, BOOL fEscapePressed, DWORD grfKeyState) {
    if (fEscapePressed) return DRAGDROP_S_CANCEL;
    if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE DS_GiveFeedback(IDropSource* This, DWORD dwEffect) { return DRAGDROP_S_USEDEFAULTCURSORS; }
static IDropSourceVtbl DropSourceVtbl = { DS_QueryInterface, DS_AddRef, DS_Release, DS_QueryContinueDrag, DS_GiveFeedback };

IDropSource* CreateDropSource() {
    CDropSource* pDS = (CDropSource*)malloc(sizeof(CDropSource)); pDS->lpVtbl = &DropSourceVtbl; pDS->refCount = 1; return (IDropSource*)pDS;
}
BOOL HasSubDirsVirtual(const char* parentId) {
    int i;
    for (i = 0; i < g_IniShortcutCount; i++) {
        if (g_IniShortcuts[i]->isFolder && lstrcmp(g_IniShortcuts[i]->parentId, parentId) == 0 && lstrcmp(g_IniShortcuts[i]->name, "-") != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL HasSubDirsFS(const char* path) {
    HANDLE hFind; WIN32_FIND_DATA file; char searchPath[MAX_PATH + 16];
    lstrcpy(searchPath, path); 
    if (searchPath[0] != '\0' && searchPath[lstrlen(searchPath)-1] != '\\') lstrcat(searchPath, "\\"); 
    lstrcat(searchPath, "*.*");
    hFind = FindFirstFile(searchPath, &file);
    if (hFind != INVALID_HANDLE_VALUE) { 
        do { 
            if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && lstrcmp(file.cFileName, ".") != 0 && lstrcmp(file.cFileName, "..") != 0) { 
                FindClose(hFind); return TRUE; 
            } 
        } while (FindNextFile(hFind, &file)); 
        FindClose(hFind); 
    }
    return FALSE;
}

void InitiateOleDragDropMultiple(HWND hwndSource, char** filePaths, int count) {
    HRESULT hr; IShellFolder *pParentFolder = NULL; 
    LPCITEMIDLIST *apidl = (LPCITEMIDLIST*)malloc(count * sizeof(LPCITEMIDLIST));
    LPITEMIDLIST *fullPidls = (LPITEMIDLIST*)malloc(count * sizeof(LPITEMIDLIST));
    int validCount = 0; IDataObject *pDataObject = NULL; DWORD dwEffect = 0; WCHAR wPath[MAX_PATH];

    OleInitialize(NULL);
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            MultiByteToWideChar(CP_ACP, 0, filePaths[i], -1, wPath, MAX_PATH);
            if (SUCCEEDED(SHParseDisplayName(wPath, NULL, &fullPidls[validCount], 0, NULL))) {
                LPCITEMIDLIST pChild = NULL;
                IShellFolder* pFolder = NULL;
                if (SUCCEEDED(SHBindToParent(fullPidls[validCount], &IID_IShellFolder, (void**)&pFolder, &pChild))) {
                    if (i == 0) pParentFolder = pFolder;
                    else pFolder->lpVtbl->Release(pFolder);
                    apidl[validCount] = pChild;
                    validCount++;
                }
            }
        }
        
        if (validCount > 0 && pParentFolder) {
            hr = pParentFolder->lpVtbl->GetUIObjectOf(pParentFolder, hwndSource, validCount, apidl, &IID_IDataObject, NULL, (void**)&pDataObject);
            if (SUCCEEDED(hr) && pDataObject) {
                IDropSource* pDropSource = CreateDropSource();
                DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY | DROPEFFECT_LINK | DROPEFFECT_MOVE, &dwEffect);
                pDropSource->lpVtbl->Release(pDropSource);
                pDataObject->lpVtbl->Release(pDataObject);
            }
            pParentFolder->lpVtbl->Release(pParentFolder);
        }
        for (int i = 0; i < validCount; i++) CoTaskMemFree(fullPidls[i]);
    }
    free(apidl); free(fullPidls);
    OleUninitialize();
}
void InitiateOleDragDrop(HWND hwndSource, const char* fullFilePath) {
    HRESULT hr; LPITEMIDLIST pidlFull = NULL; IShellFolder *pParentFolder = NULL; LPCITEMIDLIST pidlChild = NULL; IDataObject *pDataObject = NULL; DWORD dwEffect = 0; WCHAR wPath[MAX_PATH];
    OleInitialize(NULL);
    MultiByteToWideChar(CP_ACP, 0, fullFilePath, -1, wPath, MAX_PATH);
    hr = SHParseDisplayName(wPath, NULL, &pidlFull, 0, NULL);
    if (SUCCEEDED(hr)) {
        hr = SHBindToParent(pidlFull, &IID_IShellFolder, (void**)&pParentFolder, &pidlChild);
        if (SUCCEEDED(hr)) {
            hr = pParentFolder->lpVtbl->GetUIObjectOf(pParentFolder, hwndSource, 1, &pidlChild, &IID_IDataObject, NULL, (void**)&pDataObject);
            if (SUCCEEDED(hr)) {
                IDropSource* pDropSource = CreateDropSource();
                DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY | DROPEFFECT_LINK | DROPEFFECT_MOVE, &dwEffect);
                pDropSource->lpVtbl->Release(pDropSource); pDataObject->lpVtbl->Release(pDataObject);
            }
            pParentFolder->lpVtbl->Release(pParentFolder);
        }
        CoTaskMemFree(pidlFull);
    }
    OleUninitialize();
}

/* --- Forward Declarations --- */
LRESULT CALLBACK DesktopProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK FolderWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SearchWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TreeProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ToolbarProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InlineEditProc(HWND, UINT, WPARAM, LPARAM);

static FARPROC g_lpfnOldTreeProc = NULL;
static FARPROC g_lpfnOldListProc = NULL;
static FARPROC g_lpfnOldToolbarProc = NULL;
static FARPROC g_lpfnOldInlineEditProc = NULL;

static BOOL g_bListDragging = FALSE;
static POINT g_DragStartPt;
static char g_InlineRenameOldPath[MAX_PATH];
static char g_InlineRenameId[16];
static BOOL g_InlineRenameIsVirtual;

/* --- Add these two variables near the top with your other globals --- */
int g_IconSizeLarge = 32;
int g_IconSizeSmall = 16;

// --- Tracking Variables for Tree/List features ---
static BOOL g_bTreeDragging = FALSE;
static POINT g_TreeDragStartPt;
static char g_SearchBuf[64] = "";
static DWORD g_SearchLastTick = 0;
static BOOL g_bClickOnSelected = FALSE;

static void RebuildTree(HWND hTree, WindowState FAR* state);
static void RebuildList(HWND hwnd, WindowState FAR* state);
static void LoadConfig(void);
static void SaveConfig(void);
static void LoadIniShortcuts(void);
void ChangeViewMode(HWND hwnd, int mode, WindowState FAR* state);
static void SaveIniEntry(IniShortcut FAR* item);
void HandleListCommand(HWND hwndParent, WPARAM wp, LPARAM lp, WindowState FAR* state);
static void ShowContextMenu(HWND hwnd, int x, int y, BOOL isDir, BOOL isBackground, int currentViewMode);
void GetNewIniId(char* outId);
void DoRecursiveSearch(ListItemData FAR* FAR* arr, ListItemData FAR* block, const char* searchDir, const char* pattern, int* count, int filterType, unsigned long filterBytes, const char* containing);
void LayoutListItems(HWND hList, ListItemData FAR* FAR* arr, int count, int viewMode);
void HandleDrawItem(HWND hwnd, WPARAM wp, LPARAM lp);

void ProcessMessages(void);
void GetExtension(const char* filename, char* extOut) {
    char* dot = strrchr(filename, '.');
    if (dot) { int i = 0; dot++; while (*dot && i < 15) { extOut[i++] = (char)toupper((unsigned char)*dot); dot++; } extOut[i] = '\0'; } else lstrcpy(extOut, "");
}

void DoRecursiveSearch(ListItemData FAR* FAR* arr, ListItemData FAR* block, const char* searchDir, const char* pattern, int* count, int filterType, unsigned long filterBytes, const char* containing) {
    HANDLE hFind; WIN32_FIND_DATA file; char searchPath[MAX_PATH + 16]; char FAR* FAR* subDirs; int dirCount = 0, i;
    int subCapacity = 128;
    
    if (*count >= SEARCH_MAX_ITEMS || g_bStopSearch) return;

    if (lstrcmp(searchDir, "0") == 0) {
        for (i = 0; i < g_IniShortcutCount; i++) {
            if (*count >= SEARCH_MAX_ITEMS || g_bStopSearch) return;
            if (lstrcmp(g_IniShortcuts[i]->name, "-") != 0) {
                BOOL match = TRUE;
                if (lstrcmp(pattern, "*.*") != 0 && lstrcmp(pattern, "*") != 0) {
                    char patBase[64]; lstrcpy(patBase, pattern);
                    char cleanPat[64]; int idx=0, j=0;
                    while(patBase[idx]) { if (patBase[idx]!='*' && patBase[idx]!='?') cleanPat[j++] = patBase[idx]; idx++; }
                    cleanPat[j] = '\0';
                    if (cleanPat[0] != '\0') {
                        char nameLower[64], patLower[64];
                        lstrcpy(nameLower, g_IniShortcuts[i]->name); AnsiLower((LPSTR)nameLower);
                        lstrcpy(patLower, cleanPat); AnsiLower((LPSTR)patLower);
                        if (strstr(nameLower, patLower) == NULL) match = FALSE;
                    }
                }
                if (match) {
                    ListItemData FAR* item = &block[*count];
                    lstrcpy(item->name, g_IniShortcuts[i]->name);
                    if (g_IniShortcuts[i]->isFolder && g_IniShortcuts[i]->exe[0] != '\0') {
                        lstrcpy(item->path, g_IniShortcuts[i]->exe);
                        item->isVirtual = FALSE;
                    } else {
                        lstrcpy(item->path, g_IniShortcuts[i]->id);
                        item->isVirtual = TRUE;
                    }
                    item->isDir = g_IniShortcuts[i]->isFolder;
                    GetExtension(item->name, item->ext);
                    if (item->isDir) lstrcpy(item->ext, "");
                    item->size = 0; item->date = 0; item->time = 0; item->type = 1;
                    arr[*count] = item; (*count)++;
                }
            }
        }
        return;
    }
    
    lstrcpy(searchPath, searchDir); if (searchPath[0] && searchPath[lstrlen(searchPath)-1] != '\\') lstrcat(searchPath, "\\"); lstrcat(searchPath, pattern);
    
    hFind = FindFirstFile(searchPath, &file);
    if (hFind != INVALID_HANDLE_VALUE) {
        do { 
            ProcessMessages(); if (g_bStopSearch) { FindClose(hFind); return; }
            if (lstrcmp(file.cFileName, ".") != 0 && lstrcmp(file.cFileName, "..") != 0 && *count < SEARCH_MAX_ITEMS) { 
                BOOL match = TRUE; 
                if (filterBytes > 0) { if (filterType == 0 && file.nFileSizeLow < filterBytes) match = FALSE; if (filterType == 1 && file.nFileSizeLow > filterBytes) match = FALSE; } 
                if (match && containing && containing[0] != '\0' && !(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    char fullPath[MAX_PATH + 16]; lstrcpy(fullPath, searchDir); if (fullPath[0] && fullPath[lstrlen(fullPath)-1] != '\\') lstrcat(fullPath, "\\"); lstrcat(fullPath, file.cFileName);
                    FILE* f = fopen(fullPath, "rb"); if (f) { match = FALSE; char* buf = (char*)malloc(1025); if (buf) { size_t bytes; while ((bytes = fread(buf, 1, 1024, f)) > 0) { buf[bytes] = '\0'; if (strstr(buf, containing) != NULL) { match = TRUE; break; } if (g_bStopSearch) break; ProcessMessages(); } free(buf); } fclose(f); } else match = FALSE;
                }
                if (match) { 
                    ListItemData FAR* item = &block[*count]; 
                    lstrcpy(item->name, file.cFileName); lstrcpy(item->path, searchDir); if (item->path[0] && item->path[lstrlen(item->path)-1] != '\\') lstrcat(item->path, "\\"); lstrcat(item->path, file.cFileName); item->isDir = FALSE; item->isVirtual = FALSE; item->size = file.nFileSizeLow; 
                    WORD dosDate = 0, dosTime = 0; FileTimeToDosDateTime(&file.ftLastWriteTime, &dosDate, &dosTime);
                    item->date = dosDate; item->time = dosTime; 
                    GetExtension(file.cFileName, item->ext); item->type = 1; arr[*count] = item; (*count)++; 
                } 
            } 
        } while (FindNextFile(hFind, &file) && *count < SEARCH_MAX_ITEMS); 
        FindClose(hFind);
    }
    
    subDirs = (char FAR* FAR*)malloc(subCapacity * sizeof(char FAR*));
    if (!subDirs) return;
    
    lstrcpy(searchPath, searchDir); if (searchPath[0] && searchPath[lstrlen(searchPath)-1] != '\\') lstrcat(searchPath, "\\"); lstrcat(searchPath, "*.*");
    
    hFind = FindFirstFile(searchPath, &file);
    if (hFind != INVALID_HANDLE_VALUE) { 
        do { 
            if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && lstrcmp(file.cFileName, ".") != 0 && lstrcmp(file.cFileName, "..") != 0) { 
                if (dirCount >= subCapacity) {
                    subCapacity *= 2;
                    subDirs = (char FAR* FAR*)realloc(subDirs, subCapacity * sizeof(char FAR*));
                }
                subDirs[dirCount] = (char FAR*)malloc(MAX_PATH); 
                if (subDirs[dirCount]) lstrcpy(subDirs[dirCount++], file.cFileName); 
            } 
        } while (FindNextFile(hFind, &file)); 
        FindClose(hFind);
    }
    
    for (i = 0; i < dirCount; i++) {
        if (!g_bStopSearch && *count < SEARCH_MAX_ITEMS) {
            char nextDir[MAX_PATH + 16];
            lstrcpy(nextDir, searchDir); if (nextDir[0] && nextDir[lstrlen(nextDir)-1] != '\\') lstrcat(nextDir, "\\"); lstrcat(nextDir, subDirs[i]);
            DoRecursiveSearch(arr, block, nextDir, pattern, count, filterType, filterBytes, containing);
        }
        free(subDirs[i]);
    }
    free(subDirs);
}

// --- Helper Functions for History ---

void LoadRecents(HWND hComboBox) 
{
    FILE *fp = _tfopen(_T("recents.csv"), _T("r"));
    SendMessage(hComboBox, CB_RESETCONTENT, 0, 0);
    g_recentCount = 0;

    if (fp) 
    {
        TCHAR line[MAX_PATH];
        while (_fgetts(line, MAX_PATH, fp) && g_recentCount < MAX_RECENTS) 
        {
            // Strip trailing newlines
            size_t len = _tcslen(line);
            while (len > 0 && (line[len-1] == _T('\n') || line[len-1] == _T('\r'))) {
                line[--len] = _T('\0');
            }

            if (len > 0) 
            {
                _tcscpy_s(g_recents[g_recentCount++], MAX_PATH, line);
                SendMessage(hComboBox, CB_ADDSTRING, 0, (LPARAM)line);
            }
        }
        fclose(fp);
    }
}

LRESULT CALLBACK OptionsDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: {
            char buf[32];
            CreateWindow("STATIC", "Large Icon Size:", WS_CHILD|WS_VISIBLE, 10, 15, 120, 20, hwnd, NULL, g_hInst, NULL);
            sprintf(buf, "%d", g_IconSizeLarge);
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 140, 12, 60, 22, hwnd, (HMENU)101, g_hInst, NULL);
            
            CreateWindow("STATIC", "Small Icon Size:", WS_CHILD|WS_VISIBLE, 10, 45, 120, 20, hwnd, NULL, g_hInst, NULL);
            sprintf(buf, "%d", g_IconSizeSmall);
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, 140, 42, 60, 22, hwnd, (HMENU)102, g_hInst, NULL);
            
            CreateWindow("BUTTON", "Show Toolbar", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 10, 80, 150, 20, hwnd, (HMENU)103, g_hInst, NULL);
            if (g_bShowToolbar) SendMessage(GetDlgItem(hwnd, 103), BM_SETCHECK, 1, 0);
            
            CreateWindow("BUTTON", "Show Status Bar", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 10, 105, 150, 20, hwnd, (HMENU)104, g_hInst, NULL);
            if (g_bShowStatusBar) SendMessage(GetDlgItem(hwnd, 104), BM_SETCHECK, 1, 0);
            
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 30, 150, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 130, 150, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            return 0;
        }
        case WM_COMMAND: {
            if (wp == IDOK) {
                char buf[32];
                if (GetWindowText(GetDlgItem(hwnd, 101), buf, 32)) g_IconSizeLarge = atoi(buf);
                if (GetWindowText(GetDlgItem(hwnd, 102), buf, 32)) g_IconSizeSmall = atoi(buf);
                if (g_IconSizeLarge < 16) g_IconSizeLarge = 16;
                if (g_IconSizeSmall < 8) g_IconSizeSmall = 8;
                
                g_bShowToolbar = SendMessage(GetDlgItem(hwnd, 103), BM_GETCHECK, 0, 0);
                g_bShowStatusBar = SendMessage(GetDlgItem(hwnd, 104), BM_GETCHECK, 0, 0);
                
                SaveConfig();
                
                HWND hParent = GetParent(hwnd);
                if (hParent) {
                    EnableWindow(hParent, TRUE);
                    RECT rc; GetClientRect(hParent, &rc);
                    SendMessage(hParent, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom));
                    InvalidateRect(hParent, NULL, TRUE);
                }
                DestroyWindow(hwnd);
            } else if (wp == IDCANCEL) {
                HWND hParent = GetParent(hwnd); if (hParent) EnableWindow(hParent, TRUE);
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE: { HWND hParent = GetParent(hwnd); if (hParent) EnableWindow(hParent, TRUE); DestroyWindow(hwnd); return 0; }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK SearchButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hWnd);
        // Pressing Enter when a button has focus triggers that specific button
        PostMessage(hParent, WM_COMMAND, MAKEWPARAM(GetWindowLong(hWnd, GWL_ID), BN_CLICKED), (LPARAM)hWnd);
        return 0;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK SearchEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hWnd);
        // Pressing Enter in any text field triggers Find Now (IDOK)
        PostMessage(hParent, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)GetDlgItem(hParent, IDOK));
        return 0; // Consume the key
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ShowTabControls(HWND hwnd, int tab) {
    int i;
    for (i = 600; i <= 610; i++) ShowWindow(GetDlgItem(hwnd, i), tab == 0 ? SW_SHOW : SW_HIDE);
    for (i = 700; i <= 710; i++) ShowWindow(GetDlgItem(hwnd, i), tab == 1 ? SW_SHOW : SW_HIDE);
    for (i = 800; i <= 810; i++) ShowWindow(GetDlgItem(hwnd, i), tab == 2 ? SW_SHOW : SW_HIDE);
}

void ProcessMessages(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
}

void UpdateProgressGauge(HWND hDlg) {
    g_ProgressTick = (g_ProgressTick + 5) % 100;
    HWND hGauge = GetDlgItem(hDlg, 102);
    if (hGauge) {
        HDC hdc = GetDC(hGauge); RECT rc; GetClientRect(hGauge, &rc);
        int w = (rc.right * g_ProgressTick) / 100;
        RECT rFill = rc; rFill.right = w; FillRect(hdc, &rFill, g_hbrHighlight);
        RECT rClear = rc; rClear.left = w; FillRect(hdc, &rClear, g_hbrWindow);
        ReleaseDC(hGauge, hdc);
    }
}

int ShowModalReplaceDialog(HWND hParent) {
    HWND hDlg; MSG msg; int res; g_ReplaceResult = 0;
    hDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "ReplaceDlgClass", "Confirm Replace", WS_POPUP|WS_CAPTION|WS_VISIBLE|WS_SYSMENU, (GetSystemMetrics(SM_CXSCREEN)-320)/2, (GetSystemMetrics(SM_CYSCREEN)-140)/2, 320, 140, hParent, NULL, g_hInst, NULL);
    EnableWindow(hParent, FALSE);
    while (g_ReplaceResult == 0 && GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    res = g_ReplaceResult; EnableWindow(hParent, TRUE); DestroyWindow(hDlg);
    return res;
}

BOOL DoCopyFile(HWND hDlg, const char* src, const char* dst) {
    FILE *fs, *fd; char *buf; size_t n;
    if (g_bCancelDel) return FALSE;
    if (access(dst, 0) == 0) {
        if (g_ReplaceMode == 1) { } else if (g_ReplaceMode == 2) { return TRUE; } 
        else { lstrcpy(g_ReplaceTarget, dst); int res = ShowModalReplaceDialog(hDlg); if (res == 4) { g_ReplaceMode = 3; return FALSE; } if (res == 3) { return TRUE; } if (res == 2) { g_ReplaceMode = 1; } }
    }
    if (g_ReplaceMode == 3) return FALSE;
    fs = fopen(src, "rb"); fd = fopen(dst, "wb"); if (!fs || !fd) { if (fs) fclose(fs); if (fd) fclose(fd); return FALSE; }
    buf = (char*)malloc(4096);
    if (buf) { while ((n = fread(buf, 1, 4096, fs)) > 0) { fwrite(buf, 1, n, fd); UpdateProgressGauge(hDlg); ProcessMessages(); if (g_bCancelDel) break; } free(buf); }
    fclose(fs); fclose(fd); if (g_bCancelDel) remove(dst); return !g_bCancelDel;
}

void DoCopyFolder(HWND hDlg, const char* src, const char* dst) {
    HANDLE hFind; WIN32_FIND_DATA file; char *search; char *sPath; char *dPath; char FAR* FAR* subDirs; int dirCount = 0; int i;
    char srcPrefix[MAX_PATH + 2]; lstrcpy(srcPrefix, src); if (srcPrefix[0] != '\0' && srcPrefix[lstrlen(srcPrefix)-1] != '\\') lstrcat(srcPrefix, "\\");
    char dstPrefix[MAX_PATH + 2]; lstrcpy(dstPrefix, dst); if (dstPrefix[0] != '\0' && dstPrefix[lstrlen(dstPrefix)-1] != '\\') lstrcat(dstPrefix, "\\");
    if (strnicmp(dstPrefix, srcPrefix, lstrlen(srcPrefix)) == 0) { MessageBox(hDlg, "Cannot copy a folder into itself.", "Error", MB_OK|MB_ICONHAND); g_ReplaceMode = 3; return; }
    
    mkdir(dst); search = (char*)malloc(MAX_PATH + 16); if (!search) return;
    lstrcpy(search, src); if (search[0] != '\0' && search[lstrlen(search)-1] != '\\') lstrcat(search, "\\"); lstrcat(search, "*.*");
    subDirs = (char FAR* FAR*)malloc(512 * sizeof(char FAR*));
    hFind = FindFirstFile(search, &file);
    if (hFind != INVALID_HANDLE_VALUE) {
        sPath = (char*)malloc(MAX_PATH + 16); dPath = (char*)malloc(MAX_PATH + 16);
        if (sPath && dPath && subDirs) {
            do {
                if (g_ReplaceMode == 3 || g_bCancelDel) break;
                UpdateProgressGauge(hDlg); ProcessMessages();
                if (lstrcmp(file.cFileName, ".") != 0 && lstrcmp(file.cFileName, "..") != 0) {
                    if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        if (dirCount < 512) { subDirs[dirCount] = (char FAR*)malloc(16); if (subDirs[dirCount]) lstrcpy(subDirs[dirCount++], file.cFileName); }
                    } else {
                        lstrcpy(sPath, src); if (sPath[lstrlen(sPath)-1] != '\\') lstrcat(sPath, "\\"); lstrcat(sPath, file.cFileName);
                        lstrcpy(dPath, dst); if (dPath[lstrlen(dPath)-1] != '\\') lstrcat(dPath, "\\"); lstrcat(dPath, file.cFileName);
                        DoCopyFile(hDlg, sPath, dPath);
                    }
                }
            } while (FindNextFile(hFind, &file));
            FindClose(hFind);
            for (i = 0; i < dirCount; i++) {
                if (g_ReplaceMode != 3 && !g_bCancelDel) {
                    lstrcpy(sPath, src); if (sPath[lstrlen(sPath)-1] != '\\') lstrcat(sPath, "\\"); lstrcat(sPath, subDirs[i]);
                    lstrcpy(dPath, dst); if (dPath[lstrlen(dPath)-1] != '\\') lstrcat(dPath, "\\"); lstrcat(dPath, subDirs[i]);
                    DoCopyFolder(hDlg, sPath, dPath);
                } free(subDirs[i]);
            }
        } if (sPath) free(sPath); if (dPath) free(dPath);
    } if (subDirs) free(subDirs); free(search);
}

BOOL DoDeleteFolder(const char* path) {
    HANDLE hFind; WIN32_FIND_DATA file; char *search; char *child; char FAR* FAR* subDirs; int dirCount = 0; int i;
    search = (char*)malloc(MAX_PATH + 16); if (!search) return FALSE;
    lstrcpy(search, path); if (search[0] != '\0' && search[lstrlen(search)-1] != '\\') lstrcat(search, "\\"); lstrcat(search, "*.*");
    subDirs = (char FAR* FAR*)malloc(512 * sizeof(char FAR*));
    hFind = FindFirstFile(search, &file);
    if (hFind != INVALID_HANDLE_VALUE) {
        child = (char*)malloc(MAX_PATH + 16);
        if (child && subDirs) {
            do {
                if (g_bCancelDel) break;
                if (g_hDelDlg) UpdateProgressGauge(g_hDelDlg); ProcessMessages();
                if (lstrcmp(file.cFileName, ".") != 0 && lstrcmp(file.cFileName, "..") != 0) {
                    if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        if (dirCount < 512) { subDirs[dirCount] = (char FAR*)malloc(16); if (subDirs[dirCount]) lstrcpy(subDirs[dirCount++], file.cFileName); }
                    } else {
                        lstrcpy(child, path); if (child[lstrlen(child)-1] != '\\') lstrcat(child, "\\"); lstrcat(child, file.cFileName);
                        if (g_hDelDlg) { HWND hLbl = GetDlgItem(g_hDelDlg, 101); if (hLbl) { SetWindowText(hLbl, child); UpdateWindow(hLbl); } }
                        remove(child);
                    }
                }
            } while (FindNextFile(hFind, &file));
            FindClose(hFind);
            for (i = 0; i < dirCount; i++) {
                if (!g_bCancelDel) {
                    lstrcpy(child, path); if (child[lstrlen(child)-1] != '\\') lstrcat(child, "\\"); lstrcat(child, subDirs[i]);
                    if (g_hDelDlg) { HWND hLbl = GetDlgItem(g_hDelDlg, 101); if (hLbl) { SetWindowText(hLbl, child); UpdateWindow(hLbl); } }
                    DoDeleteFolder(child); if (!g_bCancelDel) rmdir(child);
                } free(subDirs[i]);
            }
        } if (child) free(child);
    } if (subDirs) free(subDirs); free(search); return !g_bCancelDel;
}

BOOL NameExists(const char* parentId, const char* name, BOOL isVirtual) {
    if (isVirtual) { int i; for (i=0; i<g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i]->parentId, parentId) == 0 && lstrcmpi(g_IniShortcuts[i]->name, name) == 0) return TRUE; } } 
    else { char path[MAX_PATH + 16]; lstrcpy(path, parentId); if (path[0] && path[lstrlen(path)-1] != '\\') lstrcat(path, "\\"); lstrcat(path, name); if (access(path, 0) == 0) return TRUE; } return FALSE;
}

void FormatDateStr(unsigned date, unsigned time, char* out) {
    int y = (date >> 9) + 1980; int m = ((date >> 5) & 0x0F); int d = (date & 0x1F); int h = (time >> 11); int min = ((time >> 5) & 0x3F);
    sprintf(out, "%02d/%02d/%04d %02d:%02d", m, d, y, h, min);
}

void FormatSizeStr(unsigned long bytes, char* out) {
    if (bytes < 1024) sprintf(out, "%lu bytes", bytes); else if (bytes < 1048576) sprintf(out, "%lu KB", bytes / 1024); else sprintf(out, "%lu.%02lu MB", bytes / 1048576, (bytes % 1048576) * 100 / 1048576);
}

BOOL BrowseFile(HWND hwnd, char* outPath, const char* filter) {
    OPENFILENAME ofn; memset(&ofn, 0, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFilter = filter; ofn.lpstrFile = outPath; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY; outPath[0] = '\0';
    return GetOpenFileName(&ofn);
}

void CreateCenteredDialog(HINSTANCE hInst, HWND hwndParent, const char* className, const char* title, int width, int height) {
    int cx = GetSystemMetrics(SM_CXSCREEN); int cy = GetSystemMetrics(SM_CYSCREEN);
    CreateWindowEx(WS_EX_DLGMODALFRAME, className, title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, (cx - width) / 2, (cy - height) / 2, width, height, hwndParent, NULL, hInst, NULL);
    EnableWindow(hwndParent, FALSE);
}

BOOL IsExpanded(const char* expandId) {
    int i; for (i = 0; i < g_ExpandedCount; i++) { if (g_ExpandedNodes[i] && lstrcmpi(g_ExpandedNodes[i], expandId) == 0) return TRUE; } return FALSE;
}

void ToggleExpand(const char* expandId) {
    int i;
    for (i = 0; i < g_ExpandedCount; i++) { if (g_ExpandedNodes[i] && lstrcmpi(g_ExpandedNodes[i], expandId) == 0) { free(g_ExpandedNodes[i]); g_ExpandedCount--; if (i < g_ExpandedCount) g_ExpandedNodes[i] = g_ExpandedNodes[g_ExpandedCount]; return; } }
    if (g_ExpandedCount < MAX_EXPANDED_NODES) { g_ExpandedNodes[g_ExpandedCount] = (char FAR*)malloc(MAX_PATH+32); if (g_ExpandedNodes[g_ExpandedCount]) { lstrcpy(g_ExpandedNodes[g_ExpandedCount++], expandId); } }
}

void ExpandAllParentsVirtual(const char* id) {
    int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i]->id, id) == 0) { if (lstrcmp(g_IniShortcuts[i]->parentId, "0") != 0) { ExpandAllParentsVirtual(g_IniShortcuts[i]->parentId); } break; } }
    if (!IsExpanded(id)) ToggleExpand(id);
}

void ExpandAllParentsFS(const char* path) {
    char temp[MAX_PATH]; char* p; lstrcpy(temp, path); p = temp; if (!IsExpanded("0")) ToggleExpand("0");
    char bestVirtualId[16] = ""; int bestMatchLen = 0;
    for (int i = 0; i < g_IniShortcutCount; i++) {
        if (g_IniShortcuts[i]->isFolder && g_IniShortcuts[i]->exe[0] != '\0') {
            int len = lstrlen(g_IniShortcuts[i]->exe);
            if (strnicmp(path, g_IniShortcuts[i]->exe, len) == 0) { if (len > bestMatchLen) { bestMatchLen = len; lstrcpy(bestVirtualId, g_IniShortcuts[i]->id); } }
        }
    }
    while (*p) { 
        if (*p == '\\' && p > temp) { 
            char saved = *(p+1); *(p+1) = '\0'; char expandId[MAX_PATH + 32];
            if (bestVirtualId[0]) sprintf(expandId, "%s|%s", bestVirtualId, temp); else lstrcpy(expandId, temp);
            if (!IsExpanded(expandId)) ToggleExpand(expandId); 
            *(p+1) = saved; 
        } p++; 
    }
    char finalExpandId[MAX_PATH + 32];
    if (bestVirtualId[0]) sprintf(finalExpandId, "%s|%s", bestVirtualId, path); else lstrcpy(finalExpandId, path);
    if (!IsExpanded(finalExpandId)) ToggleExpand(finalExpandId);
}

static void ParseIniEntry(const char* id, const char* val, IniShortcut FAR* out) {
    const char* p = val; int i; char FAR* dests[7]; int maxLens[7] = {64, MAX_PATH, MAX_PATH, MAX_PATH, 8, 16, 16};
    dests[0] = out->name; dests[1] = out->exe; dests[2] = out->params; dests[3] = out->icon; dests[4] = out->minimizedStr; dests[5] = out->parentId; dests[6] = out->hotkey;
    lstrcpy(out->id, id);
    for (i = 0; i < 7; i++) { char FAR* d = dests[i]; int c = 0; int maxL = maxLens[i]; while (*p && *p != '|') { if (c < maxL - 1) d[c++] = *p; p++; } d[c] = '\0'; if (*p == '|') p++; }
    { int flags = atoi(out->minimizedStr); out->minimized = (flags & 1) != 0; out->isFolder = (flags & 2) != 0; if (out->exe[0] == '\0' && flags == 0 && lstrcmp(out->minimizedStr, "0") == 0) out->isFolder = TRUE; }
}

static void LoadIniShortcuts(void) {
    char *keys = (char*)malloc(4096); char *val = (char*)malloc(512); char *pKey, *p; int i;
    for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i]) { free(g_IniShortcuts[i]); g_IniShortcuts[i] = NULL; } }
    g_IniShortcutCount = 0; if (!keys || !val) { if (keys) free(keys); if (val) free(val); return; }
    
    GetModuleFileName(g_hInst, g_szExplorerIni, MAX_PATH); p = strrchr(g_szExplorerIni, '\\');
    if (p) { *(p + 1) = '\0'; lstrcat(g_szExplorerIni, "e1plorer.ini"); } else { lstrcpy(g_szExplorerIni, "e1plorer.ini"); }
    
    GetPrivateProfileString("Shortcut", NULL, "", keys, 4096, g_szExplorerIni);
    
    pKey = keys;
    while (*pKey) {
        if (g_IniShortcutCount < MAX_INI_SHORTCUTS) {
            g_IniShortcuts[g_IniShortcutCount] = (IniShortcut FAR*)malloc(sizeof(IniShortcut));
            if (g_IniShortcuts[g_IniShortcutCount]) { GetPrivateProfileString("Shortcut", pKey, "", val, 512, g_szExplorerIni); ParseIniEntry(pKey, val, g_IniShortcuts[g_IniShortcutCount]); g_IniShortcutCount++; }
        } pKey += lstrlen(pKey) + 1;
    }
    free(keys); free(val);
    
    {
        DWORD drives = GetLogicalDrives();
        int d;
        for (d = 0; d < 26 && g_IniShortcutCount < MAX_INI_SHORTCUTS; d++) {
            if (drives & (1 << d)) {
                char drvPath[8]; sprintf(drvPath, "%c:\\", 'A' + d);
                int type = GetDriveType(drvPath);
                BOOL exists = FALSE; int j;
                for (j = 0; j < g_IniShortcutCount; j++) { if (lstrcmpi(g_IniShortcuts[j]->exe, drvPath) == 0 && lstrcmp(g_IniShortcuts[j]->parentId, "0") == 0) { exists = TRUE; break; } }
                if (!exists) {
                    IniShortcut FAR* sh = (IniShortcut FAR*)malloc(sizeof(IniShortcut));
                    if (sh) { 
                        sprintf(sh->id, "DRV_%c", 'A' + d); 
                        if (type == DRIVE_CDROM) sprintf(sh->name, "CD Drive (%c:)", 'A' + d);
                        else if (type == DRIVE_REMOVABLE) sprintf(sh->name, "Removable Disk (%c:)", 'A' + d);
                        else if (type == DRIVE_REMOTE) sprintf(sh->name, "Network Drive (%c:)", 'A' + d);
                        else sprintf(sh->name, "Local Disk (%c:)", 'A' + d);
                        lstrcpy(sh->exe, drvPath); sh->params[0] = '\0'; sh->icon[0] = '\0'; sh->hotkey[0] = '\0'; lstrcpy(sh->parentId, "0"); sh->minimized = 0; sh->isFolder = TRUE; lstrcpy(sh->minimizedStr, "2"); g_IniShortcuts[g_IniShortcutCount++] = sh; 
                    }
                }
            }
        }
    }
}
void GetNewIniId(char* outId) {
    int maxId = 0, i; for (i = 0; i < g_IniShortcutCount; i++) { int id = atoi(g_IniShortcuts[i]->id); if (id > maxId) maxId = id; } sprintf(outId, "%08d", maxId + 1);
}

static void SaveIniEntry(IniShortcut FAR* item) {
    char *val = (char*)malloc(1024); int flags; if (!val) return;
    flags = (item->minimized ? 1 : 0) | (item->isFolder ? 2 : 0);
    sprintf(val, "%s|%s|%s|%s|%d|%s|%s", item->name, item->exe, item->params, item->icon, flags, item->parentId, item->hotkey);
    WritePrivateProfileString("Shortcut", item->id, val, g_szExplorerIni); free(val);
}

static void LoadConfig(void) {
    LoadIniShortcuts();
    g_SplitX = GetPrivateProfileInt("Settings", "SplitX", 200, g_szExplorerIni); 
    g_SortCol = GetPrivateProfileInt("Settings", "SortCol", 0, g_szExplorerIni); 
    g_SortOrder = GetPrivateProfileInt("Settings", "SortOrder", 1, g_szExplorerIni); 
    g_bShowToolbar = GetPrivateProfileInt("Settings", "Toolbar", 1, g_szExplorerIni); 
    g_bShowStatusBar = GetPrivateProfileInt("Settings", "StatusBar", 1, g_szExplorerIni); 
    g_GlobalViewMode = GetPrivateProfileInt("Settings", "ViewMode", 3, g_szExplorerIni);
    g_IconSizeLarge = GetPrivateProfileInt("Settings", "IconSizeLarge", 32, g_szExplorerIni);
    g_IconSizeSmall = GetPrivateProfileInt("Settings", "IconSizeSmall", 16, g_szExplorerIni);
    
    g_WinX = GetPrivateProfileInt("Settings", "WinX", CW_USEDEFAULT, g_szExplorerIni);
    g_WinY = GetPrivateProfileInt("Settings", "WinY", CW_USEDEFAULT, g_szExplorerIni);
    g_WinW = GetPrivateProfileInt("Settings", "WinW", 600, g_szExplorerIni);
    g_WinH = GetPrivateProfileInt("Settings", "WinH", 400, g_szExplorerIni);
    
    if (g_SplitX < 50) g_SplitX = 50; 
    if (g_GlobalViewMode < 0 || g_GlobalViewMode > 3) g_GlobalViewMode = 3;
    if (g_WinW < 100) g_WinW = 600;
    if (g_WinH < 100) g_WinH = 400;
    if (g_IconSizeLarge < 16) g_IconSizeLarge = 16;
    if (g_IconSizeSmall < 8) g_IconSizeSmall = 8;
}

static void SaveConfig(void) {
    char buf[16];
    sprintf(buf, "%d", g_SplitX); WritePrivateProfileString("Settings", "SplitX", buf, g_szExplorerIni); 
    sprintf(buf, "%d", g_SortCol); WritePrivateProfileString("Settings", "SortCol", buf, g_szExplorerIni); 
    sprintf(buf, "%d", g_SortOrder); WritePrivateProfileString("Settings", "SortOrder", buf, g_szExplorerIni); 
    sprintf(buf, "%d", g_bShowToolbar); WritePrivateProfileString("Settings", "Toolbar", buf, g_szExplorerIni); 
    sprintf(buf, "%d", g_bShowStatusBar); WritePrivateProfileString("Settings", "StatusBar", buf, g_szExplorerIni); 
    sprintf(buf, "%d", g_GlobalViewMode); WritePrivateProfileString("Settings", "ViewMode", buf, g_szExplorerIni);
    sprintf(buf, "%d", g_IconSizeLarge); WritePrivateProfileString("Settings", "IconSizeLarge", buf, g_szExplorerIni);
    sprintf(buf, "%d", g_IconSizeSmall); WritePrivateProfileString("Settings", "IconSizeSmall", buf, g_szExplorerIni);
    
    sprintf(buf, "%d", g_WinX); WritePrivateProfileString("Settings", "WinX", buf, g_szExplorerIni);
    sprintf(buf, "%d", g_WinY); WritePrivateProfileString("Settings", "WinY", buf, g_szExplorerIni);
    sprintf(buf, "%d", g_WinW); WritePrivateProfileString("Settings", "WinW", buf, g_szExplorerIni);
    sprintf(buf, "%d", g_WinH); WritePrivateProfileString("Settings", "WinH", buf, g_szExplorerIni);
}

int CompareListItems(ListItemData FAR* FAR* a, ListItemData FAR* FAR* b) {
    ListItemData FAR* ia = *a; ListItemData FAR* ib = *b; int cmp = 0;
    if (ia->isDir != ib->isDir) return ib->isDir - ia->isDir;
    switch (g_SortCol) { case 0: cmp = lstrcmpi(ia->name, ib->name); break; case 1: cmp = (ia->size > ib->size) ? 1 : (ia->size < ib->size ? -1 : 0); break; case 2: cmp = lstrcmpi(ia->ext, ib->ext); if(cmp==0) cmp = lstrcmpi(ia->name, ib->name); break; case 3: if (ia->date != ib->date) cmp = (ia->date > ib->date) ? 1 : -1; else cmp = (ia->time > ib->time) ? 1 : (ia->time < ib->time ? -1 : 0); break; case 4: cmp = lstrcmpi(ia->path, ib->path); if (cmp == 0) cmp = lstrcmpi(ia->name, ib->name); break; }
    return cmp * g_SortOrder;
}

void SortListItems(ListItemData FAR* FAR* arr, int count) {
    int gap = count; BOOL swapped = TRUE; int i;
    while (gap > 1 || swapped) { gap = (gap * 10) / 13; if (gap == 9 || gap == 10) gap = 11; if (gap < 1) gap = 1; swapped = FALSE; for (i = 0; i < count - gap; i++) { if (CompareListItems(&arr[i], &arr[i + gap]) > 0) { ListItemData FAR* temp = arr[i]; arr[i] = arr[i + gap]; arr[i + gap] = temp; swapped = TRUE; } } }
}

int CompareSubDirs(TempSubDir FAR* FAR* a, TempSubDir FAR* FAR* b) { TempSubDir FAR* ia = *a; TempSubDir FAR* ib = *b; return lstrcmpi(ia->name, ib->name); }

void SortSubDirs(TempSubDir FAR* FAR* arr, int count) {
    int gap = count; BOOL swapped = TRUE; int i;
    while (gap > 1 || swapped) { gap = (gap * 10) / 13; if (gap == 9 || gap == 10) gap = 11; if (gap < 1) gap = 1; swapped = FALSE; for (i = 0; i < count - gap; i++) { if (CompareSubDirs(&arr[i], &arr[i + gap]) > 0) { TempSubDir FAR* temp = arr[i]; arr[i] = arr[i + gap]; arr[i + gap] = temp; swapped = TRUE; } } }
}

void AddTreeItem(HWND hTree, const char* name, const char* pathOrId, int level, BOOL hasChildren, BOOL isVirtual, BOOL* parentLastChildArr, BOOL isLast, const char* expandId) {
    TreeItemData FAR* item = (TreeItemData FAR*)malloc(sizeof(TreeItemData)); int pos, i;
    if (!item) return; lstrcpy(item->pathOrId, pathOrId); lstrcpyn(item->displayName, name, 63); item->level = level; item->hasChildren = hasChildren; item->expanded = IsExpanded(expandId); item->isVirtual = isVirtual; lstrcpy(item->expandId, expandId);
    for (i = 0; i < level; i++) item->isLastChild[i] = parentLastChildArr[i]; item->isLastChild[level] = isLast;
    pos = SendMessage(hTree, LB_ADDSTRING, 0, (LPARAM)item); if (pos < 0) { free(item); }
}

void RecursiveAddTreeFS(HWND hTree, const char* path, int level, BOOL* parentLastChildArr, const char* shortcutId) {
    HANDLE hFind; WIN32_FIND_DATA file; char searchPath[MAX_PATH + 16]; TempSubDir FAR* FAR* dirs; int dirCount = 0, i;
    dirs = (TempSubDir FAR* FAR*)malloc(256 * sizeof(TempSubDir FAR*)); if (!dirs) return;
    
    lstrcpy(searchPath, path); if (searchPath[0] != '\0' && searchPath[lstrlen(searchPath)-1] != '\\') lstrcat(searchPath, "\\"); lstrcat(searchPath, "*.*");
    hFind = FindFirstFile(searchPath, &file);
    if (hFind != INVALID_HANDLE_VALUE) { 
        do { 
            if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && lstrcmp(file.cFileName, ".") != 0 && lstrcmp(file.cFileName, "..") != 0 && dirCount < 256) { 
                dirs[dirCount] = (TempSubDir FAR*)malloc(sizeof(TempSubDir)); if (dirs[dirCount]) { lstrcpy(dirs[dirCount]->name, file.cFileName); dirCount++; } 
            } 
        } while (FindNextFile(hFind, &file)); 
        FindClose(hFind); 
    }
    SortSubDirs(dirs, dirCount);
    for (i = 0; i < dirCount; i++) {
        char fullPath[MAX_PATH + 16]; BOOL isExp, isLast = (i == dirCount - 1);
        lstrcpy(fullPath, path); if (fullPath[0] != '\0' && fullPath[lstrlen(fullPath)-1] != '\\') lstrcat(fullPath, "\\"); lstrcat(fullPath, dirs[i]->name);
        
        char expandId[MAX_PATH + 32];
        if (shortcutId && shortcutId[0]) sprintf(expandId, "%s|%s", shortcutId, fullPath); else lstrcpy(expandId, fullPath);

        BOOL hasSub = HasSubDirsFS(fullPath);
        isExp = IsExpanded(expandId); AddTreeItem(hTree, dirs[i]->name, fullPath, level, hasSub, FALSE, parentLastChildArr, isLast, expandId); 
        if (isExp && level < MAX_TREE_LEVEL - 1) { BOOL newArr[MAX_TREE_LEVEL]; int j; for (j = 0; j < level; j++) newArr[j] = parentLastChildArr[j]; newArr[level] = isLast; RecursiveAddTreeFS(hTree, fullPath, level + 1, newArr, shortcutId); }
        free(dirs[i]);
    } free(dirs);
}

void RecursiveAddTreeVirtual(HWND hTree, const char* parentId, int level, BOOL* parentLastChildArr) {
    int i, count = 0, current = 0;
    for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i]->isFolder && lstrcmp(g_IniShortcuts[i]->parentId, parentId) == 0 && lstrcmp(g_IniShortcuts[i]->name, "-") != 0) count++; }
    for (i = 0; i < g_IniShortcutCount; i++) {
        if (g_IniShortcuts[i]->isFolder && lstrcmp(g_IniShortcuts[i]->parentId, parentId) == 0 && lstrcmp(g_IniShortcuts[i]->name, "-") != 0) {
            BOOL isExp, isLast = (current == count - 1);
            if (g_IniShortcuts[i]->exe[0] != '\0') {
                char expandId[MAX_PATH + 32]; sprintf(expandId, "%s|%s", g_IniShortcuts[i]->id, g_IniShortcuts[i]->exe);
                BOOL hasSub = HasSubDirsFS(g_IniShortcuts[i]->exe);
                isExp = IsExpanded(expandId); AddTreeItem(hTree, g_IniShortcuts[i]->name, g_IniShortcuts[i]->exe, level, hasSub, FALSE, parentLastChildArr, isLast, expandId);
                if (isExp && level < MAX_TREE_LEVEL - 1) { BOOL newArr[MAX_TREE_LEVEL]; int j; for (j = 0; j < level; j++) newArr[j] = parentLastChildArr[j]; newArr[level] = isLast; RecursiveAddTreeFS(hTree, g_IniShortcuts[i]->exe, level + 1, newArr, g_IniShortcuts[i]->id); }
            } else {
                BOOL hasSub = HasSubDirsVirtual(g_IniShortcuts[i]->id);
                isExp = IsExpanded(g_IniShortcuts[i]->id); AddTreeItem(hTree, g_IniShortcuts[i]->name, g_IniShortcuts[i]->id, level, hasSub, TRUE, parentLastChildArr, isLast, g_IniShortcuts[i]->id);
                if (isExp && level < MAX_TREE_LEVEL - 1) { BOOL newArr[MAX_TREE_LEVEL]; int j; for (j = 0; j < level; j++) newArr[j] = parentLastChildArr[j]; newArr[level] = isLast; RecursiveAddTreeVirtual(hTree, g_IniShortcuts[i]->id, level + 1, newArr); }
            } current++;
        }
    }
}

static void RebuildTree(HWND hTree, WindowState FAR* state) {
    int i, count, targetOccurrence = 0, currentOccurrence = 0; char targetSel[MAX_PATH]; BOOL rootArr[MAX_TREE_LEVEL]; BOOL targetIsVirtual;
    int selIdx = SendMessage(hTree, LB_GETCURSEL, 0, 0);
    int topIdx = SendMessage(hTree, LB_GETTOPINDEX, 0, 0);
    if (selIdx != LB_ERR) {
        TreeItemData FAR* selItem = (TreeItemData FAR*)SendMessage(hTree, LB_GETITEMDATA, selIdx, 0);
        if (selItem) {
            for (i = 0; i < selIdx; i++) {
                TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hTree, LB_GETITEMDATA, i, 0);
                if (item && lstrcmpi(item->pathOrId, selItem->pathOrId) == 0 && item->isVirtual == selItem->isVirtual) targetOccurrence++;
            }
        }
    }
    memset(rootArr, 0, sizeof(rootArr)); SendMessage(hTree, WM_SETREDRAW, FALSE, 0); lstrcpy(targetSel, state->pathOrId); targetIsVirtual = state->isVirtual; SendMessage(hTree, LB_RESETCONTENT, 0, 0);
    AddTreeItem(hTree, "Desktop", "0", 0, HasSubDirsVirtual("0"), TRUE, rootArr, TRUE, "0");
    if (IsExpanded("0")) { BOOL newArr[MAX_TREE_LEVEL]; newArr[0] = TRUE; RecursiveAddTreeVirtual(hTree, "0", 1, newArr); }
    count = SendMessage(hTree, LB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) { 
        TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hTree, LB_GETITEMDATA, i, 0); 
        if (item && lstrcmpi(item->pathOrId, targetSel) == 0 && item->isVirtual == targetIsVirtual) { 
            if (currentOccurrence == targetOccurrence) { SendMessage(hTree, LB_SETCURSEL, i, 0); break; }
            currentOccurrence++;
        } 
    }
    SendMessage(hTree, LB_SETTOPINDEX, topIdx, 0);
    SendMessage(hTree, WM_SETREDRAW, TRUE, 0); InvalidateRect(hTree, NULL, TRUE);
}
void LayoutListItems(HWND hList, ListItemData FAR* FAR* arr, int count, int viewMode) {
    int i;
    SendMessage(hList, WM_SETREDRAW, FALSE, 0);
    if (viewMode == 0) {
        RECT rcList; GetClientRect(hList, &rcList); int listW = rcList.right - rcList.left; int colW = CELL_W; int cols, r;
        if (listW < colW) listW = colW; cols = listW / colW; if (cols > 16) cols = 16; if (cols < 1) cols = 1;
        for (r = 0; r < count; r += cols) {
            RowItemData FAR* row = (RowItemData FAR*)malloc(sizeof(RowItemData));
            if (row) { 
                int c, pos; row->type = 2; row->count = 0; 
                for (c = 0; c < cols && r + c < count; c++) { row->items[c] = arr[r + c]; row->items[c]->type = 1; row->count++; } 
                pos = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)row); 
                if (pos < 0) { free(row); } 
            }
        }
    } else {
        for (i = 0; i < count; i++) { arr[i]->type = 1; SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)arr[i]); }
    }
    SendMessage(hList, WM_SETREDRAW, TRUE, 0); InvalidateRect(hList, NULL, TRUE);
}

void ChangeViewMode(HWND hwnd, int mode, WindowState FAR* state) {
    HWND hOldList = GetDlgItem(hwnd, ID_LIST); char cls[64]; GetClassName(hwnd, cls, 64);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_EXTENDEDSEL; 
    BOOL isSearch = (lstrcmpi(cls, "Win95SearchClass") == 0);
    int savedCount = 0; ListItemData FAR* FAR* savedArr = NULL;
    
    if (lstrcmpi(cls, "Win95DesktopClass") != 0) style |= WS_BORDER;
    
    HMENU hMenu = GetMenu(hwnd); RECT rc;
    if (mode == 1 || mode == 2) style |= LBS_MULTICOLUMN | WS_HSCROLL; else style |= WS_VSCROLL;
    if (state) state->viewMode = mode;
    if (lstrcmpi(cls, "Win95FolderClass") == 0) { g_GlobalViewMode = mode; SaveConfig(); }
    if (hMenu) { CheckMenuItem(hMenu, 4022, MF_BYCOMMAND | (mode == 0 ? MF_CHECKED : MF_UNCHECKED)); CheckMenuItem(hMenu, 4023, MF_BYCOMMAND | (mode == 1 ? MF_CHECKED : MF_UNCHECKED)); CheckMenuItem(hMenu, 4024, MF_BYCOMMAND | (mode == 2 ? MF_CHECKED : MF_UNCHECKED)); CheckMenuItem(hMenu, 4025, MF_BYCOMMAND | (mode == 3 ? MF_CHECKED : MF_UNCHECKED)); }
    
    if (isSearch && hOldList) {
        int lbCount = SendMessage(hOldList, LB_GETCOUNT, 0, 0);
        if (lbCount > 0) {
            savedArr = (ListItemData FAR* FAR*)malloc(SEARCH_MAX_ITEMS * sizeof(ListItemData FAR*));
            if (savedArr) {
                for (int i = 0; i < lbCount; i++) {
                    int FAR* pType = (int FAR*)SendMessage(hOldList, LB_GETITEMDATA, i, 0);
                    if (pType && *pType == 1) savedArr[savedCount++] = (ListItemData FAR*)pType;
                    else if (pType && *pType == 2) {
                        RowItemData FAR* row = (RowItemData FAR*)pType;
                        for (int c = 0; c < row->count; c++) savedArr[savedCount++] = row->items[c];
                    }
                }
            }
        }
    }
    
    if (hOldList) DestroyWindow(hOldList);
    
    {
        HWND hNewList = CreateWindowEx(0, "LISTBOX", "", style | LBS_NOINTEGRALHEIGHT, 0, 0, 0, 0, hwnd, (HMENU)ID_LIST, g_hInst, NULL);
        SendMessage(hNewList, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
        if (mode == 1 || mode == 2) SendMessage(hNewList, LB_SETCOLUMNWIDTH, g_IconSizeSmall > 32 ? g_IconSizeSmall + 130 : 150, 0);
        g_lpfnOldListProc = (FARPROC)SetWindowLongPtr(hNewList, GWLP_WNDPROC, (LONG_PTR)ListProc);
    }
    
    GetClientRect(hwnd, &rc); SendMessage(hwnd, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom));
    if (state) {
        if (!isSearch) {
            RebuildList(hwnd, state);
        } else if (savedArr) {
            LayoutListItems(GetDlgItem(hwnd, ID_LIST), savedArr, savedCount, state->viewMode);
            free(savedArr);
        }
    }
}

static void RebuildList(HWND hwnd, WindowState FAR* state) {
    HWND hList = GetDlgItem(hwnd, ID_LIST); int i, count = 0; unsigned long totalBytes = 0; char statBuf[128]; char sizeStr[64];
    HANDLE oldBlock; ListItemData FAR* FAR* arr; ListItemData FAR* block; HANDLE hFind; WIN32_FIND_DATA file; char searchPath[MAX_PATH];
    
    // --- Sync Address Bar ---
    HWND hAddr = GetDlgItem(hwnd, IDC_MYADDRESSBAR);
    if (hAddr) {
        char addrText[MAX_PATH];
        if (state->isVirtual) {
            if (lstrcmp(state->pathOrId, "0") == 0) {
                lstrcpy(addrText, "Desktop");
            } else {
                BOOL found = FALSE;
                for (int k = 0; k < g_IniShortcutCount; k++) {
                    if (lstrcmp(g_IniShortcuts[k]->id, state->pathOrId) == 0) {
                        lstrcpy(addrText, g_IniShortcuts[k]->name);
                        found = TRUE; break;
                    }
                }
                if (!found) lstrcpy(addrText, state->pathOrId);
            }
        } else {
            lstrcpy(addrText, state->pathOrId);
        }
        SetWindowText(hAddr, addrText);
    }
    
    SendMessage(hList, WM_SETREDRAW, FALSE, 0);
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    
    oldBlock = GetProp(hwnd, "BulkBlock");
    if (oldBlock) { free((void FAR*)oldBlock); RemoveProp(hwnd, "BulkBlock"); }
    
    arr = (ListItemData FAR* FAR*)malloc(MAX_LIST_ITEMS * sizeof(ListItemData FAR*)); 
    block = (ListItemData FAR*)malloc(MAX_LIST_ITEMS * sizeof(ListItemData));
    if (!arr || !block) { if (arr) free(arr); if (block) free(block); return; }
    
    g_SelectedListItemPath[0] = '\0'; g_SelectedListItemName[0] = '\0';

    if (state->isVirtual) {
        for (i = 0; i < g_IniShortcutCount && count < MAX_LIST_ITEMS; i++) {
            if (lstrcmp(g_IniShortcuts[i]->parentId, state->pathOrId) == 0 && lstrcmp(g_IniShortcuts[i]->name, "-") != 0) {
                arr[count] = &block[count];
                lstrcpyn(arr[count]->name, g_IniShortcuts[i]->name, 63); 
                if (g_IniShortcuts[i]->isFolder && g_IniShortcuts[i]->exe[0] != '\0') { lstrcpy(arr[count]->path, g_IniShortcuts[i]->exe); arr[count]->isVirtual = FALSE; } else { lstrcpy(arr[count]->path, g_IniShortcuts[i]->id); arr[count]->isVirtual = TRUE; }
                arr[count]->isDir = g_IniShortcuts[i]->isFolder; GetExtension(arr[count]->name, arr[count]->ext); if (arr[count]->isDir) lstrcpy(arr[count]->ext, ""); 
                arr[count]->size = 0; arr[count]->date = 0; arr[count]->time = 0; count++; 
            }
        }
    } else {
        lstrcpy(searchPath, state->pathOrId);
        for (i = 0; i < g_IniShortcutCount && count < MAX_LIST_ITEMS; i++) {
            if (lstrcmp(g_IniShortcuts[i]->parentId, state->pathOrId) == 0 && lstrcmp(g_IniShortcuts[i]->name, "-") != 0) {
                arr[count] = &block[count];
                lstrcpyn(arr[count]->name, g_IniShortcuts[i]->name, 63); 
                if (g_IniShortcuts[i]->isFolder && g_IniShortcuts[i]->exe[0] != '\0') { lstrcpy(arr[count]->path, g_IniShortcuts[i]->exe); arr[count]->isVirtual = FALSE; } else { lstrcpy(arr[count]->path, g_IniShortcuts[i]->id); arr[count]->isVirtual = TRUE; }
                arr[count]->isDir = g_IniShortcuts[i]->isFolder; GetExtension(arr[count]->name, arr[count]->ext); if (arr[count]->isDir) lstrcpy(arr[count]->ext, ""); 
                arr[count]->size = 0; arr[count]->date = 0; arr[count]->time = 0; count++; 
            }
        }
        if (searchPath[0] != '\0' && searchPath[lstrlen(searchPath)-1] != '\\') lstrcat(searchPath, "\\"); lstrcat(searchPath, "*.*");
        
        hFind = FindFirstFile(searchPath, &file);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (lstrcmp(file.cFileName, ".") != 0 && lstrcmp(file.cFileName, "..") != 0 && count < MAX_LIST_ITEMS) {
                    arr[count] = &block[count];
                    lstrcpy(arr[count]->name, file.cFileName); lstrcpy(arr[count]->path, state->pathOrId); if (arr[count]->path[0] != '\0' && arr[count]->path[lstrlen(arr[count]->path)-1] != '\\') lstrcat(arr[count]->path, "\\"); lstrcat(arr[count]->path, file.cFileName); arr[count]->isDir = (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE; arr[count]->isVirtual = FALSE; arr[count]->size = file.nFileSizeLow; 
                    WORD dosDate = 0, dosTime = 0; FileTimeToDosDateTime(&file.ftLastWriteTime, &dosDate, &dosTime);
                    arr[count]->date = dosDate; arr[count]->time = dosTime; 
                    GetExtension(file.cFileName, arr[count]->ext); if (arr[count]->isDir) { lstrcpy(arr[count]->ext, ""); } else { totalBytes += file.nFileSizeLow; } count++; 
                }
            } while (FindNextFile(hFind, &file));
            FindClose(hFind);
        }
    }
    SortListItems(arr, count);
    LayoutListItems(hList, arr, count, state->viewMode);
    
    SetProp(hwnd, "BulkBlock", (HANDLE)block);
    free(arr);
    
    if (g_bShowStatusBar) { 
        FormatSizeStr(totalBytes, sizeStr); sprintf(statBuf, " %d object(s)     %s", count, sizeStr); 
        HWND hStat = GetDlgItem(hwnd, 300); if (hStat) SetWindowText(hStat, statBuf); 
    }
}
static void Draw3DButton(HDC hdc, int left, int top, int right, int bottom, BOOL bPushed) {
    HPEN hHi = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT)); HPEN hSh = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)); HPEN hOld = SelectObject(hdc, bPushed ? hSh : hHi);
    MoveToEx(hdc, left, bottom - 1, NULL); LineTo(hdc, left, top); LineTo(hdc, right - 1, top); SelectObject(hdc, bPushed ? hHi : hSh); MoveToEx(hdc, right - 1, top, NULL); LineTo(hdc, right - 1, bottom - 1); LineTo(hdc, left, bottom - 1);
    SelectObject(hdc, hOld); DeleteObject(hHi); DeleteObject(hSh);
}

static void DrawGDIFolder(HDC hdc, int x, int y, BOOL bOpen, BOOL bSelected, BOOL bLarge) {
    HBRUSH hBr = CreateSolidBrush(bSelected ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 128)); HBRUSH hOld = SelectObject(hdc, hBr); HPEN hPen = GetStockObject(BLACK_PEN); HPEN hOldP = SelectObject(hdc, hPen);
    int size = bLarge ? g_IconSizeLarge : g_IconSizeSmall;
    int w = size;
    int h = (size * 3) / 4;
    int tabW = w / 3;
    int tabH = h / 4;
    if (bOpen) {
        Rectangle(hdc, x + 2, y, x + tabW, y + tabH);
        Rectangle(hdc, x, y + tabH - 2, x + w - 2, y + h);
        POINT pts[4];
        pts[0].x = x; pts[0].y = y + h;
        pts[1].x = x + w / 4; pts[1].y = y + tabH + 2;
        pts[2].x = x + w + w / 8; pts[2].y = y + tabH + 2;
        pts[3].x = x + w - 2; pts[3].y = y + h;
        Polygon(hdc, pts, 4);
    } else {
        Rectangle(hdc, x + 2, y, x + tabW, y + tabH);
        Rectangle(hdc, x, y + tabH - 2, x + w, y + h);
    }
    SelectObject(hdc, hOldP); SelectObject(hdc, hOld); DeleteObject(hBr);
}
static void DrawGDIFile(HDC hdc, int x, int y, BOOL bSelected, BOOL bLarge) {
    HBRUSH hBr = CreateSolidBrush(bSelected ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 255)); HBRUSH hOld = SelectObject(hdc, hBr); HPEN hPen = GetStockObject(BLACK_PEN); HPEN hOldP = SelectObject(hdc, hPen);
    int size = bLarge ? g_IconSizeLarge : g_IconSizeSmall;
    int w = (size * 3) / 4; 
    int h = size;
    int fold = w / 3;
    POINT pts[5] = { {x, y}, {x + w - fold, y}, {x + w, y + fold}, {x + w, y + h}, {x, y + h} };
    Polygon(hdc, pts, 5);
    MoveToEx(hdc, x + w - fold, y, NULL); LineTo(hdc, x + w - fold, y + fold); LineTo(hdc, x + w, y + fold);
    SelectObject(hdc, hOldP); SelectObject(hdc, hOld); DeleteObject(hBr);
}

static void DrawPlusMinus(HDC hdc, int x, int y, BOOL bExpanded) {
    HPEN hPen = GetStockObject(BLACK_PEN); HPEN hOldP = SelectObject(hdc, hPen); HBRUSH hBr = GetStockObject(WHITE_BRUSH); HBRUSH hOld = SelectObject(hdc, hBr);
    Rectangle(hdc, x, y, x+9, y+9); MoveToEx(hdc, x+2, y+4, NULL); LineTo(hdc, x+7, y+4); if (!bExpanded) { MoveToEx(hdc, x+4, y+2, NULL); LineTo(hdc, x+4, y+7); }
    SelectObject(hdc, hOld); SelectObject(hdc, hOldP);
}

static void ShowContextMenu(HWND hwnd, int x, int y, BOOL isDir, BOOL isBackground, int currentViewMode) {
    HMENU hCtx = CreatePopupMenu();
    if (isBackground) {
        AppendMenu(hCtx, MF_STRING | (currentViewMode == 0 ? MF_CHECKED : 0), 4022, "Lar&ge Icons"); AppendMenu(hCtx, MF_STRING | (currentViewMode == 1 ? MF_CHECKED : 0), 4023, "S&mall Icons"); AppendMenu(hCtx, MF_STRING | (currentViewMode == 2 ? MF_CHECKED : 0), 4024, "&List"); AppendMenu(hCtx, MF_STRING | (currentViewMode == 3 ? MF_CHECKED : 0), 4025, "&Details"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); 
        AppendMenu(hCtx, MF_STRING | (g_ClipOp == 0 ? MF_GRAYED : 0), 4013, "&Paste"); AppendMenu(hCtx, MF_STRING | (g_ClipOp == 0 ? MF_GRAYED : 0), 4014, "Paste &Shortcut"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4001, "New &Folder"); AppendMenu(hCtx, MF_STRING, 4002, "New &Shortcut"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4005, "P&roperties");
    } else {
        if (isDir) { AppendMenu(hCtx, MF_STRING, 5001, "&Explore"); AppendMenu(hCtx, MF_STRING, 5002, "&Open"); AppendMenu(hCtx, MF_STRING, 5003, "&Find..."); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); } else { AppendMenu(hCtx, MF_STRING, 5002, "&Open"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); }
        AppendMenu(hCtx, MF_STRING, 4011, "Cu&t"); AppendMenu(hCtx, MF_STRING, 4012, "&Copy"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4002, "Create &Shortcut"); AppendMenu(hCtx, MF_STRING, 4003, "&Delete"); AppendMenu(hCtx, MF_STRING, 4004, "Re&name"); AppendMenu(hCtx, MF_SEPARATOR, 0, NULL); AppendMenu(hCtx, MF_STRING, 4005, "P&roperties");
    } TrackPopupMenu(hCtx, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL); DestroyMenu(hCtx);
}

LRESULT CALLBACK FilePropDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static BOOL isDrive = FALSE;
    static char rootPath[8];
    switch(msg) {
        case WM_CREATE: {
            isDrive = FALSE;
            if (lstrlen(g_ContextId) <= 3 && g_ContextId[1] == ':') {
                isDrive = TRUE;
                lstrcpyn(rootPath, g_ContextId, 4);
                if (lstrlen(rootPath) == 2) { rootPath[2] = '\\'; rootPath[3] = '\0'; }
            }
            
            if (isDrive) {
                char volName[MAX_PATH] = "";
                GetVolumeInformation(rootPath, volName, MAX_PATH, NULL, NULL, NULL, NULL, 0);
                CreateWindow("STATIC", "Volume Label:", WS_CHILD|WS_VISIBLE, 10, 10, 100, 20, hwnd, NULL, g_hInst, NULL);
                CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", volName, WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL, 120, 10, 200, 22, hwnd, (HMENU)201, g_hInst, NULL);
                
                ULARGE_INTEGER freeBytes, totalBytes;
                if (GetDiskFreeSpaceEx(rootPath, &freeBytes, &totalBytes, NULL)) {
                    unsigned long long total = totalBytes.QuadPart;
                    unsigned long long freeB = freeBytes.QuadPart;
                    unsigned long long used = total - freeB;
                    char buf[64];
                    
                    CreateWindow("STATIC", "Used Space:", WS_CHILD|WS_VISIBLE, 10, 50, 100, 20, hwnd, NULL, g_hInst, NULL);
                    sprintf(buf, "%I64u bytes", used);
                    CreateWindow("STATIC", buf, WS_CHILD|WS_VISIBLE, 120, 50, 200, 20, hwnd, NULL, g_hInst, NULL);
                    
                    CreateWindow("STATIC", "Free Space:", WS_CHILD|WS_VISIBLE, 10, 80, 100, 20, hwnd, NULL, g_hInst, NULL);
                    sprintf(buf, "%I64u bytes", freeB);
                    CreateWindow("STATIC", buf, WS_CHILD|WS_VISIBLE, 120, 80, 200, 20, hwnd, NULL, g_hInst, NULL);
                    
                    CreateWindow("STATIC", "Capacity:", WS_CHILD|WS_VISIBLE, 10, 110, 100, 20, hwnd, NULL, g_hInst, NULL);
                    sprintf(buf, "%I64u bytes", total);
                    CreateWindow("STATIC", buf, WS_CHILD|WS_VISIBLE, 120, 110, 200, 20, hwnd, NULL, g_hInst, NULL);
                }
            } else {
                HANDLE hFind; WIN32_FIND_DATA f; char buf[256];
                hFind = FindFirstFile(g_ContextId, &f);
                if (hFind != INVALID_HANDLE_VALUE) {
                    CreateWindow("STATIC", "Name:", WS_CHILD|WS_VISIBLE, 10, 10, 80, 20, hwnd, NULL, g_hInst, NULL);
                    CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", f.cFileName, WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_READONLY, 100, 10, 220, 22, hwnd, NULL, g_hInst, NULL);
                    CreateWindow("STATIC", "Type:", WS_CHILD|WS_VISIBLE, 10, 40, 80, 20, hwnd, NULL, g_hInst, NULL);
                    CreateWindow("STATIC", (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "File Folder" : "File", WS_CHILD|WS_VISIBLE, 100, 40, 220, 20, hwnd, NULL, g_hInst, NULL);
                    CreateWindow("STATIC", "Location:", WS_CHILD|WS_VISIBLE, 10, 65, 80, 20, hwnd, NULL, g_hInst, NULL);
                    CreateWindowEx(0, "EDIT", g_ContextId, WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_READONLY, 100, 65, 220, 20, hwnd, NULL, g_hInst, NULL);
                    CreateWindow("STATIC", "Size:", WS_CHILD|WS_VISIBLE, 10, 90, 80, 20, hwnd, NULL, g_hInst, NULL);
                    FormatSizeStr(f.nFileSizeLow, buf); CreateWindow("STATIC", buf, WS_CHILD|WS_VISIBLE, 100, 90, 220, 20, hwnd, NULL, g_hInst, NULL);
                    
                    CreateWindow("STATIC", "Modified:", WS_CHILD|WS_VISIBLE, 10, 115, 80, 20, hwnd, NULL, g_hInst, NULL);
                    WORD dosDate = 0, dosTime = 0; FileTimeToDosDateTime(&f.ftLastWriteTime, &dosDate, &dosTime);
                    FormatDateStr(dosDate, dosTime, buf); CreateWindow("STATIC", buf, WS_CHILD|WS_VISIBLE, 100, 115, 220, 20, hwnd, NULL, g_hInst, NULL);
                    
                    CreateWindow("BUTTON", "Read-only", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 100, 150, 80, 20, hwnd, (HMENU)101, g_hInst, NULL);
                    CreateWindow("BUTTON", "Hidden", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 190, 150, 80, 20, hwnd, (HMENU)102, g_hInst, NULL);
                    CreateWindow("BUTTON", "Archive", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 100, 175, 80, 20, hwnd, (HMENU)103, g_hInst, NULL);
                    CreateWindow("BUTTON", "System", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 190, 175, 80, 20, hwnd, (HMENU)104, g_hInst, NULL);
                    if (f.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SendMessage(GetDlgItem(hwnd, 101), BM_SETCHECK, 1, 0);
                    if (f.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) SendMessage(GetDlgItem(hwnd, 102), BM_SETCHECK, 1, 0);
                    if (f.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) SendMessage(GetDlgItem(hwnd, 103), BM_SETCHECK, 1, 0);
                    if (f.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) SendMessage(GetDlgItem(hwnd, 104), BM_SETCHECK, 1, 0);
                    FindClose(hFind);
                }
            }
            CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|WS_TABSTOP, 80, 230, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL); 
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 180, 230, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL); 
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); HPEN hSh = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)); HPEN hHi = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT)); HPEN hOld = SelectObject(hdc, hSh); MoveToEx(hdc, 10, 140, NULL); LineTo(hdc, 340, 140); SelectObject(hdc, hHi); MoveToEx(hdc, 10, 141, NULL); LineTo(hdc, 340, 141); SelectObject(hdc, hOld); DeleteObject(hSh); DeleteObject(hHi); EndPaint(hwnd, &ps); return 0;
        }
        case WM_COMMAND: 
            if (wp == IDOK) { 
                if (isDrive) {
                    char newLabel[MAX_PATH];
                    if (GetWindowText(GetDlgItem(hwnd, 201), newLabel, MAX_PATH)) {
                        SetVolumeLabel(rootPath, newLabel);
                    } else {
                        SetVolumeLabel(rootPath, NULL);
                    }
                } else {
                    DWORD attr = GetFileAttributes(g_ContextId);
                    if (attr != INVALID_FILE_ATTRIBUTES) {
                        if (SendMessage(GetDlgItem(hwnd, 101), BM_GETCHECK, 0, 0)) attr |= FILE_ATTRIBUTE_READONLY; else attr &= ~FILE_ATTRIBUTE_READONLY;
                        if (SendMessage(GetDlgItem(hwnd, 102), BM_GETCHECK, 0, 0)) attr |= FILE_ATTRIBUTE_HIDDEN; else attr &= ~FILE_ATTRIBUTE_HIDDEN;
                        if (SendMessage(GetDlgItem(hwnd, 103), BM_GETCHECK, 0, 0)) attr |= FILE_ATTRIBUTE_ARCHIVE; else attr &= ~FILE_ATTRIBUTE_ARCHIVE;
                        if (SendMessage(GetDlgItem(hwnd, 104), BM_GETCHECK, 0, 0)) attr |= FILE_ATTRIBUTE_SYSTEM; else attr &= ~FILE_ATTRIBUTE_SYSTEM;
                        SetFileAttributes(g_ContextId, attr);
                    }
                }
                HWND hParent = GetParent(hwnd); if (hParent) EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0);
            } else if (wp == IDCANCEL) { HWND hParent = GetParent(hwnd); if (hParent) EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } 
            return 0;
        case WM_CLOSE: { HWND hParent = GetParent(hwnd); if (hParent) EnableWindow(hParent, TRUE); DestroyWindow(hwnd); return 0; }
    } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK InlineEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            char newName[MAX_PATH]; GetWindowText(hwnd, newName, MAX_PATH);
            if (newName[0] != '\0') {
                if (g_InlineRenameIsVirtual) {
                    int i; for (i=0; i<g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i]->id, g_InlineRenameId) == 0) { if (lstrcmpi(g_IniShortcuts[i]->name, newName) == 0 || !NameExists(g_IniShortcuts[i]->parentId, newName, TRUE)) { lstrcpy(g_IniShortcuts[i]->name, newName); SaveIniEntry(g_IniShortcuts[i]); } else MessageBox(hwnd, "Name exists.", "Error", MB_OK|MB_ICONHAND); break; } }
                } else {
                    char newPath[MAX_PATH + 16]; char *p = strrchr(g_InlineRenameOldPath, '\\');
                    if (p) { int len = p - g_InlineRenameOldPath + 1; WindowState FAR* state; lstrcpyn(newPath, g_InlineRenameOldPath, len + 1); lstrcat(newPath, newName); state = (WindowState FAR*)GetWindowLongPtr(GetParent(GetParent(hwnd)), 0); if (lstrcmpi(g_InlineRenameOldPath, newPath) == 0 || !NameExists(state?state->pathOrId:"0", newName, FALSE)) { rename(g_InlineRenameOldPath, newPath); } else MessageBox(hwnd, "Name exists.", "Error", MB_OK|MB_ICONHAND); }
                }
            } 
            { HWND hParent = GetParent(hwnd); char cls[64]; GetClassName(hParent, cls, 64); if (lstrcmpi(cls, "Win95DesktopClass") == 0) { DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); } else { HWND hFolder = GetParent(hParent); DestroyWindow(hwnd); PostMessage(hFolder, WM_COMMAND, 4028, 0); } } return 0;
        } else if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
    } else if (msg == WM_KILLFOCUS) { DestroyWindow(hwnd); return 0; }
    return CallWindowProc((WNDPROC)g_lpfnOldInlineEditProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK ReplaceDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) { CreateWindow("STATIC", "File already exists:", WS_CHILD|WS_VISIBLE, 10, 10, 300, 20, hwnd, NULL, g_hInst, NULL); CreateWindow("STATIC", g_ReplaceTarget, WS_CHILD|WS_VISIBLE|SS_NOPREFIX, 10, 30, 300, 40, hwnd, NULL, g_hInst, NULL); CreateWindow("BUTTON", "Overwrite", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 10, 80, 70, 24, hwnd, (HMENU)101, g_hInst, NULL); CreateWindow("BUTTON", "All", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 85, 80, 50, 24, hwnd, (HMENU)102, g_hInst, NULL); CreateWindow("BUTTON", "Skip", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 140, 80, 50, 24, hwnd, (HMENU)103, g_hInst, NULL); CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 195, 80, 60, 24, hwnd, (HMENU)104, g_hInst, NULL); return 0; }
    if (msg == WM_COMMAND) { g_ReplaceResult = wp - 100; return 0; } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK CopyProgressDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: 
            g_bCancelDel = FALSE; 
            CreateWindow("STATIC", g_CurrentJob.isMove ? "Moving Items..." : "Copying Items...", WS_CHILD|WS_VISIBLE|SS_CENTER, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL); 
            CreateWindow("STATIC", g_CurrentJob.src, WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX|SS_PATHELLIPSIS, 10, 40, 260, 20, hwnd, (HMENU)101, g_hInst, NULL); 
            CreateWindow("STATIC", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 10, 70, 260, 20, hwnd, (HMENU)102, g_hInst, NULL); 
            CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 100, 100, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL); 
            SetTimer(hwnd, 1, 100, NULL); 
            return 0;
            
        case WM_TIMER: {
            KillTimer(hwnd, 1); 
            char* pSrc = g_CurrentJob.src;
            
            while (*pSrc && !g_bCancelDel) {
                char currentSrc[MAX_PATH]; lstrcpy(currentSrc, pSrc);
                char currentDst[MAX_PATH]; lstrcpy(currentDst, g_CurrentJob.dst);
                
                char* pSlash = strrchr(currentSrc, '\\');
                if (currentDst[0] != '\0' && currentDst[lstrlen(currentDst)-1] != '\\') lstrcat(currentDst, "\\");
                lstrcat(currentDst, pSlash ? pSlash + 1 : currentSrc);
                
                if (!g_CurrentJob.isMove && lstrcmpi(currentSrc, currentDst) == 0) {
                    char tempDst[MAX_PATH]; lstrcpy(tempDst, g_CurrentJob.dst);
                    if (tempDst[0] != '\0' && tempDst[lstrlen(tempDst)-1] != '\\') lstrcat(tempDst, "\\");
                    lstrcat(tempDst, "Copy of "); lstrcat(tempDst, pSlash ? pSlash + 1 : currentSrc); lstrcpy(currentDst, tempDst);
                }

                BOOL isDir = (GetFileAttributes(currentSrc) & FILE_ATTRIBUTE_DIRECTORY) != 0;

                HWND hLbl = GetDlgItem(hwnd, 101);
                if (hLbl) { SetWindowText(hLbl, currentSrc); UpdateWindow(hLbl); }

                if (g_CurrentJob.isMove) { 
                    if (rename(currentSrc, currentDst) != 0) { 
                        if (isDir) DoCopyFolder(hwnd, currentSrc, currentDst); 
                        else DoCopyFile(hwnd, currentSrc, currentDst); 
                        if (g_ReplaceMode != 3 && !g_bCancelDel) { 
                            if (isDir) { DoDeleteFolder(currentSrc); rmdir(currentSrc); } else remove(currentSrc); 
                        } 
                    } 
                } else { 
                    if (isDir) DoCopyFolder(hwnd, currentSrc, currentDst); 
                    else DoCopyFile(hwnd, currentSrc, currentDst); 
                }
                
                pSrc += lstrlen(pSrc) + 1;
            }
            
            HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); 
            PostMessage(hParent, WM_COMMAND, 4028, 0); 
            return 0;
        }
        case WM_COMMAND: if (wp == IDCANCEL) g_bCancelDel = TRUE; return 0;
    } 
    return DefWindowProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK DeleteProgressDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
        case WM_CREATE: g_bCancelDel = FALSE; g_hDelDlg = hwnd; CreateWindow("STATIC", "Deleting...", WS_CHILD|WS_VISIBLE|SS_CENTER, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL); CreateWindow("STATIC", g_DelPath, WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX, 10, 40, 260, 20, hwnd, (HMENU)101, g_hInst, NULL); CreateWindow("STATIC", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 10, 70, 260, 20, hwnd, (HMENU)102, g_hInst, NULL); CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 100, 100, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL); SetTimer(hwnd, 1, 50, NULL); return 0;
        case WM_TIMER: KillTimer(hwnd, 1); if (g_DelIsDir) { DoDeleteFolder(g_DelPath); if (!g_bCancelDel) rmdir(g_DelPath); } else remove(g_DelPath); { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); } return 0;
        case WM_COMMAND: if (wp == IDCANCEL) g_bCancelDel = TRUE; return 0;
        case WM_DESTROY: g_hDelDlg = NULL; return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hEdit, hParentFolder; static char parentId[MAX_PATH]; static char parentSel[128]; static IniShortcut sh;
    switch(msg) {
        case WM_CREATE: { CreateWindow("STATIC", g_PromptLabel, WS_CHILD|WS_VISIBLE, 10, 10, 260, 20, hwnd, NULL, g_hInst, NULL); hEdit = CreateWindowEx(0, "EDIT", g_PromptValue, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER, 10, 35, 260, 22, hwnd, NULL, g_hInst, NULL);
            if (g_PromptMode == PROMPT_NEWFOLDER) { int i; CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 65, 100, 20, hwnd, NULL, g_hInst, NULL); hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP, 110, 65, 160, 150, hwnd, NULL, g_hInst, NULL); SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"0 (Root)"); for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i]->isFolder) { char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i]->id, g_IniShortcuts[i]->name); SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)buf); } } lstrcpy(parentId, g_ContextId[0] ? g_ContextId : "0"); for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) { char buf[128]; buf[0] = '\0'; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)(LPSTR)buf); int pLen = lstrlen(parentId); if (pLen < lstrlen(buf)) { BOOL match = TRUE; int j; for (j=0; j<pLen; j++) { if (buf[j] != parentId[j]) { match = FALSE; break; } } if (match && buf[pLen] == ' ') { SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break; } } } if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0); CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 50, 100, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL); CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 150, 100, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL); } SetFocus(hEdit); return 0;
        }
        case WM_COMMAND: if (wp == IDOK) { GetWindowText(hEdit, g_PromptValue, MAX_PATH); if (g_PromptValue[0] != '\0' && g_PromptMode == PROMPT_NEWFOLDER) { char newId[16]; char* space; GetNewIniId(newId); lstrcpy(sh.id, newId); lstrcpy(sh.name, g_PromptValue); sh.exe[0] = '\0'; sh.params[0] = '\0'; sh.icon[0] = '\0'; sh.minimized = 0; sh.hotkey[0] = '\0'; if (hParentFolder) { GetWindowText(hParentFolder, parentSel, sizeof(parentSel)); space = strchr(parentSel, ' '); if (space) *space = '\0'; lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0"); } else lstrcpy(sh.parentId, g_ContextId[0] ? g_ContextId : "0"); if (NameExists(sh.parentId, g_PromptValue, TRUE)) { MessageBox(hwnd, "Name already exists.", "Error", MB_OK|MB_ICONHAND); return 0; } sh.isFolder = TRUE; SaveIniEntry(&sh); } HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); } else if (wp == IDCANCEL) { g_PromptValue[0] = '\0'; HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
        case WM_CLOSE: { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ShortcutDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hName, hTarget, hParams, hIcon, hMinCheck, hFolderCheck, hMapDirCheck, hParentFolder, hTargetLbl;
    static char name[MAX_PATH], target[MAX_PATH], params[MAX_PATH], iconF[MAX_PATH], parentId[MAX_PATH]; static char parentSel[128]; static IniShortcut sh;
    switch(msg) {
        case WM_CREATE: {
            int minimized = 0, isFolder = 0, bMapDir = 0, i; name[0] = '\0'; target[0] = '\0'; params[0] = '\0'; iconF[0] = '\0'; lstrcpy(parentId, "0");
            if (g_EditShortcutId[0] != '\0') { for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i]->id, g_EditShortcutId) == 0) { lstrcpy(name, g_IniShortcuts[i]->name); lstrcpy(target, g_IniShortcuts[i]->exe); lstrcpy(params, g_IniShortcuts[i]->params); lstrcpy(iconF, g_IniShortcuts[i]->icon); lstrcpyn(parentId, g_IniShortcuts[i]->parentId, MAX_PATH - 1); minimized = g_IniShortcuts[i]->minimized; isFolder = g_IniShortcuts[i]->isFolder; bMapDir = (isFolder && target[0] != '\0'); break; } } } else if (g_ContextId[0] != '\0') lstrcpyn(parentId, g_ContextId, MAX_PATH - 1);
            CreateWindow("STATIC", "Name:", WS_CHILD|WS_VISIBLE, 10, 10, 100, 20, hwnd, NULL, g_hInst, NULL); hName = CreateWindowEx(0, "EDIT", name, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 10, 210, 22, hwnd, NULL, g_hInst, NULL); hTargetLbl = CreateWindow("STATIC", isFolder ? (bMapDir ? "Dir Path:" : "Target:") : "Target (File):", WS_CHILD|WS_VISIBLE, 10, 40, 100, 20, hwnd, NULL, g_hInst, NULL); hTarget = CreateWindowEx(0, "EDIT", target, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 40, 150, 22, hwnd, NULL, g_hInst, NULL); CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 265, 40, 80, 22, hwnd, (HMENU)101, g_hInst, NULL); CreateWindow("STATIC", "Parameters:", WS_CHILD|WS_VISIBLE, 10, 70, 100, 20, hwnd, NULL, g_hInst, NULL); hParams = CreateWindowEx(0, "EDIT", params, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 70, 210, 22, hwnd, NULL, g_hInst, NULL); CreateWindow("STATIC", "Icon File:", WS_CHILD|WS_VISIBLE, 10, 100, 100, 20, hwnd, NULL, g_hInst, NULL); hIcon = CreateWindowEx(0, "EDIT", iconF, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 110, 100, 150, 22, hwnd, NULL, g_hInst, NULL); CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 265, 100, 80, 22, hwnd, (HMENU)102, g_hInst, NULL); CreateWindow("STATIC", "Parent Folder:", WS_CHILD|WS_VISIBLE, 10, 130, 100, 20, hwnd, NULL, g_hInst, NULL); hParentFolder = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL, 110, 130, 210, 150, hwnd, NULL, g_hInst, NULL); hMinCheck = CreateWindow("BUTTON", "Start Minimized", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 110, 160, 120, 20, hwnd, NULL, g_hInst, NULL); hFolderCheck = CreateWindow("BUTTON", "Is Folder", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 240, 160, 80, 20, hwnd, (HMENU)103, g_hInst, NULL); hMapDirCheck = CreateWindow("BUTTON", "Map Dir to Menu", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 110, 185, 140, 20, hwnd, (HMENU)104, g_hInst, NULL); CreateWindow("BUTTON", "OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 80, 220, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL); CreateWindow("BUTTON", "Cancel", WS_CHILD|WS_VISIBLE, 180, 220, 80, 24, hwnd, (HMENU)IDCANCEL, g_hInst, NULL);
            SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"0 (Root)"); for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i]->isFolder) { char buf[128]; sprintf(buf, "%s (%s)", g_IniShortcuts[i]->id, g_IniShortcuts[i]->name); SendMessage(hParentFolder, CB_ADDSTRING, 0, (LPARAM)(LPSTR)buf); } }
            for (i = 0; i < SendMessage(hParentFolder, CB_GETCOUNT, 0, 0); i++) { char buf[128]; buf[0] = '\0'; SendMessage(hParentFolder, CB_GETLBTEXT, i, (LPARAM)(LPSTR)buf); int pLen = lstrlen(parentId); if (pLen < lstrlen(buf)) { BOOL match = TRUE; int j; for (j=0; j<pLen; j++) { if (buf[j] != parentId[j]) { match = FALSE; break; } } if (match && buf[pLen] == ' ') { SendMessage(hParentFolder, CB_SETCURSEL, i, 0); break; } } } if (SendMessage(hParentFolder, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessage(hParentFolder, CB_SETCURSEL, 0, 0);
            if (minimized) SendMessage(hMinCheck, BM_SETCHECK, 1, 0); SendMessage(hFolderCheck, BM_SETCHECK, isFolder, 0); SendMessage(hMapDirCheck, BM_SETCHECK, bMapDir, 0);
            if (isFolder) { EnableWindow(hParams, FALSE); EnableWindow(hMinCheck, FALSE); EnableWindow(hMapDirCheck, TRUE); if (bMapDir) { EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); } else { EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE); } } else { EnableWindow(hMapDirCheck, FALSE); }
            SetFocus(hName); return 0;
        }
        case WM_COMMAND:
            if (wp == 101) { char path[MAX_PATH]; if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) { MessageBox(hwnd, "Select any file inside the mapped directory.", "Select", MB_OK); if (BrowseFile(hwnd, path, "All Files (*.*)\0*.*\0")) { char* pDir = strrchr(path, '\\'); if (pDir) { if (pDir == path || *(pDir - 1) == ':') *(pDir + 1) = '\0'; else *pDir = '\0'; } SetWindowText(hTarget, path); } } else { if (BrowseFile(hwnd, path, "Programs (*.exe;*.com;*.bat)\0*.exe;*.com;*.bat\0All Files (*.*)\0*.*\0")) { SetWindowText(hTarget, path); if (GetWindowTextLength(hIcon) == 0) SetWindowText(hIcon, path); if (GetWindowTextLength(hName) == 0) { char base[MAX_PATH], *p, *dot; lstrcpy(base, path); p = strrchr(base, '\\'); if (p) lstrcpy(base, p + 1); dot = strrchr(base, '.'); if (dot) *dot = '\0'; if (base[0]) { AnsiLower((LPSTR)base); if (base[0] >= 'a' && base[0] <= 'z') base[0] -= 32; } SetWindowText(hName, base); } } } } else if (wp == 102) { char path[MAX_PATH]; if (BrowseFile(hwnd, path, "Icons (*.exe;*.ico)\0*.exe;*.ico\0All Files (*.*)\0*.*\0")) SetWindowText(hIcon, path); } else if (wp == 103) { if (SendMessage(hFolderCheck, BM_GETCHECK, 0, 0)) { EnableWindow(hParams, FALSE); SetWindowText(hParams, ""); EnableWindow(hMinCheck, FALSE); EnableWindow(hMapDirCheck, TRUE); if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) { SetWindowText(hTargetLbl, "Dir Path:"); EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); } else { SetWindowText(hTargetLbl, "Target:"); EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE); } } else { EnableWindow(hMapDirCheck, FALSE); SendMessage(hMapDirCheck, BM_SETCHECK, 0, 0); SetWindowText(hTargetLbl, "Target (File):"); EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); EnableWindow(hParams, TRUE); EnableWindow(hMinCheck, TRUE); } } else if (wp == 104) { if (SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) { SetWindowText(hTargetLbl, "Dir Path:"); EnableWindow(hTarget, TRUE); EnableWindow(GetDlgItem(hwnd, 101), TRUE); } else { SetWindowText(hTargetLbl, "Target:"); SetWindowText(hTarget, ""); EnableWindow(hTarget, FALSE); EnableWindow(GetDlgItem(hwnd, 101), FALSE); } } else if (wp == IDOK) { char *space; GetWindowText(hName, name, MAX_PATH); GetWindowText(hTarget, target, MAX_PATH); GetWindowText(hParams, params, MAX_PATH); GetWindowText(hIcon, iconF, MAX_PATH); GetWindowText(hParentFolder, parentSel, sizeof(parentSel)); space = strchr(parentSel, ' '); if (space) *space = '\0'; if (name[0] && (target[0] || SendMessage(hFolderCheck, BM_GETCHECK, 0, 0))) { memset(&sh, 0, sizeof(sh)); if (g_EditShortcutId[0] != '\0') { int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i]->id, g_EditShortcutId) == 0) { sh = *(g_IniShortcuts[i]); break; } } } else { char newId[16]; GetNewIniId(newId); lstrcpy(sh.id, newId); } lstrcpy(sh.parentId, parentSel[0] ? parentSel : "0"); lstrcpy(sh.name, name); lstrcpy(sh.exe, target); lstrcpy(sh.params, params); lstrcpy(sh.icon, iconF); sh.hotkey[0] = '\0'; sh.minimized = SendMessage(hMinCheck, BM_GETCHECK, 0, 0) ? 1 : 0; sh.isFolder = SendMessage(hFolderCheck, BM_GETCHECK, 0, 0) ? 1 : 0; if (sh.isFolder) { sh.params[0] = '\0'; if (!SendMessage(hMapDirCheck, BM_GETCHECK, 0, 0)) sh.exe[0] = '\0'; } if (g_EditShortcutId[0] == '\0' || lstrcmpi(sh.name, name) != 0) { if (NameExists(sh.parentId, name, TRUE)) { MessageBox(hwnd, "Name exists.", "Error", MB_OK|MB_ICONHAND); return 0; } } SaveIniEntry(&sh); } HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); PostMessage(hParent, WM_COMMAND, 4028, 0); } else if (wp == IDCANCEL) { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
        case WM_CLOSE: { HWND hParent = GetParent(hwnd); EnableWindow(hParent, TRUE); DestroyWindow(hwnd); } return 0;
    } return DefWindowProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) {
        RECT rc; HBRUSH hBr; 
        GetClientRect(hwnd, &rc);
        hBr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        FillRect((HDC)wp, &rc, hBr);
        DeleteObject(hBr);
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc; RECT rc; HBRUSH hBg; 
        hdc = BeginPaint(hwnd, &ps); 
        GetClientRect(hwnd, &rc); 
        hBg = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); 
        FillRect(hdc, &rc, hBg); 
        DeleteObject(hBg);
        
        int btnW = g_IconSizeSmall + 8;
        int btnH = g_IconSizeSmall + 8;
        int padY = 4;
        int startX = 4;
        
        #define DRAW_BTN(idx) Draw3DButton(hdc, startX + (idx)*(btnW+4), padY, startX + (idx)*(btnW+4) + btnW, padY + btnH, FALSE)
        #define MAP_ICON(idx) \
            SetMapMode(hdc, MM_ANISOTROPIC); \
            SetWindowExtEx(hdc, 16, 16, NULL); \
            SetViewportExtEx(hdc, g_IconSizeSmall, g_IconSizeSmall, NULL); \
            SetViewportOrgEx(hdc, startX + (idx)*(btnW+4) + (btnW - g_IconSizeSmall)/2, padY + (btnH - g_IconSizeSmall)/2, NULL)
        #define UNMAP_ICON() \
            SetMapMode(hdc, MM_TEXT); \
            SetViewportOrgEx(hdc, 0, 0, NULL)

        /* Up Dir */
        DRAW_BTN(0);
        { HBRUSH hYel = CreateSolidBrush(RGB(255, 255, 128)); HBRUSH hOldB = SelectObject(hdc, hYel); DrawGDIFolder(hdc, startX + (btnW - g_IconSizeSmall)/2, padY + (btnH - g_IconSizeSmall)/2, TRUE, FALSE, FALSE); SelectObject(hdc, hOldB); DeleteObject(hYel); }
        MAP_ICON(0);
        { HPEN hGrn = CreatePen(PS_SOLID, 2, RGB(0, 128, 0)); HPEN hOldP = SelectObject(hdc, hGrn); MoveToEx(hdc, 9, 11, NULL); LineTo(hdc, 9, 4); MoveToEx(hdc, 6, 7, NULL); LineTo(hdc, 9, 4); MoveToEx(hdc, 12, 7, NULL); LineTo(hdc, 9, 4); SelectObject(hdc, hOldP); DeleteObject(hGrn); }
        UNMAP_ICON();
        
        /* Cut */
        DRAW_BTN(1); MAP_ICON(1);
        { HPEN hRed = CreatePen(PS_SOLID, 1, RGB(255, 0, 0)); HPEN hOldP = SelectObject(hdc, hRed); Ellipse(hdc, 2, 10, 8, 16); Ellipse(hdc, 12, 10, 18, 16); SelectObject(hdc, hOldP); DeleteObject(hRed); }
        { HPEN hBlu = CreatePen(PS_SOLID, 1, RGB(0, 0, 255)); HPEN hOldP = SelectObject(hdc, hBlu); MoveToEx(hdc, 6, 11, NULL); LineTo(hdc, 16, 2); MoveToEx(hdc, 14, 11, NULL); LineTo(hdc, 4, 2); SelectObject(hdc, hOldP); DeleteObject(hBlu); }
        UNMAP_ICON();
        
        /* Copy */
        DRAW_BTN(2); MAP_ICON(2);
        { HBRUSH hWht = GetStockObject(WHITE_BRUSH); HBRUSH hOldB = SelectObject(hdc, hWht); HPEN hCyn = CreatePen(PS_SOLID, 1, RGB(0, 128, 128)); HPEN hOldP = SelectObject(hdc, hCyn); Rectangle(hdc, 5, 5, 15, 17); Rectangle(hdc, 1, 1, 11, 13); SelectObject(hdc, hOldP); DeleteObject(hCyn); SelectObject(hdc, hOldB); }
        UNMAP_ICON();
        
        /* Paste */
        DRAW_BTN(3); MAP_ICON(3);
        { HBRUSH hBrn = CreateSolidBrush(RGB(128, 64, 0)); HBRUSH hOldB = SelectObject(hdc, hBrn); Rectangle(hdc, 2, 2, 14, 18); SelectObject(hdc, GetStockObject(WHITE_BRUSH)); Rectangle(hdc, 5, 6, 17, 16); SelectObject(hdc, GetStockObject(LTGRAY_BRUSH)); Rectangle(hdc, 6, 0, 10, 3); SelectObject(hdc, hOldB); DeleteObject(hBrn); }
        UNMAP_ICON();
        
        /* Delete */
        DRAW_BTN(4); MAP_ICON(4);
        { HPEN hRed2 = CreatePen(PS_SOLID, 2, RGB(255, 0, 0)); HPEN hOldP = SelectObject(hdc, hRed2); MoveToEx(hdc, 4, 4, NULL); LineTo(hdc, 16, 16); MoveToEx(hdc, 4, 16, NULL); LineTo(hdc, 16, 4); SelectObject(hdc, hOldP); DeleteObject(hRed2); }
        UNMAP_ICON();
        
        /* Properties */
        DRAW_BTN(5); MAP_ICON(5);
        Rectangle(hdc, 4, 2, 16, 18); 
        { HPEN hBlu2 = CreatePen(PS_SOLID, 1, RGB(0, 0, 255)); HPEN hOldP = SelectObject(hdc, hBlu2); MoveToEx(hdc, 6, 6, NULL); LineTo(hdc, 14, 6); MoveToEx(hdc, 6, 10, NULL); LineTo(hdc, 14, 10); SelectObject(hdc, hOldP); DeleteObject(hBlu2); }
        UNMAP_ICON();
        
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_LBUTTONDOWN) { 
        int x = LOWORD(lp); 
        int btnW = g_IconSizeSmall + 8;
        int sp = btnW + 4;
        if (x >= 4 && x <= 4 + btnW) PostMessage(GetParent(hwnd), WM_COMMAND, 4050, 0); 
        else if (x >= 4 + sp && x <= 4 + sp + btnW) PostMessage(GetParent(hwnd), WM_COMMAND, 4011, 0); 
        else if (x >= 4 + sp*2 && x <= 4 + sp*2 + btnW) PostMessage(GetParent(hwnd), WM_COMMAND, 4012, 0); 
        else if (x >= 4 + sp*3 && x <= 4 + sp*3 + btnW) PostMessage(GetParent(hwnd), WM_COMMAND, 4013, 0); 
        else if (x >= 4 + sp*4 && x <= 4 + sp*4 + btnW) PostMessage(GetParent(hwnd), WM_COMMAND, 4003, 0); 
        else if (x >= 4 + sp*5 && x <= 4 + sp*5 + btnW) PostMessage(GetParent(hwnd), WM_COMMAND, 4005, 0); 
        return 0; 
    }
    return CallWindowProc((WNDPROC)g_lpfnOldToolbarProc, hwnd, msg, wp, lp); 
}
void HandleListCommand(HWND hwnd, WPARAM wp, LPARAM lp, WindowState FAR* state) {
    int id = wp; HWND hList = GetDlgItem(hwnd, ID_LIST); HWND hTree = GetDlgItem(hwnd, ID_TREE); HWND hFocus = GetFocus();
    if (id == 4041) MessageBox(hwnd, "Explorer for Win32\nWindows 95 Shell Clone", "About", MB_OK | MB_ICONINFORMATION);
    else if (id == 4040) ShellExecute(hwnd, "open", "winhelp.exe", NULL, NULL, SW_SHOWNORMAL);
    else if (id == 4006) DestroyWindow(hwnd);
    else if (id == 4022) ChangeViewMode(hwnd, 0, state); else if (id == 4023) ChangeViewMode(hwnd, 1, state); else if (id == 4024) ChangeViewMode(hwnd, 2, state); else if (id == 4025) ChangeViewMode(hwnd, 3, state);
    else if (id == 4020) { g_bShowToolbar = !g_bShowToolbar; ShowWindow(GetDlgItem(hwnd, ID_TOOLBAR), g_bShowToolbar ? SW_SHOW : SW_HIDE); CheckMenuItem(GetMenu(hwnd), 4020, MF_BYCOMMAND | (g_bShowToolbar ? MF_CHECKED : MF_UNCHECKED)); RECT rc; GetClientRect(hwnd, &rc); SendMessage(hwnd, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom)); SaveConfig(); }
    else if (id == 4021) { g_bShowStatusBar = !g_bShowStatusBar; ShowWindow(GetDlgItem(hwnd, 300), g_bShowStatusBar ? SW_SHOW : SW_HIDE); CheckMenuItem(GetMenu(hwnd), 4021, MF_BYCOMMAND | (g_bShowStatusBar ? MF_CHECKED : MF_UNCHECKED)); RECT rc; GetClientRect(hwnd, &rc); SendMessage(hwnd, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom)); SaveConfig(); }
else if (id == 4028) { LoadIniShortcuts(); if(hTree) RebuildTree(hTree, state); RebuildList(hwnd, state); }
    else if (id == 4029) { CreateCenteredDialog(g_hInst, hwnd, "OptionsDlgClass", "Options", 260, 230); }
    else if (id == 4030 || id == 5003) { const char* target = (id == 5003 && g_ContextIsFolder && g_ContextId[0]) ? g_ContextId : state->pathOrId; CreateWindowEx(0, "Win95SearchClass", "Find: All Files", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 480, 420, NULL, NULL, g_hInst, (LPVOID)target); }
    else if (id == 4033) { MessageBox(hwnd, "Go To not yet implemented.", "Go To", MB_OK | MB_ICONINFORMATION); }
    else if (id == 4051) { 
        char pathTarget[MAX_PATH]; lstrcpy(pathTarget, state->pathOrId); 
        CreateWindowEx(0, "Win95FolderClass", pathTarget, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, g_WinW, g_WinH, NULL, NULL, g_hInst, (LPVOID)pathTarget); 
    }
    else if (id == 4050) { 
        if (state->isVirtual) { 
            if (lstrcmp(state->pathOrId, "0") != 0) { 
                int i; for(i=0; i<g_IniShortcutCount; i++) { 
                    if(lstrcmp(g_IniShortcuts[i]->id, state->pathOrId)==0) { 
                        lstrcpy(state->pathOrId, g_IniShortcuts[i]->parentId); break; 
                    } 
                } 
            } 
        } else { 
            char temp[MAX_PATH]; lstrcpy(temp, state->pathOrId); 
            char* p = strrchr(temp, '\\'); 
            if (p) { 
                if (p == temp) *(p+1) = '\0'; 
                else if (p > temp && *(p-1) == ':') {
                    if (*(p+1) == '\0') { lstrcpy(temp, "0"); state->isVirtual = TRUE; }
                    else *(p+1) = '\0';
                } else *p = '\0'; 
                lstrcpy(state->pathOrId, temp); 
            } else { 
                lstrcpy(state->pathOrId, "0"); state->isVirtual = TRUE; 
            } 
        }
        if (state->isVirtual) ExpandAllParentsVirtual(state->pathOrId); else ExpandAllParentsFS(state->pathOrId); 
        if(hTree) RebuildTree(hTree, state); RebuildList(hwnd, state);
    }
    else if (id == 9999) { 
         HWND hDesktop = FindWindow("Win95DesktopClass", "Desktop");
         if (hDesktop) {
             char clipP[MAX_PATH], clipN[64]; BOOL isV = FALSE, isD = FALSE;
             if (state->viewMode == 0) { lstrcpy(clipP, g_SelectedListItemPath); lstrcpy(clipN, g_SelectedListItemName); isV = g_SelectedListItemIsVirtual; isD = g_SelectedListItemIsDir; } else { int sel = SendMessage(hList, LB_GETCARETINDEX, 0, 0); if (sel != LB_ERR && SendMessage(hList, LB_GETSEL, sel, 0) > 0) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; lstrcpy(clipP, item->path); lstrcpy(clipN, item->name); isV = item->isVirtual; isD = item->isDir; } } }
             if (clipN[0] != '\0') {
                 char newId[16]; GetNewIniId(newId); IniShortcut FAR* sh = (IniShortcut FAR*)malloc(sizeof(IniShortcut)); 
                 if (sh) { memset(sh, 0, sizeof(IniShortcut)); lstrcpy(sh->id, newId); lstrcpy(sh->parentId, "0"); sprintf(sh->name, "Shortcut to %s", clipN); sh->isFolder = isD; if (isV) { int i; for (i = 0; i < g_IniShortcutCount; i++) { if (lstrcmp(g_IniShortcuts[i]->id, clipP) == 0) { lstrcpy(sh->exe, g_IniShortcuts[i]->exe); break; } } } else { lstrcpy(sh->exe, clipP); } if (NameExists("0", sh->name, TRUE)) { char temp[MAX_PATH]; sprintf(temp, "Copy of %s", sh->name); lstrcpy(sh->name, temp); } if (!NameExists("0", sh->name, TRUE)) { SaveIniEntry(sh); LoadIniShortcuts(); InvalidateRect(hDesktop, NULL, TRUE); } free(sh); }
             }
         }
    }
    else if (id == ID_TREE && (HIWORD(lp) == LBN_SELCHANGE || HIWORD(lp) == LBN_DBLCLK)) { int sel = SendMessage(hTree, LB_GETCURSEL, 0, 0); if (sel != LB_ERR) { TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hTree, LB_GETITEMDATA, sel, 0); if (item) { lstrcpy(state->pathOrId, item->pathOrId); state->isVirtual = item->isVirtual; SetWindowText(hwnd, item->displayName); RebuildList(hwnd, state); } } }
    else if (id >= 4001 && id <= 4016) {
        if (id == 4001 || id == 4002) { lstrcpy(g_ContextId, state->pathOrId); g_ContextIsVirtual = state->isVirtual; } 
        else if (id <= 4005 || id == 4011 || id == 4012) {
            if (hFocus == hTree) { lstrcpy(g_ContextId, state->pathOrId); g_ContextIsVirtual = state->isVirtual; g_ContextIsFolder = TRUE; } else { if (state->viewMode == 0) { if (g_SelectedListItemPath[0] != '\0') { lstrcpy(g_ContextId, g_SelectedListItemPath); g_ContextIsVirtual = g_SelectedListItemIsVirtual; g_ContextIsFolder = g_SelectedListItemIsDir; } else { HWND hStat = GetDlgItem(hwnd, 300); if (hStat) SetWindowText(hStat, " Please select an item first."); return; } } else { int sel = SendMessage(hList, LB_GETCARETINDEX, 0, 0); if (sel != LB_ERR && SendMessage(hList, LB_GETSEL, sel, 0) > 0) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; lstrcpy(g_ContextId, item->path); g_ContextIsVirtual = item->isVirtual; g_ContextIsFolder = item->isDir; } } else { HWND hStat = GetDlgItem(hwnd, 300); if (hStat) SetWindowText(hStat, " Please select an item first."); return; } } }
        }
        if (!g_ContextIsVirtual && (id == 4002)) { HWND hStat = GetDlgItem(hwnd, 300); if (hStat) SetWindowText(hStat, " Only virtual shortcuts can be edited."); return; }
        if (id == 4001) { 
            if (!state->isVirtual) {
                char newPath[MAX_PATH]; char baseName[64]; int idx = 0;
                do { if (idx == 0) lstrcpy(baseName, "New Folder"); else sprintf(baseName, "New Folder (%d)", idx); lstrcpy(newPath, state->pathOrId); if (newPath[0] && newPath[lstrlen(newPath)-1] != '\\') lstrcat(newPath, "\\"); lstrcat(newPath, baseName); idx++; } while (access(newPath, 0) == 0);
                if (mkdir(newPath) == 0) {
                    int i, count; RebuildList(hwnd, state); SetFocus(hList); count = SendMessage(hList, LB_GETCOUNT, 0, 0);
                    for (i = 0; i < count; i++) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, i, 0); if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; if (lstrcmp(item->name, baseName) == 0) { SendMessage(hList, LB_SETCURSEL, i, 0); lstrcpy(g_SelectedListItemPath, item->path); lstrcpy(g_SelectedListItemName, item->name); g_SelectedListItemIsVirtual = FALSE; g_SelectedListItemIsDir = TRUE; break; } } else if (pType && *pType == 2) { RowItemData FAR* row = (RowItemData FAR*)pType; int c; for (c = 0; c < row->count; c++) { if (lstrcmp(row->items[c]->name, baseName) == 0) { SendMessage(hList, LB_SETCURSEL, i, 0); lstrcpy(g_SelectedListItemPath, row->items[c]->path); lstrcpy(g_SelectedListItemName, row->items[c]->name); g_SelectedListItemIsVirtual = FALSE; g_SelectedListItemIsDir = TRUE; break; } } } }
                    InvalidateRect(hList, NULL, TRUE); UpdateWindow(hList); PostMessage(hwnd, WM_COMMAND, 4004, 0);
                } else { MessageBox(hwnd, "Failed to create folder.", "Error", MB_OK|MB_ICONHAND); }
            } else { lstrcpy(g_PromptLabel, "New Folder Name:"); g_PromptValue[0] = '\0'; g_PromptMode = PROMPT_NEWFOLDER; CreateCenteredDialog(g_hInst, hwnd, "PromptDlgClass", "New Folder", 290, 170); }
        }
        else if (id == 4002) { g_EditShortcutId[0] = '\0'; CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Create Shortcut", 360, 300); }
        else if (id == 4003 && g_ContextId[0] != '\0') {
            char msgBuf[256]; sprintf(msgBuf, "Are you sure you want to delete '%s'?", g_ContextId);
            if (MessageBox(hwnd, msgBuf, "Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) { if (g_ContextIsVirtual) { WritePrivateProfileString("Shortcut", g_ContextId, NULL, g_szExplorerIni); LoadIniShortcuts(); if(hTree) RebuildTree(hTree, state); RebuildList(hwnd, state); } else { lstrcpy(g_DelPath, g_ContextId); g_DelIsDir = g_ContextIsFolder; g_bCancelDel = FALSE; CreateCenteredDialog(g_hInst, hwnd, "DeleteProgressDlgClass", "Deleting", 280, 130); } }
        }
        else if (id == 4004) {
            if (hFocus == hTree) { HWND hStat = GetDlgItem(hwnd, 300); if (hStat) SetWindowText(hStat, " Rename unsupported in tree view directly."); }
            else {
                int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (state->viewMode == 0) {
                    if (g_SelectedListItemPath[0] != '\0') { g_InlineRenameIsVirtual = g_SelectedListItemIsVirtual; if (g_SelectedListItemIsVirtual) lstrcpy(g_InlineRenameId, g_SelectedListItemPath); else lstrcpy(g_InlineRenameOldPath, g_SelectedListItemPath); if (sel != LB_ERR) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 2) { RowItemData FAR* row = (RowItemData FAR*)pType; int c; for (c=0; c<row->count; c++) { if (lstrcmp(row->items[c]->path, g_SelectedListItemPath) == 0 && lstrcmp(row->items[c]->name, g_SelectedListItemName) == 0) { RECT rc; SendMessage(hList, LB_GETITEMRECT, sel, (LPARAM)(LPRECT)&rc); int itemX = rc.left + c * CELL_W; RECT eRc; eRc.left = itemX + 2; eRc.right = itemX + CELL_W - 2; eRc.top = rc.top + g_IconSizeLarge + 10; eRc.bottom = rc.bottom; HWND hEdit = CreateWindowEx(0, "EDIT", row->items[c]->name, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_CENTER, eRc.left, eRc.top, eRc.right - eRc.left, eRc.bottom - eRc.top, hList, (HMENU)9999, g_hInst, NULL); g_lpfnOldInlineEditProc = (FARPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)InlineEditProc); SetFocus(hEdit); SendMessage(hEdit, EM_SETSEL, 0, MAKELONG(0, -1)); break; } } } } }
                } else {
                    if (sel != LB_ERR) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; g_InlineRenameIsVirtual = item->isVirtual; if (item->isVirtual) lstrcpy(g_InlineRenameId, item->path); else lstrcpy(g_InlineRenameOldPath, item->path); RECT rc; SendMessage(hList, LB_GETITEMRECT, sel, (LPARAM)(LPRECT)&rc); rc.left += g_IconSizeSmall + 6; if (state->viewMode == 3) { int nameColW = g_IconSizeSmall > 32 ? g_IconSizeSmall + 120 : 150; rc.right = rc.left + nameColW - 4; } HWND hEdit = CreateWindowEx(0, "EDIT", item->name, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hList, (HMENU)9999, g_hInst, NULL); g_lpfnOldInlineEditProc = (FARPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)InlineEditProc); SetFocus(hEdit); SendMessage(hEdit, EM_SETSEL, 0, MAKELONG(0, -1)); } }
                }
            }
        }
        else if (id == 4005 && g_ContextId[0] != '\0') {
            if (!g_ContextIsVirtual) { CreateCenteredDialog(g_hInst, hwnd, "FilePropDlgClass", "Properties", 360, 300); }
            else { lstrcpy(g_EditShortcutId, g_ContextId); CreateCenteredDialog(g_hInst, hwnd, "ShortcutDlgClass", "Properties", 360, 300); }
        }
        else if (id == 4011 || id == 4012) {
            if (hFocus == hTree) { lstrcpy(g_ClipPath, g_ContextId); lstrcpy(g_ClipName, ""); g_ClipIsVirtual = g_ContextIsVirtual; g_ClipIsDir = g_ContextIsFolder; } 
            else { if (state->viewMode == 0) { lstrcpy(g_ClipPath, g_SelectedListItemPath); lstrcpy(g_ClipName, g_SelectedListItemName); g_ClipIsVirtual = g_SelectedListItemIsVirtual; g_ClipIsDir = g_SelectedListItemIsDir; } else { int sel = SendMessage(hList, LB_GETCARETINDEX, 0, 0); if (sel != LB_ERR && SendMessage(hList, LB_GETSEL, sel, 0) > 0) { int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, sel, 0); if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; lstrcpy(g_ClipPath, item->path); lstrcpy(g_ClipName, item->name); g_ClipIsVirtual = item->isVirtual; g_ClipIsDir = item->isDir; } } } }
            if (g_ClipName[0] == '\0') { if (!g_ClipIsVirtual) { char tempP[MAX_PATH]; lstrcpy(tempP, g_ClipPath); if (tempP[0] && tempP[lstrlen(tempP)-1] == '\\') tempP[lstrlen(tempP)-1] = '\0'; char* p = strrchr(tempP, '\\'); if (p) lstrcpy(g_ClipName, p + 1); else lstrcpy(g_ClipName, tempP); } else { int k; for(k=0; k<g_IniShortcutCount; k++) if(lstrcmp(g_IniShortcuts[k]->id, g_ClipPath) == 0) { lstrcpy(g_ClipName, g_IniShortcuts[k]->name); break; } } }
            g_ClipOp = (id == 4011) ? 2 : 1; if (g_ClipIsVirtual) lstrcpy(g_ClipId, g_ClipPath);
        }
        else if (id == 4013) {
            if (g_ClipOp == 0) return;
            if (g_ClipIsVirtual || state->isVirtual) {
                char newId[16]; IniShortcut FAR* sh = (IniShortcut FAR*)malloc(sizeof(IniShortcut));
                if (sh) { memset(sh, 0, sizeof(IniShortcut)); GetNewIniId(newId); lstrcpy(sh->id, newId); lstrcpy(sh->name, g_ClipName); lstrcpy(sh->parentId, state->pathOrId); sh->isFolder = g_ClipIsDir; if (g_ClipIsVirtual) { int i; for(i=0; i<g_IniShortcutCount; i++) if (lstrcmp(g_IniShortcuts[i]->id, g_ClipId) == 0) { lstrcpy(sh->exe, g_IniShortcuts[i]->exe); lstrcpy(sh->params, g_IniShortcuts[i]->params); lstrcpy(sh->icon, g_IniShortcuts[i]->icon); sh->minimized = g_IniShortcuts[i]->minimized; break; } if (g_ClipOp == 2) { WritePrivateProfileString("Shortcut", g_ClipId, NULL, g_szExplorerIni); g_ClipOp = 0; } } else lstrcpy(sh->exe, g_ClipPath); if (NameExists(state->pathOrId, sh->name, TRUE)) { char temp[MAX_PATH]; sprintf(temp, "Copy of %s", sh->name); lstrcpy(sh->name, temp); } if (!NameExists(state->pathOrId, sh->name, TRUE)) { SaveIniEntry(sh); LoadIniShortcuts(); if(hTree) RebuildTree(hTree, state); RebuildList(hwnd, state); } free(sh); }
            } else {
                lstrcpy(g_CurrentJob.src, g_ClipPath); lstrcpy(g_CurrentJob.dst, state->pathOrId); if (g_CurrentJob.dst[0] != '\0' && g_CurrentJob.dst[lstrlen(g_CurrentJob.dst)-1] != '\\') lstrcat(g_CurrentJob.dst, "\\"); lstrcat(g_CurrentJob.dst, g_ClipName); 
                if (g_ClipOp == 1 && lstrcmpi(g_CurrentJob.src, g_CurrentJob.dst) == 0) { char tempDst[MAX_PATH]; lstrcpy(tempDst, state->pathOrId); if (tempDst[0] != '\0' && tempDst[lstrlen(tempDst)-1] != '\\') lstrcat(tempDst, "\\"); lstrcat(tempDst, "Copy of "); lstrcat(tempDst, g_ClipName); lstrcpy(g_CurrentJob.dst, tempDst); } else if (g_ClipOp == 2 && lstrcmpi(g_CurrentJob.src, g_CurrentJob.dst) == 0) { g_ClipOp = 0; return; }
                g_CurrentJob.isMove = (g_ClipOp == 2); g_CurrentJob.isDir = g_ClipIsDir; g_ReplaceMode = 0; if (g_ClipOp == 2) g_ClipOp = 0; CreateCenteredDialog(g_hInst, hwnd, "CopyProgressDlgClass", g_CurrentJob.isMove ? "Moving" : "Copying", 280, 140);
            }
        }
        else if (id == 4014) {
            if (g_ClipOp == 0) return; char newId[16]; IniShortcut FAR* sh = (IniShortcut FAR*)malloc(sizeof(IniShortcut));
            if (sh) { memset(sh, 0, sizeof(IniShortcut)); GetNewIniId(newId); lstrcpy(sh->id, newId); sprintf(sh->name, "Shortcut to %s", g_ClipName); lstrcpy(sh->parentId, state->pathOrId); sh->isFolder = FALSE; if (g_ClipIsVirtual) { int i; for(i=0; i<g_IniShortcutCount; i++) if (lstrcmp(g_IniShortcuts[i]->id, g_ClipId) == 0) { lstrcpy(sh->exe, g_IniShortcuts[i]->exe); lstrcpy(sh->params, g_IniShortcuts[i]->params); lstrcpy(sh->icon, g_IniShortcuts[i]->icon); sh->minimized = g_IniShortcuts[i]->minimized; break; } } else lstrcpy(sh->exe, g_ClipPath); if (!NameExists(state->pathOrId, sh->name, TRUE)) SaveIniEntry(sh); LoadIniShortcuts(); if(hTree) RebuildTree(hTree, state); RebuildList(hwnd, state); free(sh); }
        }
        else if (id == 4015) { SendMessage(hList, LB_SETSEL, TRUE, -1); }
        else if (id == 4016) { int i, count = SendMessage(hList, LB_GETCOUNT, 0, 0); for (i=0; i<count; i++) { BOOL sel = SendMessage(hList, LB_GETSEL, i, 0); SendMessage(hList, LB_SETSEL, !sel, i); } }
    }
}

void HandleDrawItem(HWND hwnd, WPARAM wp, LPARAM lp) {
    LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lp; HDC hdc = lpdis->hDC; RECT rc = lpdis->rcItem; BOOL bSel = (lpdis->itemState & ODS_SELECTED); HBRUSH hWinBg;
    char cls[64]; GetClassName(hwnd, cls, 64);
    BOOL isDesktop = (lstrcmpi(cls, "Win95DesktopClass") == 0);
    WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(hwnd, 0);
    int drawMode = state ? state->viewMode : g_GlobalViewMode;
    HBRUSH hBgBr = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
    
    hWinBg = isDesktop ? g_hbrDesktop : g_hbrWindow;
    if (lpdis->itemID == (UINT)-1) { DeleteObject(hBgBr); return; }
    
    if (lpdis->CtlID == ID_TREE) {
        TreeItemData FAR* item = (TreeItemData FAR*)lpdis->itemData; 
        int indent = g_IconSizeSmall + 8;
        int lyHalf = rc.top + (rc.bottom - rc.top) / 2;
        FillRect(hdc, &rc, hWinBg);
        
        /* Draw flawless pixel-aligned parent vertical lines */
        for (int l = 0; l < item->level; l++) { 
            if (!item->isLastChild[l]) { 
                int lx = (l * indent) + (indent / 2) + 4; 
                for (int y = rc.top; y < rc.bottom; y++) if (y % 2 == 0) SetPixel(hdc, lx, y, RGB(128, 128, 128));
            } 
        }
        
        int px = (item->level * indent) + (indent / 2) + 4;
        int iconX = px + (indent / 2);

        /* Draw dotted lines for the current item ONLY if level > 0 */
        if (item->level > 0) {
            int yEnd = item->isLastChild[item->level] ? lyHalf : rc.bottom;
            for (int y = rc.top; y <= yEnd; y++) if (y % 2 == 0) SetPixel(hdc, px, y, RGB(128, 128, 128));
            for (int x = px; x <= iconX; x++) if (x % 2 == 0) SetPixel(hdc, x, lyHalf, RGB(128, 128, 128));
        }
        
        int pmSize = 9;
        int pmY = lyHalf - (pmSize / 2);

        if (item->hasChildren) DrawPlusMinus(hdc, px - 4, pmY, item->expanded); 
        DrawGDIFolder(hdc, iconX, rc.top + (rc.bottom - rc.top - g_IconSizeSmall)/2, item->expanded, FALSE, FALSE); 
        rc.left = iconX + g_IconSizeSmall + 6; 
        
        SetBkMode(hdc, OPAQUE); SetBkColor(hdc, bSel ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW)); SetTextColor(hdc, bSel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT)); 
        
        SIZE txtSize; GetTextExtentPoint32(hdc, item->displayName, lstrlen(item->displayName), &txtSize);
        int txtY = rc.top + (rc.bottom - rc.top - txtSize.cy) / 2;
        if (txtY < rc.top) txtY = rc.top;
        
        ExtTextOut(hdc, rc.left + 2, txtY, ETO_CLIPPED | ETO_OPAQUE, &rc, item->displayName, lstrlen(item->displayName), NULL);
    } else if (lpdis->CtlID == ID_LIST) {
        int FAR* pType = (int FAR*)lpdis->itemData;
        if (pType && *pType == 2) {
            RowItemData FAR* row = (RowItemData FAR*)lpdis->itemData; int c; int colW = CELL_W;
            
            FillRect(hdc, &rc, hWinBg); 

            for (c = 0; c < row->count; c++) {
                ListItemData FAR* item = row->items[c]; int itemX = rc.left + c * colW; BOOL bItemSel = (lstrcmp(item->path, g_SelectedListItemPath) == 0 && lstrcmp(item->name, g_SelectedListItemName) == 0);
                
                int iconX = itemX + (CELL_W - g_IconSizeLarge) / 2; 
                if (item->isDir) DrawGDIFolder(hdc, iconX, rc.top + 5, FALSE, bItemSel, TRUE); 
                else DrawGDIFile(hdc, iconX + g_IconSizeLarge/8, rc.top + 5, bItemSel, TRUE); 
                
                RECT tRc; tRc.left = itemX + 2; tRc.right = itemX + CELL_W - 2; tRc.top = rc.top + g_IconSizeLarge + 10; tRc.bottom = rc.bottom; 
                UINT uFormat = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_END_ELLIPSIS;

                if (bItemSel) {
                    SetBkMode(hdc, OPAQUE); SetBkColor(hdc, GetSysColor(COLOR_HIGHLIGHT)); SetTextColor(hdc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                    DrawText(hdc, item->name, -1, &tRc, uFormat);
                } else if (isDesktop) {
                    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(0, 0, 0));
                    RECT shadowRc = tRc; shadowRc.left += 1; shadowRc.top += 1; shadowRc.right += 1; shadowRc.bottom += 1;
                    DrawText(hdc, item->name, -1, &shadowRc, uFormat);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    DrawText(hdc, item->name, -1, &tRc, uFormat);
                } else {
                    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT)); 
                    DrawText(hdc, item->name, -1, &tRc, uFormat);
                }
            }
        } else if (pType && *pType == 1) {
            ListItemData FAR* item = (ListItemData FAR*)lpdis->itemData; char buf[MAX_PATH]; 
            
            FillRect(hdc, &rc, bSel ? hBgBr : hWinBg);
            
            RECT rName = rc, rSize = rc, rType = rc, rDate = rc; rName.left += g_IconSizeSmall + 6;
            int nameColW = g_IconSizeSmall > 32 ? g_IconSizeSmall + 120 : 150;

            if (drawMode == 3) { 
                rName.right = rc.left + nameColW - 4; 
                rDate.left = rc.left + nameColW + 4; rDate.right = rc.left + nameColW + 120 - 4;
                if (lstrcmpi(cls, "Win95SearchClass") == 0) {
                    rType.left = rc.left + nameColW + 120 + 4; rType.right = rc.left + nameColW + 220 - 4;
                    rSize.left = rc.left + nameColW + 220 + 4;
                } else {
                    rSize.left = rc.left + nameColW + 120 + 4; rSize.right = rc.left + nameColW + 190 - 4;
                    rType.left = rc.left + nameColW + 190 + 4;
                }
            }
            if (item->isDir) DrawGDIFolder(hdc, rc.left + 2, rc.top + 2, FALSE, FALSE, FALSE); 
            else DrawGDIFile(hdc, rc.left + 4, rc.top + 2, FALSE, FALSE);
            
            SetBkMode(hdc, TRANSPARENT); 
            SetTextColor(hdc, bSel ? GetSysColor(COLOR_HIGHLIGHTTEXT) : (isDesktop ? RGB(255,255,255) : GetSysColor(COLOR_WINDOWTEXT)));
            
            SIZE txtSize; GetTextExtentPoint32(hdc, item->name, lstrlen(item->name), &txtSize);
            int txtY = rc.top + (rc.bottom - rc.top - txtSize.cy) / 2;
            if (txtY < rc.top) txtY = rc.top;

            if (drawMode == 3) {
                ExtTextOut(hdc, rName.left, txtY, ETO_CLIPPED, &rName, item->name, lstrlen(item->name), NULL);
                if (lstrcmpi(cls, "Win95SearchClass") == 0) {
                    char folderPath[MAX_PATH]; lstrcpy(folderPath, item->path);
                    if (!item->isVirtual) {
                        char* pSlash = strrchr(folderPath, '\\');
                        if (pSlash && pSlash != folderPath) *pSlash = '\0';
                        else if (pSlash == folderPath) *(pSlash+1) = '\0';
                    } else {
                        int k; BOOL found = FALSE;
                        for(k=0; k<g_IniShortcutCount; k++) {
                            if (lstrcmp(g_IniShortcuts[k]->id, item->path) == 0) {
                                if (lstrcmp(g_IniShortcuts[k]->parentId, "0") == 0) lstrcpy(folderPath, "Desktop");
                                else lstrcpy(folderPath, g_IniShortcuts[k]->parentId);
                                found = TRUE; break;
                            }
                        }
                        if (!found) lstrcpy(folderPath, "Desktop");
                    }
                    ExtTextOut(hdc, rType.left, txtY, ETO_CLIPPED, &rType, folderPath, lstrlen(folderPath), NULL);
                    if (!item->isDir && !item->isVirtual) { FormatSizeStr(item->size, buf); ExtTextOut(hdc, rSize.left, txtY, ETO_CLIPPED, &rSize, buf, lstrlen(buf), NULL); } else ExtTextOut(hdc, rSize.left, txtY, 0, &rSize, "", 0, NULL);
                    if (!item->isVirtual && item->date > 0) { FormatDateStr(item->date, item->time, buf); ExtTextOut(hdc, rDate.left, txtY, ETO_CLIPPED, &rDate, buf, lstrlen(buf), NULL); } else ExtTextOut(hdc, rDate.left, txtY, 0, &rDate, "", 0, NULL);
                } else {
                    if (!item->isDir && !item->isVirtual) { FormatSizeStr(item->size, buf); ExtTextOut(hdc, rSize.left, txtY, ETO_CLIPPED, &rSize, buf, lstrlen(buf), NULL); } else ExtTextOut(hdc, rSize.left, txtY, 0, &rSize, "", 0, NULL);
                    if (item->isDir) lstrcpy(buf, "File Folder"); else if (item->ext[0]) sprintf(buf, "%s File", item->ext); else lstrcpy(buf, "File"); ExtTextOut(hdc, rType.left, txtY, ETO_CLIPPED, &rType, buf, lstrlen(buf), NULL);
                    if (!item->isVirtual && item->date > 0) { FormatDateStr(item->date, item->time, buf); ExtTextOut(hdc, rDate.left, txtY, ETO_CLIPPED, &rDate, buf, lstrlen(buf), NULL); } else ExtTextOut(hdc, rDate.left, txtY, 0, &rDate, "", 0, NULL);
                }
            } else { ExtTextOut(hdc, rName.left, txtY, ETO_CLIPPED, &rName, item->name, lstrlen(item->name), NULL); }
        }
    }
    DeleteObject(hBgBr);
}
LRESULT CALLBACK TreeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) { 
        if (wp == VK_F5) { PostMessage(GetParent(hwnd), WM_COMMAND, 4028, 0); return 0; } 
        if (wp == VK_F2) { PostMessage(GetParent(hwnd), WM_COMMAND, 4004, 0); return 0; } 
        if (wp == VK_DELETE) { PostMessage(GetParent(hwnd), WM_COMMAND, 4003, 0); return 0; } 
        if (GetKeyState(VK_CONTROL) & 0x8000) { 
            if (wp == 'X') { PostMessage(GetParent(hwnd), WM_COMMAND, 4011, 0); return 0; } 
            if (wp == 'C') { PostMessage(GetParent(hwnd), WM_COMMAND, 4012, 0); return 0; } 
            if (wp == 'V') { PostMessage(GetParent(hwnd), WM_COMMAND, 4013, 0); return 0; } 
            if (wp == 'N') { PostMessage(GetParent(hwnd), WM_COMMAND, 4051, 0); return 0; } 
            if (wp == 'F') { PostMessage(GetParent(hwnd), WM_COMMAND, 4030, 0); return 0; }
        } 
        if (wp == VK_BACK) { PostMessage(GetParent(hwnd), WM_COMMAND, 4050, 0); return 0; } 
        if (wp == VK_LEFT || wp == VK_OEM_MINUS || wp == VK_SUBTRACT) {
            int selIdx = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
            if (selIdx != LB_ERR) {
                TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0);
                if (item && item->expanded && item->hasChildren) {
                    WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
                    if (state) { lstrcpy(state->pathOrId, item->pathOrId); state->isVirtual = item->isVirtual; }
                    ToggleExpand(item->expandId); RebuildTree(hwnd, state);
                }
            } return 0;
        }
        if (wp == VK_RIGHT || wp == VK_OEM_PLUS || wp == VK_ADD) {
            int selIdx = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
            if (selIdx != LB_ERR) {
                TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0);
                if (item && !item->expanded && item->hasChildren) {
                    WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
                    if (state) { lstrcpy(state->pathOrId, item->pathOrId); state->isVirtual = item->isVirtual; }
                    ToggleExpand(item->expandId); RebuildTree(hwnd, state);
                }
            } return 0;
        }
        if (wp == VK_RETURN) {
            int selIdx = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
            if (selIdx != LB_ERR) {
                TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0);
                if (item) {
                    WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
                    if (state) {
                        lstrcpy(state->pathOrId, item->pathOrId); state->isVirtual = item->isVirtual;
                        SetWindowText(GetParent(hwnd), item->displayName); RebuildList(GetParent(hwnd), state);
                    }
                }
            } return 0;
        }
        if (wp == VK_TAB) { HWND hList = GetDlgItem(GetParent(hwnd), ID_LIST); if (hList && IsWindowVisible(hList)) { SetFocus(hList); return 0; } } 
        if (wp == VK_APPS) {
            int selIdx = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
            if (selIdx != LB_ERR) { RECT rc; SendMessage(hwnd, LB_GETITEMRECT, selIdx, (LPARAM)&rc); SendMessage(hwnd, WM_RBUTTONUP, 0, MAKELONG(rc.left+10, rc.top+5)); } 
            else { SendMessage(hwnd, WM_RBUTTONUP, 0, MAKELONG(10, 10)); } return 0;
        }
    }
    if (msg == WM_CHAR) {
        DWORD tick = GetTickCount();
        if (tick - g_SearchLastTick > 1000) g_SearchBuf[0] = '\0';
        g_SearchLastTick = tick;
        int len = lstrlen(g_SearchBuf);
        if (len < 63 && wp >= 32 && wp <= 126) {
            g_SearchBuf[len] = (char)wp; g_SearchBuf[len+1] = '\0';
            int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0);
            
            int startIdx = 0;
            if (len == 0) {
                int curSel = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
                if (curSel != LB_ERR) startIdx = curSel + 1;
            }
            
            BOOL found = FALSE;
            for (int pass = 0; pass < 2 && !found; pass++) {
                int endIdx = (pass == 0) ? count : startIdx;
                for (int i = (pass == 0) ? startIdx : 0; i < endIdx; i++) {
                    TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, i, 0);
                    if (item && strnicmp(item->displayName, g_SearchBuf, len + 1) == 0) {
                        SendMessage(hwnd, LB_SETCURSEL, i, 0);
                        WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
                        if (state) {
                            lstrcpy(state->pathOrId, item->pathOrId); state->isVirtual = item->isVirtual;
                            SetWindowText(GetParent(hwnd), item->displayName); RebuildList(GetParent(hwnd), state);
                        }
                        found = TRUE; break;
                    }
                }
            }
        } return 0;
    }
    if (msg == WM_LBUTTONDOWN) { g_bTreeDragging = TRUE; g_TreeDragStartPt.x = (short)LOWORD(lp); g_TreeDragStartPt.y = (short)HIWORD(lp); SetCapture(hwnd); }
    if (msg == WM_MOUSEMOVE && g_bTreeDragging && (wp & MK_LBUTTON)) {
        int x = (short)LOWORD(lp); int y = (short)HIWORD(lp);
        if (abs(x - g_TreeDragStartPt.x) > 3 || abs(y - g_TreeDragStartPt.y) > 3) {
            POINT pt; pt.x = x; pt.y = y; ClientToScreen(hwnd, &pt); HWND hTarget = WindowFromPoint(pt);
            DWORD targetPid = 0; if (hTarget) GetWindowThreadProcessId(hTarget, &targetPid);
            
            if (targetPid != GetCurrentProcessId()) {
                int selIdx = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
                if (selIdx != LB_ERR) {
                    TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0);
                    if (item) {
                        char actualDragPath[MAX_PATH]; BOOL canOleDrag = FALSE; lstrcpy(actualDragPath, item->pathOrId);
                        if (item->isVirtual) {
                            for (int k = 0; k < g_IniShortcutCount; k++) {
                                if (lstrcmp(g_IniShortcuts[k]->id, actualDragPath) == 0 && g_IniShortcuts[k]->exe[0] != '\0') {
                                    lstrcpy(actualDragPath, g_IniShortcuts[k]->exe); canOleDrag = TRUE; break;
                                }
                            }
                        } else { canOleDrag = TRUE; }
                        
                        if (canOleDrag && actualDragPath[0] != '\0') {
                            g_bTreeDragging = FALSE; ReleaseCapture(); char* singleArr[1] = { actualDragPath };
                            InitiateOleDragDropMultiple(hwnd, singleArr, 1); return 0;
                        }
                    }
                }
            } SetCursor(LoadCursor(NULL, IDC_CROSS)); return 0;
        }
    }
    
    if (msg == WM_LBUTTONUP) { 
        if (g_bTreeDragging) { 
            g_bTreeDragging = FALSE; ReleaseCapture(); 
            int x = (short)LOWORD(lp); int y = (short)HIWORD(lp);
            if (abs(x - g_TreeDragStartPt.x) > 3 || abs(y - g_TreeDragStartPt.y) > 3) {
                POINT pt; pt.x = x; pt.y = y; ClientToScreen(hwnd, &pt); HWND hTarget = WindowFromPoint(pt); 
                if (hTarget) {
                    HWND hTgtParent = hTarget; char targetCls[64]; GetClassName(hTgtParent, targetCls, 64);
                    while (hTgtParent && lstrcmpi(targetCls, "Win95FolderClass") != 0 && lstrcmpi(targetCls, "Win95DesktopClass") != 0 && lstrcmpi(targetCls, "Win95SearchClass") != 0) {
                        hTgtParent = GetParent(hTgtParent); if (hTgtParent) GetClassName(hTgtParent, targetCls, 64);
                    }

                    BOOL bInternalHandled = FALSE;

                    if (hTgtParent && (lstrcmpi(targetCls, "Win95FolderClass") == 0 || lstrcmpi(targetCls, "Win95DesktopClass") == 0 || lstrcmpi(targetCls, "Win95SearchClass") == 0)) {
                        WindowState FAR* tgtState = (WindowState FAR*)GetWindowLongPtr(hTgtParent, 0); BOOL validTarget = FALSE; char targetFolder[MAX_PATH] = ""; BOOL tgtVirtual = FALSE;
                        
                        if (hTarget && GetWindowLongPtr(hTarget, GWLP_ID) == ID_LIST && tgtState) {
                            POINT clPt = pt; ScreenToClient(hTarget, &clPt);
                            int hitIdx = -1; int topIdx = SendMessage(hTarget, LB_GETTOPINDEX, 0, 0); int count = SendMessage(hTarget, LB_GETCOUNT, 0, 0);
                            for (int i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hTarget, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (clPt.y >= rc.top && clPt.y <= rc.bottom && clPt.x >= rc.left && clPt.x <= rc.right) { hitIdx = i; break; } } else break; }
                            if (hitIdx >= 0) {
                                int FAR* pType = (int FAR*)SendMessage(hTarget, LB_GETITEMDATA, hitIdx, 0);
                                if (pType && *pType == 2 && tgtState->viewMode == 0) { RowItemData FAR* row = (RowItemData FAR*)pType; int col = clPt.x / CELL_W; if (col >= 0 && col < row->count && row->items[col]->isDir) { lstrcpy(targetFolder, row->items[col]->path); tgtVirtual = row->items[col]->isVirtual; validTarget = TRUE; } } 
                                else if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; if (item->isDir) { lstrcpy(targetFolder, item->path); tgtVirtual = item->isVirtual; validTarget = TRUE; } }
                            }
                        } else if (hTarget && GetWindowLongPtr(hTarget, GWLP_ID) == ID_TREE && tgtState) {
                            POINT clPt = pt; ScreenToClient(hTarget, &clPt); int hitIdx = -1; int topIdx = SendMessage(hTarget, LB_GETTOPINDEX, 0, 0); int count = SendMessage(hTarget, LB_GETCOUNT, 0, 0);
                            for (int i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hTarget, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (clPt.y >= rc.top && clPt.y <= rc.bottom) { hitIdx = i; break; } } else break; }
                            if (hitIdx >= 0) { TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hTarget, LB_GETITEMDATA, hitIdx, 0); if (item) { lstrcpy(targetFolder, item->pathOrId); tgtVirtual = item->isVirtual; validTarget = TRUE; } }
                        }
                        if (!validTarget && tgtState && lstrcmpi(targetCls, "Win95SearchClass") != 0) { lstrcpy(targetFolder, tgtState->pathOrId); tgtVirtual = tgtState->isVirtual; validTarget = TRUE; }
                        
                        int selIdx = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
                        TreeItemData FAR* srcItem = (selIdx != LB_ERR) ? (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0) : NULL;

                        if (validTarget && srcItem && lstrcmpi(targetFolder, srcItem->pathOrId) != 0 && srcItem->pathOrId[0]) {
                            bInternalHandled = TRUE;
                            int op = 1; if (GetKeyState(VK_SHIFT) & 0x8000) op = 2; 
                            if (GetKeyState(VK_CONTROL) & 0x8000) { if (GetKeyState(VK_SHIFT) & 0x8000) op = 3; else op = 1; }
                            if (GetKeyState(VK_MENU) & 0x8000) op = 3;

                            char* pJobSrc = g_CurrentJob.src; BOOL requiresCopy = FALSE;

                            char srcPath[MAX_PATH]; char srcName[64];
                            lstrcpy(srcPath, srcItem->pathOrId); lstrcpy(srcName, srcItem->displayName);
                            
                            if (srcItem->isVirtual) {
                                for (int k = 0; k < g_IniShortcutCount; k++) {
                                    if (lstrcmp(g_IniShortcuts[k]->id, srcItem->pathOrId) == 0 && g_IniShortcuts[k]->exe[0] != '\0') {
                                        lstrcpy(srcPath, g_IniShortcuts[k]->exe);
                                        char* pSlash = strrchr(g_IniShortcuts[k]->exe, '\\');
                                        if (pSlash) lstrcpy(srcName, pSlash + 1);
                                        break;
                                    }
                                }
                            }

                            char srcParent[MAX_PATH]; lstrcpy(srcParent, srcPath); char* pSlash = strrchr(srcParent, '\\'); if (pSlash) *pSlash = '\0';
                            if (!(lstrcmpi(srcParent, targetFolder) == 0 && op == 2)) {
                                if (op == 3 || tgtVirtual || srcItem->isVirtual) {
                                    char newId[16]; GetNewIniId(newId); IniShortcut FAR* sh = (IniShortcut FAR*)malloc(sizeof(IniShortcut)); 
                                    if (sh) { memset(sh, 0, sizeof(IniShortcut)); lstrcpy(sh->id, newId); lstrcpy(sh->parentId, targetFolder); sprintf(sh->name, "%s%s", (op==3||!tgtVirtual)?"Shortcut to ":"", srcName); sh->isFolder = TRUE; if (srcItem->isVirtual) { for (int k = 0; k < g_IniShortcutCount; k++) { if (lstrcmp(g_IniShortcuts[k]->id, srcItem->pathOrId) == 0) { lstrcpy(sh->exe, g_IniShortcuts[k]->exe); lstrcpy(sh->params, g_IniShortcuts[k]->params); lstrcpy(sh->icon, g_IniShortcuts[k]->icon); sh->minimized = g_IniShortcuts[k]->minimized; break; } } } else { lstrcpy(sh->exe, srcPath); } if (NameExists(targetFolder, sh->name, TRUE)) { char temp[MAX_PATH]; sprintf(temp, "Copy of %s", sh->name); lstrcpy(sh->name, temp); } if (!NameExists(targetFolder, sh->name, TRUE)) { SaveIniEntry(sh); } free(sh); }
                                } else { requiresCopy = TRUE; lstrcpy(pJobSrc, srcPath); pJobSrc += lstrlen(pJobSrc) + 1; g_CurrentJob.isDir = TRUE; }
                            }
                            
                            if (requiresCopy) {
                                *pJobSrc = '\0'; 
                                lstrcpy(g_CurrentJob.dst, targetFolder); g_CurrentJob.isMove = (op == 2); g_ReplaceMode = 0; 
                                CreateCenteredDialog(g_hInst, GetParent(hwnd), "CopyProgressDlgClass", g_CurrentJob.isMove ? "Moving" : "Copying", 280, 140);
                            } else { LoadIniShortcuts(); InvalidateRect(hTgtParent, NULL, TRUE); PostMessage(hTgtParent, WM_COMMAND, 4028, 0); }
                        }
                    }
                    
                    if (!bInternalHandled) {
                        int selIdx = SendMessage(hwnd, LB_GETCURSEL, 0, 0);
                        TreeItemData FAR* srcItem = (selIdx != LB_ERR) ? (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0) : NULL;

                        if (srcItem && srcItem->pathOrId[0] != '\0') {
                            HWND hDropTarget = hTarget; BOOL acceptsFiles = FALSE;
                            while (hDropTarget) {
                                if (GetWindowLong(hDropTarget, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) { acceptsFiles = TRUE; break; }
                                hDropTarget = GetParent(hDropTarget);
                            }
                            
                            if (acceptsFiles && hDropTarget) {
                                char actualDragPath[MAX_PATH]; lstrcpy(actualDragPath, srcItem->pathOrId);
                                if (srcItem->isVirtual) {
                                    for (int k = 0; k < g_IniShortcutCount; k++) {
                                        if (lstrcmp(g_IniShortcuts[k]->id, actualDragPath) == 0 && g_IniShortcuts[k]->exe[0] != '\0') {
                                            lstrcpy(actualDragPath, g_IniShortcuts[k]->exe); break;
                                        }
                                    }
                                }
                                if (actualDragPath[0]) {
                                    HGLOBAL hMem = GlobalAlloc(GHND | GMEM_SHARE, sizeof(MY_DROPFILES) + lstrlen(actualDragPath) + 2);
                                    if (hMem) {
                                        MY_DROPFILES* df = (MY_DROPFILES*)GlobalLock(hMem); df->pFiles = sizeof(MY_DROPFILES);
                                        POINT clientPt = pt; ScreenToClient(hDropTarget, &clientPt); df->pt.x = clientPt.x; df->pt.y = clientPt.y; df->fNC = FALSE; df->fWide = FALSE;
                                        char* pDest = (char*)(df + 1); lstrcpy(pDest, actualDragPath); pDest += lstrlen(actualDragPath) + 1; *pDest = '\0'; GlobalUnlock(hMem); PostMessage(hDropTarget, WM_DROPFILES, (WPARAM)hMem, 0);
                                    }
                                }
                            } else {
                                char pathBuf[MAX_PATH + 4]; sprintf(pathBuf, "\"%s\"", srcItem->pathOrId);
                                for (int cIdx = 0; pathBuf[cIdx] != '\0'; cIdx++) PostMessage(hTarget, WM_CHAR, (WPARAM)pathBuf[cIdx], 0);
                            }
                        }
                    }
                }
            }
        } 
    }
    
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONUP) {
        int x = LOWORD(lp); int y = HIWORD(lp); int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0); int topIdx = SendMessage(hwnd, LB_GETTOPINDEX, 0, 0); int hitIdx = -1; int i;
        for (i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hwnd, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (y >= rc.top && y <= rc.bottom) { hitIdx = i; break; } } else break; }
        if (hitIdx >= 0) {
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) { 
                TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, hitIdx, 0); 
                if (item) { 
                    int pmX = item->level * 16 + 2; 
                    if (item->hasChildren && (msg == WM_LBUTTONDBLCLK || (msg == WM_LBUTTONDOWN && x >= pmX && x <= pmX + 12))) { 
                        WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0); 
                        ToggleExpand(item->expandId); RebuildTree(hwnd, state); return 0; 
                    } else if (msg == WM_LBUTTONDOWN) {
                        WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
                        if (state) { SendMessage(hwnd, LB_SETCURSEL, hitIdx, 0); lstrcpy(state->pathOrId, item->pathOrId); state->isVirtual = item->isVirtual; SetWindowText(GetParent(hwnd), item->displayName); RebuildList(GetParent(hwnd), state); }
                    }
                } 
            } else if (msg == WM_RBUTTONUP) { TreeItemData FAR* item; POINT pt; pt.x = x; pt.y = y; SendMessage(hwnd, LB_SETCURSEL, hitIdx, 0); item = (TreeItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, hitIdx, 0); if (item) { WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0); g_ContextIsVirtual = item->isVirtual; lstrcpy(g_ContextId, item->pathOrId); g_ContextIsFolder = TRUE; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, TRUE, FALSE, state->viewMode); } return 0; }
        }
    } return CallWindowProc((WNDPROC)g_lpfnOldTreeProc, hwnd, msg, wp, lp);
}
void GetRealPaths(ListItemData FAR* item, char* outPath, char* outName) {
    if (outPath) lstrcpy(outPath, item->path);
    if (outName) lstrcpy(outName, item->name);
    if (item->isVirtual) {
        for (int k = 0; k < g_IniShortcutCount; k++) {
            if (lstrcmp(g_IniShortcuts[k]->id, item->path) == 0 && g_IniShortcuts[k]->exe[0] != '\0') {
                if (outPath) lstrcpy(outPath, g_IniShortcuts[k]->exe);
                if (outName) {
                    char* pSlash = strrchr(g_IniShortcuts[k]->exe, '\\');
                    if (pSlash) lstrcpy(outName, pSlash + 1);
                    else lstrcpy(outName, g_IniShortcuts[k]->exe);
                }
                break;
            }
        }
    }
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_F5) { PostMessage(GetParent(hwnd), WM_COMMAND, 4028, 0); return 0; } 
        if (wp == VK_F2) { PostMessage(GetParent(hwnd), WM_COMMAND, 4004, 0); return 0; } 
        if (wp == VK_DELETE) { PostMessage(GetParent(hwnd), WM_COMMAND, 4003, 0); return 0; }
        if (GetKeyState(VK_CONTROL) & 0x8000) { 
            if (wp == 'A') { PostMessage(GetParent(hwnd), WM_COMMAND, 4015, 0); return 0; } 
            if (wp == 'X') { PostMessage(GetParent(hwnd), WM_COMMAND, 4011, 0); return 0; } 
            if (wp == 'C') { PostMessage(GetParent(hwnd), WM_COMMAND, 4012, 0); return 0; } 
            if (wp == 'V') { PostMessage(GetParent(hwnd), WM_COMMAND, 4013, 0); return 0; } 
            if (wp == 'N') { PostMessage(GetParent(hwnd), WM_COMMAND, 4051, 0); return 0; } 
            if (wp == 'F') { PostMessage(GetParent(hwnd), WM_COMMAND, 4030, 0); return 0; } 
        }
        if (wp == VK_BACK) { PostMessage(GetParent(hwnd), WM_COMMAND, 4050, 0); return 0; }
        if (wp == VK_TAB) { HWND hTree = GetDlgItem(GetParent(hwnd), ID_TREE); if (hTree && IsWindowVisible(hTree)) { SetFocus(hTree); return 0; } }
        if (wp == VK_RETURN) {
            if (g_SelectedListItemPath[0] != '\0') {
                if (g_SelectedListItemIsDir) {
                    char pcls[64]; GetClassName(GetParent(hwnd), pcls, 64);
                    if (lstrcmpi(pcls, "Win95DesktopClass") == 0 || lstrcmpi(pcls, "Win95SearchClass") == 0) {
                        char pathTarget[MAX_PATH]; lstrcpy(pathTarget, g_SelectedListItemPath);
                        CreateWindowEx(0, "Win95FolderClass", g_SelectedListItemName, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, g_WinX, g_WinY, g_WinW, g_WinH, NULL, NULL, g_hInst, (LPVOID)pathTarget);
                    } else {
                        WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
                        if (state) {
                            lstrcpy(state->pathOrId, g_SelectedListItemPath); state->isVirtual = g_SelectedListItemIsVirtual; SetWindowText(GetParent(hwnd), g_SelectedListItemName);
                            if (state->isVirtual) ExpandAllParentsVirtual(state->pathOrId); else ExpandAllParentsFS(state->pathOrId);
                            RebuildTree(GetDlgItem(GetParent(hwnd), ID_TREE), state); RebuildList(GetParent(hwnd), state);
                        }
                    }
                } else {
                    if (g_SelectedListItemIsVirtual) {
                        for (int k = 0; k < g_IniShortcutCount; k++) {
                            if (lstrcmpi(g_IniShortcuts[k]->id, g_SelectedListItemPath) == 0) { if (g_IniShortcuts[k]->exe[0]) ShellExecute(hwnd, "open", g_IniShortcuts[k]->exe, g_IniShortcuts[k]->params, NULL, SW_SHOWNORMAL); break; }
                        }
                    } else { ShellExecute(hwnd, "open", g_SelectedListItemPath, NULL, NULL, SW_SHOWNORMAL); }
                }
            } return 0;
        }
        if (wp == VK_APPS) {
            int selIdx = SendMessage(hwnd, LB_GETCARETINDEX, 0, 0);
            if (selIdx != LB_ERR) { RECT rc; SendMessage(hwnd, LB_GETITEMRECT, selIdx, (LPARAM)&rc); SendMessage(hwnd, WM_RBUTTONUP, 0, MAKELONG(rc.left+10, rc.top+10)); } 
            else { SendMessage(hwnd, WM_RBUTTONUP, 0, MAKELONG(10, 10)); } return 0;
        }

        WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
        if (state && state->viewMode == 0) { 
            if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END) {
                int selIdx = SendMessage(hwnd, LB_GETCARETINDEX, 0, 0); if (selIdx == LB_ERR) selIdx = 0;
                int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0);
                if (pType && *pType == 2) {
                    RowItemData FAR* row = (RowItemData FAR*)pType; int c, col = -1;
                    for (c = 0; c < row->count; c++) { if (lstrcmp(row->items[c]->path, g_SelectedListItemPath) == 0 && lstrcmp(row->items[c]->name, g_SelectedListItemName) == 0) { col = c; break; } }
                    if (col == -1) col = 0;
                    
                    if (wp == VK_LEFT) { if (col > 0) col--; else if (selIdx > 0) { selIdx--; row = (RowItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0); if (row) col = row->count - 1; } } 
                    else if (wp == VK_RIGHT) { if (col < row->count - 1) col++; else if (selIdx < SendMessage(hwnd, LB_GETCOUNT, 0, 0) - 1) { selIdx++; col = 0; } } 
                    else if (wp == VK_UP) { if (selIdx > 0) { selIdx--; row = (RowItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0); if (row && col >= row->count) col = row->count - 1; } } 
                    else if (wp == VK_DOWN) { if (selIdx < SendMessage(hwnd, LB_GETCOUNT, 0, 0) - 1) { selIdx++; row = (RowItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0); if (row && col >= row->count) col = row->count - 1; } }
                    else if (wp == VK_HOME) { selIdx = 0; col = 0; row = (RowItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0); }
                    else if (wp == VK_END) { selIdx = SendMessage(hwnd, LB_GETCOUNT, 0, 0) - 1; if (selIdx >= 0) { row = (RowItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0); if (row) col = row->count - 1; } }
                    
                    SendMessage(hwnd, LB_SETSEL, FALSE, -1); SendMessage(hwnd, LB_SETSEL, TRUE, selIdx); SendMessage(hwnd, LB_SETCARETINDEX, selIdx, MAKELONG(TRUE, 0));
                    row = (RowItemData FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0);
                    if (row && col >= 0 && col < row->count) { lstrcpy(g_SelectedListItemPath, row->items[col]->path); lstrcpy(g_SelectedListItemName, row->items[col]->name); g_SelectedListItemIsVirtual = row->items[col]->isVirtual; g_SelectedListItemIsDir = row->items[col]->isDir; InvalidateRect(hwnd, NULL, TRUE); }
                } return 0; 
            }
        } else {
            if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END) {
                LRESULT res = CallWindowProc((WNDPROC)g_lpfnOldListProc, hwnd, msg, wp, lp);
                int selIdx = SendMessage(hwnd, LB_GETCARETINDEX, 0, 0);
                if (selIdx != LB_ERR) { 
                    int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIdx, 0); 
                    if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; lstrcpy(g_SelectedListItemPath, item->path); lstrcpy(g_SelectedListItemName, item->name); g_SelectedListItemIsVirtual = item->isVirtual; g_SelectedListItemIsDir = item->isDir; } 
                } return res;
            }
        }
    }
    
    if (msg == WM_CHAR) {
        DWORD tick = GetTickCount();
        if (tick - g_SearchLastTick > 1000) g_SearchBuf[0] = '\0';
        g_SearchLastTick = tick;
        int len = lstrlen(g_SearchBuf);
        if (len < 63 && wp >= 32 && wp <= 126) {
            g_SearchBuf[len] = (char)wp; g_SearchBuf[len+1] = '\0';
            int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0);
            
            int startIdx = 0;
            if (len == 0) {
                int curSel = SendMessage(hwnd, LB_GETCARETINDEX, 0, 0);
                if (curSel != LB_ERR) startIdx = curSel + 1;
            }
            
            BOOL found = FALSE;
            for (int pass = 0; pass < 2 && !found; pass++) {
                int endIdx = (pass == 0) ? count : startIdx;
                for (int i = (pass == 0) ? startIdx : 0; i < endIdx; i++) {
                    int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, i, 0);
                    if (pType && *pType == 1) { 
                        ListItemData FAR* item = (ListItemData FAR*)pType;
                        if (strnicmp(item->name, g_SearchBuf, len + 1) == 0) {
                            SendMessage(hwnd, LB_SETSEL, FALSE, -1); SendMessage(hwnd, LB_SETSEL, TRUE, i); SendMessage(hwnd, LB_SETCARETINDEX, i, MAKELONG(TRUE, 0));
                            lstrcpy(g_SelectedListItemPath, item->path); lstrcpy(g_SelectedListItemName, item->name); g_SelectedListItemIsVirtual = item->isVirtual; g_SelectedListItemIsDir = item->isDir;
                            InvalidateRect(hwnd, NULL, TRUE); found = TRUE; break;
                        }
                    } else if (pType && *pType == 2) { 
                        RowItemData FAR* row = (RowItemData FAR*)pType;
                        for (int c = 0; c < row->count; c++) {
                            if (strnicmp(row->items[c]->name, g_SearchBuf, len + 1) == 0) {
                                SendMessage(hwnd, LB_SETSEL, FALSE, -1); SendMessage(hwnd, LB_SETSEL, TRUE, i); SendMessage(hwnd, LB_SETCARETINDEX, i, MAKELONG(TRUE, 0));
                                lstrcpy(g_SelectedListItemPath, row->items[c]->path); lstrcpy(g_SelectedListItemName, row->items[c]->name); g_SelectedListItemIsVirtual = row->items[c]->isVirtual; g_SelectedListItemIsDir = row->items[c]->isDir;
                                InvalidateRect(hwnd, NULL, TRUE); found = TRUE; break;
                            }
                        } if (found) break;
                    }
                }
            }
        } return 0;
    }

    if (msg == WM_ERASEBKGND) {
        char pCls[64]; GetClassName(GetParent(hwnd), pCls, 64);
        if (lstrcmpi(pCls, "Win95DesktopClass") == 0) { HDC hdc = (HDC)wp; if (!g_bIsWindowed) PaintDesktop(hdc); else { RECT rc; GetClientRect(hwnd, &rc); FillRect(hdc, &rc, g_hbrDesktop); } return 1; }
    }
    
    if (msg == WM_LBUTTONDOWN) { 
        g_bListDragging = TRUE; g_DragStartPt.x = (short)LOWORD(lp); g_DragStartPt.y = (short)HIWORD(lp); 
        int hitIdx = -1; int topIdx = SendMessage(hwnd, LB_GETTOPINDEX, 0, 0); int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0);
        for (int i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hwnd, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (g_DragStartPt.y >= rc.top && g_DragStartPt.y <= rc.bottom) { hitIdx = i; break; } } else break; }
        
        // Populate path reference immediately so drag drops have a valid target verification
        if (hitIdx >= 0) {
            int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, hitIdx, 0);
            if (pType && *pType == 1) {
                ListItemData FAR* item = (ListItemData FAR*)pType;
                lstrcpy(g_SelectedListItemPath, item->path); lstrcpy(g_SelectedListItemName, item->name); g_SelectedListItemIsVirtual = item->isVirtual; g_SelectedListItemIsDir = item->isDir;
            } else if (pType && *pType == 2) {
                RowItemData FAR* row = (RowItemData FAR*)pType; int col = g_DragStartPt.x / CELL_W;
                if (col >= 0 && col < row->count) {
                    ListItemData FAR* item = row->items[col];
                    lstrcpy(g_SelectedListItemPath, item->path); lstrcpy(g_SelectedListItemName, item->name); g_SelectedListItemIsVirtual = item->isVirtual; g_SelectedListItemIsDir = item->isDir;
                }
            }
        }

        g_bClickOnSelected = (hitIdx >= 0 && SendMessage(hwnd, LB_GETSEL, hitIdx, 0));
        if (g_bClickOnSelected && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000)) { SetCapture(hwnd); return 0; }
        SetCapture(hwnd); 
    }
    
    if (msg == WM_MOUSEMOVE && g_bListDragging && (wp & MK_LBUTTON)) { 
        int x = (short)LOWORD(lp); int y = (short)HIWORD(lp);
        if (abs(x - g_DragStartPt.x) > 3 || abs(y - g_DragStartPt.y) > 3) {
            POINT pt; pt.x = x; pt.y = y; ClientToScreen(hwnd, &pt); HWND hTarget = WindowFromPoint(pt);
            DWORD targetPid = 0; if (hTarget) GetWindowThreadProcessId(hTarget, &targetPid);
            
            if (targetPid != GetCurrentProcessId()) {
                g_bListDragging = FALSE; ReleaseCapture(); 
                int selCount = SendMessage(hwnd, LB_GETSELCOUNT, 0, 0);
                if (selCount > 0) {
                    int* selIndices = (int*)malloc(selCount * sizeof(int)); SendMessage(hwnd, LB_GETSELITEMS, selCount, (LPARAM)selIndices);
                    char** fileNames = (char**)malloc(selCount * 16 * sizeof(char*)); int fnCount = 0;
                    for (int i = 0; i < selCount; i++) {
                        int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIndices[i], 0);
                        if (pType && *pType == 1) {
                            ListItemData FAR* item = (ListItemData FAR*)pType; fileNames[fnCount] = (char*)malloc(MAX_PATH); GetRealPaths(item, fileNames[fnCount], NULL); fnCount++;
                        } else if (pType && *pType == 2) {
                            RowItemData FAR* row = (RowItemData FAR*)pType;
                            for (int c = 0; c < row->count; c++) { fileNames[fnCount] = (char*)malloc(MAX_PATH); GetRealPaths(row->items[c], fileNames[fnCount], NULL); fnCount++; }
                        }
                    }
                    if (fnCount > 0) InitiateOleDragDropMultiple(hwnd, fileNames, fnCount);
                    for (int i = 0; i < fnCount; i++) free(fileNames[i]); free(fileNames); free(selIndices);
                } return 0;
            } SetCursor(LoadCursor(NULL, IDC_CROSS)); return 0; 
        }
    }
    
    if (msg == WM_LBUTTONUP) {
        if (g_bListDragging) {
            g_bListDragging = FALSE; ReleaseCapture(); 
            int x = (short)LOWORD(lp); int y = (short)HIWORD(lp);
            if (abs(x - g_DragStartPt.x) <= 3 && abs(y - g_DragStartPt.y) <= 3) {
                if (g_bClickOnSelected) {
                    int hitIdx = -1; int topIdx = SendMessage(hwnd, LB_GETTOPINDEX, 0, 0); int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0);
                    for (int i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hwnd, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (y >= rc.top && y <= rc.bottom) { hitIdx = i; break; } } else break; }
                    if (hitIdx >= 0) { SendMessage(hwnd, LB_SETSEL, FALSE, -1); SendMessage(hwnd, LB_SETSEL, TRUE, hitIdx); SendMessage(hwnd, LB_SETCARETINDEX, hitIdx, MAKELONG(TRUE, 0)); }
                }
            } else {
                POINT pt; pt.x = x; pt.y = y; ClientToScreen(hwnd, &pt); HWND hTarget = WindowFromPoint(pt); 
                if (hTarget) {
                    HWND hTgtParent = hTarget; char targetCls[64]; GetClassName(hTgtParent, targetCls, 64);
                    while (hTgtParent && lstrcmpi(targetCls, "Win95FolderClass") != 0 && lstrcmpi(targetCls, "Win95DesktopClass") != 0 && lstrcmpi(targetCls, "Win95SearchClass") != 0) {
                        hTgtParent = GetParent(hTgtParent); if (hTgtParent) GetClassName(hTgtParent, targetCls, 64);
                    }

                    BOOL bInternalHandled = FALSE;

                    if (hTgtParent && (lstrcmpi(targetCls, "Win95FolderClass") == 0 || lstrcmpi(targetCls, "Win95DesktopClass") == 0 || lstrcmpi(targetCls, "Win95SearchClass") == 0)) {
                        WindowState FAR* tgtState = (WindowState FAR*)GetWindowLongPtr(hTgtParent, 0); BOOL validTarget = FALSE; char targetFolder[MAX_PATH] = ""; BOOL tgtVirtual = FALSE;
                        if (hTarget && GetWindowLongPtr(hTarget, GWLP_ID) == ID_LIST && tgtState) {
                            POINT clPt = pt; ScreenToClient(hTarget, &clPt);
                            int hitIdx = -1; int topIdx = SendMessage(hTarget, LB_GETTOPINDEX, 0, 0); int count = SendMessage(hTarget, LB_GETCOUNT, 0, 0);
                            for (int i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hTarget, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (clPt.y >= rc.top && clPt.y <= rc.bottom && clPt.x >= rc.left && clPt.x <= rc.right) { hitIdx = i; break; } } else break; }
                            if (hitIdx >= 0) {
                                int FAR* pType = (int FAR*)SendMessage(hTarget, LB_GETITEMDATA, hitIdx, 0);
                                if (pType && *pType == 2 && tgtState->viewMode == 0) { RowItemData FAR* row = (RowItemData FAR*)pType; int col = clPt.x / CELL_W; if (col >= 0 && col < row->count && row->items[col]->isDir) { lstrcpy(targetFolder, row->items[col]->path); tgtVirtual = row->items[col]->isVirtual; validTarget = TRUE; } } 
                                else if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; if (item->isDir) { lstrcpy(targetFolder, item->path); tgtVirtual = item->isVirtual; validTarget = TRUE; } }
                            }
                        } else if (hTarget && GetWindowLongPtr(hTarget, GWLP_ID) == ID_TREE && tgtState) {
                            POINT clPt = pt; ScreenToClient(hTarget, &clPt); int hitIdx = -1; int topIdx = SendMessage(hTarget, LB_GETTOPINDEX, 0, 0); int count = SendMessage(hTarget, LB_GETCOUNT, 0, 0);
                            for (int i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hTarget, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (clPt.y >= rc.top && clPt.y <= rc.bottom) { hitIdx = i; break; } } else break; }
                            if (hitIdx >= 0) { TreeItemData FAR* item = (TreeItemData FAR*)SendMessage(hTarget, LB_GETITEMDATA, hitIdx, 0); if (item) { lstrcpy(targetFolder, item->pathOrId); tgtVirtual = item->isVirtual; validTarget = TRUE; } }
                        }
                        if (!validTarget && tgtState && lstrcmpi(targetCls, "Win95SearchClass") != 0) { lstrcpy(targetFolder, tgtState->pathOrId); tgtVirtual = tgtState->isVirtual; validTarget = TRUE; }
                        
                        if (validTarget && lstrcmpi(targetFolder, g_SelectedListItemPath) != 0 && g_SelectedListItemPath[0]) {
                            bInternalHandled = TRUE;
                            int op = 1; if (GetKeyState(VK_SHIFT) & 0x8000) op = 2; 
                            if (GetKeyState(VK_CONTROL) & 0x8000) { if (GetKeyState(VK_SHIFT) & 0x8000) op = 3; else op = 1; }
                            if (GetKeyState(VK_MENU) & 0x8000) op = 3;

                            int selCount = SendMessage(hwnd, LB_GETSELCOUNT, 0, 0);
                            if (selCount > 0) {
                                int* selIndices = (int*)malloc(selCount * sizeof(int)); SendMessage(hwnd, LB_GETSELITEMS, selCount, (LPARAM)selIndices);
                                char* pJobSrc = g_CurrentJob.src; BOOL requiresCopy = FALSE;

                                for (int i = 0; i < selCount; i++) {
                                    int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIndices[i], 0);
                                    if (pType && (*pType == 1 || *pType == 2)) {
                                        if (*pType == 1) {
                                            ListItemData FAR* item = (ListItemData FAR*)pType; char srcPath[MAX_PATH]; char srcName[64]; GetRealPaths(item, srcPath, srcName);
                                            char srcParent[MAX_PATH]; lstrcpy(srcParent, srcPath); char* pSlash = strrchr(srcParent, '\\'); if (pSlash) *pSlash = '\0';
                                            if (lstrcmpi(srcParent, targetFolder) == 0 && op == 2) continue; 

                                            if (op == 3 || tgtVirtual || item->isVirtual) {
                                                char newId[16]; GetNewIniId(newId); IniShortcut FAR* sh = (IniShortcut FAR*)malloc(sizeof(IniShortcut)); 
                                                if (sh) { memset(sh, 0, sizeof(IniShortcut)); lstrcpy(sh->id, newId); lstrcpy(sh->parentId, targetFolder); sprintf(sh->name, "%s%s", (op==3||!tgtVirtual)?"Shortcut to ":"", srcName); sh->isFolder = item->isDir; if (item->isVirtual) { for (int k = 0; k < g_IniShortcutCount; k++) { if (lstrcmp(g_IniShortcuts[k]->id, item->path) == 0) { lstrcpy(sh->exe, g_IniShortcuts[k]->exe); lstrcpy(sh->params, g_IniShortcuts[k]->params); lstrcpy(sh->icon, g_IniShortcuts[k]->icon); sh->minimized = g_IniShortcuts[k]->minimized; break; } } } else { lstrcpy(sh->exe, srcPath); } if (NameExists(targetFolder, sh->name, TRUE)) { char temp[MAX_PATH]; sprintf(temp, "Copy of %s", sh->name); lstrcpy(sh->name, temp); } if (!NameExists(targetFolder, sh->name, TRUE)) { SaveIniEntry(sh); } free(sh); }
                                            } else { if (pJobSrc - g_CurrentJob.src + lstrlen(srcPath) + 2 < 16384) { requiresCopy = TRUE; lstrcpy(pJobSrc, srcPath); pJobSrc += lstrlen(pJobSrc) + 1; g_CurrentJob.isDir = item->isDir; } }
                                        } else {
                                            RowItemData FAR* row = (RowItemData FAR*)pType;
                                            for(int c=0; c < row->count; c++) {
                                                char srcPath[MAX_PATH]; char srcName[64]; GetRealPaths(row->items[c], srcPath, srcName);
                                                char srcParent[MAX_PATH]; lstrcpy(srcParent, srcPath); char* pSlash = strrchr(srcParent, '\\'); if (pSlash) *pSlash = '\0';
                                                if (lstrcmpi(srcParent, targetFolder) == 0 && op == 2) continue;
                                                
                                                if (op == 3 || tgtVirtual || row->items[c]->isVirtual) {
                                                    char newId[16]; GetNewIniId(newId); IniShortcut FAR* sh = (IniShortcut FAR*)malloc(sizeof(IniShortcut));
                                                    if (sh) { memset(sh, 0, sizeof(IniShortcut)); lstrcpy(sh->id, newId); lstrcpy(sh->parentId, targetFolder); sprintf(sh->name, "%s%s", (op==3||!tgtVirtual)?"Shortcut to ":"", srcName); sh->isFolder = row->items[c]->isDir; if (row->items[c]->isVirtual) { for (int k = 0; k < g_IniShortcutCount; k++) { if (lstrcmp(g_IniShortcuts[k]->id, row->items[c]->path) == 0) { lstrcpy(sh->exe, g_IniShortcuts[k]->exe); lstrcpy(sh->params, g_IniShortcuts[k]->params); lstrcpy(sh->icon, g_IniShortcuts[k]->icon); sh->minimized = g_IniShortcuts[k]->minimized; break; } } } else { lstrcpy(sh->exe, srcPath); } if (NameExists(targetFolder, sh->name, TRUE)) { char temp[MAX_PATH]; sprintf(temp, "Copy of %s", sh->name); lstrcpy(sh->name, temp); } if (!NameExists(targetFolder, sh->name, TRUE)) { SaveIniEntry(sh); } free(sh); }
                                                } else { if (pJobSrc - g_CurrentJob.src + lstrlen(srcPath) + 2 < 16384) { requiresCopy = TRUE; lstrcpy(pJobSrc, srcPath); pJobSrc += lstrlen(pJobSrc) + 1; g_CurrentJob.isDir = row->items[c]->isDir; } }
                                            }
                                        }
                                    }
                                }
                                
                                if (requiresCopy) {
                                    *pJobSrc = '\0'; 
                                    lstrcpy(g_CurrentJob.dst, targetFolder); g_CurrentJob.isMove = (op == 2); g_ReplaceMode = 0; 
                                    CreateCenteredDialog(g_hInst, GetParent(hwnd), "CopyProgressDlgClass", g_CurrentJob.isMove ? "Moving" : "Copying", 280, 140);
                                } else { LoadIniShortcuts(); InvalidateRect(hTgtParent, NULL, TRUE); PostMessage(hTgtParent, WM_COMMAND, 4028, 0); }
                                free(selIndices);
                            }
                        }
                    }
                    
                    if (!bInternalHandled && g_SelectedListItemPath[0] != '\0') {
                        HWND hDropTarget = hTarget; BOOL acceptsFiles = FALSE;
                        while (hDropTarget) {
                            if (GetWindowLong(hDropTarget, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) { acceptsFiles = TRUE; break; }
                            hDropTarget = GetParent(hDropTarget);
                        }
                        
                        if (acceptsFiles && hDropTarget) {
                            int selCount = SendMessage(hwnd, LB_GETSELCOUNT, 0, 0);
                            if (selCount > 0) {
                                int* selIndices = (int*)malloc(selCount * sizeof(int)); SendMessage(hwnd, LB_GETSELITEMS, selCount, (LPARAM)selIndices);
                                int reqSize = 1; char** fullPaths = (char**)malloc(selCount * 16 * sizeof(char*)); int fnCount = 0;
                                for (int i = 0; i < selCount; i++) {
                                    int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, selIndices[i], 0);
                                    if (pType && *pType == 1) { ListItemData FAR* item = (ListItemData FAR*)pType; fullPaths[fnCount] = (char*)malloc(MAX_PATH); GetRealPaths(item, fullPaths[fnCount], NULL); reqSize += lstrlen(fullPaths[fnCount]) + 1; fnCount++; } 
                                    else if (pType && *pType == 2) { RowItemData FAR* row = (RowItemData FAR*)pType; for (int c = 0; c < row->count; c++) { fullPaths[fnCount] = (char*)malloc(MAX_PATH); GetRealPaths(row->items[c], fullPaths[fnCount], NULL); reqSize += lstrlen(fullPaths[fnCount]) + 1; fnCount++; } }
                                }
                                
                                HGLOBAL hMem = GlobalAlloc(GHND | GMEM_SHARE, sizeof(MY_DROPFILES) + reqSize);
                                if (hMem) {
                                    MY_DROPFILES* df = (MY_DROPFILES*)GlobalLock(hMem); df->pFiles = sizeof(MY_DROPFILES);
                                    POINT clientPt = pt; ScreenToClient(hDropTarget, &clientPt); df->pt.x = clientPt.x; df->pt.y = clientPt.y; df->fNC = FALSE; df->fWide = FALSE;
                                    char* pDest = (char*)(df + 1); for (int i = 0; i < fnCount; i++) { lstrcpy(pDest, fullPaths[i]); pDest += lstrlen(fullPaths[i]) + 1; free(fullPaths[i]); }
                                    *pDest = '\0'; GlobalUnlock(hMem); PostMessage(hDropTarget, WM_DROPFILES, (WPARAM)hMem, 0);
                                } free(fullPaths); free(selIndices);
                            }
                        } else {
                            char pathBuf[MAX_PATH + 4]; sprintf(pathBuf, "\"%s\"", g_SelectedListItemPath);
                            for (int cIdx = 0; pathBuf[cIdx] != '\0'; cIdx++) PostMessage(hTarget, WM_CHAR, (WPARAM)pathBuf[cIdx], 0);
                        }
                    }
                }
            }
        }
    }

    if (msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONUP) {
        int x = (short)LOWORD(lp); int y = (short)HIWORD(lp); int count = SendMessage(hwnd, LB_GETCOUNT, 0, 0); int topIdx = SendMessage(hwnd, LB_GETTOPINDEX, 0, 0); int hitIdx = -1; int i;
        WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(GetParent(hwnd), 0);
        for (i = topIdx; i < count; i++) { RECT rc; if (SendMessage(hwnd, LB_GETITEMRECT, i, (LPARAM)(LPRECT)&rc) != LB_ERR) { if (y >= rc.top && y <= rc.bottom && x >= rc.left && x <= rc.right) { hitIdx = i; break; } } else break; }
        if (hitIdx >= 0) {
            int FAR* pType = (int FAR*)SendMessage(hwnd, LB_GETITEMDATA, hitIdx, 0);
            if (pType && *pType == 2 && state && state->viewMode == 0) {
                RowItemData FAR* row = (RowItemData FAR*)pType; int col = x / CELL_W;
                if (col >= 0 && col < row->count) {
                    ListItemData FAR* item = row->items[col]; lstrcpy(g_SelectedListItemPath, item->path); lstrcpy(g_SelectedListItemName, item->name); g_SelectedListItemIsVirtual = item->isVirtual; g_SelectedListItemIsDir = item->isDir; InvalidateRect(hwnd, NULL, TRUE);
                    if (msg == WM_LBUTTONDBLCLK) {
                        if (item->isDir) { char pcls[64]; GetClassName(GetParent(hwnd), pcls, 64); if (lstrcmpi(pcls, "Win95DesktopClass") == 0 || lstrcmpi(pcls, "Win95SearchClass") == 0) { char pathTarget[MAX_PATH]; lstrcpy(pathTarget, item->path); CreateWindowEx(0, "Win95FolderClass", item->name, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, g_WinX, g_WinY, g_WinW, g_WinH, NULL, NULL, g_hInst, (LPVOID)pathTarget); } else { lstrcpy(state->pathOrId, item->path); state->isVirtual = item->isVirtual; SetWindowText(GetParent(hwnd), item->name); if (state->isVirtual) ExpandAllParentsVirtual(state->pathOrId); else ExpandAllParentsFS(state->pathOrId); RebuildTree(GetDlgItem(GetParent(hwnd), ID_TREE), state); RebuildList(GetParent(hwnd), state); } } 
                        else { if (item->isVirtual) { int k; for (k = 0; k < g_IniShortcutCount; k++) { if (lstrcmpi(g_IniShortcuts[k]->id, item->path) == 0) { if (g_IniShortcuts[k]->exe[0]) ShellExecute(hwnd, "open", g_IniShortcuts[k]->exe, g_IniShortcuts[k]->params, NULL, SW_SHOWNORMAL); break; } } } else ShellExecute(hwnd, "open", item->path, NULL, NULL, SW_SHOWNORMAL); }
                        return 0; 
                    } else if (msg == WM_RBUTTONUP) { g_ContextIsVirtual = item->isVirtual; lstrcpy(g_ContextId, item->path); g_ContextIsFolder = item->isDir; POINT pt; pt.x = x; pt.y = y; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, item->isDir, FALSE, state->viewMode); }
                } else if (msg == WM_RBUTTONUP) { POINT pt; pt.x = x; pt.y = y; g_ContextIsVirtual = state->isVirtual; lstrcpy(g_ContextId, state->pathOrId); g_ContextIsFolder = TRUE; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, TRUE, TRUE, state->viewMode); return 0; }
            } else if (pType && *pType == 1) {
                ListItemData FAR* item; POINT pt; pt.x = x; pt.y = y; SendMessage(hwnd, LB_SETSEL, FALSE, -1); SendMessage(hwnd, LB_SETSEL, TRUE, hitIdx); SendMessage(hwnd, LB_SETCARETINDEX, hitIdx, 0); item = (ListItemData FAR*)pType;
                if (msg == WM_LBUTTONDBLCLK) {
                    if (item->isDir) { char pcls[64]; GetClassName(GetParent(hwnd), pcls, 64); if (lstrcmpi(pcls, "Win95DesktopClass") == 0 || lstrcmpi(pcls, "Win95SearchClass") == 0) { char pathTarget[MAX_PATH]; lstrcpy(pathTarget, item->path); CreateWindowEx(0, "Win95FolderClass", item->name, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, g_WinX, g_WinY, g_WinW, g_WinH, NULL, NULL, g_hInst, (LPVOID)pathTarget); } else { lstrcpy(state->pathOrId, item->path); state->isVirtual = item->isVirtual; SetWindowText(GetParent(hwnd), item->name); if (state->isVirtual) ExpandAllParentsVirtual(state->pathOrId); else ExpandAllParentsFS(state->pathOrId); RebuildTree(GetDlgItem(GetParent(hwnd), ID_TREE), state); RebuildList(GetParent(hwnd), state); } }
                    else { if (item->isVirtual) { int k; for (k = 0; k < g_IniShortcutCount; k++) { if (lstrcmpi(g_IniShortcuts[k]->id, item->path) == 0) { if (g_IniShortcuts[k]->exe[0]) ShellExecute(hwnd, "open", g_IniShortcuts[k]->exe, g_IniShortcuts[k]->params, NULL, SW_SHOWNORMAL); break; } } } else ShellExecute(hwnd, "open", item->path, NULL, NULL, SW_SHOWNORMAL); }
                    return 0; 
                } else if (msg == WM_RBUTTONUP) { g_ContextIsVirtual = item->isVirtual; lstrcpy(g_ContextId, item->path); g_ContextIsFolder = item->isDir; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, item->isDir, FALSE, state?state->viewMode:3); return 0; }
            }
        } else if (msg == WM_RBUTTONUP) { POINT pt; pt.x = x; pt.y = y; g_ContextIsVirtual = state ? state->isVirtual : TRUE; lstrcpy(g_ContextId, state ? state->pathOrId : "0"); g_ContextIsFolder = TRUE; ClientToScreen(hwnd, &pt); ShowContextMenu(GetParent(hwnd), pt.x, pt.y, TRUE, TRUE, state ? state->viewMode : 3); return 0; }
    }
    
    return CallWindowProc((WNDPROC)g_lpfnOldListProc, hwnd, msg, wp, lp);
}
HWND FindTaskbar(void) {
    HWND hwnd = GetWindow(GetDesktopWindow(), GW_CHILD);
    while (hwnd) {
        if (IsWindowVisible(hwnd)) {
            char cls[64]; GetClassName(hwnd, cls, 64);
            if (lstrcmpi(cls, "Shell_TrayWnd") == 0 || lstrcmpi(cls, "TaskBar") == 0) return hwnd;
            {
                HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
                char modName[MAX_PATH];
                if (GetModuleFileName(hInst, modName, MAX_PATH)) {
                    char *p = strrchr(modName, '\\');
                    if (p) p++; else p = modName;
                    if (lstrcmpi(p, "taskbar.exe") == 0) return hwnd;
                }
            }
        }
        hwnd = GetWindow(hwnd, GW_HWNDNEXT);
    }
    return NULL;
}

LRESULT CALLBACK DesktopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(hwnd, 0);
    switch (msg) {
        case WM_CREATE: {
            state = (WindowState FAR*)malloc(sizeof(WindowState));
            lstrcpy(state->pathOrId, "0"); state->isVirtual = TRUE; SetWindowLongPtr(hwnd, 0, (LONG_PTR)state); LoadConfig(); state->viewMode = 0; ChangeViewMode(hwnd, 0, state); 
            if (!g_bIsWindowed) SetTimer(hwnd, 1000, 2000, NULL);
            return 0;
        }
        case WM_TIMER: {
            if (wp == 1000 && !g_bIsWindowed) {
                int cx = GetSystemMetrics(SM_CXSCREEN);
                int cy = GetSystemMetrics(SM_CYSCREEN);
                RECT rcWnd; GetWindowRect(hwnd, &rcWnd);
                if ((rcWnd.right - rcWnd.left) != cx || (rcWnd.bottom - rcWnd.top) != cy) {
                    SetWindowPos(hwnd, NULL, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                RECT rc; GetClientRect(hwnd, &rc);
                SendMessage(hwnd, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom));
            } return 0;
        }
        case WM_DISPLAYCHANGE: {
            if (!g_bIsWindowed) {
                int cx = LOWORD(lp); int cy = HIWORD(lp);
                SetWindowPos(hwnd, NULL, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            } return 0;
        }
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp; 
            if (!g_bIsWindowed) { PaintDesktop(hdc); } 
            else { RECT rc; GetClientRect(hwnd, &rc); FillRect(hdc, &rc, g_hbrDesktop); }
            return 1;
        }
        case WM_SETFOCUS: { HWND hList = GetDlgItem(hwnd, ID_LIST); if (hList) SetFocus(hList); return 0; }
        case WM_WINDOWPOSCHANGING: { 
            if (!g_bIsWindowed) { WINDOWPOS FAR* pos = (WINDOWPOS FAR*)lp; pos->hwndInsertAfter = HWND_BOTTOM; pos->flags &= ~SWP_NOZORDER; } break; 
        }
        case WM_MEASUREITEM: { 
            LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lp; 
            if (lpmis->CtlID == ID_LIST) { if (state && state->viewMode == 0) lpmis->itemHeight = CELL_H; else if (state && state->viewMode == 1) lpmis->itemHeight = g_IconSizeSmall + 6; else lpmis->itemHeight = g_IconSizeSmall + 4; return TRUE; } break; 
        }
        case WM_DRAWITEM: { HandleDrawItem(hwnd, wp, lp); return TRUE; }
        case WM_DELETEITEM: { 
            LPDELETEITEMSTRUCT lpdis = (LPDELETEITEMSTRUCT)lp; 
            if (lpdis->itemData && lpdis->itemData != (DWORD)LB_ERR) { 
                if (lpdis->CtlID == ID_TREE) { free((void FAR*)lpdis->itemData); } 
                else if (lpdis->CtlID == ID_LIST) { int FAR* pType = (int FAR*)lpdis->itemData; if (*pType == 2) { RowItemData FAR* row = (RowItemData FAR*)lpdis->itemData; free(row); } } 
            } return TRUE; 
        }
        case WM_COMMAND: { int id = wp; if (id >= 4001 && id <= 4060 && state) HandleListCommand(hwnd, wp, lp, state); return 0; }
        case WM_SIZE: { 
            HWND hList = GetDlgItem(hwnd, ID_LIST); 
            if (hList) { 
                RECT rcClient; rcClient.left = 0; rcClient.top = 0; rcClient.right = LOWORD(lp); rcClient.bottom = HIWORD(lp);
                if (!g_bIsWindowed) {
                    HWND hTaskbar = FindTaskbar();
                    if (hTaskbar) {
                        RECT rcT; GetWindowRect(hTaskbar, &rcT);
                        if (rcT.top > rcClient.bottom / 2 && rcT.left <= 0) rcClient.bottom = rcT.top;
                        else if (rcT.bottom < rcClient.bottom / 2 && rcT.left <= 0) rcClient.top = rcT.bottom;
                        else if (rcT.right < rcClient.right / 2 && rcT.top <= 0) rcClient.left = rcT.right;
                        else if (rcT.left > rcClient.right / 2 && rcT.top <= 0) rcClient.right = rcT.left;
                    }
                }
                MoveWindow(hList, rcClient.left, rcClient.top, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, TRUE); 
            } return 0; 
        }
        case WM_DESTROY: { 
            HWND hList = GetDlgItem(hwnd, ID_LIST); if (hList) SendMessage(hList, LB_RESETCONTENT, 0, 0);
            HANDLE oldBlock; if (!g_bIsWindowed) KillTimer(hwnd, 1000);
            oldBlock = GetProp(hwnd, "BulkBlock"); if (oldBlock) { free((void FAR*)oldBlock); RemoveProp(hwnd, "BulkBlock"); }
            if (state) { free(state); SetWindowLongPtr(hwnd, 0, 0); } 
            if (hwnd == g_hwndMain) PostQuitMessage(0); return 0; 
        }
    } return DefWindowProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK FolderWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(hwnd, 0);
    switch(msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lp; char* targetPath = (char*)cs->lpCreateParams; HWND hTree, hToolbar; HMENU hMenu, hFile, hEdit, hView, hTools, hHelp; 
            BOOL isVirt = TRUE;
            
            state = (WindowState FAR*)malloc(sizeof(WindowState)); SetWindowLongPtr(hwnd, 0, (LONG_PTR)state); LoadConfig();
            
            if (targetPath && (strchr(targetPath, '\\') || (lstrlen(targetPath)>1 && targetPath[1] == ':'))) isVirt = FALSE;
            
            if (targetPath && targetPath[0] != '\0') { 
                lstrcpy(state->pathOrId, targetPath); 
                state->isVirtual = isVirt; 
                if(!isVirt) ExpandAllParentsFS(targetPath); 
                else ExpandAllParentsVirtual(targetPath); 
            } else { 
                lstrcpy(state->pathOrId, "0"); 
                state->isVirtual = TRUE; 
                if (!IsExpanded("0")) ToggleExpand("0"); 
            }
            state->viewMode = g_GlobalViewMode;

            hMenu = CreateMenu(); hFile = CreatePopupMenu(); AppendMenu(hFile, MF_STRING, 4001, "New &Folder\t"); AppendMenu(hFile, MF_SEPARATOR, 0, NULL); AppendMenu(hFile, MF_STRING, 4002, "Create &Shortcut"); AppendMenu(hFile, MF_STRING, 4003, "&Delete\tDel"); AppendMenu(hFile, MF_STRING, 4004, "Re&name\tF2"); AppendMenu(hFile, MF_STRING, 4005, "P&roperties"); AppendMenu(hFile, MF_SEPARATOR, 0, NULL); AppendMenu(hFile, MF_STRING, 4006, "&Close"); AppendMenu(hMenu, MF_POPUP, (UINT)hFile, "&File");
            hEdit = CreatePopupMenu(); AppendMenu(hEdit, MF_STRING, 4011, "Cu&t\tCtrl+X"); AppendMenu(hEdit, MF_STRING, 4012, "&Copy\tCtrl+C"); AppendMenu(hEdit, MF_STRING, 4013, "&Paste\tCtrl+V"); AppendMenu(hEdit, MF_STRING, 4014, "Paste &Shortcut"); AppendMenu(hEdit, MF_SEPARATOR, 0, NULL); AppendMenu(hEdit, MF_STRING, 4015, "Select &All\tCtrl+A"); AppendMenu(hEdit, MF_STRING, 4016, "&Invert Selection"); AppendMenu(hMenu, MF_POPUP, (UINT)hEdit, "&Edit");
            hView = CreatePopupMenu(); AppendMenu(hView, MF_STRING | (g_bShowToolbar ? MF_CHECKED : 0), 4020, "&Toolbar"); AppendMenu(hView, MF_STRING | (g_bShowStatusBar ? MF_CHECKED : 0), 4021, "&Status Bar"); AppendMenu(hView, MF_SEPARATOR, 0, NULL); AppendMenu(hView, MF_STRING | (state->viewMode == 0 ? MF_CHECKED : 0), 4022, "Lar&ge Icons"); AppendMenu(hView, MF_STRING | (state->viewMode == 1 ? MF_CHECKED : 0), 4023, "S&mall Icons"); AppendMenu(hView, MF_STRING | (state->viewMode == 2 ? MF_CHECKED : 0), 4024, "&List"); AppendMenu(hView, MF_STRING | (state->viewMode == 3 ? MF_CHECKED : 0), 4025, "&Details"); AppendMenu(hView, MF_SEPARATOR, 0, NULL); AppendMenu(hView, MF_STRING, 4028, "&Refresh\tF5"); AppendMenu(hView, MF_STRING, 4029, "&Options..."); AppendMenu(hMenu, MF_POPUP, (UINT)hView, "&View");
            hTools = CreatePopupMenu(); AppendMenu(hTools, MF_STRING, 4030, "&Find..."); AppendMenu(hTools, MF_SEPARATOR, 0, NULL); AppendMenu(hTools, MF_STRING, 4033, "&Go to..."); AppendMenu(hMenu, MF_POPUP, (UINT)hTools, "&Tools");
            hHelp = CreatePopupMenu(); AppendMenu(hHelp, MF_STRING, 4040, "&Help Topics"); AppendMenu(hHelp, MF_SEPARATOR, 0, NULL); AppendMenu(hHelp, MF_STRING, 4041, "&About Calmira"); AppendMenu(hMenu, MF_POPUP, (UINT)hHelp, "&Help"); SetMenu(hwnd, hMenu); DrawMenuBar(hwnd);
            
            hToolbar = CreateWindowEx(0, "STATIC", "", WS_CHILD | (g_bShowToolbar ? WS_VISIBLE : 0) | SS_NOTIFY, 0, 0, 0, 0, hwnd, (HMENU)ID_TOOLBAR, g_hInst, NULL); g_lpfnOldToolbarProc = (FARPROC)SetWindowLongPtr(hToolbar, GWLP_WNDPROC, (LONG_PTR)ToolbarProc);
            
            // Address Bar Initialization
            HWND hAddressBarLocal = CreateWindowEx(0, WC_COMBOBOX, NULL, 
                WS_CHILD | (g_bShowToolbar ? WS_VISIBLE : 0) | CBS_DROPDOWN | CBS_AUTOHSCROLL, 
                0, 0, 0, 0, hwnd, (HMENU)IDC_MYADDRESSBAR, g_hInst, NULL);
                
            SendMessage(hAddressBarLocal, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            LoadRecents(hAddressBarLocal);

            COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
            if (GetComboBoxInfo(hAddressBarLocal, &cbi)) {
                SetWindowSubclass(cbi.hwndItem, AddressBarSubclassProc, 1, 0);
            }

            CreateWindow("BUTTON", "Name", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_NAME, g_hInst, NULL); 
            CreateWindow("BUTTON", "Modified", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_DATE, g_hInst, NULL); 
            CreateWindow("BUTTON", "Size", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_SIZE, g_hInst, NULL); 
            CreateWindow("BUTTON", "Type", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_TYPE, g_hInst, NULL);
            
            hTree = CreateWindowEx(0, "LISTBOX", "", WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_BORDER, 0, 0, 0, 0, hwnd, (HMENU)ID_TREE, g_hInst, NULL); SendMessage(hTree, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE); g_lpfnOldTreeProc = (FARPROC)SetWindowLongPtr(hTree, GWLP_WNDPROC, (LONG_PTR)TreeProc);
            
            CreateWindowEx(0, "STATIC", " Ready", WS_CHILD | (g_bShowStatusBar ? WS_VISIBLE : 0) | WS_BORDER | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)300, g_hInst, NULL); 
            SendMessage(GetDlgItem(hwnd, 300), WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE); 
            
            ChangeViewMode(hwnd, state->viewMode, state);
            RebuildTree(hTree, state); 
            SetFocus(GetDlgItem(hwnd, ID_LIST));
            return 0;
        }
        case WM_SETFOCUS: {
            HWND hList = GetDlgItem(hwnd, ID_LIST);
            if (hList) SetFocus(hList);
            return 0;
        }
        case WM_INITMENUPOPUP: { if (LOWORD(lp) == 1) { EnableMenuItem((HMENU)wp, 4013, MF_BYCOMMAND | (g_ClipOp == 0 ? MF_GRAYED : MF_ENABLED)); EnableMenuItem((HMENU)wp, 4014, MF_BYCOMMAND | (g_ClipOp == 0 ? MF_GRAYED : MF_ENABLED)); } return 0; }
        case WM_SIZE: {
            if (!state) return DefWindowProc(hwnd, msg, wp, lp);
            int cx = LOWORD(lp); int cy = HIWORD(lp); int tbH = g_bShowToolbar ? (g_IconSizeSmall + 16) : 0; int statH = g_bShowStatusBar ? 22 : 0; int listX = g_SplitX + 4; int listW = cx - listX; int hdrH = (state->viewMode == 3) ? 22 : 0;
            
            // Layout Address Bar and Toolbar
            if (g_bShowToolbar) {
                int btnW = g_IconSizeSmall + 8;
                int tbWidth = 4 + 6 * (btnW + 4); 
                
                MoveWindow(GetDlgItem(hwnd, ID_TOOLBAR), 0, 0, tbWidth, tbH, TRUE);
                
                HWND hAddr = GetDlgItem(hwnd, IDC_MYADDRESSBAR);
                if (hAddr) {
                    int addrX = tbWidth + 4;
                    int addrW = cx - addrX - 4;
                    if (addrW < 0) addrW = 0;
                    
                    // Force the inner edit box height to match the toolbar
                    SendMessage(hAddr, CB_SETITEMHEIGHT, (WPARAM)-1, tbH - 6);
                    MoveWindow(hAddr, addrX, 3, addrW, 200, TRUE);
                    ShowWindow(hAddr, SW_SHOW);
                }
            } else {
                HWND hAddr = GetDlgItem(hwnd, IDC_MYADDRESSBAR);
                if (hAddr) ShowWindow(hAddr, SW_HIDE);
            }
            MoveWindow(GetDlgItem(hwnd, ID_TREE), 0, tbH, g_SplitX, cy - statH - tbH, TRUE);
            
            int nameColW = g_IconSizeSmall > 32 ? g_IconSizeSmall + 120 : 150;
            if (state->viewMode == 3) { 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_NAME), listX, tbH, nameColW, hdrH, TRUE); 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_DATE), listX + nameColW, tbH, 120, hdrH, TRUE); 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_SIZE), listX + nameColW + 120, tbH, 70, hdrH, TRUE); 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_TYPE), listX + nameColW + 190, tbH, listW - (nameColW + 190) > 0 ? listW - (nameColW + 190) : 80, hdrH, TRUE); 
                ShowWindow(GetDlgItem(hwnd, ID_HDR_NAME), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_DATE), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_SIZE), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_TYPE), SW_SHOW); 
            } else { ShowWindow(GetDlgItem(hwnd, ID_HDR_NAME), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_DATE), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_SIZE), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_TYPE), SW_HIDE); }
            MoveWindow(GetDlgItem(hwnd, ID_LIST), listX, tbH + hdrH, listW, cy - hdrH - statH - tbH, TRUE); if (g_bShowStatusBar) MoveWindow(GetDlgItem(hwnd, 300), 0, cy - statH, cx, statH, TRUE);
            if (state->viewMode == 0 || state->viewMode == 1) RebuildList(hwnd, state); return 0;
        }
        case WM_LBUTTONDOWN: { int x = LOWORD(lp); int y = HIWORD(lp); int tbH = g_bShowToolbar ? (g_IconSizeSmall + 16) : 0; if (y > tbH && x >= g_SplitX - 4 && x <= g_SplitX + 4) { SetCapture(hwnd); g_bDraggingSplitter = TRUE; } return 0; }
        case WM_MOUSEMOVE: { int x = LOWORD(lp); int y = HIWORD(lp); int tbH = g_bShowToolbar ? (g_IconSizeSmall + 16) : 0; if (y > tbH && x >= g_SplitX - 4 && x <= g_SplitX + 4) SetCursor(LoadCursor(NULL, IDC_SIZEWE)); if (g_bDraggingSplitter) { RECT rc; GetClientRect(hwnd, &rc); if (x > 50 && x < rc.right - 100) { g_SplitX = x; SendMessage(hwnd, WM_SIZE, 0, MAKELONG(rc.right, rc.bottom)); } } return 0; }
        case WM_LBUTTONUP: if (g_bDraggingSplitter) { ReleaseCapture(); g_bDraggingSplitter = FALSE; SaveConfig(); } return 0;
        case WM_MEASUREITEM: { 
            LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lp; 
            if (lpmis->CtlType == ODT_MENU) break; 
            if (lpmis->CtlID == ID_LIST) { 
                if (state && state->viewMode == 0) lpmis->itemHeight = CELL_H; 
                else if (state && state->viewMode == 1) lpmis->itemHeight = g_IconSizeSmall + 6; 
                else lpmis->itemHeight = g_IconSizeSmall + 4; 
                return TRUE; 
            } else if (lpmis->CtlID == ID_TREE) { 
                lpmis->itemHeight = g_IconSizeSmall + 4; 
                return TRUE; 
            } 
            break; 
        }
        case WM_DRAWITEM: { HandleDrawItem(hwnd, wp, lp); return TRUE; }
        case WM_DELETEITEM: { 
            LPDELETEITEMSTRUCT lpdis = (LPDELETEITEMSTRUCT)lp; 
            if (lpdis->itemData && lpdis->itemData != (DWORD)LB_ERR) { 
                if (lpdis->CtlID == ID_TREE) { free((void FAR*)lpdis->itemData); } 
                else if (lpdis->CtlID == ID_LIST) { 
                    int FAR* pType = (int FAR*)lpdis->itemData; 
                    if (*pType == 2) { RowItemData FAR* row = (RowItemData FAR*)lpdis->itemData; free(row); } 
                } 
            } return TRUE; 
        }
        case WM_COMMAND: { 
            if (wp == ID_HDR_NAME) { if (g_SortCol==0) g_SortOrder*=-1; else { g_SortCol=0; g_SortOrder=1; } RebuildList(hwnd, state); return 0; }
            if (wp == ID_HDR_DATE) { if (g_SortCol==3) g_SortOrder*=-1; else { g_SortCol=3; g_SortOrder=1; } RebuildList(hwnd, state); return 0; }
            if (wp == ID_HDR_SIZE) { if (g_SortCol==1) g_SortOrder*=-1; else { g_SortCol=1; g_SortOrder=1; } RebuildList(hwnd, state); return 0; }
            if (wp == ID_HDR_TYPE) { if (g_SortCol==2) g_SortOrder*=-1; else { g_SortCol=2; g_SortOrder=1; } RebuildList(hwnd, state); return 0; }
            
            // Handle Address Bar execution (Enter pressed or Selection confirmed)
            if (LOWORD(wp) == IDC_MYADDRESSBAR) {
                if (HIWORD(wp) == CBN_SELENDOK || HIWORD(wp) == 9999) {
                    HWND hAddr = GetDlgItem(hwnd, IDC_MYADDRESSBAR);
                    char szText[MAX_PATH];
                    
                    if (HIWORD(wp) == CBN_SELENDOK) {
                        int selIndex = SendMessage(hAddr, CB_GETCURSEL, 0, 0);
                        if (selIndex != CB_ERR) {
                            SendMessage(hAddr, CB_GETLBTEXT, selIndex, (LPARAM)szText);
                        } else return 0;
                    } else {
                        GetWindowText(hAddr, szText, MAX_PATH);
                    }
                    
                    if (szText[0] != '\0') {
                        AddRecent(hAddr, szText);
                        
                        // Check if the path is a file or a folder
                        DWORD attr = GetFileAttributes(szText);
                        if (attr != INVALID_FILE_ATTRIBUTES) {
                            if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                                // Path is a Folder: Navigate e1plorer
                                if (state) {
                                    lstrcpy(state->pathOrId, szText);
                                    state->isVirtual = FALSE; 
                                    SetWindowText(hwnd, szText);
                                    ExpandAllParentsFS(szText);
                                    RebuildTree(GetDlgItem(hwnd, ID_TREE), state);
                                    RebuildList(hwnd, state);
                                }
                            } else {
                                // Path is a File: Launch it
                                ShellExecute(hwnd, "open", szText, NULL, NULL, SW_SHOWNORMAL);
                            }
                        } else {
                            // Let Windows try parsing it (URL, virtual path, or unhandled file)
                            HINSTANCE hInst = ShellExecute(hwnd, "open", szText, NULL, NULL, SW_SHOWNORMAL);
                            if ((INT_PTR)hInst <= 32) {
                                MessageBox(hwnd, "Path or file not found.", "Error", MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                    return 0;
                }
            }

            if (state) HandleListCommand(hwnd, wp, lp, state); return 0; 
        }
        case WM_CLOSE: { DestroyWindow(hwnd); return 0; }
        case WM_DESTROY: { 
            HWND hList = GetDlgItem(hwnd, ID_LIST);
            if (hList) SendMessage(hList, LB_RESETCONTENT, 0, 0);
            HWND hTree = GetDlgItem(hwnd, ID_TREE);
            if (hTree) SendMessage(hTree, LB_RESETCONTENT, 0, 0);

            HANDLE oldBlock = GetProp(hwnd, "BulkBlock");
            if (oldBlock) { free((void FAR*)oldBlock); RemoveProp(hwnd, "BulkBlock"); }
            if (state) { free(state); SetWindowLongPtr(hwnd, 0, 0); } 
            if (hwnd == g_hwndMain) PostQuitMessage(0); 
            return 0; 
        }
    } return DefWindowProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK SearchWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WindowState FAR* state = (WindowState FAR*)GetWindowLongPtr(hwnd, 0);
    switch(msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lp; char* targetPath = (char*)cs->lpCreateParams; int tX = 15, tY = 55; HWND hc1, hc2, hList;
            state = (WindowState FAR*)malloc(sizeof(WindowState)); SetWindowLongPtr(hwnd, 0, (LONG_PTR)state);
            lstrcpy(state->pathOrId, (targetPath && targetPath[0]) ? targetPath : "C:\\"); 
            if (lstrcmp(state->pathOrId, "0") == 0) state->isVirtual = TRUE; else state->isVirtual = FALSE;
            state->activeTab = 0; state->viewMode = 3;
            
            HMENU hMenu = CreateMenu(); HMENU hFile = CreatePopupMenu(); AppendMenu(hFile, MF_STRING, 4006, "&Close"); AppendMenu(hMenu, MF_POPUP, (UINT)hFile, "&File");
            HMENU hEdit = CreatePopupMenu(); AppendMenu(hEdit, MF_STRING, 4015, "Select &All\tCtrl+A"); AppendMenu(hEdit, MF_STRING, 4016, "&Invert Selection"); AppendMenu(hMenu, MF_POPUP, (UINT)hEdit, "&Edit");
            HMENU hView = CreatePopupMenu(); AppendMenu(hView, MF_STRING, 4022, "Lar&ge Icons"); AppendMenu(hView, MF_STRING, 4023, "S&mall Icons"); AppendMenu(hView, MF_STRING, 4024, "&List"); AppendMenu(hView, MF_STRING|MF_CHECKED, 4025, "&Details"); AppendMenu(hMenu, MF_POPUP, (UINT)hView, "&View"); SetMenu(hwnd, hMenu); DrawMenuBar(hwnd);
            
            /* Tab 0 Controls - Adjusted Edit widths to give Browse 95px */
            CreateWindow("STATIC", "Named:", WS_CHILD|WS_VISIBLE, tX+5, tY, 60, 20, hwnd, (HMENU)600, g_hInst, NULL); 
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "*.*", WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, tX+65, tY-2, 185, 22, hwnd, (HMENU)601, g_hInst, NULL); 
            CreateWindow("STATIC", "Look in:", WS_CHILD|WS_VISIBLE, tX+5, tY+30, 60, 20, hwnd, (HMENU)602, g_hInst, NULL); 
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", lstrcmp(state->pathOrId, "0") == 0 ? "Desktop" : state->pathOrId, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP, tX+65, tY+28, 185, 22, hwnd, (HMENU)603, g_hInst, NULL); 
            CreateWindow("BUTTON", "Browse...", WS_CHILD|WS_VISIBLE|WS_TABSTOP, tX+255, tY+27, 95, 24, hwnd, (HMENU)604, g_hInst, NULL); 
            CreateWindow("BUTTON", "Include subfolders", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, tX+65, tY+60, 150, 20, hwnd, (HMENU)605, g_hInst, NULL); 
            SendMessage(GetDlgItem(hwnd, 605), BM_SETCHECK, 1, 0);

            /* Tab 1 Controls */
            CreateWindow("BUTTON", "All files", WS_CHILD|BS_AUTORADIOBUTTON, tX+5, tY, 150, 20, hwnd, (HMENU)700, g_hInst, NULL); 
            CreateWindow("BUTTON", "Find all files created or modified:", WS_CHILD|BS_AUTORADIOBUTTON, tX+5, tY+25, 250, 20, hwnd, (HMENU)701, g_hInst, NULL); 
            CreateWindow("BUTTON", "between", WS_CHILD|BS_AUTORADIOBUTTON, tX+25, tY+50, 80, 20, hwnd, (HMENU)702, g_hInst, NULL); 
            CreateWindowEx(0, DATETIMEPICK_CLASS, "", WS_CHILD|WS_TABSTOP, tX+105, tY+48, 100, 22, hwnd, (HMENU)703, g_hInst, NULL); 
            CreateWindow("STATIC", "and", WS_CHILD, tX+210, tY+50, 30, 20, hwnd, (HMENU)704, g_hInst, NULL); 
            CreateWindowEx(0, DATETIMEPICK_CLASS, "", WS_CHILD|WS_TABSTOP, tX+245, tY+48, 100, 22, hwnd, (HMENU)705, g_hInst, NULL); 
            SendMessage(GetDlgItem(hwnd, 700), BM_SETCHECK, 1, 0);

            /* Tab 2 Controls */
            CreateWindow("STATIC", "Of type:", WS_CHILD, tX+5, tY, 100, 20, hwnd, (HMENU)800, g_hInst, NULL); 
            hc1 = CreateWindow("COMBOBOX", "", WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP, tX+105, tY-2, 200, 100, hwnd, (HMENU)801, g_hInst, NULL); 
            SendMessage(hc1, CB_ADDSTRING, 0, (LPARAM)"All Files and Folders"); SendMessage(hc1, CB_SETCURSEL, 0, 0); 
            CreateWindow("STATIC", "Containing text:", WS_CHILD, tX+5, tY+30, 100, 20, hwnd, (HMENU)802, g_hInst, NULL); 
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_BORDER|WS_TABSTOP, tX+105, tY+28, 200, 22, hwnd, (HMENU)803, g_hInst, NULL); 
            CreateWindow("STATIC", "Size is:", WS_CHILD, tX+5, tY+60, 60, 20, hwnd, (HMENU)804, g_hInst, NULL); 
            hc2 = CreateWindow("COMBOBOX", "", WS_CHILD|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP, tX+105, tY+58, 80, 100, hwnd, (HMENU)805, g_hInst, NULL); 
            SendMessage(hc2, CB_ADDSTRING, 0, (LPARAM)"At least"); SendMessage(hc2, CB_ADDSTRING, 0, (LPARAM)"At most"); SendMessage(hc2, CB_SETCURSEL, 0, 0); 
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_BORDER|WS_TABSTOP, tX+195, tY+58, 60, 22, hwnd, (HMENU)806, g_hInst, NULL); 
            CreateWindow("STATIC", "KB", WS_CHILD, tX+260, tY+60, 30, 20, hwnd, (HMENU)807, g_hInst, NULL);

            /* Action Buttons */
            CreateWindow("BUTTON", "Find Now", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 370, 20, 80, 24, hwnd, (HMENU)IDOK, g_hInst, NULL); 
            CreateWindow("BUTTON", "Stop", WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_DISABLED, 370, 50, 80, 24, hwnd, (HMENU)105, g_hInst, NULL); 
            CreateWindow("BUTTON", "New Search", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 370, 80, 80, 24, hwnd, (HMENU)104, g_hInst, NULL);

            /* Apply Enter Key Subclasses */
            SetWindowSubclass(GetDlgItem(hwnd, 601), SearchEditSubclassProc, 1, 0);
            SetWindowSubclass(GetDlgItem(hwnd, 603), SearchEditSubclassProc, 1, 0);
            SetWindowSubclass(GetDlgItem(hwnd, 803), SearchEditSubclassProc, 1, 0);
            SetWindowSubclass(GetDlgItem(hwnd, 806), SearchEditSubclassProc, 1, 0);
            SetWindowSubclass(GetDlgItem(hwnd, IDOK), SearchButtonSubclassProc, 1, 0);
            SetWindowSubclass(GetDlgItem(hwnd, 104), SearchButtonSubclassProc, 1, 0);
            SetWindowSubclass(GetDlgItem(hwnd, 105), SearchButtonSubclassProc, 1, 0);

            /* List Headers */
            CreateWindow("BUTTON", "Name", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_NAME, g_hInst, NULL); 
            CreateWindow("BUTTON", "Modified", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_DATE, g_hInst, NULL); 
            CreateWindow("BUTTON", "In Folder", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_INFOLDER, g_hInst, NULL); 
            CreateWindow("BUTTON", "Size", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_HDR_SIZE, g_hInst, NULL);
            
            hList = CreateWindowEx(0, "LISTBOX", "", WS_CHILD|WS_VISIBLE|LBS_OWNERDRAWFIXED|LBS_NOTIFY|WS_VSCROLL|WS_HSCROLL|WS_BORDER|LBS_EXTENDEDSEL, 10, 150, 445, 200, hwnd, (HMENU)ID_LIST, g_hInst, NULL); 
            g_lpfnOldListProc = (FARPROC)SetWindowLongPtr(hList, GWLP_WNDPROC, (LONG_PTR)ListProc);
            
            ShowTabControls(hwnd, 0); ChangeViewMode(hwnd, state->viewMode, state); return 0;
        }
        case WM_LBUTTONDOWN: { int x = LOWORD(lp); int y = HIWORD(lp); int currX = 10; int tabW[] = {120, 100, 80}; int i; for (i=0; i<3; i++) { if (x >= currX && x <= currX + tabW[i] && y >= 15 && y <= 35) { if (state->activeTab != i) { state->activeTab = i; ShowTabControls(hwnd, i); InvalidateRect(hwnd, NULL, TRUE); } break; } currX += tabW[i] + 2; } return 0; }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); HFONT hOldFont = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT)); int i; char* tabs[] = {"Name && Location", "Date Modified", "Advanced"}; int tabW[] = {120, 100, 80}; int currX = 10; SetBkMode(hdc, TRANSPARENT); HPEN hHi = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT)); HPEN hSh = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)); HPEN hOldP = SelectObject(hdc, hHi);
            MoveToEx(hdc, 10, 140, NULL); LineTo(hdc, 10, 35); LineTo(hdc, currX, 35); SelectObject(hdc, hSh); LineTo(hdc, 355, 35); LineTo(hdc, 355, 140); LineTo(hdc, 10, 140); MoveToEx(hdc, 355, 35, NULL);
            for (i=0; i<3; i++) {
                int startX = currX; int endX = startX + tabW[i]; int topY = (state->activeTab == i) ? 15 : 18; RECT tRect; tRect.left = startX; tRect.right = endX; tRect.top = topY + 3; tRect.bottom = 35; DrawText(hdc, tabs[i], -1, &tRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(hdc, hHi); MoveToEx(hdc, startX, 35, NULL); LineTo(hdc, startX, topY+2); LineTo(hdc, startX+2, topY); LineTo(hdc, endX-2, topY); SelectObject(hdc, hSh); LineTo(hdc, endX, topY+2); LineTo(hdc, endX, 35);
                if (state->activeTab != i) { SelectObject(hdc, hHi); MoveToEx(hdc, startX, 35, NULL); LineTo(hdc, endX, 35); } else { SelectObject(hdc, GetStockObject(WHITE_PEN)); MoveToEx(hdc, startX+1, 35, NULL); LineTo(hdc, endX, 35); } currX += tabW[i] + 2;
            }
            SelectObject(hdc, hHi); MoveToEx(hdc, currX, 35, NULL); LineTo(hdc, 355, 35); SelectObject(hdc, hOldP); SelectObject(hdc, hOldFont); DeleteObject(hHi); DeleteObject(hSh); EndPaint(hwnd, &ps); return 0;
        }
        case WM_SIZE: {
            if (!state) return DefWindowProc(hwnd, msg, wp, lp);
            int listW = LOWORD(lp) - 20; int hdrH = (state->viewMode == 3) ? 22 : 0;
            int nameColW = g_IconSizeSmall > 32 ? g_IconSizeSmall + 120 : 150;
            if (state->viewMode == 3) { 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_NAME), 10, 150, nameColW, hdrH, TRUE); 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_DATE), 10 + nameColW, 150, 120, hdrH, TRUE); 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_INFOLDER), 10 + nameColW + 120, 150, 100, hdrH, TRUE); 
                MoveWindow(GetDlgItem(hwnd, ID_HDR_SIZE), 10 + nameColW + 220, 150, listW - (nameColW + 220) > 0 ? listW - (nameColW + 220) : 80, hdrH, TRUE); 
                ShowWindow(GetDlgItem(hwnd, ID_HDR_NAME), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_DATE), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_INFOLDER), SW_SHOW); ShowWindow(GetDlgItem(hwnd, ID_HDR_SIZE), SW_SHOW); 
            } else { ShowWindow(GetDlgItem(hwnd, ID_HDR_NAME), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_DATE), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_INFOLDER), SW_HIDE); ShowWindow(GetDlgItem(hwnd, ID_HDR_SIZE), SW_HIDE); }
            MoveWindow(GetDlgItem(hwnd, ID_LIST), 10, 150 + hdrH, listW, HIWORD(lp) - 160 - hdrH, TRUE); return 0;
        }
        case WM_MEASUREITEM: { 
            LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lp; 
            if (lpmis->CtlID == ID_LIST) { 
                if (state && state->viewMode == 0) lpmis->itemHeight = CELL_H; 
                else if (state && state->viewMode == 1) lpmis->itemHeight = g_IconSizeSmall + 6; 
                else lpmis->itemHeight = g_IconSizeSmall + 4; 
                return TRUE; 
            } 
            break; 
        }
        case WM_DRAWITEM: { HandleDrawItem(hwnd, wp, lp); return TRUE; }
        case WM_DELETEITEM: { 
            LPDELETEITEMSTRUCT lpdis = (LPDELETEITEMSTRUCT)lp; 
            if (lpdis->itemData && lpdis->itemData != (DWORD)LB_ERR && lpdis->CtlID == ID_LIST) { 
                int FAR* pType = (int FAR*)lpdis->itemData; 
                if (*pType == 2) { 
                    RowItemData FAR* row = (RowItemData FAR*)lpdis->itemData; 
                    free(row); 
                } 
            } 
            return TRUE; 
        }
        case WM_COMMAND: {
            if (wp == ID_HDR_NAME || wp == ID_HDR_DATE || wp == ID_HDR_INFOLDER || wp == ID_HDR_SIZE) {
                if (wp == ID_HDR_NAME) { if (g_SortCol==0) g_SortOrder*=-1; else { g_SortCol=0; g_SortOrder=1; } }
                if (wp == ID_HDR_DATE) { if (g_SortCol==3) g_SortOrder*=-1; else { g_SortCol=3; g_SortOrder=1; } }
                if (wp == ID_HDR_INFOLDER) { if (g_SortCol==4) g_SortOrder*=-1; else { g_SortCol=4; g_SortOrder=1; } }
                if (wp == ID_HDR_SIZE) { if (g_SortCol==1) g_SortOrder*=-1; else { g_SortCol=1; g_SortOrder=1; } }
                
                HWND hList = GetDlgItem(hwnd, ID_LIST); int lbCount = SendMessage(hList, LB_GETCOUNT, 0, 0);
                if (lbCount > 0) {
                    ListItemData FAR* FAR* savedArr = (ListItemData FAR* FAR*)malloc(SEARCH_MAX_ITEMS * sizeof(ListItemData FAR*));
                    if (savedArr) {
                        int savedCount = 0;
                        for (int i = 0; i < lbCount; i++) {
                            int FAR* pType = (int FAR*)SendMessage(hList, LB_GETITEMDATA, i, 0);
                            if (pType && *pType == 1) savedArr[savedCount++] = (ListItemData FAR*)pType;
                            else if (pType && *pType == 2) { RowItemData FAR* row = (RowItemData FAR*)pType; for (int c = 0; c < row->count; c++) savedArr[savedCount++] = row->items[c]; }
                        }
                        SendMessage(hList, WM_SETREDRAW, FALSE, 0); SendMessage(hList, LB_RESETCONTENT, 0, 0);
                        SortListItems(savedArr, savedCount); LayoutListItems(hList, savedArr, savedCount, state->viewMode);
                        SendMessage(hList, WM_SETREDRAW, TRUE, 0); free(savedArr);
                    }
                } return 0;
            }
            if (LOWORD(wp) == IDOK) { // Mapped the Find Now command natively to IDOK
                HWND hList = GetDlgItem(hwnd, ID_LIST); char name[MAX_PATH], path[MAX_PATH], sizeStr[32], containing[MAX_PATH]; int count = 0; 
                HANDLE oldBlock; ListItemData FAR* FAR* arr; ListItemData FAR* block; unsigned long filterBytes; int filterType;
                
                GetWindowText(GetDlgItem(hwnd, 601), name, MAX_PATH); GetWindowText(GetDlgItem(hwnd, 603), path, MAX_PATH); GetWindowText(GetDlgItem(hwnd, 803), containing, MAX_PATH); if (name[0] == '\0') lstrcpy(name, "*.*");
                if (lstrcmpi(path, "Desktop") == 0) lstrcpy(path, "0");
                GetWindowText(GetDlgItem(hwnd, 806), sizeStr, 32); 
                filterBytes = (unsigned long)atol(sizeStr) * 1024; filterType = sizeStr[0] != '\0' ? SendMessage(GetDlgItem(hwnd, 805), CB_GETCURSEL, 0, 0) : -1;
                
                SendMessage(hList, WM_SETREDRAW, FALSE, 0); SendMessage(hList, LB_RESETCONTENT, 0, 0); 
                oldBlock = GetProp(hwnd, "BulkBlock"); if (oldBlock) { free((void FAR*)oldBlock); RemoveProp(hwnd, "BulkBlock"); }
                
                arr = (ListItemData FAR* FAR*)malloc(SEARCH_MAX_ITEMS * sizeof(ListItemData FAR*)); 
                block = (ListItemData FAR*)malloc(SEARCH_MAX_ITEMS * sizeof(ListItemData));
                if (!arr || !block) { if (arr) free(arr); if (block) free(block); return 0; }
                
                g_bStopSearch = FALSE; EnableWindow(GetDlgItem(hwnd, 105), TRUE); EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
                DoRecursiveSearch(arr, block, path, name, &count, filterType, filterBytes, containing);
                g_bStopSearch = FALSE; EnableWindow(GetDlgItem(hwnd, 105), FALSE); EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
                SortListItems(arr, count); LayoutListItems(hList, arr, count, state->viewMode); 
                
                SetProp(hwnd, "BulkBlock", (HANDLE)block); free(arr);
            } else if (wp == 105) { g_bStopSearch = TRUE; EnableWindow(GetDlgItem(hwnd, 105), FALSE); EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
            } else if (wp == 104) { SetWindowText(GetDlgItem(hwnd, 601), "*.*"); SendMessage(GetDlgItem(hwnd, ID_LIST), LB_RESETCONTENT, 0, 0); 
            } else if (wp >= 4001 && wp <= 4060) { HandleListCommand(hwnd, wp, lp, state); }
            return 0;
        }
        case WM_CLOSE: {
            if (!IsWindowEnabled(GetDlgItem(hwnd, IDOK))) {
                g_bStopSearch = TRUE;
                PostMessage(hwnd, WM_CLOSE, 0, 0); 
                return 0;
            }
            DestroyWindow(hwnd); 
            return 0;
        }
        case WM_DESTROY: { 
            HWND hList = GetDlgItem(hwnd, ID_LIST);
            if (hList) SendMessage(hList, LB_RESETCONTENT, 0, 0);

            HANDLE oldBlock = GetProp(hwnd, "BulkBlock");
            if (oldBlock) { free((void FAR*)oldBlock); RemoveProp(hwnd, "BulkBlock"); }
            if (state) { free(state); SetWindowLongPtr(hwnd, 0, 0); } 
            if (hwnd == g_hwndMain) PostQuitMessage(0); 
            return 0; 
        }
    } return DefWindowProc(hwnd, msg, wp, lp);
}
// --- Main Window Procedure ---

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;

            // 1. Create the Toolbar with CCS_NORESIZE
            hToolBar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, 
                WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | CCS_NORESIZE, 
                0, 0, 0, 0, hWnd, (HMENU)IDC_MYTOOLBAR, hInst, NULL);
            
            // (Add standard toolbar buttons here via TB_ADDBUTTONS / TB_BUTTONSTRUCT)

            // 2. Create the Address Bar (ComboBox)
            hAddressBar = CreateWindowEx(0, WC_COMBOBOX, NULL, 
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL, 
                0, 0, 0, 0, hWnd, (HMENU)IDC_MYADDRESSBAR, hInst, NULL);
                
            SendMessage(hAddressBar, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

            // 3. Load MRU history
            LoadRecents(hAddressBar);

            // 4. Subclass internal Edit control to handle 'Enter' key
            COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
            if (GetComboBoxInfo(hAddressBar, &cbi)) 
            {
                SetWindowSubclass(cbi.hwndItem, AddressBarSubclassProc, 1, 0);
            }
            break;
        }

        case WM_SIZE:
        {
            int clientWidth = LOWORD(lParam);
            int clientHeight = HIWORD(lParam);

            // Only layout if the toolbar is visible
            if (hToolBar && (GetWindowLong(hToolBar, GWL_STYLE) & WS_VISIBLE))
            {
                // Get toolbar width based on its buttons
                SIZE tbSize = {0};
                SendMessage(hToolBar, TB_GETMAXSIZE, 0, (LPARAM)&tbSize);

                // Lock Toolbar to the left
                SetWindowPos(hToolBar, NULL, 0, 0, tbSize.cx, tbSize.cy, SWP_NOZORDER);

                // Stretch Address Bar across the remaining width
                int addressX = tbSize.cx + 2; 
                int addressWidth = clientWidth - addressX - 2;
                if (addressWidth < 0) addressWidth = 0;
                
                // Height (200) determines the dropdown menu length, not the box height
                SetWindowPos(hAddressBar, NULL, addressX, 2, addressWidth, 200, SWP_NOZORDER);
            }
            break;
        }

        case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            switch (wmId)
            {
                // Toggle Toolbar and Address Bar visibility
                case ID_VIEW_TOOLBAR: 
                {
                    BOOL bIsVisible = (GetWindowLong(hToolBar, GWL_STYLE) & WS_VISIBLE);
                    int showCmd = bIsVisible ? SW_HIDE : SW_SHOW;
                    
                    ShowWindow(hToolBar, showCmd);
                    ShowWindow(hAddressBar, showCmd);

                    // Force WM_SIZE to immediately reorganize layout
                    RECT rc;
                    GetClientRect(hWnd, &rc);
                    SendMessage(hWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
                    break;
                }

                // Handle Address Bar mouse interactions
                case IDC_MYADDRESSBAR:
                {
                    // User clicked an item in the dropdown
                    if (wmEvent == CBN_SELENDOK) 
                    {
                        int selIndex = SendMessage(hAddressBar, CB_GETCURSEL, 0, 0);
                        if (selIndex != CB_ERR) 
                        {
                            TCHAR szText[MAX_PATH];
                            SendMessage(hAddressBar, CB_GETLBTEXT, selIndex, (LPARAM)szText);
                            
                            // Move to top of MRU and save
                            AddRecent(hAddressBar, szText);
                            
                            // TODO: Call your folder navigation logic here!
                            // NavigateToPath(szText);
                        }
                    }
                    break;
                }
            }
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc; MSG msg; char startPath[MAX_PATH] = ""; char searchPath[MAX_PATH] = ""; char* p = lpCmdLine; g_hInst = hInst; int i; HWND hExistingDesktop;
    INITCOMMONCONTROLSEX icex;
    
    memset(&msg, 0, sizeof(MSG));
    
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_DATE_CLASSES;
    InitCommonControlsEx(&icex);
    
    while (*p) { 
        while (*p == ' ') p++; if (!*p) break; 
        if (strncmp(p, "-w", 2) == 0 || strncmp(p, "-W", 2) == 0) { g_bIsWindowed = TRUE; p += 2; } 
        else if (strncmp(p, "-search:", 8) == 0) {
            p += 8; i = 0; if (*p == '"') { p++; while (*p && *p != '"' && i < MAX_PATH - 1) searchPath[i++] = *p++; if (*p == '"') p++; } else { while (*p && *p != ' ' && i < MAX_PATH - 1) searchPath[i++] = *p++; } searchPath[i] = '\0';
        } else { 
            i = 0; if (*p == '"') { p++; while (*p && *p != '"' && i < MAX_PATH - 1) startPath[i++] = *p++; if (*p == '"') p++; } else { while (*p && *p != ' ' && i < MAX_PATH - 1) startPath[i++] = *p++; } startPath[i] = '\0'; 
        } 
    }

    g_hbrDesktop = g_bIsWindowed ? CreateSolidBrush(RGB(0, 128, 128)) : CreateSolidBrush(GetSysColor(COLOR_BACKGROUND));
    g_hbrWindow = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    g_hbrHighlight = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));

    memset(&wc, 0, sizeof(WNDCLASS)); wc.cbWndExtra = sizeof(WindowState FAR*); wc.style = CS_DBLCLKS; wc.lpfnWndProc = DesktopProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = g_hbrDesktop; wc.lpszClassName = "Win95DesktopClass"; RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.cbWndExtra = sizeof(WindowState FAR*); wc.style = CS_DBLCLKS; wc.lpfnWndProc = FolderWndProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "Win95FolderClass"; RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.cbWndExtra = sizeof(WindowState FAR*); wc.style = CS_DBLCLKS; wc.lpfnWndProc = SearchWndProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = "Win95SearchClass"; RegisterClass(&wc);
    
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = PromptDlgProc; wc.hInstance = hInst; wc.lpszClassName = "PromptDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = ShortcutDlgProc; wc.hInstance = hInst; wc.lpszClassName = "ShortcutDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = CopyProgressDlgProc; wc.hInstance = hInst; wc.lpszClassName = "CopyProgressDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = DeleteProgressDlgProc; wc.hInstance = hInst; wc.lpszClassName = "DeleteProgressDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = ReplaceDlgProc; wc.hInstance = hInst; wc.lpszClassName = "ReplaceDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = FilePropDlgProc; wc.hInstance = hInst; wc.lpszClassName = "FilePropDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);
    memset(&wc, 0, sizeof(WNDCLASS)); wc.lpfnWndProc = OptionsDlgProc; wc.hInstance = hInst; wc.lpszClassName = "OptionsDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wc);

    LoadConfig();

    hExistingDesktop = FindWindow("Win95DesktopClass", "Desktop");

    if (startPath[0] == '\0' && searchPath[0] == '\0') {
        if (hExistingDesktop) { lstrcpy(startPath, "C:\\"); }
    }

    if (!hExistingDesktop) {
        if (g_bIsWindowed) g_hwndMain = CreateWindowEx(0, "Win95DesktopClass", "Desktop", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, hInst, NULL);
        else { g_hwndMain = CreateWindowEx(WS_EX_TOOLWINDOW, "Win95DesktopClass", "Desktop", WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, hInst, NULL); SetWindowPos(g_hwndMain, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
    } else {
        g_hwndMain = hExistingDesktop;
    }
    
    if (startPath[0] != '\0') { 
        HWND hFolder = CreateWindowEx(0, "Win95FolderClass", startPath, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, g_WinX, g_WinY, g_WinW, g_WinH, NULL, NULL, hInst, (LPVOID)startPath); 
        if (hFolder && hExistingDesktop && g_hwndMain == hExistingDesktop) g_hwndMain = hFolder;
    }
    
    if (searchPath[0] != '\0') { 
        HWND hSrch = CreateWindowEx(0, "Win95SearchClass", "Find: All Files", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 480, 420, NULL, NULL, hInst, (LPVOID)searchPath); 
        if (hSrch && hExistingDesktop && g_hwndMain == hExistingDesktop) g_hwndMain = hSrch;
    }

    if (!g_hwndMain || (hExistingDesktop && startPath[0] == '\0' && searchPath[0] == '\0')) { goto Cleanup; }
    
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    
Cleanup:
    if (g_hbrDesktop) DeleteObject(g_hbrDesktop); 
    if (g_hbrWindow) DeleteObject(g_hbrWindow); 
    if (g_hbrHighlight) DeleteObject(g_hbrHighlight);
    
    for (i = 0; i < g_ExpandedCount; i++) { if (g_ExpandedNodes[i]) free(g_ExpandedNodes[i]); }
    for (i = 0; i < g_IniShortcutCount; i++) { if (g_IniShortcuts[i]) free(g_IniShortcuts[i]); }
    
    return (int)msg.wParam;
}
