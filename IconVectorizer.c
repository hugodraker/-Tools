/*
 * ============================================================================
 * IconVectorizer.c - Inkscape Layered PolyPolygon Edition
 * ============================================================================
 * 
 * COMPILE INSTRUCTIONS (MinGW GCC):
 * gcc -O2 IconVectorizer.c -o IconVectorizer.exe -lgdi32 -lcomdlg32 -lcomctl32 -mwindows
 * 
 * ============================================================================
 * LICENSE / DISCLAIMER:
 * This work is released into the Public Domain.
 * ============================================================================
 */

#define _WIN32_IE 0x0500
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define ID_COMBO_FILE 100
#define ID_BTN_BROWSE 101
#define ID_BTN_LOAD 102
#define ID_BTN_GENERATE 103
#define ID_LIST_ICONS 104
#define ID_TRACK_SIMP 105
#define ID_TRACK_COLORS 106
#define ID_TRACK_BRIGHT 107
#define ID_TRACK_CONTRAST 108
#define ID_TRACK_SAT 109
#define ID_TRACK_MINSIZE 110
#define ID_TRACK_PRIM 111
#define ID_EDIT_CODE 112
#define ID_PREVIEW 113
#define ID_CHK_INKSCAPE 114

// Globals
HINSTANCE hInst;
HICON* loadedIcons = NULL;
UINT iconCount = 0;
char iniPath[MAX_PATH] = "";

HWND hCombo, hBtnBrowse, hBtnLoad, hList, hBtnGen, hEdit, hPreview, hChkInkscape;
HWND hTrackSimp, hTrackColors, hTrackBright, hTrackContrast, hTrackSat, hTrackPrim, hTrackMinSize;

// Shape Classification Types
#define SHAPE_POLY 0
#define SHAPE_RECT 1
#define SHAPE_ELLIPSE 2

typedef struct {
    int id;
    int area;
    int minX, minY, maxX, maxY;
    POINT* pts;
    int ptCount;
    int shapeType;
} Blob;

typedef struct { 
    COLORREF c; 
    int count; 
} CFreq;

// Function Prototypes
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void InitConfigPath();
void LoadRecentFiles();
void SaveRecentFile(const char* newFile);
void TriggerLoadFile(HWND hwnd);
void BrowseFile(HWND hwnd);
void GenerateCodeAndPreview(HWND hwnd);
void DrawLowPolyIcon(HDC hdc, HICON hIcon, RECT rect, int generateCode, char** codeBuf, size_t* bufSize, int iconIndex);

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;
    InitCommonControls();
    InitConfigPath();

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "IconVectorizerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("IconVectorizerClass", "Win32 Vectorizer (Inkscape PolyPolygon)",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             50, 50, 1040, 720, NULL, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}

void InitConfigPath() {
    GetModuleFileName(NULL, iniPath, MAX_PATH);
    char* ext = strrchr(iniPath, '.');
    if (ext) strcpy(ext, ".ini");
    else strcat(iniPath, ".ini");
}

void LoadRecentFiles() {
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    char key[32], val[MAX_PATH];
    for (int i = 0; i < 50; i++) {
        sprintf(key, "File%d", i);
        GetPrivateProfileString("RecentFiles", key, "", val, MAX_PATH, iniPath);
        if (strlen(val) > 0) SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)val);
    }
    SendMessage(hCombo, CB_SETCURSEL, 0, 0);
}

