/* ============================================================================
 * C Procedure Function Replacer - Replaces Functions with updated ones
 *
 * Compile instructions (GCC / MinGW):
 * gcc -Os -s -mwindows -o ProcManager.exe ProcManager.c -lcomctl32 -lcomdlg32 -lshell32
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>

#define IDB_BROWSE       101
#define IDB_PASTEREPLACE 102
#define IDB_PASTE        103
#define IDB_UPDATE       104
#define IDB_SAVE         105
#define IDC_MRUCOMBO     106
#define IDB_EDIT         107
#define IDB_COMPILE      108
#define IDB_COPYLEFT     109
#define IDB_DELETELEFT   110
#define IDL_LEFT         201
#define IDL_RIGHT        202
#define IDS_STATUS       301

typedef struct Chunk {
    int isProc;
    char name[256];
    char* text;
    struct Chunk* next;
} Chunk;

typedef struct {
    Chunk* head;
    Chunk* tail;
} ProcGroup;

void SetStatus(const char* msg);
void PopulateList(HWND hList, Chunk* head);
void EnsureCRLF(Chunk* c);
void MoveLeftChunk(int fromIdx, int toIdx);

LRESULT CALLBACK LeftListWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

HWND hMainWindow;
HWND hComboMru;
HWND hBtnBrowse, hBtnPasteRep, hBtnPaste, hBtnUpdate, hBtnSave, hBtnEdit, hBtnCompile, hBtnCopyLeft, hBtnDeleteLeft;
HWND hListLeft, hListRight;
HWND hStatus;

WNDPROC OldListProc;

Chunk* g_LeftChunks = NULL;
Chunk* g_RightChunks = NULL;
char szCurrentFile[MAX_PATH] = {0};
char szIniFile[MAX_PATH] = {0};

char mruList[20][MAX_PATH];
int mruCount = 0;

void EnsureCRLF(Chunk* c) {
    if (!c || !c->text) return;
    int len = strlen(c->text);
    if (len == 0) return;
    
    if (c->text[len - 1] != '\n') {
        char* newText = (char*)malloc(len + 3); 
        strcpy(newText, c->text);
        strcat(newText, "\r\n");
        free(c->text);
        c->text = newText;
    }
}

LRESULT CALLBACK LeftListWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static POINT ptDown;
    static BOOL bDragging = FALSE;
    static int dragIndex = -1;
    static BOOL bOurCapture = FALSE;

    switch (msg) {
        case WM_LBUTTONDOWN: {
            int idx = SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            if (!HIWORD(idx)) {
                dragIndex = LOWORD(idx);
                ptDown.x = (short)LOWORD(lParam);
                ptDown.y = (short)HIWORD(lParam);
                bDragging = FALSE;
                
                if ((wParam & MK_SHIFT) || (wParam & MK_CONTROL)) {
                    bOurCapture = FALSE;
                } else {
                    SendMessage(hwnd, LB_SETSEL, FALSE, -1);
                    SendMessage(hwnd, LB_SETSEL, TRUE, dragIndex);
                    
                    bOurCapture = TRUE;
                    SetCapture(hwnd);
                    return 0; 
                }
            } else {
                dragIndex = -1;
            }
            break; 
        }
        case WM_MOUSEMOVE: {
            if (bOurCapture && dragIndex != -1 && (wParam & MK_LBUTTON)) {
                int x = (short)LOWORD(lParam);
                int y = (short)HIWORD(lParam);
                if (!bDragging) {
                    if (abs(x - ptDown.x) > GetSystemMetrics(SM_CXDRAG) ||
                        abs(y - ptDown.y) > GetSystemMetrics(SM_CYDRAG)) {
                        bDragging = TRUE;
                    }
                }
                if (bDragging) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return 0; 
                }
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (bOurCapture) {
                ReleaseCapture();
                bOurCapture = FALSE;
                
                if (bDragging && dragIndex != -1) {
                    bDragging = FALSE;
                    int x = (short)LOWORD(lParam);
                    int y = (short)HIWORD(lParam);
                    
                    int idx = SendMessage(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
                    int dropIndex = LOWORD(idx);
                    
                    if (HIWORD(idx)) {
                        dropIndex = SendMessage(hwnd, LB_GETCOUNT, 0, 0) - 1;
                    }
                    
                    if (dropIndex >= 0 && dropIndex != dragIndex) {
                        MoveLeftChunk(dragIndex, dropIndex);
                        PopulateList(hwnd, g_LeftChunks);
                        
                        SendMessage(hwnd, LB_SETSEL, FALSE, -1);
                        SendMessage(hwnd, LB_SETSEL, TRUE, dropIndex);
                        
                        SetStatus("Function order changed (will apply on save).");
                    }
                } else if (dragIndex != -1) {
                    SendMessage(hwnd, LB_SETSEL, FALSE, -1);
                    SendMessage(hwnd, LB_SETSEL, TRUE, dragIndex);
                }
                dragIndex = -1;
                return 0; 
            }
            dragIndex = -1;
            bDragging = FALSE;
            break;
        }
    }
    return CallWindowProc(OldListProc, hwnd, msg, wParam, lParam);
}

void MoveLeftChunk(int fromIdx, int toIdx) {
    if (fromIdx == toIdx || fromIdx < 0 || toIdx < 0) return;

    int pCount = 0;
    Chunk* curr = g_LeftChunks;
    while (curr) {
        if (curr->isProc) pCount++;
        curr = curr->next;
    }

    if (pCount == 0 || fromIdx >= pCount) return;
    if (toIdx >= pCount) toIdx = pCount - 1;

    ProcGroup* groups = (ProcGroup*)malloc(sizeof(ProcGroup) * pCount);
    
    Chunk* headerHead = NULL;
    Chunk* headerTail = NULL;
    curr = g_LeftChunks;

    if (curr && !curr->isProc) {
        headerHead = curr;
        while (curr && !curr->isProc) {
            headerTail = curr;
            curr = curr->next;
        }
    }

    int idx = 0;
    while (curr && idx < pCount) {
        groups[idx].head = curr;
        Chunk* tail = curr;
        curr = curr->next;
        
        while (curr && !curr->isProc) {
            tail = curr;
            curr = curr->next;
        }
        groups[idx].tail = tail;
        idx++;
    }

    ProcGroup movingGroup = groups[fromIdx];
    if (fromIdx < toIdx) {
        for (int i = fromIdx; i < toIdx; i++) {
            groups[i] = groups[i + 1];
        }
    } else {
        for (int i = fromIdx; i > toIdx; i--) {
            groups[i] = groups[i - 1];
        }
    }
    groups[toIdx] = movingGroup;

    g_LeftChunks = headerHead ? headerHead : groups[0].head;
    
    if (headerTail) {
        EnsureCRLF(headerTail);
        headerTail->next = groups[0].head;
    }

    for (int i = 0; i < pCount; i++) {
        EnsureCRLF(groups[i].tail); 
        if (i < pCount - 1) {
            groups[i].tail->next = groups[i + 1].head;
        } else {
            groups[i].tail->next = NULL;
        }
    }

    free(groups);
}

char* my_strdup(const char* s) {
    char* d = (char*)malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

char* safe_strdup_newline(const char* s) {
    int len = strlen(s);
    int needsNewline = (len > 0 && s[len-1] != '\n');
    char* d = (char*)malloc(len + (needsNewline ? 3 : 1));
    if (d) {
        strcpy(d, s);
        if (needsNewline) strcat(d, "\r\n");
    }
    return d;
}

void SetStatus(const char* msg) {
    SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)msg);
}

void FreeChunks(Chunk** head) {
    Chunk* curr = *head;
    while (curr) {
        Chunk* next = curr->next;
        if (curr->text) free(curr->text);
        free(curr);
        curr = next;
    }
    *head = NULL;
}

void AddChunk(Chunk** head, Chunk** tail, int isProc, const char* name, const char* text) {
    if (!text || strlen(text) == 0) return;
    Chunk* c = (Chunk*)malloc(sizeof(Chunk));
    c->isProc = isProc;
    strncpy(c->name, name, 255);
    c->name[255] = '\0';
    c->text = my_strdup(text);
    c->next = NULL;
    if (*tail) {
        (*tail)->next = c;
        *tail = c;
    } else {
        *head = *tail = c;
    }
}

void ExtractMasmName(const char* line, char* procName) {
    const char* p = strstr(line, " PROC");
    if (!p) return;
    p--;
    while (p >= line && (*p == ' ' || *p == '\t')) p--;
    const char* end = p;
    while (p >= line && (*p != ' ' && *p != '\t')) p--;
    p++;
    int len = end - p + 1;
    if (len > 0 && len < 255) {
        strncpy(procName, p, len);
        procName[len] = '\0';
    }
}

void CleanLineForParsing(const char* line, char* cleanLine, int* globalInBlock) {
    int i = 0, j = 0;
    while (line[i]) {
        if (*globalInBlock) {
            if (line[i] == '*' && line[i+1] == '/') {
                *globalInBlock = 0;
                i += 2;
            } else {
                i++;
            }
        } else {
            if (line[i] == '/' && line[i+1] == '*') {
                *globalInBlock = 1;
                i += 2;
            } else if (line[i] == '/' && line[i+1] == '/') {
                break; 
            } else if (line[i] == '"' || line[i] == '\'') {
                char q = line[i];
                i++; 
                while (line[i] && line[i] != q) {
                    if (line[i] == '\\' && line[i+1]) i += 2;
                    else i++;
                }
                if (line[i] == q) i++;
            } else {
                cleanLine[j++] = line[i++];
            }
        }
    }
    cleanLine[j] = '\0';
}

int IsLikeCFunction(const char* cleanLine, char* procName) {
    const char* check = cleanLine;
    while (*check == ' ' || *check == '\t') check++;
    if (*check == '#' || *check == '\0') return 0;
    
    if (strstr(cleanLine, "if ") || strstr(cleanLine, "if(") || strstr(cleanLine, "for ") || strstr(cleanLine, "for(") ||
        strstr(cleanLine, "while ") || strstr(cleanLine, "while(") || strstr(cleanLine, "switch ")) {
        
        if (strstr(cleanLine, "=>") && strchr(cleanLine, '=')) {
            const char* pEq = strchr(cleanLine, '=');
            const char* p = pEq - 1;
            while (p >= cleanLine && (*p == ' ' || *p == '\t')) p--;
            const char* endW = p;
            while (p >= cleanLine && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) p--;
            p++;
            int len = endW - p + 1;
            if (len > 0 && len < 255) {
                strncpy(procName, p, len);
                procName[len] = '\0';
                return 1;
            }
        }
        if (strstr(cleanLine, "=>") == NULL) return 0;
    }

    if (strstr(cleanLine, "function ")) {
        const char* pf = strstr(cleanLine, "function ") + 9;
        while (*pf == ' ' || *pf == '\t') pf++;
        const char* endW = pf;
        while ((*endW >= 'a' && *endW <= 'z') || (*endW >= 'A' && *endW <= 'Z') || (*endW >= '0' && *endW <= '9') || *endW == '_') endW++;
        int len = endW - pf;
        if (len > 0 && len < 255) {
            strncpy(procName, pf, len);
            procName[len] = '\0';
            return 1;
        }
    }

    int len_line = strlen(cleanLine);
    if (len_line == 0) return 0;
    
    const char* end = cleanLine + len_line - 1;
    while (end >= cleanLine && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) end--;
    if (end >= cleanLine && *end == ';') return 0;

    char* pOpen = strchr(cleanLine, '(');
    char* pClose = strchr(cleanLine, ')');
    if (pOpen && pClose && pOpen < pClose) {
        const char* p = pOpen - 1;
        while (p >= cleanLine && (*p == ' ' || *p == '\t')) p--;
        const char* endWord = p;
        while (p >= cleanLine && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) p--;
        p++;
        int len = endWord - p + 1;
        if (len > 0 && len < 255) {
            strncpy(procName, p, len);
            procName[len] = '\0';
            if (strcmp(procName, "return") == 0 || strcmp(procName, "sizeof") == 0 || strcmp(procName, "if") == 0) return 0;
            return 1;
        }
    }
    return 0;
}

void UpdateBraces(const char* cleanLine, int* count, int* seen, int* seenSemicolon) {
    int i = 0;
    while (cleanLine[i]) {
        if (cleanLine[i] == '{') {
            (*count)++;
            *seen = 1;
        }
        else if (cleanLine[i] == '}') {
            (*count)--;
        }
        else if (cleanLine[i] == ';') {
            if (seenSemicolon) *seenSemicolon = 1;
        }
        i++;
    }
}

Chunk* ParseTextToChunks(const char* text) {
    Chunk* head = NULL;
    Chunk* tail = NULL;
    int maxBuf = strlen(text) + 2;
    char* buf = (char*)malloc(maxBuf);
    buf[0] = '\0';
    int bufLen = 0;

    int state = 0; 
    int globalInBlock = 0;
    char procName[256] = {0};
    const char* p = text;
    
    char line[4096];
    char cleanLine[4096];
    int lineLen = 0;
    int braceCount = 0;
    int seenBrace = 0;

    while (*p) {
        lineLen = 0;
        while (*p && *p != '\n' && lineLen < 4094) {
            line[lineLen++] = *p++;
        }
        if (*p == '\n') line[lineLen++] = *p++;
        line[lineLen] = '\0';

        CleanLineForParsing(line, cleanLine, &globalInBlock);

        if (state == 0) {
            if (strstr(cleanLine, " PROC ") || (lineLen > 5 && strstr(line, " PROC\r") == line + lineLen - 6) || (lineLen > 4 && strstr(line, " PROC\n") == line + lineLen - 6)) {
                if (bufLen > 0) {
                    AddChunk(&head, &tail, 0, "", buf);
                    buf[0] = '\0'; bufLen = 0;
                }
                state = 1;
                ExtractMasmName(line, procName);
                
                strcpy(buf + bufLen, line); 
                bufLen += lineLen;
            } else {
                if (IsLikeCFunction(cleanLine, procName)) {
                    if (bufLen > 0) {
                        AddChunk(&head, &tail, 0, "", buf);
                        buf[0] = '\0'; bufLen = 0;
                    }
                    state = 2;
                    braceCount = 0;
                    seenBrace = 0;
                    
                    strcpy(buf + bufLen, line); 
                    bufLen += lineLen;
                    
                    int seenSemi = 0;
                    UpdateBraces(cleanLine, &braceCount, &seenBrace, &seenSemi);
                    
                    if (seenBrace == 0 && seenSemi) {
                        state = 0;
                    } else if (seenBrace && braceCount <= 0) {
                        AddChunk(&head, &tail, 1, procName, buf);
                        buf[0] = '\0'; bufLen = 0;
                        state = 0;
                    }
                } else {
                    strcpy(buf + bufLen, line); 
                    bufLen += lineLen;
                }
            }
        }
        else if (state == 1) { 
            strcpy(buf + bufLen, line); 
            bufLen += lineLen;
            
            if (strstr(line, " ENDP")) {
                AddChunk(&head, &tail, 1, procName, buf);
                buf[0] = '\0'; bufLen = 0;
                state = 0;
            }
        }
        else if (state == 2) { 
            strcpy(buf + bufLen, line); 
            bufLen += lineLen;
            
            int seenSemi = 0;
            UpdateBraces(cleanLine, &braceCount, &seenBrace, &seenSemi);
            
            if (seenBrace == 0 && seenSemi) {
                state = 0;
            } else if (seenBrace && braceCount <= 0) {
                AddChunk(&head, &tail, 1, procName, buf);
                buf[0] = '\0'; bufLen = 0;
                state = 0;
            }
        }
    }
    if (bufLen > 0) {
        AddChunk(&head, &tail, (state > 0), procName, buf);
    }
    free(buf);
    return head;
}

void PopulateList(HWND hList, Chunk* head) {
    SendMessage(hList, WM_SETREDRAW, FALSE, 0);
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    
    Chunk* c = head;
    while (c) {
        if (c->isProc) {
            SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)c->name);
        }
        c = c->next;
    }
    
    SendMessage(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, NULL, TRUE);
}

char* ReadClipboardText() {
    if (!OpenClipboard(hMainWindow)) return NULL;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) { CloseClipboard(); return NULL; }
    char* pszText = (char*)GlobalLock(hData);
    if (!pszText) { CloseClipboard(); return NULL; }
    char* copy = my_strdup(pszText);
    GlobalUnlock(hData);
    CloseClipboard();
    return copy;
}

char* ReadFileText(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

void SaveMRU() {
    for (int i = 0; i < 20; i++) {
        char key[32];
        sprintf(key, "File%d", i + 1);
        if (i < mruCount) {
            WritePrivateProfileString("MRU", key, mruList[i], szIniFile);
        } else {
            WritePrivateProfileString("MRU", key, NULL, szIniFile);
        }
    }
}

void LoadMRU() {
    mruCount = 0;
    for (int i = 0; i < 20; i++) {
        char key[32];
        sprintf(key, "File%d", i + 1);
        char val[MAX_PATH];
        GetPrivateProfileString("MRU", key, "", val, MAX_PATH, szIniFile);
        if (val[0] != '\0') {
            strcpy(mruList[mruCount++], val);
        }
    }
}

void PopulateMRUCombo() {
    SendMessage(hComboMru, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < mruCount; i++) {
        SendMessage(hComboMru, CB_ADDSTRING, 0, (LPARAM)mruList[i]);
    }
    if (mruCount > 0) {
        SendMessage(hComboMru, CB_SETCURSEL, 0, 0);
    } else {
        SetWindowText(hComboMru, "");
    }
}

void AddMRU(const char* newPath) {
    if (!newPath || !*newPath) return;
    int foundIdx = -1;
    for (int i = 0; i < mruCount; i++) {
        if (_stricmp(mruList[i], newPath) == 0) { foundIdx = i; break; }
    }
    if (foundIdx != -1) {
        char temp[MAX_PATH];
        strcpy(temp, mruList[foundIdx]);
        for (int i = foundIdx; i > 0; i--) {
            strcpy(mruList[i], mruList[i - 1]);
        }
        strcpy(mruList[0], temp);
    } else {
        if (mruCount < 20) mruCount++;
        for (int i = mruCount - 1; i > 0; i--) {
            strcpy(mruList[i], mruList[i - 1]);
        }
        strcpy(mruList[0], newPath);
    }
    SaveMRU();
    PopulateMRUCombo();
}

void LoadFile(const char* path) {
    if (!path || !path[0]) return;

    char fullPath[MAX_PATH];
    if (!GetFullPathName(path, MAX_PATH, fullPath, NULL)) {
        strcpy(fullPath, path);
    }

    char dirPath[MAX_PATH];
    strcpy(dirPath, fullPath);
    char* lastSlash = strrchr(dirPath, '\\');
    char* fwdSlash = strrchr(dirPath, '/');
    if (fwdSlash > lastSlash) lastSlash = fwdSlash;
    if (lastSlash) {
        *lastSlash = '\0';
        SetCurrentDirectory(dirPath);
    }

    char* text = ReadFileText(fullPath);
    if (text) {
        strcpy(szCurrentFile, fullPath);
        FreeChunks(&g_LeftChunks);
        g_LeftChunks = ParseTextToChunks(text);
        PopulateList(hListLeft, g_LeftChunks);
        free(text);
        
        char msg[512];
        sprintf(msg, "Loaded: %s", szCurrentFile);
        SetStatus(msg);
        
        AddMRU(szCurrentFile);
    } else {
        char msg[512];
        sprintf(msg, "Failed to read file: %s", fullPath);
        SetStatus(msg);
    }
}

void ReloadLeftList() {
    if (szCurrentFile[0] != '\0') {
        char* text = ReadFileText(szCurrentFile);
        if (text) {
            FreeChunks(&g_LeftChunks);
            g_LeftChunks = ParseTextToChunks(text);
            PopulateList(hListLeft, g_LeftChunks);
            free(text);
        }
    }
}

void ReloadRightList() {
    char* cb = ReadClipboardText();
    FreeChunks(&g_RightChunks);
    if (cb) {
        g_RightChunks = ParseTextToChunks(cb);
        free(cb);
    }
    PopulateList(hListRight, g_RightChunks);
}

void OnBrowse() {
    ReloadRightList(); 
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFilter = "Source Files\0*.c;*.cpp;*.js;*.java;*.asm\0All Files\0*.*\0";
    ofn.lpstrFile = szCurrentFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileName(&ofn)) {
        LoadFile(szCurrentFile);
    }
}

int UpdateProcInLeft(Chunk* sourceChunk) {
    Chunk* l = g_LeftChunks;
    while (l) {
        if (l->isProc && strcmp(l->name, sourceChunk->name) == 0) break;
        l = l->next;
    }

    if (l) {
        if (strcmp(l->text, sourceChunk->text) == 0) {
            return 0; 
        }
        
        free(l->text);
        l->text = safe_strdup_newline(sourceChunk->text);
        return 1;
    } else {
        Chunk* nc = (Chunk*)malloc(sizeof(Chunk));
        nc->isProc = 1;
        strcpy(nc->name, sourceChunk->name);
        nc->text = safe_strdup_newline(sourceChunk->text);
        nc->next = NULL;

        Chunk* curr = g_LeftChunks;
        Chunk* prev = NULL;
        int inserted = 0;

        while (curr) {
            if (curr->isProc) {
                nc->next = curr;
                if (prev) {
                    prev->next = nc;
                } else {
                    g_LeftChunks = nc; 
                }
                inserted = 1;
                break;
            }
            prev = curr;
            curr = curr->next;
        }

        if (!inserted) {
            if (prev) {
                prev->next = nc;
            } else {
                g_LeftChunks = nc; 
            }
        }
        return 2;
    }
}

void OnPasteAndReplace() {
    ReloadLeftList();
    ReloadRightList();

    if (!g_RightChunks) { SetStatus("Clipboard is empty or inaccessible."); return; }
    
    int addedCount = 0, updatedCount = 0;
    char addedNames[1024] = {0};
    char updatedNames[1024] = {0};

    Chunk* tc = g_RightChunks;
    while (tc) {
        if (tc->isProc) {
            int res = UpdateProcInLeft(tc);
            if (res == 1) {
                updatedCount++;
                if (strlen(updatedNames) < 800) { strcat(updatedNames, tc->name); strcat(updatedNames, " "); }
            } else if (res == 2) {
                addedCount++;
                if (strlen(addedNames) < 800) { strcat(addedNames, tc->name); strcat(addedNames, " "); }
            }
        }
        tc = tc->next;
    }
    
    PopulateList(hListLeft, g_LeftChunks);
    
    if (addedCount == 0 && updatedCount == 0) {
        SetStatus("No changes (content identical to current source).");
    } else {
        char msg[1024];
        sprintf(msg, "Changed! Added %d [%s] | Updated %d [%s]", addedCount, addedNames, updatedCount, updatedNames);
        SetStatus(msg);
    }
}

void OnPaste() {
    ReloadLeftList();
    ReloadRightList();
    SetStatus("Reloaded file and clipboard. Clipboard procedures in right list.");
}

void OnUpdate() {
    int count = SendMessage(hListRight, LB_GETSELCOUNT, 0, 0);
    if (count <= 0) {
        SetStatus("No procedures selected in the right list.");
        return;
    }

    int* indices = (int*)malloc(count * sizeof(int));
    SendMessage(hListRight, LB_GETSELITEMS, count, (LPARAM)indices);

    char** selectedNames = (char**)malloc(count * sizeof(char*));
    for (int i = 0; i < count; i++) {
        selectedNames[i] = (char*)malloc(256);
        SendMessage(hListRight, LB_GETTEXT, indices[i], (LPARAM)selectedNames[i]);
    }
    free(indices);

    ReloadLeftList();
    ReloadRightList();
    
    int addedCount = 0, updatedCount = 0;
    char addedNames[1024] = {0};
    char updatedNames[1024] = {0};

    for (int i = 0; i < count; i++) {
        Chunk* r = g_RightChunks;
        while (r) {
            if (r->isProc && strcmp(r->name, selectedNames[i]) == 0) break;
            r = r->next;
        }
        if (r) {
            int res = UpdateProcInLeft(r);
            if (res == 1) {
                updatedCount++;
                if (strlen(updatedNames) < 800) { strcat(updatedNames, r->name); strcat(updatedNames, " "); }
            } else if (res == 2) {
                addedCount++;
                if (strlen(addedNames) < 800) { strcat(addedNames, r->name); strcat(addedNames, " "); }
            }
        }
        free(selectedNames[i]);
    }
    free(selectedNames);

    PopulateList(hListLeft, g_LeftChunks);
    
    if (addedCount == 0 && updatedCount == 0) {
        SetStatus("No changes (selected content identical to current source).");
    } else {
        char msg[1024];
        sprintf(msg, "Changed! Added %d [%s] | Updated %d [%s]", addedCount, addedNames, updatedCount, updatedNames);
        SetStatus(msg);
    }
}

void OnSave() {
    ReloadRightList();

    if (szCurrentFile[0] == '\0') {
        OPENFILENAME ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hMainWindow;
        ofn.lpstrFilter = "All Files\0*.*\0";
        ofn.lpstrFile = szCurrentFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (!GetSaveFileName(&ofn)) {
            SetStatus("Save cancelled.");
            return;
        }
    }
    FILE* f = fopen(szCurrentFile, "wb");
    if (f) {
        Chunk* c = g_LeftChunks;
        while (c) {
            fwrite(c->text, 1, strlen(c->text), f);
            c = c->next;
        }
        fclose(f);
        
        ReloadLeftList();

        char msg[512];
        sprintf(msg, "Saved changes to: %s", szCurrentFile);
        SetStatus(msg);
    } else {
        SetStatus("Failed to save file.");
    }
}

void OnEdit() {
    if (szCurrentFile[0] == '\0') {
        SetStatus("No file loaded to edit.");
        return;
    }
    ShellExecute(hMainWindow, "open", "notepad.exe", szCurrentFile, NULL, SW_SHOW);
    SetStatus("Opened current file in Notepad.");
}

void OnCompile() {
    if (szCurrentFile[0] == '\0') {
        SetStatus("No file loaded to compile.");
        return;
    }
    
    FILE* f = fopen(szCurrentFile, "rt");
    if (!f) {
        SetStatus("Failed to read file for compile command scan.");
        return;
    }

    char line[1024];
    char cmd[1024] = {0};
    int linesRead = 0;
    
    while (fgets(line, sizeof(line), f) && linesRead < 40) {
        char* p = strstr(line, "gcc ");
        if (!p) p = strstr(line, "wcl ");
        if (!p) p = strstr(line, "wcl386 ");
        
        if (p) {
            char* endComm = strstr(p, "*/");
            if (endComm) *endComm = '\0';
            
            char* nl = strpbrk(p, "\r\n");
            if (nl) *nl = '\0';
            
            strcpy(cmd, p);
            break;
        }
        linesRead++;
    }
    fclose(f);

    if (cmd[0] != '\0') {
        char msg[1024];
        sprintf(msg, "Compiling: %s", cmd);
        SetStatus(msg);
        
        char sysCmd[1024];
        sprintf(sysCmd, "cmd.exe /c \"%s || pause\"", cmd);
        system(sysCmd);
        
        SetStatus("Compilation command finished.");
    } else {
        SetStatus("Error: Could not find gcc, wcl, or wcl386 string in the first 40 lines.");
    }
}

