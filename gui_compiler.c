/*
 * ============================================================================
 * Touch-Friendly GCC C Compiler GUI
 * ============================================================================
 *
 * COMPILATION INSTRUCTIONS:
 * Using MinGW GCC on Windows:
 *   gcc gui_compiler.c -Os -s -o gui_compiler.exe -mwindows -lcomdlg32 -lshell32
 *
 * ============================================================================
 * PUBLIC DOMAIN DEDICATION:
 *
 * This software is released into the public domain. It is not fit for any 
 * purpose. Use entirely at your own risk.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ============================================================================
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commdlg.h>
#include <shellapi.h>

#define ID_GCC_PATH   101
#define ID_FILE_COMBO 102
#define ID_BTN_BROWSE 103
#define ID_BTN_EDIT   104
#define ID_BTN_COMPILE 105
#define ID_BTN_COPY   106
#define ID_OUTPUT     107
#define ID_BTN_RUN    108
#define ID_ARGS      109
#define ID_BTN_BACKUP 110
#define ID_BTN_PASTE  111
#define ID_BTN_RESTORE 112

// Global Variables
HWND hGccPath, hFileCombo, hBtnBrowse, hBtnEdit, hBtnCompile, hBtnCopy, hOutput, hBtnRun, hArgs, hBtnRestore, hBtnBackup, hBtnPaste;

HFONT hLargeFont;
char iniPath[MAX_PATH];

// Change current working directory to the directory of the currently selected file
void UpdateCurrentDirectoryFromFile() {
    char filepath[MAX_PATH];
    GetWindowText(hFileCombo, filepath, MAX_PATH);
    if (strlen(filepath) > 0) {
        char dir[MAX_PATH];
        strcpy(dir, filepath);
        char *lastSlash = strrchr(dir, '\\');
        if (!lastSlash) lastSlash = strrchr(dir, '/');
        if (lastSlash) {
            *lastSlash = '\0';
            SetCurrentDirectory(dir);
        }
    }
}

// Get the path to the INI file
void InitIniPath() {
    GetModuleFileName(NULL, iniPath, MAX_PATH);
    char *lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat(iniPath, "gui_compiler.ini");
}

// Load settings from INI
void LoadSettings() {
    char buffer[MAX_PATH];
    GetPrivateProfileString("Settings", "GCCPath", "C:\\MinGW\\bin", buffer, MAX_PATH, iniPath);
    SetWindowText(hGccPath, buffer);

    // Load file history into the dropdown
    int count = GetPrivateProfileInt("Settings", "HistoryCount", 0, iniPath);
    for (int i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "File%d", i);
        GetPrivateProfileString("History", key, "", buffer, MAX_PATH, iniPath);
        if (strlen(buffer) > 0) {
            SendMessage(hFileCombo, CB_ADDSTRING, 0, (LPARAM)buffer);
        }
    }

    // Load the most recently used file
    GetPrivateProfileString("Settings", "LastFile", "", buffer, MAX_PATH, iniPath);
    if (strlen(buffer) > 0) {
        int idx = SendMessage(hFileCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)buffer);
        if (idx == CB_ERR) {
            idx = SendMessage(hFileCombo, CB_ADDSTRING, 0, (LPARAM)buffer);
        }
        SendMessage(hFileCombo, CB_SETCURSEL, idx, 0);
        
        // Load arguments for this specific file
        char argsBuf[1024];
        GetPrivateProfileString("Args", buffer, "", argsBuf, sizeof(argsBuf), iniPath);
        SetWindowText(hArgs, argsBuf);
        
        UpdateCurrentDirectoryFromFile();
    }
}

// Save settings to INI
void SaveSettings() {
    char pathBuf[MAX_PATH];
    GetWindowText(hGccPath, pathBuf, MAX_PATH);
    WritePrivateProfileString("Settings", "GCCPath", pathBuf, iniPath);

    char fileBuf[MAX_PATH];
    GetWindowText(hFileCombo, fileBuf, MAX_PATH);
    WritePrivateProfileString("Settings", "LastFile", fileBuf, iniPath);

    // Save combo box history (limit to 15 items)
    int count = SendMessage(hFileCombo, CB_GETCOUNT, 0, 0);
    if (count > 15) count = 15; 
    
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d", count);
    WritePrivateProfileString("Settings", "HistoryCount", countStr, iniPath);
    
    WritePrivateProfileSection("History", "", iniPath); 
    
    for (int i = 0; i < count; i++) {
        char historyBuf[MAX_PATH];
        char key[16];
        SendMessage(hFileCombo, CB_GETLBTEXT, i, (LPARAM)historyBuf);
        snprintf(key, sizeof(key), "File%d", i);
        WritePrivateProfileString("History", key, historyBuf, iniPath);
    }

    if (strlen(fileBuf) > 0) {
        char argsBuf[1024];
        GetWindowText(hArgs, argsBuf, sizeof(argsBuf));
        WritePrivateProfileString("Args", fileBuf, argsBuf, iniPath);
    }
}

void PasteClipboardToFile(HWND hwndOwner) {
    char filepath[MAX_PATH];
    GetWindowText(hFileCombo, filepath, MAX_PATH);
    if (strlen(filepath) == 0) {
        SetWindowText(hOutput, "Error: No file selected to overwrite.");
        return;
    }

    if (!IsClipboardFormatAvailable(CF_TEXT)) {
        SetWindowText(hOutput, "Error: No text found in clipboard.");
        return;
    }

    if (!OpenClipboard(hwndOwner)) {
        SetWindowText(hOutput, "Error: Could not open clipboard.");
        return;
    }

    HGLOBAL hMem = GetClipboardData(CF_TEXT);
    if (hMem != NULL) {
        char *pText = (char*)GlobalLock(hMem);
        if (pText != NULL) {
            FILE *f = fopen(filepath, "w");
            if (f) {
                fputs(pText, f);
                fclose(f);
                
                char logMsg[MAX_PATH + 64];
                snprintf(logMsg, sizeof(logMsg), "Success: Overwrote file with clipboard contents:\r\n%s", filepath);
                SetWindowText(hOutput, logMsg);
            } else {
                SetWindowText(hOutput, "Error: Could not open file for writing. It might be in use.");
            }
            GlobalUnlock(hMem);
        }
    }
    CloseClipboard();
}

void CopyOutputToClipboard(HWND hwndOwner) {
    int len = GetWindowTextLength(hOutput);
    if (len <= 0) return;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
    if (!hMem) return;

    char *pMem = (char*)GlobalLock(hMem);
    if (pMem) {
        GetWindowText(hOutput, pMem, len + 1);
        GlobalUnlock(hMem);

        if (OpenClipboard(hwndOwner)) {
            EmptyClipboard();
            SetClipboardData(CF_TEXT, hMem);
            CloseClipboard();
        } else {
            GlobalFree(hMem);
        }
    }
}

int GetHeaderCommand(const char* filepath, char* outCmd, size_t outSize) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    char line[1024];
    int found = 0;
    
    for (int i = 0; i < 40 && fgets(line, sizeof(line), f); i++) {
        char *cmdPtr = strstr(line, "gcc");
        if (!cmdPtr) {
            cmdPtr = strstr(line, "wcl");
        }

        if (cmdPtr) {
            int valid = 0;
            if (cmdPtr == line) {
                valid = 1;
            } else {
                char prev = *(cmdPtr - 1);
                if (prev == ' ' || prev == '\t' || prev == '*' || prev == '/') {
                    valid = 1;
                }
            }

            if (valid) {
                strncpy(outCmd, cmdPtr, outSize);
                outCmd[outSize - 1] = '\0'; 
                outCmd[strcspn(outCmd, "\r\n")] = 0; 
                found = 1;
                break;
            }
        }
    }
    fclose(f);
    return found;
}

void CompileFile() {
    char filepath[MAX_PATH];
    GetWindowText(hFileCombo, filepath, MAX_PATH);
    if (strlen(filepath) == 0) {
        SetWindowText(hOutput, "Error: No file selected.");
        return;
    }

    char gccPath[MAX_PATH];
    GetWindowText(hGccPath, gccPath, MAX_PATH);

    char compileCmd[2048];
    if (!GetHeaderCommand(filepath, compileCmd, sizeof(compileCmd))) {
        char outExe[MAX_PATH];
        strcpy(outExe, filepath);
        char *dot = strrchr(outExe, '.');
        if (dot) *dot = '\0';
        strcat(outExe, ".exe");
        snprintf(compileCmd, sizeof(compileCmd), "gcc \"%s\" -o \"%s\"", filepath, outExe);
    }

    char fullCmd[4096];
    snprintf(fullCmd, sizeof(fullCmd), "set \"PATH=%s;%%PATH%%\" && %s 2>&1", gccPath, compileCmd);

    SetWindowText(hOutput, "Compiling...\r\n");
    FILE *pipe = _popen(fullCmd, "r");
    if (!pipe) {
        SetWindowText(hOutput, "Error: Failed to launch cmd pipe.");
        return;
    }

    char buffer[1024];
    char outputLog[16384] = "";
    
    strcpy(outputLog, "Command: ");
    strcat(outputLog, compileCmd);
    strcat(outputLog, "\r\n\r\n");

    size_t currentLen = strlen(outputLog);
    size_t maxLen = sizeof(outputLog) - 1;

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t bufLen = strlen(buffer);
        if (currentLen + bufLen < maxLen) {
            strcpy(outputLog + currentLen, buffer);
            currentLen += bufLen;
        }
    }
    
    int exitCode = _pclose(pipe);
    
    const char* successMsg = "\r\n[Success] Compiled with 0 errors.";
    const char* failMsg = "\r\n[Failed] Compilation encountered errors.";
    const char* finalMsg = (exitCode == 0) ? successMsg : failMsg;
    
    if (currentLen + strlen(finalMsg) < maxLen) {
        strcpy(outputLog + currentLen, finalMsg);
    }
    
    SetWindowText(hOutput, outputLog);
}

void BackupFile() {
    char filepath[MAX_PATH];
    GetWindowText(hFileCombo, filepath, MAX_PATH);
    if (strlen(filepath) == 0) {
        SetWindowText(hOutput, "Error: No file selected to backup.");
        return;
    }

    char base[MAX_PATH];
    char ext[32];
    strcpy(base, filepath);
    
    char *dot = strrchr(base, '.');
    if (dot) {
        strcpy(ext, dot);
        *dot = '\0'; 
    } else {
        strcpy(ext, "");
    }

    char backupPath[MAX_PATH];
    int counter = 1;
    
    while (1) {
        snprintf(backupPath, sizeof(backupPath), "%s%d%s", base, counter, ext);
        if (GetFileAttributes(backupPath) == INVALID_FILE_ATTRIBUTES) {
            break; 
        }
        counter++;
    }

    if (CopyFile(filepath, backupPath, FALSE)) {
        char logMsg[1024];
        snprintf(logMsg, sizeof(logMsg), "Backup created successfully:\r\n%s", backupPath);
        SetWindowText(hOutput, logMsg);
    } else {
        SetWindowText(hOutput, "Error: Failed to create backup file.");
    }
}

// Restore from the last created backup file to the target C file
void RestoreFile() {
    char filepath[MAX_PATH];
    GetWindowText(hFileCombo, filepath, MAX_PATH);
    if (strlen(filepath) == 0) {
        SetWindowText(hOutput, "Error: No file selected to restore.");
        return;
    }

    char base[MAX_PATH];
    char ext[32];
    strcpy(base, filepath);
    
    char *dot = strrchr(base, '.');
    if (dot) {
        strcpy(ext, dot);
        *dot = '\0'; 
    } else {
        strcpy(ext, "");
    }

    int highest = 0;
    char lastBackupPath[MAX_PATH] = "";
    char testPath[MAX_PATH];

    // Find the highest-numbered backup file
    for (int counter = 1; ; counter++) {
        snprintf(testPath, sizeof(testPath), "%s%d%s", base, counter, ext);
        if (GetFileAttributes(testPath) != INVALID_FILE_ATTRIBUTES) {
            highest = counter;
            strcpy(lastBackupPath, testPath);
        } else {
            break;
        }
    }

    if (highest == 0) {
        SetWindowText(hOutput, "Error: No backup files found for this file.");
        return;
    }

    if (CopyFile(lastBackupPath, filepath, FALSE)) {
        char logMsg[1024];
        snprintf(logMsg, sizeof(logMsg), "Successfully restored file from backup:\r\n%s", lastBackupPath);
        SetWindowText(hOutput, logMsg);
    } else {
        SetWindowText(hOutput, "Error: Failed to restore file from backup.");
    }
}

// Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hLargeFont = CreateFont(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                    DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            hGccPath   = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_GCC_PATH, NULL, NULL);
            hFileCombo = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_FILE_COMBO, NULL, NULL);
            hBtnBrowse = CreateWindow("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
            hArgs      = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_ARGS, NULL, NULL);
            hBtnEdit   = CreateWindow("BUTTON", "Edit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_EDIT, NULL, NULL);
            hBtnCompile= CreateWindow("BUTTON", "COMPILE", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_COMPILE, NULL, NULL);
            hBtnRun    = CreateWindow("BUTTON", "RUN EXE", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_RUN, NULL, NULL);
            hBtnRestore= CreateWindow("BUTTON", "Restore", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_RESTORE, NULL, NULL);
            hBtnBackup = CreateWindow("BUTTON", "Backup", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_BACKUP, NULL, NULL);
            hBtnPaste  = CreateWindow("BUTTON", "Paste", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_PASTE, NULL, NULL);
            hBtnCopy   = CreateWindow("BUTTON", "Copy Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)ID_BTN_COPY, NULL, NULL);

            hOutput    = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 0, 0, 0, 0, hwnd, (HMENU)ID_OUTPUT, NULL, NULL);

            SendMessage(hGccPath, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hFileCombo, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hArgs, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnEdit, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnCompile, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnRun, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnRestore, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnBackup, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnPaste, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hBtnCopy, WM_SETFONT, (WPARAM)hLargeFont, TRUE);
            SendMessage(hOutput, WM_SETFONT, (WPARAM)hLargeFont, TRUE);

            // Enable Drag and Drop
            DragAcceptFiles(hwnd, TRUE);

            InitIniPath();
            LoadSettings();
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            char szFile[MAX_PATH];
            if (DragQueryFile(hDrop, 0, szFile, MAX_PATH)) {
                int idx = SendMessage(hFileCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)szFile);
                if (idx == CB_ERR) {
                    idx = SendMessage(hFileCombo, CB_ADDSTRING, 0, (LPARAM)szFile);
                }
                SendMessage(hFileCombo, CB_SETCURSEL, idx, 0);

                char argsBuf[1024];
                GetPrivateProfileString("Args", szFile, "", argsBuf, sizeof(argsBuf), iniPath);
                SetWindowText(hArgs, argsBuf);

                SaveSettings();
                UpdateCurrentDirectoryFromFile();
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            int pad = 10;
            int rowH = 35; 

            // Row 1: GCC Path
            MoveWindow(hGccPath, pad, pad, width - (pad*2), rowH, TRUE);
            
            // Row 2: File Dropdown & Browse
            int btnW = 100;
            MoveWindow(hFileCombo, pad, pad*2 + rowH, width - (pad*3) - btnW, rowH + 250, TRUE);
            MoveWindow(hBtnBrowse, width - pad - btnW, pad*2 + rowH, btnW, rowH, TRUE);

            // Row 3: Command Line Args
            MoveWindow(hArgs, pad, pad*3 + rowH*2, width - (pad*2), rowH, TRUE);

            // Row 4: Action Buttons (7 Buttons)
            int numBtns = 7;
            int itemW = (width - (pad * (numBtns + 1))) / numBtns;
            int btnY = pad*4 + rowH*3;
            int btnH = (int)(rowH * 1.2);
            
            MoveWindow(hBtnEdit,    pad,                   btnY, itemW, btnH, TRUE);
            MoveWindow(hBtnCompile, pad*2 + itemW,         btnY, itemW, btnH, TRUE);
            MoveWindow(hBtnRun,     pad*3 + itemW*2,       btnY, itemW, btnH, TRUE);
            MoveWindow(hBtnRestore, pad*4 + itemW*3,       btnY, itemW, btnH, TRUE);
            MoveWindow(hBtnBackup,  pad*5 + itemW*4,       btnY, itemW, btnH, TRUE);
            MoveWindow(hBtnPaste,   pad*6 + itemW*5,       btnY, itemW, btnH, TRUE);
            MoveWindow(hBtnCopy,    pad*7 + itemW*6,       btnY, width - (pad*8) - (itemW*6), btnH, TRUE);

            // Row 5: Output Log
            int outY = btnY + btnH + pad;
            MoveWindow(hOutput, pad, outY, width - (pad*2), height - outY - pad, TRUE);
            return 0;
        }

        case WM_COMMAND: {
            // Update directory prior to handling button events
            if (HIWORD(wParam) == BN_CLICKED) {
                UpdateCurrentDirectoryFromFile();
            }

            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == ID_FILE_COMBO) {
                int idx = SendMessage(hFileCombo, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) {
                    char filepath[MAX_PATH];
                    SendMessage(hFileCombo, CB_GETLBTEXT, idx, (LPARAM)filepath);
                    
                    char argsBuf[1024];
                    GetPrivateProfileString("Args", filepath, "", argsBuf, sizeof(argsBuf), iniPath);
                    SetWindowText(hArgs, argsBuf);
                    
                    UpdateCurrentDirectoryFromFile();
                }
            }
            else if (LOWORD(wParam) == ID_BTN_BROWSE) {
                OPENFILENAME ofn;
                char szFile[MAX_PATH] = {0};
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "C Source Files\0*.c\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileName(&ofn) == TRUE) {
                    int idx = SendMessage(hFileCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)szFile);
                    if (idx == CB_ERR) {
                        idx = SendMessage(hFileCombo, CB_ADDSTRING, 0, (LPARAM)szFile);
                    }
                    SendMessage(hFileCombo, CB_SETCURSEL, idx, 0);
                    
                    char argsBuf[1024];
                    GetPrivateProfileString("Args", szFile, "", argsBuf, sizeof(argsBuf), iniPath);
                    SetWindowText(hArgs, argsBuf);
                    
                    SaveSettings();
                    UpdateCurrentDirectoryFromFile();
                }
            }
            else if (LOWORD(wParam) == ID_BTN_EDIT) {
                char filepath[MAX_PATH];
                GetWindowText(hFileCombo, filepath, MAX_PATH);
                if (strlen(filepath) > 0) {
                    ShellExecute(NULL, "open", "notepad.exe", filepath, NULL, SW_SHOWNORMAL);
                }
            }
            else if (LOWORD(wParam) == ID_BTN_COMPILE) {
                SaveSettings();
                CompileFile();
            }
            else if (LOWORD(wParam) == ID_BTN_RUN) {
                char filepath[MAX_PATH];
                GetWindowText(hFileCombo, filepath, MAX_PATH);
                if (strlen(filepath) > 0) {
                    SaveSettings(); 
                    
                    char argsBuf[1024];
                    GetWindowText(hArgs, argsBuf, sizeof(argsBuf));

                    char exePath[MAX_PATH];
                    strcpy(exePath, filepath);
                    char *dot = strrchr(exePath, '.');
                    if (dot) *dot = '\0';
                    strcat(exePath, ".exe");

                    HINSTANCE hInst = ShellExecute(NULL, "open", exePath, argsBuf, NULL, SW_SHOWNORMAL);
                    if ((INT_PTR)hInst <= 32) {
                        SetWindowText(hOutput, "Error: Failed to run executable. Ensure it compiled successfully.");
                    } else {
                        SetWindowText(hOutput, "Executable launched successfully.");
                    }
                }
            }
            else if (LOWORD(wParam) == ID_BTN_RESTORE) {
                RestoreFile();
            }
            else if (LOWORD(wParam) == ID_BTN_BACKUP) {
                BackupFile();
            }
            else if (LOWORD(wParam) == ID_BTN_PASTE) {
                PasteClipboardToFile(hwnd);
            }
            else if (LOWORD(wParam) == ID_BTN_COPY) {
                CopyOutputToClipboard(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            SaveSettings();
            DeleteObject(hLargeFont);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "GccGuiClass";
    WNDCLASS wc = {0};

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Touch GCC Compiler", 
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 650, 480, 
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}