void SaveRecentFile(const char* newFile) {
    char files[50][MAX_PATH] = {0};
    strcpy(files[0], newFile);
    int count = 1;
    char key[32], val[MAX_PATH];
    for (int i = 0; i < 50; i++) {
        sprintf(key, "File%d", i);
        GetPrivateProfileString("RecentFiles", key, "", val, MAX_PATH, iniPath);
        if (strlen(val) > 0 && stricmp(val, newFile) != 0 && count < 50) {
            strcpy(files[count++], val);
        }
    }
    for (int i = 0; i < 50; i++) {
        sprintf(key, "File%d", i);
        if (i < count) WritePrivateProfileString("RecentFiles", key, files[i], iniPath);
        else WritePrivateProfileString("RecentFiles", key, NULL, iniPath);
    }
    LoadRecentFiles();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateWindow("STATIC", "Target File (.exe, .ico, .dll):", WS_CHILD | WS_VISIBLE, 10, 10, 200, 20, hwnd, NULL, hInst, NULL);
            hCombo = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 10, 30, 400, 250, hwnd, (HMENU)ID_COMBO_FILE, hInst, NULL);
            hBtnBrowse = CreateWindow("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE, 420, 29, 80, 26, hwnd, (HMENU)ID_BTN_BROWSE, hInst, NULL);
            hBtnLoad = CreateWindow("BUTTON", "Load", WS_CHILD | WS_VISIBLE, 510, 29, 80, 26, hwnd, (HMENU)ID_BTN_LOAD, hInst, NULL);
            LoadRecentFiles();

            CreateWindow("STATIC", "Icons:", WS_CHILD | WS_VISIBLE, 10, 70, 140, 20, hwnd, NULL, hInst, NULL);
            hList = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_EXTENDEDSEL | LBS_NOTIFY, 10, 90, 140, 560, hwnd, (HMENU)ID_LIST_ICONS, hInst, NULL);

            int sx = 160;
            CreateWindow("STATIC", "Shape Simplification:", WS_CHILD | WS_VISIBLE, sx, 70, 200, 20, hwnd, NULL, hInst, NULL);
            hTrackSimp = CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, sx, 90, 200, 30, hwnd, (HMENU)ID_TRACK_SIMP, hInst, NULL);
            SendMessage(hTrackSimp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 50)); SendMessage(hTrackSimp, TBM_SETPOS, TRUE, 10);

            CreateWindow("STATIC", "Color Posterize (2 - 256):", WS_CHILD | WS_VISIBLE, sx, 120, 200, 20, hwnd, NULL, hInst, NULL);
            hTrackColors = CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, sx, 140, 200, 30, hwnd, (HMENU)ID_TRACK_COLORS, hInst, NULL);
            SendMessage(hTrackColors, TBM_SETRANGE, TRUE, MAKELPARAM(2, 256)); SendMessage(hTrackColors, TBM_SETPOS, TRUE, 32);

            CreateWindow("STATIC", "Brightness (-100 to +100):", WS_CHILD | WS_VISIBLE, sx, 170, 200, 20, hwnd, NULL, hInst, NULL);
            hTrackBright = CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, sx, 190, 200, 30, hwnd, (HMENU)ID_TRACK_BRIGHT, hInst, NULL);
            SendMessage(hTrackBright, TBM_SETRANGEMIN, TRUE, -100); SendMessage(hTrackBright, TBM_SETRANGEMAX, TRUE, 100); SendMessage(hTrackBright, TBM_SETPOS, TRUE, 0);

            CreateWindow("STATIC", "Contrast (-100 to +100):", WS_CHILD | WS_VISIBLE, sx, 220, 200, 20, hwnd, NULL, hInst, NULL);
            hTrackContrast = CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, sx, 240, 200, 30, hwnd, (HMENU)ID_TRACK_CONTRAST, hInst, NULL);
            SendMessage(hTrackContrast, TBM_SETRANGEMIN, TRUE, -100); SendMessage(hTrackContrast, TBM_SETRANGEMAX, TRUE, 100); SendMessage(hTrackContrast, TBM_SETPOS, TRUE, 0);

            CreateWindow("STATIC", "Saturation (0 - 200%):", WS_CHILD | WS_VISIBLE, sx, 270, 200, 20, hwnd, NULL, hInst, NULL);
            hTrackSat = CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, sx, 290, 200, 30, hwnd, (HMENU)ID_TRACK_SAT, hInst, NULL);
            SendMessage(hTrackSat, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200)); SendMessage(hTrackSat, TBM_SETPOS, TRUE, 100);

            CreateWindow("STATIC", "Min Shape Size (Noise filter):", WS_CHILD | WS_VISIBLE, sx, 320, 200, 20, hwnd, NULL, hInst, NULL);
            hTrackMinSize = CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, sx, 340, 200, 30, hwnd, (HMENU)ID_TRACK_MINSIZE, hInst, NULL);
            SendMessage(hTrackMinSize, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50)); SendMessage(hTrackMinSize, TBM_SETPOS, TRUE, 4);

            CreateWindow("STATIC", "Primitive Detect (0=Off, 2=Max):", WS_CHILD | WS_VISIBLE, sx, 370, 200, 20, hwnd, NULL, hInst, NULL);
            hTrackPrim = CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS, sx, 390, 200, 30, hwnd, (HMENU)ID_TRACK_PRIM, hInst, NULL);
            SendMessage(hTrackPrim, TBM_SETRANGE, TRUE, MAKELPARAM(0, 2)); SendMessage(hTrackPrim, TBM_SETPOS, TRUE, 1);

            hChkInkscape = CreateWindow("BUTTON", "Inkscape Layered Mode", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, sx, 430, 200, 20, hwnd, (HMENU)ID_CHK_INKSCAPE, hInst, NULL);
            SendMessage(hChkInkscape, BM_SETCHECK, BST_CHECKED, 0);

            CreateWindow("STATIC", "Live Preview:", WS_CHILD | WS_VISIBLE, sx, 460, 200, 20, hwnd, NULL, hInst, NULL);
            hPreview = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, sx, 480, 200, 200, hwnd, (HMENU)ID_PREVIEW, hInst, NULL);

            int cx = 380;
            CreateWindow("STATIC", "C Code Output:", WS_CHILD | WS_VISIBLE, cx, 70, 200, 20, hwnd, NULL, hInst, NULL);
            hBtnGen = CreateWindow("BUTTON", "Update / Generate All Selected", WS_CHILD | WS_VISIBLE, cx + 380, 65, 220, 26, hwnd, (HMENU)ID_BTN_GENERATE, hInst, NULL);
            
            // Removed WS_HSCROLL to enable Word Wrapping
            hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, cx, 95, 620, 555, hwnd, (HMENU)ID_EDIT_CODE, hInst, NULL);
            HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_BROWSE) BrowseFile(hwnd);
            else if (LOWORD(wParam) == ID_BTN_LOAD) TriggerLoadFile(hwnd);
            else if (LOWORD(wParam) == ID_LIST_ICONS && HIWORD(wParam) == LBN_SELCHANGE) InvalidateRect(hPreview, NULL, TRUE);
            else if (LOWORD(wParam) == ID_BTN_GENERATE) { GenerateCodeAndPreview(hwnd); InvalidateRect(hPreview, NULL, TRUE); }
            else if (LOWORD(wParam) == ID_CHK_INKSCAPE) InvalidateRect(hPreview, NULL, TRUE);
            break;

        case WM_HSCROLL:
            InvalidateRect(hPreview, NULL, TRUE);
            break;

        case WM_DRAWITEM:
            if (wParam == ID_PREVIEW) {
                LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
                FillRect(pdis->hDC, &pdis->rcItem, (HBRUSH)(COLOR_WINDOW + 1));
                if (iconCount > 0) {
                    int caretIndex = SendMessage(hList, LB_GETCARETINDEX, 0, 0);
                    if (caretIndex != LB_ERR && caretIndex >= 0 && caretIndex < (int)iconCount) {
                        DrawLowPolyIcon(pdis->hDC, loadedIcons[caretIndex], pdis->rcItem, 0, NULL, NULL, 0);
                    }
                }
                return TRUE;
            }
            break;

        case WM_DESTROY:
            for (UINT i = 0; i < iconCount; i++) DestroyIcon(loadedIcons[i]);
            free(loadedIcons);
            PostQuitMessage(0);
            break;

        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void BrowseFile(HWND hwnd) {
    OPENFILENAME ofn;
    char szFile[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Executables, DLLs and Icons\0*.exe;*.ico;*.dll\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileName(&ofn)) {
        SetWindowText(hCombo, szFile);
        TriggerLoadFile(hwnd);
    }
}