void OnCopyLeft() {
    int count = SendMessage(hListLeft, LB_GETSELCOUNT, 0, 0);
    if (count <= 0) {
        SetStatus("No procedures selected in the left list.");
        return;
    }

    int* indices = (int*)malloc(count * sizeof(int));
    SendMessage(hListLeft, LB_GETSELITEMS, count, (LPARAM)indices);

    int totalLen = 0;
    char** selectedNames = (char**)malloc(count * sizeof(char*));
    
    for (int i = 0; i < count; i++) {
        selectedNames[i] = (char*)malloc(256);
        SendMessage(hListLeft, LB_GETTEXT, indices[i], (LPARAM)selectedNames[i]);
        
        Chunk* l = g_LeftChunks;
        while (l) {
            if (l->isProc && strcmp(l->name, selectedNames[i]) == 0) {
                totalLen += strlen(l->text) + 4; // Buffer space for "\r\n\r\n"
                break;
            }
            l = l->next;
        }
    }

    if (totalLen > 0) {
        char* clipBuf = (char*)malloc(totalLen + 1);
        clipBuf[0] = '\0';

        for (int i = 0; i < count; i++) {
            Chunk* l = g_LeftChunks;
            while (l) {
                if (l->isProc && strcmp(l->name, selectedNames[i]) == 0) {
                    strcat(clipBuf, l->text);
                    if (i < count - 1) {
                        strcat(clipBuf, "\r\n\r\n"); // Append the 2 CRLF separators
                    }
                    break;
                }
                l = l->next;
            }
            free(selectedNames[i]);
        }
        
        if (OpenClipboard(hMainWindow)) {
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(clipBuf) + 1);
            if (hMem) {
                memcpy(GlobalLock(hMem), clipBuf, strlen(clipBuf) + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
                
                char msg[256];
                sprintf(msg, "Copied %d procedures to clipboard.", count);
                SetStatus(msg);
            }
            CloseClipboard();
        }
        free(clipBuf);
    }
    
    free(selectedNames);
    free(indices);
}