void TriggerLoadFile(HWND hwnd) {
    char szFile[MAX_PATH];
    GetWindowText(hCombo, szFile, MAX_PATH);
    if (GetFileAttributes(szFile) == INVALID_FILE_ATTRIBUTES) return;
    if (loadedIcons) {
        for (UINT i = 0; i < iconCount; i++) DestroyIcon(loadedIcons[i]);
        free(loadedIcons); loadedIcons = NULL;
    }
    iconCount = ExtractIconEx(szFile, -1, NULL, NULL, 0);
    if (iconCount > 0) {
        loadedIcons = (HICON*)malloc(sizeof(HICON) * iconCount);
        ExtractIconEx(szFile, 0, loadedIcons, NULL, iconCount);
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        char buf[64];
        for (UINT i = 0; i < iconCount; i++) {
            sprintf(buf, "Icon %u", i);
            SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessage(hList, LB_SETSEL, TRUE, 0);
        SendMessage(hList, LB_SETCARETINDEX, 0, FALSE);
        SaveRecentFile(szFile);
        SetWindowText(hCombo, szFile); 
        InvalidateRect(hPreview, NULL, TRUE);
    }
}

void GenerateCodeAndPreview(HWND hwnd) {
    int selCount = SendMessage(hList, LB_GETSELCOUNT, 0, 0);
    if (selCount == 0) return;
    int* selItems = (int*)malloc(sizeof(int) * selCount);
    SendMessage(hList, LB_GETSELITEMS, selCount, (LPARAM)selItems);

    size_t bufSize = 4 * 1024 * 1024;
    char* codeBuf = (char*)malloc(bufSize);
    strcpy(codeBuf, "// Generated Layered PolyPolygon C GDI Code\n// Call these functions in your WM_PAINT\n\n");
    char* ptr = codeBuf + strlen(codeBuf);

    for (int i = 0; i < selCount; i++) {
        int idx = selItems[i];
        RECT dummy = {0,0,256,256};
        DrawLowPolyIcon(NULL, loadedIcons[idx], dummy, 1, &ptr, &bufSize, idx);
    }

    SetWindowText(hEdit, codeBuf);
    free(codeBuf); free(selItems);
}

// Math Helpers
float pDistance(POINT p, POINT p1, POINT p2) {
    float A = p.x - p1.x; float B = p.y - p1.y; float C = p2.x - p1.x; float D = p2.y - p1.y;
    float dot = A * C + B * D; float len_sq = C * C + D * D; float param = -1;
    if (len_sq != 0) param = dot / len_sq;
    float xx, yy;
    if (param < 0) { xx = p1.x; yy = p1.y; }
    else if (param > 1) { xx = p2.x; yy = p2.y; }
    else { xx = p1.x + param * C; yy = p1.y + param * D; }
    float dx = p.x - xx; float dy = p.y - yy;
    return sqrt(dx * dx + dy * dy);
}

void DouglasPeucker(POINT* pts, int start, int end, float epsilon, int* keep) {
    float dmax = 0; int index = start;
    for (int i = start + 1; i < end; i++) {
        float d = pDistance(pts[i], pts[start], pts[end]);
        if (d > dmax) { index = i; dmax = d; }
    }
    if (dmax > epsilon) {
        keep[index] = 1;
        DouglasPeucker(pts, start, index, epsilon, keep);
        DouglasPeucker(pts, index, end, epsilon, keep);
    }
}

int CompareFreq(const void* a, const void* b) {
    return ((CFreq*)b)->count - ((CFreq*)a)->count;
}

Blob* ExtractBlobsForMask(int* boolMask, int w, int h, int minSize, float epsilon, int primStrength, int* outCount) {
    int* blobIDMap = (int*)calloc(w * h, sizeof(int));
    Blob* blobs = (Blob*)malloc(5000 * sizeof(Blob));
    int bCount = 0;
    int* qx = (int*)malloc(w * h * sizeof(int));
    int* qy = (int*)malloc(w * h * sizeof(int));

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (!boolMask[y * w + x] || blobIDMap[y * w + x]) continue;

            int bID = bCount + 1;
            blobs[bCount].id = bID;
            blobs[bCount].area = 0;
            blobs[bCount].minX = w; blobs[bCount].minY = h;
            blobs[bCount].maxX = 0; blobs[bCount].maxY = 0;
            
            int head = 0, tail = 0;
            qx[tail] = x; qy[tail] = y; tail++;
            blobIDMap[y * w + x] = bID;

            while(head < tail) {
                int cx = qx[head]; int cy = qy[head]; head++;
                blobs[bCount].area++;
                if(cx < blobs[bCount].minX) blobs[bCount].minX = cx;
                if(cx > blobs[bCount].maxX) blobs[bCount].maxX = cx;
                if(cy < blobs[bCount].minY) blobs[bCount].minY = cy;
                if(cy > blobs[bCount].maxY) blobs[bCount].maxY = cy;

                int dx[] = {1, -1, 0, 0}; int dy[] = {0, 0, 1, -1};
                for(int d=0; d<4; d++) {
                    int nx = cx+dx[d], ny = cy+dy[d];
                    if(nx>=0 && nx<w && ny>=0 && ny<h) {
                        if(boolMask[ny * w + nx] && !blobIDMap[ny * w + nx]) {
                            blobIDMap[ny * w + nx] = bID;
                            qx[tail] = nx; qy[tail] = ny; tail++;
                        }
                    }
                }
            }

            if (blobs[bCount].area < minSize) { blobs[bCount].ptCount = 0; bCount++; continue; }

            // Trace Contour
            int startX = -1, startY = -1;
            for (int cy = blobs[bCount].minY; cy <= blobs[bCount].maxY && startY == -1; cy++) {
                for (int cx = blobs[bCount].minX; cx <= blobs[bCount].maxX; cx++) {
                    if (blobIDMap[cy * w + cx] == bID) { startX = cx; startY = cy; break; }
                }
            }

            POINT rawPts[4096]; int rawCount = 0;
            int vx = startX, vy = startY, dir = 0;
            do {
                rawPts[rawCount++] = (POINT){vx, vy};
                int c_in = 0, c_out = 0;
                if (dir == 0) { 
                    if(vx<w && vy<h) c_in = (blobIDMap[vy*w+vx]==bID); 
                    if(vx<w && vy-1>=0) c_out = (blobIDMap[(vy-1)*w+vx]==bID); 
                } else if (dir == 1) { 
                    if(vx-1>=0 && vy<h) c_in = (blobIDMap[vy*w+(vx-1)]==bID); 
                    if(vx<w && vy<h) c_out = (blobIDMap[vy*w+vx]==bID); 
                } else if (dir == 2) { 
                    if(vx-1>=0 && vy-1>=0) c_in = (blobIDMap[(vy-1)*w+(vx-1)]==bID); 
                    if(vx<w && vy-1>=0) c_out = (blobIDMap[(vy-1)*w+vx]==bID); 
                } else { 
                    if(vx<w && vy-1>=0) c_in = (blobIDMap[(vy-1)*w+vx]==bID); 
                    if(vx-1>=0 && vy-1>=0) c_out = (blobIDMap[(vy-1)*w+(vx-1)]==bID); 
                }

                if (c_out) dir = (dir + 3) % 4;
                else if (!c_in) dir = (dir + 1) % 4;
                
                if (dir == 0) vx++; else if (dir == 1) vy++; else if (dir == 2) vx--; else vy--;
            } while ((vx != startX || vy != startY) && rawCount < 4095);

            // DP Simp
            int* keep = (int*)calloc(rawCount, sizeof(int));
            keep[0] = 1; keep[rawCount - 1] = 1;
            int split = 0; float maxD = 0;
            for (int i = 1; i < rawCount - 1; i++) {
                float d = pDistance(rawPts[i], rawPts[0], rawPts[rawCount/2]);
                if (d > maxD) { maxD = d; split = i; }
            }
            keep[split] = 1;
            DouglasPeucker(rawPts, 0, split, epsilon, keep);
            DouglasPeucker(rawPts, split, rawCount - 1, epsilon, keep);

            blobs[bCount].pts = (POINT*)malloc(rawCount * sizeof(POINT));
            blobs[bCount].ptCount = 0;
            for (int i = 0; i < rawCount; i++) {
                if (keep[i]) blobs[bCount].pts[blobs[bCount].ptCount++] = rawPts[i];
            }
            free(keep);

            // Primitives
            blobs[bCount].shapeType = SHAPE_POLY;
            if (primStrength > 0) {
                float bbArea = (blobs[bCount].maxX - blobs[bCount].minX + 1) * (blobs[bCount].maxY - blobs[bCount].minY + 1);
                float rectThresh = (primStrength == 2) ? 0.85f : 0.95f;
                float circThreshLow = (primStrength == 2) ? 0.70f : 0.75f;
                float circThreshHigh = (primStrength == 2) ? 0.90f : 0.82f;
                
                if (blobs[bCount].area >= bbArea * rectThresh) {
                    blobs[bCount].shapeType = SHAPE_RECT;
                } else if (blobs[bCount].area >= bbArea * circThreshLow && blobs[bCount].area <= bbArea * circThreshHigh) {
                    int c1 = (blobIDMap[blobs[bCount].minY * w + blobs[bCount].minX] != bID);
                    int c2 = (blobIDMap[blobs[bCount].minY * w + blobs[bCount].maxX] != bID);
                    int c3 = (blobIDMap[blobs[bCount].maxY * w + blobs[bCount].minX] != bID);
                    int c4 = (blobIDMap[blobs[bCount].maxY * w + blobs[bCount].maxX] != bID);
                    if (c1 && c2 && c3 && c4) blobs[bCount].shapeType = SHAPE_ELLIPSE;
                }
            }
            bCount++;
        }
    }
    free(qx); free(qy); free(blobIDMap);
    *outCount = bCount;
    return blobs;
}

void RenderLayer(HDC hdc, HDC hdcTarget, RECT rect, int generateCode, char** codeBuf, COLORREF c, Blob* blobs, int bCount, float scaleX, float scaleY, int* layerIndex) {
    int totalPolyPts = 0;
    int numPolyBlobs = 0;
    int activeShapes = 0;

    for (int i=0; i<bCount; i++) {
        if (blobs[i].ptCount == 0) continue;
        activeShapes++;
        if (blobs[i].shapeType == SHAPE_POLY) {
            totalPolyPts += blobs[i].ptCount;
            numPolyBlobs++;
        }
    }
    if (activeShapes == 0) return;

    if (generateCode) {
        *codeBuf += sprintf(*codeBuf, "    // Layer %d | Color #%02X%02X%02X\n", *layerIndex, GetRValue(c), GetGValue(c), GetBValue(c));
        *codeBuf += sprintf(*codeBuf, "    hBr = CreateSolidBrush(RGB(%d, %d, %d));\n", GetRValue(c), GetGValue(c), GetBValue(c));
        *codeBuf += sprintf(*codeBuf, "    hPen = CreatePen(PS_SOLID, 1, RGB(%d, %d, %d));\n", GetRValue(c), GetGValue(c), GetBValue(c));
        *codeBuf += sprintf(*codeBuf, "    SelectObject(hdc, hBr);\n    SelectObject(hdc, hPen);\n");

        for (int i=0; i<bCount; i++) {
            if (blobs[i].ptCount == 0) continue;
            if (blobs[i].shapeType == SHAPE_RECT) {
                *codeBuf += sprintf(*codeBuf, "    Rectangle(hdc, offsetX + %d, offsetY + %d, offsetX + %d, offsetY + %d);\n", 
                                    blobs[i].minX, blobs[i].minY, blobs[i].maxX + 1, blobs[i].maxY + 1);
            } else if (blobs[i].shapeType == SHAPE_ELLIPSE) {
                *codeBuf += sprintf(*codeBuf, "    Ellipse(hdc, offsetX + %d, offsetY + %d, offsetX + %d, offsetY + %d);\n", 
                                    blobs[i].minX, blobs[i].minY, blobs[i].maxX + 1, blobs[i].maxY + 1);
            }
        }

        if (numPolyBlobs > 0) {
            *codeBuf += sprintf(*codeBuf, "    POINT pts_%d[] = {\n", *layerIndex);
            for (int i=0; i<bCount; i++) {
                if (blobs[i].ptCount > 0 && blobs[i].shapeType == SHAPE_POLY) {
                    for(int p=0; p<blobs[i].ptCount; p++) {
                        *codeBuf += sprintf(*codeBuf, "        {offsetX + %d, offsetY + %d},\n", blobs[i].pts[p].x, blobs[i].pts[p].y);
                    }
                }
            }
            *codeBuf += sprintf(*codeBuf, "    };\n");
            
            *codeBuf += sprintf(*codeBuf, "    int counts_%d[] = {", *layerIndex);
            for (int i=0; i<bCount; i++) {
                if (blobs[i].ptCount > 0 && blobs[i].shapeType == SHAPE_POLY) {
                    *codeBuf += sprintf(*codeBuf, "%d, ", blobs[i].ptCount);
                }
            }
            *codeBuf += sprintf(*codeBuf, "};\n");
            *codeBuf += sprintf(*codeBuf, "    PolyPolygon(hdc, pts_%d, counts_%d, %d);\n", *layerIndex, *layerIndex, numPolyBlobs);
        }
        *codeBuf += sprintf(*codeBuf, "    DeleteObject(hBr);\n    DeleteObject(hPen);\n\n");
    } else {
        HBRUSH hBr = CreateSolidBrush(c);
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdcTarget, hBr);
        HPEN hPen = CreatePen(PS_SOLID, 1, c);
        HPEN hOldPen = (HPEN)SelectObject(hdcTarget, hPen);

        for (int i=0; i<bCount; i++) {
            if (blobs[i].ptCount == 0) continue;
            if (blobs[i].shapeType == SHAPE_RECT) {
                Rectangle(hdcTarget, rect.left + (int)(blobs[i].minX * scaleX), rect.top + (int)(blobs[i].minY * scaleY), 
                                     rect.left + (int)((blobs[i].maxX + 1) * scaleX), rect.top + (int)((blobs[i].maxY + 1) * scaleY));
            } else if (blobs[i].shapeType == SHAPE_ELLIPSE) {
                Ellipse(hdcTarget, rect.left + (int)(blobs[i].minX * scaleX), rect.top + (int)(blobs[i].minY * scaleY), 
                                   rect.left + (int)((blobs[i].maxX + 1) * scaleX), rect.top + (int)((blobs[i].maxY + 1) * scaleY));
            }
        }

        if (numPolyBlobs > 0) {
            POINT* allPts = (POINT*)malloc(totalPolyPts * sizeof(POINT));
            int* counts = (int*)malloc(numPolyBlobs * sizeof(int));
            int pIdx = 0, cIdx = 0;
            for (int i=0; i<bCount; i++) {
                if (blobs[i].ptCount > 0 && blobs[i].shapeType == SHAPE_POLY) {
                    counts[cIdx++] = blobs[i].ptCount;
                    for (int p=0; p<blobs[i].ptCount; p++) {
                        allPts[pIdx].x = rect.left + (int)(blobs[i].pts[p].x * scaleX);
                        allPts[pIdx].y = rect.top + (int)(blobs[i].pts[p].y * scaleY);
                        pIdx++;
                    }
                }
            }
            PolyPolygon(hdcTarget, allPts, counts, numPolyBlobs);
            free(allPts); free(counts);
        }

        SelectObject(hdcTarget, hOldPen); SelectObject(hdcTarget, hOldBr);
        DeleteObject(hPen); DeleteObject(hBr);
    }
    (*layerIndex)++;
}