void OnDeleteLeft() {
    int count = SendMessage(hListLeft, LB_GETSELCOUNT, 0, 0);
    if (count <= 0) {
        SetStatus("No procedures selected to delete.");
        return;
    }

    int* indices = (int*)malloc(count * sizeof(int));
    SendMessage(hListLeft, LB_GETSELITEMS, count, (LPARAM)indices);

    char** selectedNames = (char**)malloc(count * sizeof(char*));
    for (int i = 0; i < count; i++) {
        selectedNames[i] = (char*)malloc(256);
        SendMessage(hListLeft, LB_GETTEXT, indices[i], (LPARAM)selectedNames[i]);
    }
    free(indices);

    int deletedCount = 0;
    for (int i = 0; i < count; i++) {
        Chunk* curr = g_LeftChunks;
        Chunk* prev = NULL;

        while (curr) {
            if (curr->isProc && strcmp(curr->name, selectedNames[i]) == 0) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    g_LeftChunks = curr->next;
                }
                
                Chunk* toDelete = curr;
                curr = curr->next;
                free(toDelete->text);
                free(toDelete);
                deletedCount++;
                break; 
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
        free(selectedNames[i]);
    }
    free(selectedNames);

    PopulateList(hListLeft, g_LeftChunks);

    char msg[256];
    sprintf(msg, "Deleted %d procedures.", deletedCount);
    SetStatus(msg);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icex);

            hComboMru      = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 0, 4, 220, 300, hwnd, (HMENU)IDC_MRUCOMBO, NULL, NULL);
            hBtnBrowse     = CreateWindow("BUTTON", "Browse", WS_CHILD | WS_VISIBLE, 0, 0, 60, 30, hwnd, (HMENU)IDB_BROWSE, NULL, NULL);
            hBtnPasteRep   = CreateWindow("BUTTON", "Paste & Rep", WS_CHILD | WS_VISIBLE, 0, 0, 90, 30, hwnd, (HMENU)IDB_PASTEREPLACE, NULL, NULL);
            hBtnPaste      = CreateWindow("BUTTON", "Paste", WS_CHILD | WS_VISIBLE, 0, 0, 50, 30, hwnd, (HMENU)IDB_PASTE, NULL, NULL);
            hBtnUpdate     = CreateWindow("BUTTON", "Update", WS_CHILD | WS_VISIBLE, 0, 0, 60, 30, hwnd, (HMENU)IDB_UPDATE, NULL, NULL);
            hBtnSave       = CreateWindow("BUTTON", "Save", WS_CHILD | WS_VISIBLE, 0, 0, 50, 30, hwnd, (HMENU)IDB_SAVE, NULL, NULL);
            hBtnEdit       = CreateWindow("BUTTON", "Edit", WS_CHILD | WS_VISIBLE, 0, 0, 50, 30, hwnd, (HMENU)IDB_EDIT, NULL, NULL);
            hBtnCompile    = CreateWindow("BUTTON", "Compile", WS_CHILD | WS_VISIBLE, 0, 0, 70, 30, hwnd, (HMENU)IDB_COMPILE, NULL, NULL);
            hBtnCopyLeft   = CreateWindow("BUTTON", "Copy (L)", WS_CHILD | WS_VISIBLE, 0, 0, 70, 30, hwnd, (HMENU)IDB_COPYLEFT, NULL, NULL);
            hBtnDeleteLeft = CreateWindow("BUTTON", "Delete", WS_CHILD | WS_VISIBLE, 0, 0, 60, 30, hwnd, (HMENU)IDB_DELETELEFT, NULL, NULL);

            hListLeft  = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_EXTENDEDSEL, 0, 0, 0, 0, hwnd, (HMENU)IDL_LEFT, NULL, NULL);
            hListRight = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_EXTENDEDSEL, 0, 0, 0, 0, hwnd, (HMENU)IDL_RIGHT, NULL, NULL);
            
            // Subclass the left listbox to intercept mouse events for drag and drop
            OldListProc = (WNDPROC)SetWindowLongPtr(hListLeft, GWLP_WNDPROC, (LONG_PTR)LeftListWndProc);

            hStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, (HMENU)IDS_STATUS, NULL, NULL);
            SetStatus("Ready. Release to Public domain.");
            
            LoadMRU();
            PopulateMRUCombo();

            DragAcceptFiles(hwnd, TRUE);
        } break;

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char droppedFile[MAX_PATH];
            
            if (DragQueryFile(hDrop, 0, droppedFile, MAX_PATH)) {
                LoadFile(droppedFile);
            }
            
            DragFinish(hDrop);
        } break;

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            SendMessage(hStatus, WM_SIZE, 0, 0);
            RECT rectStatus;
            GetWindowRect(hStatus, &rectStatus);
            int statusHeight = rectStatus.bottom - rectStatus.top;

            int listY = 32;
            int listH = height - listY - statusHeight;
            int halfW = width / 2;

            int rightX = width - 4;
            rightX -= 70; MoveWindow(hBtnCompile, rightX, 0, 70, 30, TRUE);
            rightX -= 50; MoveWindow(hBtnEdit, rightX, 0, 50, 30, TRUE);
            rightX -= 50; MoveWindow(hBtnSave, rightX, 0, 50, 30, TRUE);
            rightX -= 60; MoveWindow(hBtnUpdate, rightX, 0, 60, 30, TRUE);
            rightX -= 50; MoveWindow(hBtnPaste, rightX, 0, 50, 30, TRUE);
            rightX -= 90; MoveWindow(hBtnPasteRep, rightX, 0, 90, 30, TRUE);
            rightX -= 60; MoveWindow(hBtnBrowse, rightX, 0, 60, 30, TRUE);
            rightX -= 60; MoveWindow(hBtnDeleteLeft, rightX, 0, 60, 30, TRUE);
            rightX -= 70; MoveWindow(hBtnCopyLeft, rightX, 0, 70, 30, TRUE);

            int comboWidth = rightX - 8;
            if (comboWidth < 100) comboWidth = 100;
            MoveWindow(hComboMru, 4, 4, comboWidth, 300, TRUE);

            MoveWindow(hListLeft, 0, listY, halfW, listH, TRUE);
            MoveWindow(hListRight, halfW, listY, width - halfW, listH, TRUE);
        } break;

        case WM_COMMAND: {
            if (LOWORD(wParam) == IDC_MRUCOMBO && HIWORD(wParam) == CBN_SELENDOK) {
                int idx = SendMessage(hComboMru, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) {
                    char buf[MAX_PATH];
                    SendMessage(hComboMru, CB_GETLBTEXT, idx, (LPARAM)buf);
                    LoadFile(buf);
                }
            }
            else if (LOWORD(wParam) == IDB_BROWSE) OnBrowse();
            else if (LOWORD(wParam) == IDB_PASTEREPLACE) OnPasteAndReplace();
            else if (LOWORD(wParam) == IDB_PASTE) OnPaste();
            else if (LOWORD(wParam) == IDB_UPDATE) OnUpdate();
            else if (LOWORD(wParam) == IDB_SAVE) OnSave();
            else if (LOWORD(wParam) == IDB_EDIT) OnEdit();
            else if (LOWORD(wParam) == IDB_COMPILE) OnCompile();
            else if (LOWORD(wParam) == IDB_COPYLEFT) OnCopyLeft();
            else if (LOWORD(wParam) == IDB_DELETELEFT) OnDeleteLeft();
        } break;

        case WM_DESTROY:
            FreeChunks(&g_LeftChunks);
            FreeChunks(&g_RightChunks);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    GetModuleFileName(NULL, szIniFile, MAX_PATH);
    char* pExt = strrchr(szIniFile, '.');
    if (pExt) strcpy(pExt, ".ini");
    else strcat(szIniFile, ".ini");

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ProcManagerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    if (!RegisterClass(&wc)) return 0;

    hMainWindow = CreateWindow("ProcManagerClass", "Procedure Manager", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);

    char* cmd = lpCmdLine;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '"') {
        cmd++;
        char* p = cmd + strlen(cmd) - 1;
        while (p > cmd && (*p == ' ' || *p == '\t')) p--;
        if (*p == '"') *p = '\0';
    }
    
    if (strlen(cmd) > 0) {
        LoadFile(cmd);
    } else if (mruCount > 0) {
        LoadFile(mruList[0]);
    }

    ReloadRightList();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND hFocus = GetFocus();
            if (hFocus == hComboMru || GetParent(hFocus) == hComboMru) {
                char buf[MAX_PATH];
                GetWindowText(hComboMru, buf, MAX_PATH);
                LoadFile(buf);
                continue; 
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}