void DrawLowPolyIcon(HDC hdcTarget, HICON hIcon, RECT rect, int generateCode, char** codeBuf, size_t* bufSize, int iconIndex) {
    ICONINFO info;
    if (!GetIconInfo(hIcon, &info)) return;

    float epsilon = SendMessage(hTrackSimp, TBM_GETPOS, 0, 0) / 10.0f;
    int colorLevels = SendMessage(hTrackColors, TBM_GETPOS, 0, 0);
    int brightness = SendMessage(hTrackBright, TBM_GETPOS, 0, 0);
    int contrast = SendMessage(hTrackContrast, TBM_GETPOS, 0, 0);
    float satFactor = SendMessage(hTrackSat, TBM_GETPOS, 0, 0) / 100.0f;
    int minSize = SendMessage(hTrackMinSize, TBM_GETPOS, 0, 0);
    int primStrength = SendMessage(hTrackPrim, TBM_GETPOS, 0, 0);
    int isInkscape = SendMessage(hChkInkscape, BM_GETCHECK, 0, 0);

    BITMAP bm;
    GetObject(info.hbmColor, sizeof(bm), &bm);
    int w = bm.bmWidth, h = bm.bmHeight;
    if (w <= 0 || h <= 0) { w = 32; h = 32; }

    HDC hdcMem = CreateCompatibleDC(hdcTarget);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h; 
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = (DWORD*)malloc(w * h * sizeof(DWORD));
    GetDIBits(hdcMem, info.hbmColor, 0, h, pixels, &bmi, DIB_RGB_COLORS);

    float factor = (259.0f * (contrast + 255.0f)) / (255.0f * (259.0f - contrast));
    int step = 256 / colorLevels; if (step == 0) step = 1;

    HDC hdcMask = CreateCompatibleDC(hdcTarget);
    HBITMAP hbmMaskOld = (HBITMAP)SelectObject(hdcMask, info.hbmMask);

    CFreq freqs[1024]; int numFreqs = 0;

    for(int i = 0; i < w * h; i++) {
        if (GetPixel(hdcMask, i%w, i/w) != RGB(0,0,0)) continue; 

        DWORD pColor = pixels[i];
        int r = (pColor >> 16) & 0xFF, g = (pColor >> 8) & 0xFF, b = pColor & 0xFF;
        
        int L = (int)(0.299f * r + 0.587f * g + 0.114f * b);
        r = L + (int)(satFactor * (r - L)); g = L + (int)(satFactor * (g - L)); b = L + (int)(satFactor * (b - L));
        r = (int)(factor * (r + brightness - 128) + 128); g = (int)(factor * (g + brightness - 128) + 128); b = (int)(factor * (b + brightness - 128) + 128);
        r = r < 0 ? 0 : (r > 255 ? 255 : r); g = g < 0 ? 0 : (g > 255 ? 255 : g); b = b < 0 ? 0 : (b > 255 ? 255 : b);
        if (colorLevels < 256) { r = (r / step) * step; g = (g / step) * step; b = (b / step) * step; }
        
        COLORREF finalC = RGB(r, g, b);
        pixels[i] = finalC;

        int found = 0;
        for (int f = 0; f < numFreqs; f++) {
            if (freqs[f].c == finalC) { freqs[f].count++; found = 1; break; }
        }
        if (!found && numFreqs < 1024) { freqs[numFreqs].c = finalC; freqs[numFreqs].count = 1; numFreqs++; }
    }

    qsort(freqs, numFreqs, sizeof(CFreq), CompareFreq);
    COLORREF domCol = numFreqs > 0 ? freqs[0].c : RGB(255,255,255);

    if (generateCode) {
        *codeBuf += sprintf(*codeBuf, "void DrawLowPolyIcon_%d(HDC hdc, int offsetX, int offsetY) {\n", iconIndex);
        *codeBuf += sprintf(*codeBuf, "    HBRUSH hBr;\n    HPEN hPen;\n\n");
    }

    int* boolMask = (int*)malloc(w * h * sizeof(int));
    int layerIndex = 0;

    // Inkscape Base Silhouette Layer
    if (isInkscape && numFreqs > 0) {
        for(int i=0; i<w*h; i++) boolMask[i] = (GetPixel(hdcMask, i%w, i/w) == RGB(0,0,0));
        int bCount = 0;
        Blob* baseBlobs = ExtractBlobsForMask(boolMask, w, h, minSize, epsilon, primStrength, &bCount);
        
        if (bCount > 0) {
            RenderLayer(generateCode ? NULL : hdcTarget, hdcTarget, rect, generateCode, codeBuf, domCol, baseBlobs, bCount, (float)(rect.right - rect.left)/w, (float)(rect.bottom - rect.top)/h, &layerIndex);
            for(int b=0; b<bCount; b++) if(baseBlobs[b].ptCount>0) free(baseBlobs[b].pts);
        }
        free(baseBlobs);
    }

    // Stack unique color layers
    for (int f = 0; f < numFreqs; f++) {
        COLORREF c = freqs[f].c;
        if (isInkscape && c == domCol) continue; // Already covered via base layer silhouette

        for(int i=0; i<w*h; i++) {
            boolMask[i] = (GetPixel(hdcMask, i%w, i/w) == RGB(0,0,0) && pixels[i] == c);
        }
        
        int bCount = 0;
        Blob* cBlobs = ExtractBlobsForMask(boolMask, w, h, minSize, epsilon, primStrength, &bCount);
        if (bCount > 0) {
            RenderLayer(generateCode ? NULL : hdcTarget, hdcTarget, rect, generateCode, codeBuf, c, cBlobs, bCount, (float)(rect.right - rect.left)/w, (float)(rect.bottom - rect.top)/h, &layerIndex);
            for(int b=0; b<bCount; b++) if(cBlobs[b].ptCount>0) free(cBlobs[b].pts);
        }
        free(cBlobs);
    }

    if (generateCode) *codeBuf += sprintf(*codeBuf, "}\n\n");

    free(boolMask); free(pixels);
    SelectObject(hdcMask, hbmMaskOld); DeleteDC(hdcMask); DeleteDC(hdcMem);
    DeleteObject(info.hbmColor); DeleteObject(info.hbmMask);
}