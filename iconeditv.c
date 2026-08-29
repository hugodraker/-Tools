/* ============================================================================
 * PUBLIC DOMAIN NOTICE
 * Free and unencumbered software released into the public domain.
 * ============================================================================
 *
 * GCC COMPILE INSTRUCTIONS (MinGW):
 * gcc iconeditv.c -o iconeditv.exe -mwindows -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lshell32 -lmsimg32
 *
 * ============================================================================ */

#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PI 3.14159265358979323846
#define GRID_SIZE 32
#define MAX_POINTS 2048
#define MAX_SHAPES 200
#define MAX_UNDO 50
#define PANEL_WIDTH 340

// --- Data Structures ---
typedef struct { 
    int type; // 0=Polygon, 1=Line, 2=Polyline
    double ptsX[MAX_POINTS]; double ptsY[MAX_POINTS]; int ptCount; 
    COLORREF fill; COLORREF stroke; int useFill; int useStroke; 
} Shape;

typedef struct { double x1, y1, x2, y2; } Edge;

Shape shapes[MAX_SHAPES]; int shapeCount = 0;
Shape dragStartSnapshot[MAX_SHAPES];
Shape history[MAX_UNDO][MAX_SHAPES]; int historyShapeCount[MAX_UNDO]; int undoIndex = -1, undoCount = 0;

typedef struct { int caseId; char name[128]; Shape* shapes; int shapeCount; } IconDef;
IconDef parsedIcons[300]; int parsedCount = 0; int currentIconIdx = -1;
char loadedCFile[260] = {0}; int currentCaseId = 1;
int origFirstCaseId = -1, origLastCaseId = -1;

Shape currentShape;
int isDrawing = 0, currentMode = 6; // Start in SELECT/EDIT mode
int selectedShape = -1, selectedPtIdx = -1;
int isDraggingNodes = 0, isDraggingPoint = 0, shapeWasMoved = 0;
int ptSelected[MAX_SHAPES][MAX_POINTS] = {0}; 
int selOrderS[MAX_POINTS], selOrderP[MAX_POINTS], selOrderCount = 0;
double dragStartX = 0, dragStartY = 0; int isMovingInRotate = 0;
double rotCenterX = 0, rotCenterY = 0, startRotAngle = 0, startScaleDist = 0;

int hoverShape = -1, hoverPt = -1, hoverSegShape = -1, hoverSegPt = -1;
double hoverProjX = 0, hoverProjY = 0; int startX = 0, startY = 0;
double currentEndX = 0, currentEndY = 0;
double textCursorX = 0, textCursorY = 0; int textCursorActive = 0;
int snapToGrid = 1;

COLORREF palette[16] = {
    RGB(0,0,0), RGB(255,255,255), RGB(128,128,128), RGB(192,192,192),
    RGB(255,0,0), RGB(128,0,0), RGB(255,255,0), RGB(128,128,0),
    RGB(0,255,0), RGB(0,128,0), RGB(0,255,255), RGB(0,128,128),
    RGB(0,0,255), RGB(0,0,128), RGB(255,0,255), RGB(128,0,128)
};
COLORREF currentFill = RGB(128, 128, 128); int useFill = 1;
COLORREF currentStroke = RGB(0, 0, 0); int useStroke = 1;

HWND hMain, hTrackbar, hTrkSides, hTrkStar, hTrkShapeScale, hChkSnap, hBtn[25], hStatus;
HWND hTrkScale, hTrkPosX, hTrkPosY, hTrkStrX, hTrkStrY;
HWND hScrlIcon, hBtnAddIcon, hBtnDelIcon;
HWND hDistEdit = NULL; WNDPROC oldEditProc;

int editMode = 0; // 0: None, 1: Distance, 2: Width, 3: Height
int distMoveS = -1, distMoveP = -1, distAnchorS = -1, distAnchorP = -1; double distOriginalAngle = 0;
int editTargetShape = -1; double editOldVal = 0, editCx = 0, editCy = 0;

HBITMAP hRefBmp = NULL; HICON hRefIcon = NULL;

int refAlpha = 128, paramSides = 4, paramStar = 100;
int refScale = 100, refPosX = 100, refPosY = 100, refStrX = 100, refStrY = 100;
double absRefScale = 1.0, absRefPosX = 0.0, absRefPosY = 0.0, absRefStrX = 1.0, absRefStrY = 1.0;
int canvasSize = 480, scaleFactor = 15, clientW = 0, clientH = 0;

int font5x3[128] = {0};

// --- Forward Declarations ---
void GenShape(double eX, double eY);
void GenerateCCode(char** b_out, int isSplicing);
void CommitCurrentIcon();
void SwitchToIcon(int idx);
void ClearSelection();
void ToggleSelection(int s, int p);
void ParseSVG(const char* path, HWND hwnd);
int HandlePaletteClick(HWND hwnd, int x, int y, int isLeft);
int IsPointOnPolyEdge(double px, double py, Shape* s);

// --- Utilities & State ---
void ShowStatus(const char* msg) {
    SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)msg);
}
void InitFont() {
    font5x3['A']=0x25755; font5x3['B']=0x65656; font5x3['C']=0x34443; font5x3['D']=0x65556; font5x3['E']=0x74747; font5x3['F']=0x74744; font5x3['G']=0x34553; font5x3['H']=0x55755;
    font5x3['I']=0x72227; font5x3['J']=0x31152; font5x3['K']=0x55655; font5x3['L']=0x44447; font5x3['M']=0x57755; font5x3['N']=0x75555; font5x3['O']=0x25552; font5x3['P']=0x75744;
    font5x3['Q']=0x25531; font5x3['R']=0x75755; font5x3['S']=0x34216; font5x3['T']=0x72222; font5x3['U']=0x55557; font5x3['V']=0x55552; font5x3['W']=0x55775; font5x3['X']=0x55255;
    font5x3['Y']=0x55222; font5x3['Z']=0x71247; font5x3['0']=0x25552; font5x3['1']=0x26227; font5x3['2']=0x71747; font5x3['3']=0x71717; font5x3['4']=0x55711; font5x3['5']=0x74717;
    font5x3['6']=0x74757; font5x3['7']=0x71111; font5x3['8']=0x75757; font5x3['9']=0x75717; font5x3['.']=0x00002; font5x3['-']=0x00700;
}
void UpdateStatusBar() {
    char sb[256]; snprintf(sb, 256, " [ID: %d] Shapes: %d | Vert: %d | Depth: %d", currentCaseId, shapeCount, paramSides, paramStar);
    ShowStatus(sb);
}
void UpdateIconScrollbar() {
    SCROLLINFO si; si.cbSize = sizeof(si); si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = parsedCount > 0 ? parsedCount - 1 : 0;
    si.nPage = 1; si.nPos = currentIconIdx >= 0 ? currentIconIdx : 0;
    SetScrollInfo(hScrlIcon, SB_CTL, &si, TRUE);
}

void SaveState() { 
    if (undoIndex >= MAX_UNDO - 1) {
        for(int i = 0; i < MAX_UNDO - 1; i++) {
            historyShapeCount[i] = historyShapeCount[i+1];
            memcpy(history[i], history[i+1], sizeof(Shape)*MAX_SHAPES);
        }
        undoIndex = MAX_UNDO - 2;
    }
    undoIndex++; 
    historyShapeCount[undoIndex] = shapeCount; 
    memcpy(history[undoIndex], shapes, sizeof(Shape)*shapeCount); 
    UpdateStatusBar(); 
}

void Undo(HWND hwnd) { 
    if (isDrawing && currentShape.ptCount > 0) { 
        currentShape.ptCount--; 
        if(currentShape.ptCount == 0) isDrawing = 0; 
    }
    else if (undoIndex >= 0) { 
        shapeCount = historyShapeCount[undoIndex]; 
        memcpy(shapes, history[undoIndex], sizeof(Shape)*shapeCount); 
        undoIndex--; 
    } 
    selectedShape = -1; selectedPtIdx = -1; ClearSelection(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); 
}

void ToClipboard(HWND hwnd, const char* text) { if (OpenClipboard(hwnd)) { EmptyClipboard(); HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(text)+1); memcpy(GlobalLock(hMem), text, strlen(text)+1); GlobalUnlock(hMem); SetClipboardData(CF_TEXT, hMem); CloseClipboard(); ShowStatus("Success: GDI Code exported to clipboard!"); } }

LRESULT CALLBACK DistEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) { SendMessage(GetParent(hwnd), WM_APP+1, 0, 0); return 0; }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_HIDE); editMode = 0; return 0; }
    return CallWindowProc(oldEditProc, hwnd, msg, wParam, lParam);
}

void ClearSelection() { memset(ptSelected, 0, sizeof(ptSelected)); selOrderCount = 0; }
void ToggleSelection(int s, int p) {
    if (!ptSelected[s][p]) { ptSelected[s][p] = 1; selOrderS[selOrderCount] = s; selOrderP[selOrderCount] = p; selOrderCount++; } 
    else {
        ptSelected[s][p] = 0;
        for(int i=0; i<selOrderCount; i++) {
            if (selOrderS[i]==s && selOrderP[i]==p) {
                for(int j=i; j<selOrderCount-1; j++) { selOrderS[j]=selOrderS[j+1]; selOrderP[j]=selOrderP[j+1]; }
                selOrderCount--; break;
            }
        }
    }
}

// --- Math & Collision ---
double Snap(double val) { return snapToGrid ? round(val) : val; }
void PtToSegProj(double px, double py, double x1, double y1, double x2, double y2, double* prX, double* prY, double* dist) {
    double l2 = (x2-x1)*(x2-x1) + (y2-y1)*(y2-y1); if (l2 == 0) { *prX = x1; *prY = y1; *dist = sqrt((px-x1)*(px-x1) + (py-y1)*(py-y1)); return; }
    double t = max(0, min(1, ((px-x1)*(x2-x1) + (py-y1)*(py-y1)) / l2)); *prX = x1 + t*(x2-x1); *prY = y1 + t*(y2-y1); *dist = sqrt((px-*prX)*(px-*prX) + (py-*prY)*(py-*prY));
}
int PointInPolyShape(double px, double py, Shape* s) {
    if (s->ptCount < 3) return 0; int c = 0, n = s->ptCount;
    for (int i=0, j=n-1; i<n; j=i++) {
        if (((s->ptsY[i] > py) != (s->ptsY[j] > py)) && (px < (s->ptsX[j] - s->ptsX[i]) * (py - s->ptsY[i]) / (s->ptsY[j] - s->ptsY[i]) + s->ptsX[i])) c = !c;
    } return c;
}
void GenShape(double eX, double eY) {
    currentShape.ptCount = paramSides; double cx = (startX + eX) / 2.0, cy = (startY + eY) / 2.0; double rx = fabs(eX - startX) / 2.0, ry = fabs(eY - startY) / 2.0;
    if (paramSides == 4 && paramStar == 100) { currentShape.ptsX[0]=min(startX, eX); currentShape.ptsY[0]=min(startY, eY); currentShape.ptsX[1]=max(startX, eX); currentShape.ptsY[1]=min(startY, eY); currentShape.ptsX[2]=max(startX, eX); currentShape.ptsY[2]=max(startY, eY); currentShape.ptsX[3]=min(startX, eX); currentShape.ptsY[3]=max(startY, eY); return; }
    for (int i = 0; i < paramSides; i++) { double ang = i * (2.0 * PI / paramSides) - (PI / 2.0), rF = (paramSides>4 && paramStar<100 && i%2!=0) ? fmax(0.2, paramStar/100.0) : 1.0; currentShape.ptsX[i] = cx + cos(ang) * rx * rF; currentShape.ptsY[i] = cy + sin(ang) * ry * rF; }
}
void RegenSelectedShape() {
    if (selectedShape == -1 || shapes[selectedShape].ptCount == 0) return;
    double cx = 0, cy = 0;
    for(int p=0; p<shapes[selectedShape].ptCount; p++) { cx += shapes[selectedShape].ptsX[p]; cy += shapes[selectedShape].ptsY[p]; }
    cx /= shapes[selectedShape].ptCount; cy /= shapes[selectedShape].ptCount;
    double maxR = 0;
    for(int p=0; p<shapes[selectedShape].ptCount; p++) {
        double d = sqrt(pow(shapes[selectedShape].ptsX[p]-cx, 2) + pow(shapes[selectedShape].ptsY[p]-cy, 2));
        if (d > maxR) maxR = d;
    }
    shapes[selectedShape].ptCount = paramSides;
    if (paramSides == 4 && paramStar == 100) {
        double r = maxR / sqrt(2.0);
        shapes[selectedShape].ptsX[0] = cx - r; shapes[selectedShape].ptsY[0] = cy - r;
        shapes[selectedShape].ptsX[1] = cx + r; shapes[selectedShape].ptsY[1] = cy - r;
        shapes[selectedShape].ptsX[2] = cx + r; shapes[selectedShape].ptsY[2] = cy + r;
        shapes[selectedShape].ptsX[3] = cx - r; shapes[selectedShape].ptsY[3] = cy + r;
    } else {
        for(int i=0; i<paramSides; i++) {
            double ang = i * (2.0 * PI / paramSides) - (PI / 2.0); 
            double rF = (paramSides>4 && paramStar<100 && i%2!=0) ? fmax(0.2, paramStar/100.0) : 1.0; 
            shapes[selectedShape].ptsX[i] = cx + cos(ang)*maxR*rF; 
            shapes[selectedShape].ptsY[i] = cy + sin(ang)*maxR*rF; 
        }
    }
}
void DouglasPeucker(double* pX, double* pY, int start, int end, double epsilon, int* keep) {
    double dmax = 0; int index = start;
    for(int i = start + 1; i < end; i++) {
        double prX, prY, d; PtToSegProj(pX[i], pY[i], pX[start], pY[start], pX[end], pY[end], &prX, &prY, &d);
        if (d > dmax) { dmax = d; index = i; }
    }
    if (dmax > epsilon) { DouglasPeucker(pX, pY, start, index, epsilon, keep); DouglasPeucker(pX, pY, index, end, epsilon, keep); } 
    else { for(int i = start + 1; i < end; i++) keep[i] = 0; }
}
void SimplifyShapeDP(Shape* s, double epsilon) {
    if(s->ptCount < 3) return;
    int keep[MAX_POINTS]; for(int i=0; i<s->ptCount; i++) keep[i] = 1;
    double maxD = 0; int i1 = 0, i2 = s->ptCount/2;
    for(int i=0; i<s->ptCount; i++) { for(int j=i+1; j<s->ptCount; j++) { double d = pow(s->ptsX[i]-s->ptsX[j], 2) + pow(s->ptsY[i]-s->ptsY[j], 2); if(d > maxD) { maxD = d; i1 = i; i2 = j; } } }
    
    double tX[MAX_POINTS], tY[MAX_POINTS];
    for(int i=0; i<s->ptCount; i++) { tX[i] = s->ptsX[(i1+i)%s->ptCount]; tY[i] = s->ptsY[(i1+i)%s->ptCount]; }
    for(int i=0; i<s->ptCount; i++) { s->ptsX[i]=tX[i]; s->ptsY[i]=tY[i]; }
    int newI2 = (i2 - i1 + s->ptCount) % s->ptCount;
    
    DouglasPeucker(s->ptsX, s->ptsY, 0, newI2, epsilon, keep); DouglasPeucker(s->ptsX, s->ptsY, newI2, s->ptCount-1, epsilon, keep);
    
    double prX, prY, d; PtToSegProj(s->ptsX[s->ptCount-1], s->ptsY[s->ptCount-1], s->ptsX[newI2], s->ptsY[newI2], s->ptsX[0], s->ptsY[0], &prX, &prY, &d);
    if (d <= epsilon) keep[s->ptCount-1] = 0;

    int nP = 0; for(int i=0; i<s->ptCount; i++) { if(keep[i]) { s->ptsX[nP] = s->ptsX[i]; s->ptsY[nP] = s->ptsY[i]; nP++; } }
    if (nP > 1 && pow(s->ptsX[0]-s->ptsX[nP-1],2) + pow(s->ptsY[0]-s->ptsY[nP-1],2) < epsilon*epsilon) nP--;
    
    if (snapToGrid) {
        for(int i=0; i<nP; i++) { if (fabs(s->ptsX[i] - round(s->ptsX[i])) < 0.2) s->ptsX[i] = round(s->ptsX[i]); if (fabs(s->ptsY[i] - round(s->ptsY[i])) < 0.2) s->ptsY[i] = round(s->ptsY[i]); }
        int nP2 = 0; double nX2[MAX_POINTS], nY2[MAX_POINTS]; nX2[0] = s->ptsX[0]; nY2[0] = s->ptsY[0]; nP2 = 1;
        for(int i=1; i<nP; i++) { if (pow(s->ptsX[i]-nX2[nP2-1], 2) + pow(s->ptsY[i]-nY2[nP2-1], 2) > 0.01) { nX2[nP2] = s->ptsX[i]; nY2[nP2++] = s->ptsY[i]; } }
        if (nP2 > 1 && pow(nX2[0]-nX2[nP2-1],2) + pow(nY2[0]-nY2[nP2-1],2) < 0.01) nP2--;
        nP = nP2; for(int i=0; i<nP; i++) { s->ptsX[i]=nX2[i]; s->ptsY[i]=nY2[i]; }
    } s->ptCount = nP;
}

// --- Color Mapping ---
COLORREF ParseColor(const char* name) {
    if (strstr(name, "white")) return RGB(255,255,255); if (strstr(name, "black")) return RGB(0,0,0); if (strstr(name, "ltGray")) return RGB(192,192,192); if (strstr(name, "gray")) return RGB(128,128,128); if (strstr(name, "dkYellow")) return RGB(128,128,0); if (strstr(name, "yellow")) return RGB(255,255,0); if (strstr(name, "green")) return RGB(0,128,0); if (strstr(name, "blue")) return RGB(0,0,128); if (strstr(name, "cyan")) return RGB(0,128,128); if (strstr(name, "silver")) return RGB(224,224,224); if (strstr(name, "brown")) return RGB(128,64,0); if (strstr(name, "orange")) return RGB(255,128,0); if (strstr(name, "purple")) return RGB(128,0,128); if (strstr(name, "red")) return RGB(255,0,0); return RGB(0,0,0);
}
const char* GetThemeColor(COLORREF c) {
    struct { COLORREF c; const char* n; } t[] = { {RGB(255,255,255),"white"}, {RGB(0,0,0),"black"}, {RGB(128,128,128),"gray"}, {RGB(192,192,192),"ltGray"}, {RGB(224,224,224),"silver"}, {RGB(128,64,0),"brown"}, {RGB(255,128,0),"orange"}, {RGB(128,0,128),"purple"}, {RGB(255,0,0),"red"}, {RGB(0,128,0),"green"}, {RGB(0,0,128),"blue"}, {RGB(0,128,128),"cyan"}, {RGB(255,255,0),"yellow"}, {RGB(128,128,0),"dkYellow"} };
    int bD = 9999999; const char* bN = "gray";
    for(int i=0; i<14; i++) { int d = (GetRValue(c)-GetRValue(t[i].c))*(GetRValue(c)-GetRValue(t[i].c)) + (GetGValue(c)-GetGValue(t[i].c))*(GetGValue(c)-GetGValue(t[i].c)) + (GetBValue(c)-GetBValue(t[i].c))*(GetBValue(c)-GetBValue(t[i].c)); if (d < bD) { bD = d; bN = t[i].n; } } return bN;
}

// --- C Export Engine ---
void GenerateCCode(char** b_out, int isSplicing) {
    size_t bufSize = 1024 * 1024; char* b = (char*)malloc(bufSize); if (!b) { *b_out = NULL; return; }
    b[0] = '\0'; size_t len = 0; char t[512]; 
    
    #define APPEND_T() do { \
        size_t tLen = strlen(t); \
        if (len + tLen + 128 >= bufSize) { \
            bufSize = len + tLen + 1024 * 1024; \
            char* nb = (char*)realloc(b, bufSize); \
            if(nb) b = nb; else { free(b); *b_out=NULL; return; } \
        } \
        strcpy(b + len, t); len += tLen; \
    } while(0)

    if (!isSplicing) { snprintf(t, 512, "case %d: { // Exported Icon\n", currentCaseId == -1 ? 1 : currentCaseId); APPEND_T(); }
    
    int cF = -2, cS = -2;
    for (int i = 0; i < shapeCount; i++) {
        Shape* s = &shapes[i]; int wS = s->useStroke ? s->stroke : -1;
        if (wS != cS) { if (wS == -1) snprintf(t, 512, "    SelectObject(hdc, GetStockObject(NULL_PEN));\n"); else snprintf(t, 512, "    SelectObject(hdc, t->%sPen);\n", GetThemeColor(wS)); APPEND_T(); cS = wS; }
        if (s->type == 0 || s->type == 2) { int wF = s->useFill ? s->fill : -1; if (wF != cF) { if (wF == -1) snprintf(t, 512, "    SelectObject(hdc, GetStockObject(NULL_BRUSH));\n"); else snprintf(t, 512, "    SelectObject(hdc, t->%s);\n", GetThemeColor(wF)); APPEND_T(); cF = wF; } }
        
        if (s->type == 0 && s->ptCount > 2) {
            snprintf(t, 512, "    POINT p%d[] = { ", i); APPEND_T();
            for (int p = 0; p < s->ptCount; p++) { snprintf(t, 512, "PT(%g,%g)%s", s->ptsX[p], s->ptsY[p], p==s->ptCount-1?"":", "); APPEND_T(); }
            snprintf(t, 512, " }; POLY(p%d);\n", i); APPEND_T();
        } else if (s->type == 2 && s->ptCount >= 2) {
            snprintf(t, 512, "    POINT p%d[] = { ", i); APPEND_T();
            for (int p = 0; p < s->ptCount; p++) { snprintf(t, 512, "PT(%g,%g)%s", s->ptsX[p], s->ptsY[p], p==s->ptCount-1?"":", "); APPEND_T(); }
            snprintf(t, 512, " }; Polyline(hdc, p%d, %d);\n", i, s->ptCount); APPEND_T();
        } else if (s->type == 1 && s->ptCount == 2) {
            snprintf(t, 512, "    L(%g,%g,%g,%g);\n", s->ptsX[0], s->ptsY[0], s->ptsX[1], s->ptsY[1]); APPEND_T();
        }
    } 
    snprintf(t, 512, "    SelectObject(hdc, GetStockObject(BLACK_PEN));\n    SelectObject(hdc, GetStockObject(WHITE_BRUSH));\n"); APPEND_T();
    
    *b_out = b;
    #undef APPEND_T
}

void GenerateAllCCode(char** b_out) {
    CommitCurrentIcon();
    size_t bufSize = 1024 * 1024 * 2; char* b = (char*)malloc(bufSize); if (!b) { *b_out = NULL; return; }
    b[0] = '\0'; size_t len = 0;
    
    #define APPEND_STR(str) do { \
        size_t sLen = strlen(str); \
        if (len + sLen + 128 >= bufSize) { \
            bufSize = len + sLen + 1024 * 1024 * 2; \
            char* nb = (char*)realloc(b, bufSize); \
            if(nb) b = nb; else { free(b); *b_out=NULL; return; } \
        } \
        strcpy(b + len, str); len += sLen; \
    } while(0)
    
    if (parsedCount == 0) { char* single = NULL; GenerateCCode(&single, 0); if (single) { APPEND_STR(single); free(single); } APPEND_STR("    break;\n}\n"); *b_out = b; return; }
    
    Shape* bk_s = malloc(sizeof(Shape) * MAX_SHAPES); if (!bk_s) { free(b); *b_out = NULL; return; }
    memcpy(bk_s, shapes, sizeof(Shape)*MAX_SHAPES); int bk_sc = shapeCount; int bk_id = currentCaseId;

    for(int idx = 0; idx < parsedCount; idx++) {
        shapeCount = parsedIcons[idx].shapeCount; if (shapeCount > MAX_SHAPES) shapeCount = MAX_SHAPES;
        if (parsedIcons[idx].shapes) memcpy(shapes, parsedIcons[idx].shapes, sizeof(Shape) * shapeCount); else shapeCount = 0;
        currentCaseId = parsedIcons[idx].caseId;
        char* singleCode = NULL; GenerateCCode(&singleCode, 0); 
        if (singleCode) { APPEND_STR(singleCode); APPEND_STR("    break;\n}\n"); free(singleCode); }
    }
    shapeCount = bk_sc; currentCaseId = bk_id; memcpy(shapes, bk_s, sizeof(Shape)*MAX_SHAPES); free(bk_s);
    *b_out = b;
    #undef APPEND_STR
}

void ExportCode(HWND hwnd) { char* b = NULL; GenerateCCode(&b, 0); if (b) { strcat(b, "    break;\n}\n"); ToClipboard(hwnd, b); free(b); } }

void CommitCurrentIcon() {
    if (currentIconIdx >= 0 && currentIconIdx < parsedCount) {
        if (parsedIcons[currentIconIdx].shapes) { free(parsedIcons[currentIconIdx].shapes); parsedIcons[currentIconIdx].shapes = NULL; }
        if (shapeCount > 0) {
            parsedIcons[currentIconIdx].shapes = malloc(sizeof(Shape) * shapeCount);
            if (parsedIcons[currentIconIdx].shapes) memcpy(parsedIcons[currentIconIdx].shapes, shapes, sizeof(Shape) * shapeCount);
        }
        parsedIcons[currentIconIdx].shapeCount = shapeCount;
    }
}
void SwitchToIcon(int idx) {
    if (currentIconIdx == idx) return;
    CommitCurrentIcon(); currentIconIdx = idx;
    if (idx >= 0 && idx < parsedCount) {
        currentCaseId = parsedIcons[idx].caseId; shapeCount = parsedIcons[idx].shapeCount;
        if (parsedIcons[idx].shapes) memcpy(shapes, parsedIcons[idx].shapes, sizeof(Shape) * shapeCount); else shapeCount = 0;
    } else shapeCount = 0;
    undoIndex = -1; ClearSelection(); selectedShape = -1;
    UpdateIconScrollbar(); UpdateStatusBar(); InvalidateRect(hMain, NULL, FALSE);
}

// --- Advanced SVG Parser ---
double GetAttr(const char* tag, const char* end, const char* attr, double def) { char* p = strstr((char*)tag, attr); if (p && p < end) { p += strlen(attr); return atof(p); } return def; }
void ParseSVG(const char* path, HWND hwnd) {
    FILE* f = fopen(path, "rb"); if (!f) return; fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); char* d = (char*)malloc(sz + 1); fread(d, 1, sz, f); d[sz] = 0; fclose(f);
    double gTx = 0, gTy = 0; char* gT = strstr(d, "<g"); if (gT) { char* tr = strstr(gT, "translate("); if (tr && tr < strchr(gT, '>')) sscanf(tr, "translate(%lf,%lf)", &gTx, &gTy); }
    double minX=99999, minY=99999, maxX=-99999, maxY=-99999; Shape* tmp = (Shape*)calloc(MAX_SHAPES, sizeof(Shape)); int tC = 0; char* r = d;
    
    while ((r = strpbrk(r, "<")) != NULL && tC < MAX_SHAPES) {
        if (strncmp(r, "<rect", 5) != 0 && strncmp(r, "<polygon", 8) != 0 && strncmp(r, "<path", 5) != 0) { r++; continue; }
        char* eT = strchr(r, '>'); if (!eT) break;
        
        double a=1,b=0,c=0,D=1,e=0,F=0; char* mat=strstr(r, "matrix("); if (mat && mat < eT) sscanf(mat, "matrix(%lf,%lf,%lf,%lf,%lf,%lf)", &a,&b,&c,&D,&e,&F);
        COLORREF fCol=RGB(128,128,128); int uF=1; COLORREF sCol=RGB(0,0,0); int uS=0;
        char* fill = strstr(r, "fill:#"); if (fill && fill < eT) { int rC,gC,bC; sscanf(fill, "fill:#%2x%2x%2x", &rC,&gC,&bC); fCol=RGB(rC,gC,bC); } else if (strstr(r, "fill=\"none\"")) uF=0;
        char* strk = strstr(r, "stroke:#"); if (strk && strk < eT) { int rC,gC,bC; sscanf(strk, "stroke:#%2x%2x%2x", &rC,&gC,&bC); sCol=RGB(rC,gC,bC); uS=1; }
        Shape s = {0}; s.type = 0; s.fill = fCol; s.useFill = uF; s.stroke = sCol; s.useStroke = uS;

        if (strncmp(r, "<rect", 5) == 0) {
            double rx=GetAttr(r, eT, "x=\"", 0), ry=GetAttr(r, eT, "y=\"", 0), rw=GetAttr(r, eT, "width=\"", 0), rh=GetAttr(r, eT, "height=\"", 0);
            s.ptsX[0]=rx; s.ptsY[0]=ry; s.ptsX[1]=rx+rw; s.ptsY[1]=ry; s.ptsX[2]=rx+rw; s.ptsY[2]=ry+rh; s.ptsX[3]=rx; s.ptsY[3]=ry+rh; s.ptCount = 4;
        } else if (strncmp(r, "<polygon", 8) == 0 || strncmp(r, "<path", 5) == 0) {
            char* pts = strstr(r, strncmp(r,"<path",5)==0 ? "d=\"" : "points=\"");
            if (pts && pts < eT) {
                pts += (strncmp(r,"<path",5)==0 ? 3 : 8); double px, py; int n;
                while (*pts && *pts != '"' && s.ptCount < MAX_POINTS) {
                    if (isalpha(*pts)) { pts++; continue; }
                    if (isspace(*pts) || *pts == ',') { pts++; continue; }
                    if (sscanf(pts, "%lf%n", &px, &n) == 1) {
                        pts += n; while(*pts == ' ' || *pts == ',') pts++;
                        if (sscanf(pts, "%lf%n", &py, &n) == 1) { s.ptsX[s.ptCount]=px; s.ptsY[s.ptCount]=py; s.ptCount++; pts += n; }
                    } else pts++;
                }
            }
        }
        for (int i=0; i<s.ptCount; i++) {
            double nx = a*s.ptsX[i] + c*s.ptsY[i] + e + gTx; double ny = b*s.ptsX[i] + D*s.ptsY[i] + F + gTy;
            if (nx<minX)minX=nx; if(nx>maxX)maxX=nx; if(ny<minY)minY=ny; if(ny>maxY)maxY=ny;
            s.ptsX[i] = nx; s.ptsY[i] = ny;
        } if (s.ptCount >= 2 && tC < MAX_SHAPES) tmp[tC++] = s; r = eT;
    } free(d);
    
    if (tC > 0) {
        SaveState(); shapeCount = 0; double w = maxX-minX, ht = maxY-minY, maxD = (w>ht?w:ht); if (maxD==0) maxD=1; double sf = (GRID_SIZE-2.0)/maxD; double offX = ((GRID_SIZE-2.0)-(w*sf))/2.0+1.0, offY = ((GRID_SIZE-2.0)-(ht*sf))/2.0+1.0;
        for (int i=0; i<tC; i++) { shapes[shapeCount] = tmp[i]; for (int p=0; p<tmp[i].ptCount; p++) { shapes[shapeCount].ptsX[p] = (tmp[i].ptsX[p]-minX)*sf+offX; shapes[shapeCount].ptsY[p] = (tmp[i].ptsY[p]-minY)*sf+offY; } shapeCount++; }
    } UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); free(tmp);
}

// --- Source (.C) File Parser ---
void LoadCFile(const char* path, HWND hwnd) {
    FILE* f = fopen(path, "rb"); if (!f) return; fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); char* d = (char*)malloc(sz + 1); fread(d, 1, sz, f); d[sz] = 0; fclose(f);
    for(int i=0; i<300; i++) { if (parsedIcons[i].shapes) { free(parsedIcons[i].shapes); parsedIcons[i].shapes = NULL; } }
    parsedCount = 0; char* cur = d;
    while ((cur = strstr(cur, "case ")) != NULL && parsedCount < 300) {
        int cId = atoi(cur + 5); char* endBlock = strstr(cur, "break;"); if (!endBlock) endBlock = cur + strlen(cur);
        char name[128] = {0}; char* cmt = strstr(cur, "//"); char* nl = strchr(cur, '\n');
        if (cmt && nl && cmt < nl) { strncpy(name, cmt, 63); char* nl2 = strchr(name, '\n'); if(nl2)*nl2=0; } else strcpy(name, "Icon");
        COLORREF cFill = RGB(128,128,128); int uFill = 1; COLORREF cStroke = RGB(0,0,0); int uStroke = 1; char* st = cur;
        Shape* tmpShapes = (Shape*)calloc(MAX_SHAPES, sizeof(Shape)); int tmpCount = 0;
        
        while (st < endBlock) {
            char* sel = strstr(st, "SelectObject"); char* pt = strstr(st, "POINT ");
            char* line = strstr(st, "L("); char* rectm = strstr(st, "R("); char* ellpm = strstr(st, "E(");
            char* next = sel;
            if (pt && (!next || pt < next)) next = pt; if (line && (!next || line < next)) next = line;
            if (rectm && (!next || rectm < next)) next = rectm; if (ellpm && (!next || ellpm < next)) next = ellpm;
            if (!next || next >= endBlock) break;

            char* lineEnd = strpbrk(next, "\n;");
            char backup = 0;
            if (lineEnd && lineEnd < endBlock) { backup = *lineEnd; *lineEnd = '\0'; }

            if (next == sel) {
                char* tStr = strstr(sel, "t->"); 
                if (tStr) { 
                    char cName[32] = {0}; int idx=0; 
                    while(tStr[idx+3] && isalnum(tStr[idx+3]) && idx<31) { cName[idx]=tStr[idx+3]; idx++; } 
                    if (strstr(cName, "Pen")) { cStroke = ParseColor(cName); uStroke = 1; } else { cFill = ParseColor(cName); uFill = 1; } 
                }
                if (strstr(sel, "NULL_PEN")) uStroke = 0; if (strstr(sel, "NULL_BRUSH")) uFill = 0;
            } else if (next == pt || next == rectm || next == ellpm) {
                Shape s = {0}; s.fill = cFill; s.useFill = uFill; s.stroke = cStroke; s.useStroke = uStroke;
                if (next == pt) {
                    if (lineEnd && backup) { *lineEnd = backup; lineEnd = NULL; }
                    char* pC = next; char* bracket = strchr(next, '}');
                    while (bracket && (pC = strstr(pC, "PT(")) != NULL && pC < bracket) { double px, py; if (sscanf(pC, "PT(%lf,%lf)", &px, &py) == 2) { if (s.ptCount < MAX_POINTS) { s.ptsX[s.ptCount]=px; s.ptsY[s.ptCount]=py; s.ptCount++; } } pC += 3; }
                    char* pEnd = strchr(next, '}'); 
                    if (pEnd) { 
                        char* boundnl = strchr(pEnd, '\n');
                        if (boundnl && boundnl < endBlock) {
                            char nbackup = *boundnl; *boundnl = '\0';
                            char* pl = strstr(pEnd, "Polyline"); char* py = strstr(pEnd, "POLY"); 
                            if (pl && (!py || pl < py)) { s.type = 2; s.useFill = 0; } else s.type = 0; 
                            *boundnl = nbackup;
                        } else s.type = 0;
                    } else s.type = 0;
                }
                else if (next == rectm) { s.type = 0; double l, t, r, b; if (sscanf(next, "R(%lf,%lf,%lf,%lf)", &l, &t, &r, &b) == 4) { s.ptsX[0]=l; s.ptsY[0]=t; s.ptsX[1]=r; s.ptsY[1]=t; s.ptsX[2]=r; s.ptsY[2]=b; s.ptsX[3]=l; s.ptsY[3]=b; s.ptCount = 4; } }
                else if (next == ellpm) { s.type = 0; double l, t, r, b; if (sscanf(next, "E(%lf,%lf,%lf,%lf)", &l, &t, &r, &b) == 4) { double cx = (l+r)/2.0, cy = (t+b)/2.0, rx = fabs(r-l)/2.0, ry = fabs(b-t)/2.0; for (int i=0; i<16; i++) { double ang = i*(2.0*PI/16.0); s.ptsX[s.ptCount]=cx+cos(ang)*rx; s.ptsY[s.ptCount]=cy+sin(ang)*ry; s.ptCount++; } } }
                if (s.ptCount > 0 && tmpCount < MAX_SHAPES) tmpShapes[tmpCount++] = s;
            } else if (next == line) {
                Shape s = {0}; s.type = 1; s.fill = cFill; s.useFill = 0; s.stroke = cStroke; s.useStroke = uStroke;
                double x1, y1, x2, y2; if (sscanf(next, "L(%lf,%lf,%lf,%lf)", &x1, &y1, &x2, &y2) == 4) { s.ptsX[0]=x1; s.ptsY[0]=y1; s.ptsX[1]=x2; s.ptsY[1]=y2; s.ptCount = 2; if (tmpCount < MAX_SHAPES) tmpShapes[tmpCount++] = s; }
            } 
            if (lineEnd && backup) *lineEnd = backup;
            if (next == pt) { char* bracket = strchr(next, '}'); if (bracket) st = bracket + 1; else st = next + 1; }
            else st = next + 1;
        } 
        parsedIcons[parsedCount].caseId = cId; strcpy(parsedIcons[parsedCount].name, name);
        parsedIcons[parsedCount].shapes = tmpShapes; parsedIcons[parsedCount].shapeCount = tmpCount; parsedCount++; cur = endBlock;
    } free(d);
    
    if (parsedCount > 0) { 
        strcpy(loadedCFile, path);
        origFirstCaseId = parsedIcons[0].caseId;
        origLastCaseId = parsedIcons[parsedCount-1].caseId;
        UpdateIconScrollbar();
        currentIconIdx = -1; SwitchToIcon(0);
    } else ShowStatus("Error: No GDI Icons found in C file.");
}

void SaveAsNewCFile(HWND hwnd) {
    OPENFILENAME ofn; char szF[260]={0}; ZeroMemory(&ofn, sizeof(ofn)); 
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szF; ofn.nMaxFile = sizeof(szF); 
    ofn.lpstrFilter = "C Source File\0*.c;*.cpp;*.h\0"; ofn.Flags = OFN_OVERWRITEPROMPT; ofn.lpstrDefExt = "c";
    if (GetSaveFileName(&ofn)) {
        char* b = NULL; GenerateAllCCode(&b); 
        if (b) { 
            FILE* f = fopen(szF, "wb");
            if (f) { fwrite(b, 1, strlen(b), f); fclose(f); strcpy(loadedCFile, szF); ShowStatus("Success: Saved to new C file!"); }
            free(b); 
        }
    }
}

void SaveCFile(HWND hwnd) {
    if (currentCaseId == -1 || strlen(loadedCFile) == 0 || origFirstCaseId == -1 || origLastCaseId == -1) { SaveAsNewCFile(hwnd); return; }
    FILE* f = fopen(loadedCFile, "rb"); if(!f) return; fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    char* data = malloc(sz+1); fread(data,1,sz,f); data[sz]=0; fclose(f);
    
    char srchFirst[32]; sprintf(srchFirst, "case %d:", origFirstCaseId);
    char srchLast[32]; sprintf(srchLast, "case %d:", origLastCaseId);
    char* start = strstr(data, srchFirst); char* lastCase = strstr(data, srchLast);
    char* end = NULL; if (lastCase) end = strstr(lastCase, "break;");
    
    if (!start || !end) { free(data); SaveAsNewCFile(hwnd); return; }
    end += 6; 
    
    char* b = NULL; GenerateAllCCode(&b); 
    if (!b) { free(data); ShowStatus("Error: Export string generation failed!"); return; } 
    
    f = fopen(loadedCFile, "wb"); 
    if (f) {
        fwrite(data, 1, start - data, f); 
        fwrite(b, 1, strlen(b), f); 
        char* aft = end; while(*aft == ' ' || *aft == '\r' || *aft == '\n') aft++; if (*aft == '}') aft++;
        fwrite(aft, 1, strlen(aft), f); 
        fclose(f); 
        CommitCurrentIcon();
        origFirstCaseId = parsedIcons[0].caseId; origLastCaseId = parsedIcons[parsedCount-1].caseId;
        ShowStatus("Success: Icons saved and spliced into original file!");
    }
    free(b); free(data); 
}

// --- Math & Vector Union Tool ---
int IsPointOnPolyEdge(double px, double py, Shape* s) {
    for(int i=0; i<s->ptCount; i++) {
        int j = (i+1)%s->ptCount;
        double x1 = s->ptsX[i], y1 = s->ptsY[i], x2 = s->ptsX[j], y2 = s->ptsY[j];
        double l2 = pow(x2-x1, 2) + pow(y2-y1, 2); if (l2 < 1e-7) continue;
        if (fabs((px-x1)*(y2-y1) - (py-y1)*(x2-x1)) / sqrt(l2) < 1e-5) {
            double t = ((px-x1)*(x2-x1) + (py-y1)*(y2-y1)) / l2;
            if (t >= -1e-5 && t <= 1 + 1e-5) return 1;
        }
    } return 0;
}

void AddEdgesFromShape(Shape* s1, Shape* s2, Edge* pool, int* edgeCount) {
    for (int i = 0; i < s1->ptCount; i++) {
        double p1x = s1->ptsX[i], p1y = s1->ptsY[i];
        double p2x = s1->ptsX[(i+1)%s1->ptCount], p2y = s1->ptsY[(i+1)%s1->ptCount];
        double nx[MAX_POINTS], ny[MAX_POINTS]; int nCnt = 0;
        nx[nCnt]=p1x; ny[nCnt++]=p1y;
        
        for (int j = 0; j < s2->ptCount; j++) {
            double p3x = s2->ptsX[j], p3y = s2->ptsY[j];
            double p4x = s2->ptsX[(j+1)%s2->ptCount], p4y = s2->ptsY[(j+1)%s2->ptCount];
            double d = (p1x-p2x)*(p3y-p4y) - (p1y-p2y)*(p3x-p4x);
            if (fabs(d) > 1e-7) {
                double t = ((p1x-p3x)*(p3y-p4y) - (p1y-p3y)*(p3x-p4x)) / d;
                double u = ((p1x-p3x)*(p1y-p2y) - (p1y-p3y)*(p1x-p2x)) / d;
                if (t > 1e-5 && t < 1 - 1e-5 && u > 1e-5 && u < 1 - 1e-5) { nx[nCnt] = p1x + t*(p2x-p1x); ny[nCnt++] = p1y + t*(p2y-p1y); }
            }
        }
        
        for (int j = 0; j < s2->ptCount; j++) {
            double px = s2->ptsX[j], py = s2->ptsY[j];
            double l2 = pow(p2x-p1x, 2) + pow(p2y-p1y, 2);
            if (l2 > 1e-7 && fabs((px-p1x)*(p2y-p1y) - (py-p1y)*(p2x-p1x)) / sqrt(l2) < 1e-5) {
                double t = ((px-p1x)*(p2x-p1x) + (py-p1y)*(p2y-p1y)) / l2;
                if (t > 1e-5 && t < 1 - 1e-5) { nx[nCnt] = px; ny[nCnt++] = py; }
            }
        }

        nx[nCnt]=p2x; ny[nCnt++]=p2y;
        for(int a=0; a<nCnt-1; a++) { for(int b=a+1; b<nCnt; b++) { if (pow(nx[b]-p1x,2) + pow(ny[b]-p1y,2) < pow(nx[a]-p1x,2) + pow(ny[a]-p1y,2)) { double tx = nx[a], ty = ny[a]; nx[a] = nx[b]; ny[a] = ny[b]; nx[b] = tx; ny[b] = ty; } } }
        for(int a=0; a<nCnt-1; a++) {
            if (pow(nx[a]-nx[a+1], 2) + pow(ny[a]-ny[a+1], 2) < 1e-7) continue;
            double midX = (nx[a]+nx[a+1])/2.0; double midY = (ny[a]+ny[a+1])/2.0;
            if (!PointInPolyShape(midX, midY, s2) || IsPointOnPolyEdge(midX, midY, s2)) { 
                pool[*edgeCount].x1 = nx[a]; pool[*edgeCount].y1 = ny[a]; pool[*edgeCount].x2 = nx[a+1]; pool[*edgeCount].y2 = ny[a+1]; (*edgeCount)++; 
            }
        }
    }
}

// --- Render Helper ---
void RenderShapes(HDC dc, Shape* sArr, int sCnt, int sc, Shape* activeShape, int isActDrawing) {
    for (int i = 0; i <= sCnt; i++) {
        Shape* s = (i == sCnt) ? (isActDrawing ? activeShape : NULL) : &sArr[i]; if (!s || s->ptCount == 0) continue;
        if (s->type == 0 || s->type == 2) {
            HBRUSH b = s->useFill ? CreateSolidBrush(s->fill) : (HBRUSH)GetStockObject(NULL_BRUSH);
            HPEN p = s->useStroke ? CreatePen(PS_SOLID, (s->type == 2 && sc>1) ? 3 : 1, s->stroke) : (HPEN)GetStockObject(NULL_PEN);
            if (i == sCnt) { if(!s->useStroke) p = CreatePen(PS_DASH, 1, RGB(255,255,0)); else { DeleteObject(p); p = CreatePen(PS_DASH, (s->type == 2 && sc>1) ? 3 : 1, s->stroke); } }
            HGDIOBJ ob = SelectObject(dc, b); HGDIOBJ op = SelectObject(dc, p);
            POINT pA[MAX_POINTS]; for(int j=0; j<s->ptCount; j++) { pA[j].x = (LONG)round(s->ptsX[j] * sc); pA[j].y = (LONG)round(s->ptsY[j] * sc); }
            if (s->type == 0) { SetPolyFillMode(dc, ALTERNATE); Polygon(dc, pA, s->ptCount); } else Polyline(dc, pA, s->ptCount);
            SelectObject(dc, ob); SelectObject(dc, op);
            if (s->useFill) DeleteObject(b); if (s->useStroke || i == sCnt) DeleteObject(p);
        } else {
            HPEN p = s->useStroke ? CreatePen(PS_SOLID, (sc>1)?3:1, s->stroke) : (HPEN)GetStockObject(NULL_PEN);
            if (i == sCnt && !s->useStroke) p = CreatePen(PS_DASH, 1, RGB(255,255,0));
            HGDIOBJ op = SelectObject(dc, p);
            MoveToEx(dc, (LONG)round(s->ptsX[0] * sc), (LONG)round(s->ptsY[0] * sc), NULL); LineTo(dc, (LONG)round(s->ptsX[1] * sc), (LONG)round(s->ptsY[1] * sc));
            SelectObject(dc, op); if (s->useStroke || i == sCnt) DeleteObject(p);
        }
    }
}

int HandlePaletteClick(HWND hwnd, int x, int y, int isLeft) {
    int cx = canvasSize + 15;
    if (x >= cx && x < cx + 256 && y >= 110 && y < 142) {
        int col = (x - cx) / 32, row = (y - 110) / 16, index = row * 8 + col;
        if (index >= 0 && index < 16) {
            if (isLeft) { currentFill = palette[index]; useFill = 1; }
            else { currentStroke = palette[index]; useStroke = 1; }
            if (selectedShape != -1) {
                SaveState();
                if (isLeft) { shapes[selectedShape].fill = currentFill; shapes[selectedShape].useFill = 1; }
                else { shapes[selectedShape].stroke = currentStroke; shapes[selectedShape].useStroke = 1; }
            }
            InvalidateRect(hwnd, NULL, FALSE); return 1;
        }
    }
    if (x >= cx && x < cx + 256 && y >= 180 && y < 210) {
        if (isLeft) useFill = 0; else useStroke = 0;
        if (selectedShape != -1) {
            SaveState();
            if (isLeft) shapes[selectedShape].useFill = 0;
            else shapes[selectedShape].useStroke = 0;
        }
        InvalidateRect(hwnd, NULL, FALSE); return 1;
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RECT rCanv = {0, 0, canvasSize, canvasSize};
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX ic = {sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES}; InitCommonControlsEx(&ic); InitFont();
            hStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, NULL, NULL, NULL); UpdateStatusBar();
            
            CreateWindow("STATIC", "Vertices:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)103, NULL, NULL);
            hTrkSides = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)104, NULL, NULL); SendMessage(hTrkSides, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200)); SendMessage(hTrkSides, TBM_SETPOS, TRUE, 100);
            CreateWindow("STATIC", "Depth:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)105, NULL, NULL);
            hTrkStar  = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)106, NULL, NULL); SendMessage(hTrkStar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200)); SendMessage(hTrkStar, TBM_SETPOS, TRUE, 100);
            CreateWindow("STATIC", "Item Scale:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)107, NULL, NULL);
            hTrkShapeScale = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)108, NULL, NULL); SendMessage(hTrkShapeScale, TBM_SETRANGE, TRUE, MAKELPARAM(10, 300)); SendMessage(hTrkShapeScale, TBM_SETPOS, TRUE, 100);
            hChkSnap = CreateWindow("BUTTON", "Snap to Grid", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 0,0,1,1, hwnd, (HMENU)110, NULL, NULL); SendMessage(hChkSnap, BM_SETCHECK, BST_CHECKED, 0);

            CreateWindow("STATIC", "Opacity:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)101, NULL, NULL);
            hTrackbar = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)102, NULL, NULL); SendMessage(hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255)); SendMessage(hTrackbar, TBM_SETPOS, TRUE, 128);
            CreateWindow("STATIC", "Ref Scale:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)111, NULL, NULL);
            hTrkScale = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)121, NULL, NULL); SendMessage(hTrkScale, TBM_SETRANGE, TRUE, MAKELPARAM(10, 190)); SendMessage(hTrkScale, TBM_SETPOS, TRUE, 100);
            CreateWindow("STATIC", "Ref X Pos:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)112, NULL, NULL);
            hTrkPosX = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)122, NULL, NULL); SendMessage(hTrkPosX, TBM_SETRANGE, TRUE, MAKELPARAM(10, 190)); SendMessage(hTrkPosX, TBM_SETPOS, TRUE, 100);
            CreateWindow("STATIC", "Ref Y Pos:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)113, NULL, NULL);
            hTrkPosY = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)123, NULL, NULL); SendMessage(hTrkPosY, TBM_SETRANGE, TRUE, MAKELPARAM(10, 190)); SendMessage(hTrkPosY, TBM_SETPOS, TRUE, 100);
            CreateWindow("STATIC", "Ref X Str:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)114, NULL, NULL);
            hTrkStrX = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)124, NULL, NULL); SendMessage(hTrkStrX, TBM_SETRANGE, TRUE, MAKELPARAM(10, 190)); SendMessage(hTrkStrX, TBM_SETPOS, TRUE, 100);
            CreateWindow("STATIC", "Ref Y Str:", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)115, NULL, NULL);
            hTrkStrY = CreateWindow(TRACKBAR_CLASS, "Trackbar", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)125, NULL, NULL); SendMessage(hTrkStrY, TBM_SETRANGE, TRUE, MAKELPARAM(10, 190)); SendMessage(hTrkStrY, TBM_SETPOS, TRUE, 100);

            const char* bT[] = {"Select / Edit", "Rotate", "Scale", "Polygon", "Line", "Polyline", "Shapes", "Text", "Flood Fill", "Undo", "Clear", "Delete", "Import SVG", "Open Ref Img", "Open .C", "Save .C", "Export Code", "Merge Shapes", "Move Up", "Move Down", "Align Vert", "Align Horz", "Set Dist", "Set Width", "Set Height"};
            for(int i=0; i<25; i++) hBtn[i] = CreateWindow("BUTTON", bT[i], WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)(200+i), NULL, NULL);
            
            hScrlIcon = CreateWindow("SCROLLBAR", "", WS_CHILD|WS_VISIBLE|SBS_HORZ, 0,0,1,1, hwnd, (HMENU)150, NULL, NULL);
            hBtnAddIcon = CreateWindow("BUTTON", "+", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)151, NULL, NULL);
            hBtnDelIcon = CreateWindow("BUTTON", "-", WS_CHILD|WS_VISIBLE, 0,0,1,1, hwnd, (HMENU)152, NULL, NULL);
            
            hDistEdit = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 60, 20, hwnd, (HMENU)300, NULL, NULL);
            oldEditProc = (WNDPROC)SetWindowLongPtr(hDistEdit, GWLP_WNDPROC, (LONG_PTR)DistEditProc);
            break;
        }
        case WM_APP+1: {
            if (editMode != 0 && IsWindowVisible(hDistEdit)) {
                char buf[32]; GetWindowText(hDistEdit, buf, 32); double newVal = atof(buf);
                SaveState();
                
                if (editMode == 1 && distMoveS != -1) {
                    shapes[distMoveS].ptsX[distMoveP] = shapes[distAnchorS].ptsX[distAnchorP] + cos(distOriginalAngle) * newVal;
                    shapes[distMoveS].ptsY[distMoveP] = shapes[distAnchorS].ptsY[distAnchorP] + sin(distOriginalAngle) * newVal;
                } else if (editMode == 2 && editTargetShape != -1) {
                    if (editOldVal > 0) {
                        double scale = newVal / editOldVal;
                        for(int p=0; p<shapes[editTargetShape].ptCount; p++) {
                            double nx = editCx + (shapes[editTargetShape].ptsX[p] - editCx) * scale;
                            shapes[editTargetShape].ptsX[p] = snapToGrid ? round(nx) : nx;
                        }
                    }
                } else if (editMode == 3 && editTargetShape != -1) {
                    if (editOldVal > 0) {
                        double scale = newVal / editOldVal;
                        for(int p=0; p<shapes[editTargetShape].ptCount; p++) {
                            double ny = editCy + (shapes[editTargetShape].ptsY[p] - editCy) * scale;
                            shapes[editTargetShape].ptsY[p] = snapToGrid ? round(ny) : ny;
                        }
                    }
                }
                
                ShowWindow(hDistEdit, SW_HIDE); editMode = 0; 
                UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); SetFocus(hwnd);
            } break;
        }
        case WM_SIZE: {
            clientW = LOWORD(lParam); clientH = HIWORD(lParam); SendMessage(hStatus, WM_SIZE, 0, 0); clientH -= 20; 
            int bottomPanelH = 100;
            int avail = (clientW - PANEL_WIDTH < clientH - bottomPanelH) ? (clientW - PANEL_WIDTH) : (clientH - bottomPanelH);
            scaleFactor = avail / GRID_SIZE; if (scaleFactor < 1) scaleFactor = 1; canvasSize = scaleFactor * GRID_SIZE;
            int cx = canvasSize + 15, w = PANEL_WIDTH - 30, by = 220;
            
            MoveWindow(GetDlgItem(hwnd, 103), cx, by, 70, 20, TRUE); MoveWindow(hTrkSides, cx+70, by-4, w-70, 24, TRUE); by += 22;
            MoveWindow(GetDlgItem(hwnd, 105), cx, by, 70, 20, TRUE); MoveWindow(hTrkStar, cx+70, by-4, w-70, 24, TRUE); by += 22;
            MoveWindow(GetDlgItem(hwnd, 107), cx, by, 70, 20, TRUE); MoveWindow(hTrkShapeScale, cx+70, by-4, w-70, 24, TRUE); by += 22;
            MoveWindow(hChkSnap, cx, by, 150, 20, TRUE); by += 24;

            for(int i=0; i<25; i++) { MoveWindow(hBtn[i], cx + (i%2)*(w/2 + 2), by + (i/2)*28, (w/2)-4, 25, TRUE); }
            
            int botY = canvasSize + 15;
            int bottomAvailWidth = canvasSize; 
            int sw = (bottomAvailWidth - 20) / 3; 

            MoveWindow(GetDlgItem(hwnd, 101), 10, botY, 60, 20, TRUE); 
            MoveWindow(hTrackbar, 70, botY-4, sw - 60, 24, TRUE);
            
            MoveWindow(GetDlgItem(hwnd, 111), 10 + sw, botY, 70, 20, TRUE); 
            MoveWindow(hTrkScale, 10 + sw + 70, botY-4, sw - 70, 24, TRUE);
            
            MoveWindow(GetDlgItem(hwnd, 112), 10 + sw*2, botY, 70, 20, TRUE); 
            MoveWindow(hTrkPosX, 10 + sw*2 + 70, botY-4, bottomAvailWidth - (10 + sw*2 + 70), 24, TRUE); 
            botY += 26;
            
            MoveWindow(GetDlgItem(hwnd, 114), 10, botY, 70, 20, TRUE); 
            MoveWindow(hTrkStrX, 80, botY-4, sw - 70, 24, TRUE);
            
            MoveWindow(GetDlgItem(hwnd, 115), 10 + sw, botY, 70, 20, TRUE); 
            MoveWindow(hTrkStrY, 10 + sw + 70, botY-4, sw - 70, 24, TRUE);
            
            MoveWindow(GetDlgItem(hwnd, 113), 10 + sw*2, botY, 70, 20, TRUE); 
            MoveWindow(hTrkPosY, 10 + sw*2 + 70, botY-4, bottomAvailWidth - (10 + sw*2 + 70), 24, TRUE); 
            botY += 30;
            
            MoveWindow(hBtnAddIcon, 10, botY, 24, 24, TRUE); 
            MoveWindow(hScrlIcon, 38, botY, bottomAvailWidth - 76, 24, TRUE); 
            MoveWindow(hBtnDelIcon, bottomAvailWidth - 34, botY, 24, 24, TRUE);

            InvalidateRect(hwnd, NULL, FALSE); break;
        }
        case WM_CHAR: {
            if (currentMode == 4 && textCursorActive) {
                char c = toupper((char)wParam);
                if (c == 13) { textCursorY += 6; textCursorX = startX; } 
                else if (c == 8) { textCursorX = max(0, textCursorX - 4); } 
                else if (font5x3[c] || c == ' ') {
                    if (c != ' ') {
                        SaveState(); int glyph = font5x3[c];
                        for(int r=0; r<5; r++) { 
                            int rowVal = (glyph >> ((4-r)*4)) & 0xF; int start_c = -1;
                            for(int c_idx=0; c_idx<=3; c_idx++) {
                                int bit = (c_idx < 3) ? ((rowVal >> (2 - c_idx)) & 1) : 0;
                                if (bit && start_c == -1) start_c = c_idx;
                                else if (!bit && start_c != -1) {
                                    Shape s = {0}; s.type = 0; s.ptCount = 4;
                                    s.fill = currentFill; s.stroke = currentStroke; s.useFill = useFill; s.useStroke = useStroke;
                                    s.ptsX[0] = textCursorX + start_c; s.ptsY[0] = textCursorY + r; s.ptsX[1] = textCursorX + c_idx; s.ptsY[1] = textCursorY + r;
                                    s.ptsX[2] = textCursorX + c_idx; s.ptsY[2] = textCursorY + r + 1; s.ptsX[3] = textCursorX + start_c; s.ptsY[3] = textCursorY + r + 1;
                                    if(shapeCount < MAX_SHAPES) shapes[shapeCount++] = s; start_c = -1;
                                }
                            }
                        }
                    } textCursorX += 4; UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE);
                }
            } break;
        }
        case WM_KEYDOWN: {
            if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) Undo(hwnd);
            if (wParam == VK_ESCAPE) { 
                if (editMode != 0 && IsWindowVisible(hDistEdit)) { ShowWindow(hDistEdit, SW_HIDE); editMode = 0; InvalidateRect(hwnd, NULL, FALSE); return 0; }
                if (isDrawing) { isDrawing = 0; currentShape.ptCount = 0; }
                if (shapeCount > 0) currentMode = 6; ClearSelection(); InvalidateRect(hwnd, NULL, FALSE); 
            }
            if (wParam == VK_RETURN) {
                if (currentMode == 6 && selOrderCount == 2) {
                    int anchorS = selOrderS[0], anchorP = selOrderP[0];
                    int moveS = selOrderS[1], moveP = selOrderP[1];
                    double dx = shapes[moveS].ptsX[moveP] - shapes[anchorS].ptsX[anchorP];
                    double dy = shapes[moveS].ptsY[moveP] - shapes[anchorS].ptsY[anchorP];
                    double dist = sqrt(dx*dx + dy*dy); distOriginalAngle = atan2(dy, dx);
                    distMoveS = moveS; distMoveP = moveP; distAnchorS = anchorS; distAnchorP = anchorP;
                    
                    editMode = 1;
                    char buf[32]; snprintf(buf, 32, "%.2f", dist); SetWindowText(hDistEdit, buf);
                    int midX = (int)((shapes[anchorS].ptsX[anchorP] + shapes[moveS].ptsX[moveP]) / 2.0 * scaleFactor);
                    int midY = (int)((shapes[anchorS].ptsY[anchorP] + shapes[moveS].ptsY[moveP]) / 2.0 * scaleFactor);
                    MoveWindow(hDistEdit, midX - 30, midY - 10, 60, 20, TRUE); 
                    EnableWindow(hDistEdit, TRUE); ShowWindow(hDistEdit, SW_SHOW); 
                    SetFocus(hDistEdit); SendMessage(hDistEdit, EM_SETSEL, 0, -1);
                    return 0; 
                }
                if (isDrawing && (currentMode == 0 || currentMode == 2) && currentShape.ptCount >= 2) { SaveState(); shapes[shapeCount++] = currentShape; isDrawing = 0; currentShape.ptCount = 0; selectedShape = shapeCount - 1; currentMode = 6; UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); }
            }
            if (wParam == VK_DELETE || wParam == VK_BACK) {
                if (isDrawing && currentShape.ptCount > 0) { currentShape.ptCount--; if (currentShape.ptCount == 0) isDrawing = 0; InvalidateRect(hwnd, NULL, FALSE); }
                else if (currentMode == 6 || currentMode == 7 || currentMode == 8) {
                    if (selOrderCount > 0) {
                        SaveState();
                        for (int i=shapeCount-1; i>=0; i--) {
                            for (int k = shapes[i].ptCount - 1; k >= 0; k--) {
                                if (ptSelected[i][k]) {
                                    for(int j = k; j < shapes[i].ptCount - 1; j++) { shapes[i].ptsX[j] = shapes[i].ptsX[j+1]; shapes[i].ptsY[j] = shapes[i].ptsY[j+1]; }
                                    shapes[i].ptCount--;
                                }
                            }
                            if (shapes[i].ptCount < (shapes[i].type==0?3:2)) { for(int k=i; k<shapeCount-1; k++) shapes[k] = shapes[k+1]; shapeCount--; }
                        }
                        ClearSelection(); selectedShape = -1; hoverShape = -1; selectedPtIdx = -1; UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            } break;
        }
        case WM_LBUTTONDBLCLK: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            int cx = canvasSize + 15;
            if (x >= cx && x < cx + 256 && y >= 110 && y < 110 + 32) {
                int col = (x - cx) / 32, row = (y - 110) / 16, index = row * 8 + col;
                if (index >= 0 && index < 16) {
                    CHOOSECOLOR cc; static COLORREF acrCustClr[16];
                    ZeroMemory(&cc, sizeof(cc));
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.lpCustColors = acrCustClr;
                    cc.rgbResult = palette[index];
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColor(&cc)) {
                        palette[index] = cc.rgbResult;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            }
            break;
        }
        case WM_COMMAND: {
            int cmdId = LOWORD(wParam); 
            if (cmdId < 200) SetFocus(hwnd); 
            if (cmdId == 110) { snapToGrid = SendMessage(hChkSnap, BM_GETCHECK, 0, 0) == BST_CHECKED; SetFocus(hwnd); }
            if (cmdId == 151) {
                if (parsedCount >= 300) return 0;
                CommitCurrentIcon(); 
                int newId = 1; for(int i=0; i<parsedCount; i++) if(parsedIcons[i].caseId >= newId) newId = parsedIcons[i].caseId + 1;
                parsedIcons[parsedCount].caseId = newId; strcpy(parsedIcons[parsedCount].name, "New"); parsedIcons[parsedCount].shapes = NULL; parsedIcons[parsedCount].shapeCount = 0; parsedCount++;
                UpdateIconScrollbar(); SwitchToIcon(parsedCount - 1);
            }
            if (cmdId == 152) {
                if (parsedCount > 0 && currentIconIdx >= 0) {
                    if (parsedIcons[currentIconIdx].shapes) free(parsedIcons[currentIconIdx].shapes);
                    for(int i=currentIconIdx; i<parsedCount-1; i++) parsedIcons[i] = parsedIcons[i+1];
                    parsedCount--;
                    if (parsedCount == 0) { shapeCount = 0; currentCaseId = 1; currentIconIdx = -1; UpdateIconScrollbar(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); } 
                    else { UpdateIconScrollbar(); int nIdx = currentIconIdx >= parsedCount ? parsedCount - 1 : currentIconIdx; currentIconIdx = -1; SwitchToIcon(nIdx); }
                }
            }
            
            if (cmdId >= 200) {
                int id = cmdId - 200;
                int btnMap[] = {6, 7, 8, 0, 1, 2, 3, 4, 5};
                if (id >= 0 && id <= 8) { currentMode = btnMap[id]; isDrawing = 0; currentShape.ptCount = 0; selectedShape = -1; selectedPtIdx = -1; textCursorActive = 0; ClearSelection(); }
                if (id == 9) Undo(hwnd); if (id == 10) { SaveState(); shapeCount = 0; hRefBmp = NULL; selectedShape = -1; textCursorActive = 0; ClearSelection(); UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); }
                if (id == 11) SendMessage(hwnd, WM_KEYDOWN, VK_DELETE, 0); 
                if (id == 12) { OPENFILENAME ofn; char szF[260]={0}; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szF; ofn.nMaxFile = sizeof(szF); ofn.lpstrFilter = "SVG Vector\0*.svg\0"; ofn.Flags = OFN_FILEMUSTEXIST; if (GetOpenFileName(&ofn)) ParseSVG(szF, hwnd); }
                if (id == 13) { 
                    OPENFILENAME ofn; char szF[260]={0}; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szF; ofn.nMaxFile = sizeof(szF); ofn.lpstrFilter = "Ref Image\0*.bmp;*.exe;*.ico\0"; ofn.Flags = OFN_FILEMUSTEXIST; 
                    if (GetOpenFileName(&ofn)) { 
                        if (hRefBmp) DeleteObject(hRefBmp); if (hRefIcon) DestroyIcon(hRefIcon); hRefBmp = NULL; hRefIcon = NULL; 
                        if (strstr(szF, ".exe") || strstr(szF, ".ico")) ExtractIconEx(szF, 0, &hRefIcon, NULL, 1); 
                        else hRefBmp = (HBITMAP)LoadImage(NULL, szF, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE); 
                        absRefScale = 1.0; absRefPosX = 0.0; absRefPosY = 0.0; absRefStrX = 1.0; absRefStrY = 1.0;
                        SendMessage(hTrkScale, TBM_SETPOS, TRUE, 100); SendMessage(hTrkPosX, TBM_SETPOS, TRUE, 100); SendMessage(hTrkPosY, TBM_SETPOS, TRUE, 100); SendMessage(hTrkStrX, TBM_SETPOS, TRUE, 100); SendMessage(hTrkStrY, TBM_SETPOS, TRUE, 100);
                        InvalidateRect(hwnd, NULL, FALSE); 
                    } 
                }
                if (id == 14) { OPENFILENAME ofn; char szF[260]={0}; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFile = szF; ofn.nMaxFile = sizeof(szF); ofn.lpstrFilter = "C Source File\0*.c;*.cpp;*.h\0"; ofn.Flags = OFN_FILEMUSTEXIST; if (GetOpenFileName(&ofn)) LoadCFile(szF, hwnd); }
                if (id == 15) SaveCFile(hwnd); if (id == 16) ExportCode(hwnd);
                if (id == 17) {
                    int selShapes[MAX_SHAPES]; int selCount = 0;
                    for(int i=0; i<shapeCount; i++) { for(int p=0; p<shapes[i].ptCount; p++) { if(ptSelected[i][p]) { selShapes[selCount++] = i; break; } } }
                    if (selCount == 1 && shapes[selShapes[0]].type == 0) { SaveState(); SimplifyShapeDP(&shapes[selShapes[0]], 0.05); UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); }
                    else if (selCount == 2 && shapes[selShapes[0]].type == 0 && shapes[selShapes[1]].type == 0) {
                        SaveState(); Edge pool[4000]; int edgeCount = 0;
                        AddEdgesFromShape(&shapes[selShapes[0]], &shapes[selShapes[1]], pool, &edgeCount); 
                        AddEdgesFromShape(&shapes[selShapes[1]], &shapes[selShapes[0]], pool, &edgeCount);
                        
                        int keep[4000]; for(int i=0; i<edgeCount; i++) keep[i] = 1;
                        for(int i=0; i<edgeCount; i++) {
                            if (!keep[i]) continue;
                            for(int j=i+1; j<edgeCount; j++) {
                                if (!keep[j]) continue;
                                int same = (fabs(pool[i].x1 - pool[j].x1) < 1e-5 && fabs(pool[i].y1 - pool[j].y1) < 1e-5 && fabs(pool[i].x2 - pool[j].x2) < 1e-5 && fabs(pool[i].y2 - pool[j].y2) < 1e-5);
                                int opp  = (fabs(pool[i].x1 - pool[j].x2) < 1e-5 && fabs(pool[i].y1 - pool[j].y2) < 1e-5 && fabs(pool[i].x2 - pool[j].x1) < 1e-5 && fabs(pool[i].y2 - pool[j].y1) < 1e-5);
                                if (same || opp) { keep[i] = 0; keep[j] = 0; break; }
                            }
                        }
                        
                        Edge filtered[4000]; int fCount = 0;
                        for(int i=0; i<edgeCount; i++) if (keep[i]) filtered[fCount++] = pool[i];
                        
                        if (fCount > 0) {
                            Shape merged = shapes[selShapes[0]]; merged.ptCount = 0; 
                            int used[4000] = {0}; int loopsFound = 0;
                            
                            while(1) {
                                int firstEdge = -1;
                                for(int j=0; j<fCount; j++) if(!used[j]) { firstEdge = j; break; }
                                if (firstEdge == -1) break;
                                
                                double cx = filtered[firstEdge].x1, cy = filtered[firstEdge].y1;
                                int loopStartIdx = merged.ptCount;
                                
                                while(1) {
                                    if (merged.ptCount >= MAX_POINTS - 2) break;
                                    merged.ptsX[merged.ptCount] = cx; merged.ptsY[merged.ptCount] = cy; merged.ptCount++;
                                    int found = -1;
                                    for (int j=0; j<fCount; j++) {
                                        if (!used[j]) {
                                            if (fabs(filtered[j].x1 - cx) < 1e-5 && fabs(filtered[j].y1 - cy) < 1e-5) { found = j; cx = filtered[j].x2; cy = filtered[j].y2; break; }
                                            if (fabs(filtered[j].x2 - cx) < 1e-5 && fabs(filtered[j].y2 - cy) < 1e-5) { found = j; cx = filtered[j].x1; cy = filtered[j].y1; break; }
                                        }
                                    }
                                    if (found != -1) used[found] = 1; else break;
                                }
                                
                                if (loopsFound > 0 && merged.ptCount < MAX_POINTS - 2) {
                                    merged.ptsX[merged.ptCount] = merged.ptsX[loopStartIdx]; merged.ptsY[merged.ptCount] = merged.ptsY[loopStartIdx]; merged.ptCount++;
                                    merged.ptsX[merged.ptCount] = merged.ptsX[loopStartIdx - 1]; merged.ptsY[merged.ptCount] = merged.ptsY[loopStartIdx - 1]; merged.ptCount++;
                                }
                                loopsFound++;
                            }
                            
                            int cleanCnt = 0; double clnX[MAX_POINTS], clnY[MAX_POINTS];
                            for (int i=0; i<merged.ptCount; i++) { if (cleanCnt == 0 || pow(merged.ptsX[i]-clnX[cleanCnt-1], 2) + pow(merged.ptsY[i]-clnY[cleanCnt-1], 2) > 1e-5) { clnX[cleanCnt] = merged.ptsX[i]; clnY[cleanCnt++] = merged.ptsY[i]; } }
                            if (cleanCnt > 1 && pow(clnX[0]-clnX[cleanCnt-1], 2) + pow(clnY[0]-clnY[cleanCnt-1], 2) < 1e-5) cleanCnt--;
                            merged.ptCount = cleanCnt; for(int i=0; i<cleanCnt; i++) { merged.ptsX[i]=clnX[i]; merged.ptsY[i]=clnY[i]; }
                            
                            shapes[selShapes[0]] = merged;
                            for(int k=selShapes[1]; k<shapeCount-1; k++) shapes[k] = shapes[k+1]; shapeCount--; 
                            ClearSelection(); selectedShape = selShapes[0]; for(int p=0; p<shapes[selectedShape].ptCount; p++) ToggleSelection(selectedShape, p);
                        } UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                if (id == 18) { if (selectedShape != -1 && selectedShape < shapeCount - 1) { SaveState(); Shape temp = shapes[selectedShape]; shapes[selectedShape] = shapes[selectedShape + 1]; shapes[selectedShape + 1] = temp; selectedShape++; UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); } }
                if (id == 19) { if (selectedShape > 0) { SaveState(); Shape temp = shapes[selectedShape]; shapes[selectedShape] = shapes[selectedShape - 1]; shapes[selectedShape - 1] = temp; selectedShape--; UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); } }
                if (id == 20) { if (selOrderCount == 2) { SaveState(); shapes[selOrderS[1]].ptsX[selOrderP[1]] = shapes[selOrderS[0]].ptsX[selOrderP[0]]; UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); } } // Align Vert
                if (id == 21) { if (selOrderCount == 2) { SaveState(); shapes[selOrderS[1]].ptsY[selOrderP[1]] = shapes[selOrderS[0]].ptsY[selOrderP[0]]; UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); } } // Align Horz
                if (id == 22) { if (currentMode == 6 && selOrderCount == 2) { SendMessage(hwnd, WM_KEYDOWN, VK_RETURN, 0); } } // Set Dist
                if (id == 23 || id == 24) { 
                    if (selectedShape != -1 && shapes[selectedShape].ptCount > 0) {
                        double minX = 9999, maxX = -9999, minY = 9999, maxY = -9999;
                        for(int p=0; p<shapes[selectedShape].ptCount; p++) { minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]); minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]); }
                        editCx = (minX + maxX) / 2.0; editCy = (minY + maxY) / 2.0; editTargetShape = selectedShape;
                        double val = (id == 23) ? (maxX - minX) : (maxY - minY); editOldVal = val; editMode = (id == 23) ? 2 : 3;
                        char buf[32]; snprintf(buf, 32, "%.2f", val); SetWindowText(hDistEdit, buf);
                        int midX = (int)(editCx * scaleFactor); int midY = (int)(editCy * scaleFactor);
                        MoveWindow(hDistEdit, midX - 30, midY - 10, 60, 20, TRUE); EnableWindow(hDistEdit, TRUE); ShowWindow(hDistEdit, SW_SHOW); SetFocus(hDistEdit); SendMessage(hDistEdit, EM_SETSEL, 0, -1);
                    }
                }
            } break;
        }
        case WM_HSCROLL: {
            HWND hTrk = (HWND)lParam;
            if (hTrk == hScrlIcon) {
                SCROLLINFO si; si.cbSize = sizeof(si); si.fMask = SIF_ALL; GetScrollInfo(hScrlIcon, SB_CTL, &si);
                int pos = si.nPos;
                switch(LOWORD(wParam)) { 
                    case SB_LINELEFT: pos--; break; 
                    case SB_LINERIGHT: pos++; break; 
                    case SB_PAGELEFT: pos-=si.nPage; break; 
                    case SB_PAGERIGHT: pos+=si.nPage; break; 
                    case SB_THUMBTRACK: 
                    case SB_THUMBPOSITION: pos = si.nTrackPos; break; 
                }
                if (pos < si.nMin) pos = si.nMin; 
                if (pos > si.nMax - (int)si.nPage + 1) pos = si.nMax - si.nPage + 1;
                SwitchToIcon(pos);
            }
            else if (hTrk == hTrackbar) refAlpha = SendMessage(hTrackbar, TBM_GETPOS, 0, 0);
            else if (hTrk == hTrkScale) { int p = SendMessage(hTrkScale, TBM_GETPOS, 0, 0); absRefScale *= (p / 100.0); }
            else if (hTrk == hTrkPosX)  { int p = SendMessage(hTrkPosX, TBM_GETPOS, 0, 0); absRefPosX += (p - 100); }
            else if (hTrk == hTrkPosY)  { int p = SendMessage(hTrkPosY, TBM_GETPOS, 0, 0); absRefPosY += (p - 100); }
            else if (hTrk == hTrkStrX)  { int p = SendMessage(hTrkStrX, TBM_GETPOS, 0, 0); absRefStrX *= (p / 100.0); }
            else if (hTrk == hTrkStrY)  { int p = SendMessage(hTrkStrY, TBM_GETPOS, 0, 0); absRefStrY *= (p / 100.0); }
            else if (hTrk == hTrkSides) {
                static int lastSidesDelta = 0;
                int val = SendMessage(hTrkSides, TBM_GETPOS, 0, 0);
                int delta = (val - 100) / 10;
                if (delta != lastSidesDelta) {
                    if (lastSidesDelta == 0) SaveState();
                    paramSides += (delta - lastSidesDelta); 
                    if (paramSides < 3) paramSides = 3; if (paramSides > 64) paramSides = 64;
                    lastSidesDelta = delta;
                    if (selectedShape != -1 && currentMode == 6 && shapes[selectedShape].type == 0) RegenSelectedShape();
                    UpdateStatusBar();
                }
                if (LOWORD(wParam) == TB_ENDTRACK) { SendMessage(hTrkSides, TBM_SETPOS, TRUE, 100); lastSidesDelta = 0; }
            }
            else if (hTrk == hTrkStar) {
                static int lastStarDelta = 0;
                int val = SendMessage(hTrkStar, TBM_GETPOS, 0, 0);
                int delta = (val - 100) / 2;
                if (delta != lastStarDelta) {
                    if (lastStarDelta == 0) SaveState();
                    paramStar += (delta - lastStarDelta) * 2; 
                    if (paramStar < 10) paramStar = 10; if (paramStar > 100) paramStar = 100;
                    lastStarDelta = delta;
                    if (selectedShape != -1 && currentMode == 6 && shapes[selectedShape].type == 0) RegenSelectedShape();
                    UpdateStatusBar();
                }
                if (LOWORD(wParam) == TB_ENDTRACK) { SendMessage(hTrkStar, TBM_SETPOS, TRUE, 100); lastStarDelta = 0; }
            }
            else if (hTrk == hTrkShapeScale) {
                static int lastShapeScale = 100;
                int val = SendMessage(hTrkShapeScale, TBM_GETPOS, 0, 0);
                if (val != lastShapeScale && selectedShape != -1 && shapes[selectedShape].ptCount > 0) {
                    if (lastShapeScale == 100) SaveState();
                    double sf = (double)val / lastShapeScale; lastShapeScale = val;
                    double minX = 9999, minY = 9999, maxX = -9999, maxY = -9999;
                    for(int p=0; p<shapes[selectedShape].ptCount; p++) { minX = fmin(minX, shapes[selectedShape].ptsX[p]); maxX = fmax(maxX, shapes[selectedShape].ptsX[p]); minY = fmin(minY, shapes[selectedShape].ptsY[p]); maxY = fmax(maxY, shapes[selectedShape].ptsY[p]); }
                    double cx = (minX + maxX) / 2.0; double cy = (minY + maxY) / 2.0;
                    for(int p=0; p<shapes[selectedShape].ptCount; p++) { shapes[selectedShape].ptsX[p] = cx + (shapes[selectedShape].ptsX[p] - cx) * sf; shapes[selectedShape].ptsY[p] = cy + (shapes[selectedShape].ptsY[p] - cy) * sf; }
                }
                if (LOWORD(wParam) == TB_ENDTRACK) { SendMessage(hTrkShapeScale, TBM_SETPOS, TRUE, 100); lastShapeScale = 100; }
            }

            if (LOWORD(wParam) == TB_ENDTRACK && (hTrk == hTrkScale || hTrk == hTrkPosX || hTrk == hTrkPosY || hTrk == hTrkStrX || hTrk == hTrkStrY)) { SendMessage(hTrk, TBM_SETPOS, TRUE, 100); }

            if (isDrawing && currentMode == 3) GenShape(currentEndX, currentEndY);
            SetFocus(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
        }
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam), y = HIWORD(lParam); double gx = Snap((double)x / scaleFactor), gy = Snap((double)y / scaleFactor);
            if (currentMode == 6 || currentMode == 7 || currentMode == 8) {
                if (isDraggingNodes) { 
                    int shiftDown = (GetKeyState(VK_SHIFT) & 0x8000);
                    int ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000);
                    
                    if (isMovingInRotate || currentMode == 6) {
                        if (currentMode == 6 && shiftDown && !ctrlDown && !isDraggingPoint) {
                            double currentAngle = atan2(gy - rotCenterY, gx - rotCenterX); double delta = currentAngle - startRotAngle;
                            if (snapToGrid) delta = round(delta / (PI/12.0)) * (PI/12.0); 
                            if (delta != 0) {
                                if (!shapeWasMoved) { SaveState(); shapeWasMoved = 1; }
                                for(int i=0; i<shapeCount; i++) { for (int p=0; p<shapes[i].ptCount; p++) { if (ptSelected[i][p]) { double d_x = dragStartSnapshot[i].ptsX[p] - rotCenterX; double d_y = dragStartSnapshot[i].ptsY[p] - rotCenterY; shapes[i].ptsX[p] = rotCenterX + d_x*cos(delta) - d_y*sin(delta); shapes[i].ptsY[p] = rotCenterY + d_x*sin(delta) + d_y*cos(delta); } } }
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                        } else {
                            double dx = gx - dragStartX, dy = gy - dragStartY;
                            if (ctrlDown && isDraggingPoint) { if (fabs(dx) > fabs(dy)) dy = 0; else dx = 0; }
                            
                            if (dx != 0 || dy != 0) {
                                if (!shapeWasMoved) { SaveState(); shapeWasMoved = 1; }
                                double minX = GRID_SIZE, maxX = 0, minY = GRID_SIZE, maxY = 0;
                                for(int i=0; i<shapeCount; i++) { for(int p=0; p<shapes[i].ptCount; p++) { if (ptSelected[i][p]) { minX = fmin(minX, dragStartSnapshot[i].ptsX[p]); maxX = fmax(maxX, dragStartSnapshot[i].ptsX[p]); minY = fmin(minY, dragStartSnapshot[i].ptsY[p]); maxY = fmax(maxY, dragStartSnapshot[i].ptsY[p]); } } }
                                if (minX + dx < 0) dx = -minX; if (maxX + dx > GRID_SIZE) dx = GRID_SIZE - maxX; if (minY + dy < 0) dy = -minY; if (maxY + dy > GRID_SIZE) dy = GRID_SIZE - maxY;
                                for(int i=0; i<shapeCount; i++) { for(int p=0; p<shapes[i].ptCount; p++) { if (ptSelected[i][p]) { double nx = dragStartSnapshot[i].ptsX[p] + dx; double ny = dragStartSnapshot[i].ptsY[p] + dy; shapes[i].ptsX[p] = snapToGrid ? round(nx) : nx; shapes[i].ptsY[p] = snapToGrid ? round(ny) : ny; } } }
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                        }
                    } else if (currentMode == 7) { 
                        double currentAngle = atan2(gy - rotCenterY, gx - rotCenterX); double delta = currentAngle - startRotAngle;
                        if (snapToGrid) delta = round(delta / (PI/12.0)) * (PI/12.0); 
                        if (delta != 0) {
                            if (!shapeWasMoved) { SaveState(); shapeWasMoved = 1; }
                            for(int i=0; i<shapeCount; i++) { for (int p=0; p<shapes[i].ptCount; p++) { if (ptSelected[i][p]) { double d_x = dragStartSnapshot[i].ptsX[p] - rotCenterX; double d_y = dragStartSnapshot[i].ptsY[p] - rotCenterY; shapes[i].ptsX[p] = rotCenterX + d_x*cos(delta) - d_y*sin(delta); shapes[i].ptsY[p] = rotCenterY + d_x*sin(delta) + d_y*cos(delta); } } }
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    } else if (currentMode == 8) { 
                        double currentDist = sqrt(pow(gy - rotCenterY, 2) + pow(gx - rotCenterX, 2)); double scale = startScaleDist > 0 ? (currentDist / startScaleDist) : 1.0;
                        if (scale != 1.0) {
                            if (!shapeWasMoved) { SaveState(); shapeWasMoved = 1; }
                            for(int i=0; i<shapeCount; i++) { for (int p=0; p<shapes[i].ptCount; p++) { if (ptSelected[i][p]) { double nx = rotCenterX + (dragStartSnapshot[i].ptsX[p] - rotCenterX) * scale; double ny = rotCenterY + (dragStartSnapshot[i].ptsY[p] - rotCenterY) * scale; shapes[i].ptsX[p] = snapToGrid ? round(nx) : nx; shapes[i].ptsY[p] = snapToGrid ? round(ny) : ny; } } }
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                } else { 
                    hoverShape = hoverPt = hoverSegShape = hoverSegPt = -1; double bestDist = 9999;
                    if (selectedShape != -1) { for(int p=0; p<shapes[selectedShape].ptCount; p++) { double d = sqrt(pow(shapes[selectedShape].ptsX[p]*scaleFactor - x, 2) + pow(shapes[selectedShape].ptsY[p]*scaleFactor - y, 2)); if (d < 15.0 && d < bestDist) { hoverShape = selectedShape; hoverPt = p; bestDist = d; } } }
                    if (hoverPt == -1) { for(int i=0; i<shapeCount; i++) for(int p=0; p<shapes[i].ptCount; p++) { double d = sqrt(pow(shapes[i].ptsX[p]*scaleFactor - x, 2) + pow(shapes[i].ptsY[p]*scaleFactor - y, 2)); if (d < 15.0 && d < bestDist) { hoverShape = i; hoverPt = p; bestDist = d; } } }
                    if (hoverPt == -1) { bestDist = 9999; for(int i=0; i<shapeCount; i++) { if (shapes[i].ptCount >= MAX_POINTS) continue; for(int p=0; p < (shapes[i].type==0 ? shapes[i].ptCount : 1); p++) { int np = (p+1) % shapes[i].ptCount; double prX, prY, d; PtToSegProj((double)x, (double)y, shapes[i].ptsX[p]*scaleFactor, shapes[i].ptsY[p]*scaleFactor, shapes[i].ptsX[np]*scaleFactor, shapes[i].ptsY[np]*scaleFactor, &prX, &prY, &d); if (d < 10.0 && d < bestDist) { hoverSegShape = i; hoverSegPt = p; hoverProjX = prX; hoverProjY = prY; bestDist = d; } } } } 
                    if (hoverPt == -1 && hoverSegShape == -1) { for(int i=shapeCount-1; i>=0; i--) { if (shapes[i].type == 0 && PointInPolyShape(gx, gy, &shapes[i])) { hoverShape = i; break; } } }
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else if (isDrawing && currentMode == 3) { currentEndX = fmax(0.0, fmin(GRID_SIZE, gx)); currentEndY = fmax(0.0, fmin(GRID_SIZE, gy)); GenShape(currentEndX, currentEndY); InvalidateRect(hwnd, NULL, FALSE); }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (editMode != 0 && IsWindowVisible(hDistEdit)) { SendMessage(hwnd, WM_APP+1, 0, 0); } 
            SetFocus(hwnd); int x = LOWORD(lParam), y = HIWORD(lParam); if (HandlePaletteClick(hwnd, x, y, 1)) return 0;
            if (x >= canvasSize || y >= canvasSize) return 0; double gx = Snap((double)x / scaleFactor), gy = Snap((double)y / scaleFactor);
            
            if (currentMode == 4) { // TEXT 
                startX = gx; textCursorX = gx; textCursorY = gy; textCursorActive = 1; InvalidateRect(hwnd, NULL, FALSE); return 0;
            }
            if (currentMode == 5) { // FLOOD FILL MOORE TRACE
                SaveState(); int RES_SCALE = 20; int CANV_RES = GRID_SIZE * RES_SCALE;
                HDC hdc = GetDC(hwnd); HDC hMem = CreateCompatibleDC(hdc); HBITMAP hBmp = CreateCompatibleBitmap(hdc, CANV_RES, CANV_RES); SelectObject(hMem, hBmp);
                RECT r = {0,0,CANV_RES,CANV_RES}; FillRect(hMem, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); RenderShapes(hMem, shapes, shapeCount, RES_SCALE, NULL, 0);
                HBRUSH magic = CreateSolidBrush(RGB(255,0,255)); SelectObject(hMem, magic); ExtFloodFill(hMem, (int)(gx*RES_SCALE), (int)(gy*RES_SCALE), GetPixel(hMem, (int)(gx*RES_SCALE), (int)(gy*RES_SCALE)), FLOODFILLSURFACE);
                int dx[]={1,1,0,-1,-1,-1,0,1}, dy[]={0,1,1,1,0,-1,-1,-1}; int sX=-1, sY=-1;
                for(int py=0; py<CANV_RES && sX==-1; py++) for(int px=0; px<CANV_RES; px++) if(GetPixel(hMem,px,py)==RGB(255,0,255)) {sX=px; sY=py; break;}
                if(sX!=-1){
                    currentShape.type=0; currentShape.fill=currentFill; currentShape.useFill=useFill; currentShape.stroke=currentStroke; currentShape.useStroke=useStroke; currentShape.ptCount=0;
                    int cX=sX, cY=sY, dir=7, it=0;
                    do { 
                        int nD=-1; for(int i=0; i<8; i++) { int d=(dir+5+i)%8; if(GetPixel(hMem, cX+dx[d], cY+dy[d])==RGB(255,0,255)) {nD=d; break;} }
                        if(nD==-1) break; 
                        if (currentShape.ptCount < MAX_POINTS-1) { currentShape.ptsX[currentShape.ptCount]=(double)cX/RES_SCALE; currentShape.ptsY[currentShape.ptCount]=(double)cY/RES_SCALE; currentShape.ptCount++; }
                        cX+=dx[nD]; cY+=dy[nD]; dir=nD; it++;
                    } while((cX!=sX || cY!=sY) && it<150000);
                    
                    SimplifyShapeDP(&currentShape, 0.05);
                    if(currentShape.ptCount>=3) { shapes[shapeCount++] = currentShape; selectedShape = shapeCount-1; }
                }
                if (shapeCount > 0) currentMode = 6;
                DeleteObject(magic); DeleteObject(hBmp); DeleteDC(hMem); ReleaseDC(hwnd, hdc); UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); return 0;
            }

            if (currentMode == 7 || currentMode == 8) { // ROTATE or SCALE
                if (hoverShape != -1) {
                    int sHasSel = 0; for(int p=0; p<shapes[hoverShape].ptCount; p++) { if (ptSelected[hoverShape][p]) { sHasSel = 1; break; } }
                    if (!sHasSel) { ClearSelection(); for(int p=0; p<shapes[hoverShape].ptCount; p++) ToggleSelection(hoverShape, p); selectedShape = hoverShape; }
                }
                
                int hasSel = 0; double minX = 9999, maxX = -9999, minY = 9999, maxY = -9999;
                for(int i=0; i<shapeCount; i++) {
                    for(int p=0; p<shapes[i].ptCount; p++) {
                        if (ptSelected[i][p]) { hasSel = 1; minX = fmin(minX, shapes[i].ptsX[p]); maxX = fmax(maxX, shapes[i].ptsX[p]); minY = fmin(minY, shapes[i].ptsY[p]); maxY = fmax(maxY, shapes[i].ptsY[p]); }
                    }
                }
                if (hasSel) {
                    rotCenterX = (minX + maxX) / 2.0; rotCenterY = (minY + maxY) / 2.0;
                    double distToPivot = sqrt(pow(gx - rotCenterX, 2) + pow(gy - rotCenterY, 2));
                    if (currentMode == 7 && distToPivot * scaleFactor < 15.0) { isMovingInRotate = 1; dragStartX = gx; dragStartY = gy; } 
                    else { isMovingInRotate = 0; startRotAngle = atan2(gy - rotCenterY, gx - rotCenterX); startScaleDist = sqrt(pow(gy - rotCenterY, 2) + pow(gx - rotCenterX, 2)); }
                    memcpy(dragStartSnapshot, shapes, sizeof(Shape) * shapeCount); SetCapture(hwnd); isDraggingNodes = 1;
                }
            } 
            else if (currentMode == 6) { // SELECT / EDIT Mode
                int clickedNode = 0;
                if (hoverPt != -1) { 
                    clickedNode = 1;
                    if (GetKeyState(VK_SHIFT) & 0x8000) ToggleSelection(hoverShape, hoverPt); else if (!ptSelected[hoverShape][hoverPt]) { ClearSelection(); ToggleSelection(hoverShape, hoverPt); }
                    selectedShape = hoverShape; currentFill = shapes[selectedShape].fill; useFill = shapes[selectedShape].useFill; currentStroke = shapes[selectedShape].stroke; useStroke = shapes[selectedShape].useStroke;
                } else if (hoverSegShape != -1 && (GetKeyState(VK_SHIFT) & 0x8000)) { 
                    SaveState(); int i = hoverSegShape, p = hoverSegPt; for(int k=shapes[i].ptCount; k > p+1; k--) { shapes[i].ptsX[k] = shapes[i].ptsX[k-1]; shapes[i].ptsY[k] = shapes[i].ptsY[k-1]; }
                    shapes[i].ptsX[p+1] = Snap(hoverProjX/scaleFactor); shapes[i].ptsY[p+1] = Snap(hoverProjY/scaleFactor); shapes[i].ptCount++;
                    ClearSelection(); ToggleSelection(i, p+1); clickedNode = 1; selectedShape = i; currentFill = shapes[selectedShape].fill; useFill = shapes[selectedShape].useFill; currentStroke = shapes[selectedShape].stroke; useStroke = shapes[selectedShape].useStroke;
                } else if (hoverShape != -1) {
                    clickedNode = 1; int sHasSel = 0; for(int p=0; p<shapes[hoverShape].ptCount; p++) if(ptSelected[hoverShape][p]) sHasSel = 1;
                    if (GetKeyState(VK_SHIFT) & 0x8000) { int allSel = 1; for(int p=0; p<shapes[hoverShape].ptCount; p++) if(!ptSelected[hoverShape][p]) allSel = 0; for(int p=0; p<shapes[hoverShape].ptCount; p++) { if (allSel && ptSelected[hoverShape][p]) ToggleSelection(hoverShape, p); else if (!allSel && !ptSelected[hoverShape][p]) ToggleSelection(hoverShape, p); }
                    } else if (!sHasSel) { ClearSelection(); for(int p=0; p<shapes[hoverShape].ptCount; p++) ToggleSelection(hoverShape, p); }
                    selectedShape = hoverShape; currentFill = shapes[selectedShape].fill; useFill = shapes[selectedShape].useFill; currentStroke = shapes[selectedShape].stroke; useStroke = shapes[selectedShape].useStroke;
                } else { 
                    ClearSelection(); selectedShape = -1; textCursorActive = 0;
                    if (shapeCount == 0) { currentMode = 3; isDrawing = 1; currentShape.type = 0; currentShape.fill = currentFill; currentShape.useFill = useFill; currentShape.stroke = currentStroke; currentShape.useStroke = useStroke; currentShape.ptsX[0] = gx; currentShape.ptsY[0] = gy; currentShape.ptCount = 1; }
                }
                
                if (clickedNode) {
                    int isPartialSelection = 0;
                    for (int i=0; i<shapeCount; i++) {
                        int pSelCount = 0;
                        for(int p=0; p<shapes[i].ptCount; p++) if(ptSelected[i][p]) pSelCount++;
                        if (pSelCount > 0 && pSelCount < shapes[i].ptCount) isPartialSelection = 1;
                    }
                    
                    if ((GetKeyState(VK_CONTROL) & 0x8000) && !isPartialSelection) {
                        SaveState(); int origCount = shapeCount;
                        for (int i=0; i<origCount; i++) {
                            int sHasSel = 0; for(int p=0; p<shapes[i].ptCount; p++) if(ptSelected[i][p]) sHasSel = 1;
                            if (sHasSel && shapeCount < MAX_SHAPES) { shapes[shapeCount] = shapes[i]; for(int p=0; p<shapes[i].ptCount; p++) { if(ptSelected[i][p]) { ToggleSelection(i, p); ToggleSelection(shapeCount, p); } } shapeCount++; }
                        }
                    }
                    memcpy(dragStartSnapshot, shapes, sizeof(Shape) * shapeCount); isDraggingNodes = 1; dragStartX = gx; dragStartY = gy; 
                    isDraggingPoint = isPartialSelection;
                    
                    double minX = 9999, maxX = -9999, minY = 9999, maxY = -9999; int hasSel = 0;
                    for(int i=0; i<shapeCount; i++) { for(int p=0; p<shapes[i].ptCount; p++) { if (ptSelected[i][p]) { hasSel=1; minX = fmin(minX, shapes[i].ptsX[p]); maxX = fmax(maxX, shapes[i].ptsX[p]); minY = fmin(minY, shapes[i].ptsY[p]); maxY = fmax(maxY, shapes[i].ptsY[p]); } } }
                    if (hasSel) { rotCenterX = (minX + maxX)/2.0; rotCenterY = (minY + maxY)/2.0; startRotAngle = atan2(gy - rotCenterY, gx - rotCenterX); }
                    SetCapture(hwnd);
                }
            } else if (currentMode == 3) { // Primitives
                isDrawing = 1; startX = gx; startY = gy; currentEndX = gx; currentEndY = gy; currentShape.type = 0; currentShape.fill = currentFill; currentShape.useFill = useFill; currentShape.stroke = currentStroke; currentShape.useStroke = useStroke; GenShape(gx, gy); 
            } else if (shapeCount < MAX_SHAPES) { // Poly, Line, Polyline
                isDrawing = 1; currentShape.type = currentMode; currentShape.fill = currentFill; currentShape.useFill = useFill; currentShape.stroke = currentStroke; currentShape.useStroke = useStroke; 
                if (currentMode == 1 || currentMode == 2) { currentShape.useStroke = 1; currentShape.useFill = 0; } 
                currentShape.ptsX[currentShape.ptCount]=gx; currentShape.ptsY[currentShape.ptCount]=gy; currentShape.ptCount++; 
                if (currentMode == 1 && currentShape.ptCount == 2) { SaveState(); shapes[shapeCount++] = currentShape; currentShape.ptCount = 0; isDrawing = 0; selectedShape = shapeCount - 1; currentMode = 6; } 
            } UpdateStatusBar(); InvalidateRect(hwnd, NULL, FALSE); break;
        }
        case WM_LBUTTONUP: { 
            if (isDraggingNodes) { ReleaseCapture(); isDraggingNodes = 0; shapeWasMoved = 0; InvalidateRect(hwnd, NULL, FALSE); } 
            if (isDrawing && currentMode == 3) { SaveState(); shapes[shapeCount++] = currentShape; currentShape.ptCount = 0; isDrawing = 0; selectedShape = shapeCount - 1; currentMode = 6; InvalidateRect(hwnd, NULL, FALSE); } UpdateStatusBar(); break; 
        }
        case WM_RBUTTONDOWN: {
            if (editMode != 0 && IsWindowVisible(hDistEdit)) { ShowWindow(hDistEdit, SW_HIDE); editMode = 0; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            int x = LOWORD(lParam), y = HIWORD(lParam); if (HandlePaletteClick(hwnd, x, y, 0)) return 0; if (x >= canvasSize || y >= canvasSize) return 0;
            
            if (currentMode == 6 && hoverPt != -1) { 
                SaveState(); int i = hoverShape; for(int k=hoverPt; k<shapes[i].ptCount-1; k++) { shapes[i].ptsX[k] = shapes[i].ptsX[k+1]; shapes[i].ptsY[k] = shapes[i].ptsY[k+1]; } shapes[i].ptCount--; 
                if (shapes[i].ptCount < (shapes[i].type == 0 ? 3 : 2)) { for(int k=i; k<shapeCount-1; k++) shapes[k] = shapes[k+1]; shapeCount--; selectedShape = -1; selectedPtIdx = -1; } 
                hoverShape = hoverPt = -1; ClearSelection(); InvalidateRect(hwnd, NULL, FALSE); 
            } else if (isDrawing && (currentMode == 0 || currentMode == 2) && currentShape.ptCount >= (currentMode == 0 ? 3 : 2)) { 
                SaveState(); shapes[shapeCount++] = currentShape; currentShape.ptCount = 0; isDrawing = 0; selectedShape = shapeCount - 1; currentMode = 6; InvalidateRect(hwnd, NULL, FALSE); 
            } else { 
                currentShape.ptCount = 0; isDrawing = 0; if (shapeCount > 0) currentMode = 6; InvalidateRect(hwnd, NULL, FALSE); 
            } UpdateStatusBar(); break;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            HDC hdcMem = CreateCompatibleDC(hdc); HBITMAP hbmMem = CreateCompatibleBitmap(hdc, clientW, clientH); SelectObject(hdcMem, hbmMem);
            RECT rc = {0,0,clientW,clientH}; FillRect(hdcMem, &rc, (HBRUSH)(COLOR_BTNFACE+1)); RECT cvRect = {0, 0, canvasSize, canvasSize}; 
            HBRUSH hbg = CreateSolidBrush(RGB(245, 245, 245)); FillRect(hdcMem, &cvRect, hbg);
            
            HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(220,220,220)); SelectObject(hdcMem, gridPen); for(int i=0; i<=canvasSize; i+=scaleFactor) { MoveToEx(hdcMem, i, 0, NULL); LineTo(hdcMem, i, canvasSize); MoveToEx(hdcMem, 0, i, NULL); LineTo(hdcMem, canvasSize, i); } DeleteObject(gridPen);

            RenderShapes(hdcMem, shapes, shapeCount, scaleFactor, &currentShape, isDrawing);

            if (hRefBmp || hRefIcon) {
                HDC hi = CreateCompatibleDC(hdcMem); HBITMAP hbmI = CreateCompatibleBitmap(hdcMem, canvasSize, canvasSize); SelectObject(hi, hbmI); FillRect(hi, &cvRect, hbg); 
                int dstW = (int)(canvasSize * absRefScale * absRefStrX); int dstH = (int)(canvasSize * absRefScale * absRefStrY);
                int dstX = (canvasSize - dstW) / 2 + (int)absRefPosX; int dstY = (canvasSize - dstH) / 2 + (int)absRefPosY;
                if (hRefBmp) { HDC ht = CreateCompatibleDC(hdcMem); SelectObject(ht, hRefBmp); BITMAP bm; GetObject(hRefBmp, sizeof(bm), &bm); SetStretchBltMode(hi, HALFTONE); StretchBlt(hi, dstX, dstY, dstW, dstH, ht, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY); DeleteDC(ht); } else DrawIconEx(hi, dstX, dstY, hRefIcon, dstW, dstH, 0, NULL, DI_NORMAL);
                BLENDFUNCTION bf = {AC_SRC_OVER, 0, refAlpha, 0}; AlphaBlend(hdcMem, 0, 0, canvasSize, canvasSize, hi, 0, 0, canvasSize, canvasSize, bf); DeleteObject(hbmI); DeleteDC(hi);
            }

            if (currentMode == 4 && textCursorActive) {
                HPEN pCur = CreatePen(PS_SOLID, 2, RGB(255,165,0)); HGDIOBJ old = SelectObject(hdcMem, pCur);
                MoveToEx(hdcMem, (LONG)(textCursorX*scaleFactor), (LONG)(textCursorY*scaleFactor), NULL); LineTo(hdcMem, (LONG)(textCursorX*scaleFactor), (LONG)((textCursorY+5)*scaleFactor));
                SelectObject(hdcMem, old); DeleteObject(pCur);
            }

            if (currentMode == 6 || currentMode == 7 || currentMode == 8) { 
                HBRUSH hbNode = CreateSolidBrush((currentMode==7||currentMode==8) ? RGB(255,165,0) : RGB(150,150,150)); 
                HBRUSH hbHov = CreateSolidBrush(RGB(255,255,0)); HBRUSH hbSel = CreateSolidBrush(RGB(255,0,0)); HBRUSH hbEdge = CreateSolidBrush(RGB(0,255,255)); 
                SelectObject(hdcMem, GetStockObject(NULL_PEN));
                
                int hasAnySel = 0;
                for(int i=0; i<shapeCount; i++) {
                    int drawNodes = 0; if (i == selectedShape || i == hoverShape) drawNodes = 1;
                    for(int p=0; p<shapes[i].ptCount; p++) if (ptSelected[i][p]) { drawNodes = 1; hasAnySel = 1; }
                    if (drawNodes) { 
                        for(int p=0; p<shapes[i].ptCount; p++) { 
                            HBRUSH br = hbNode; int sz = 4;
                            if (ptSelected[i][p]) { br = hbSel; sz = 6; } else if (hoverShape == i && hoverPt == p) { br = hbHov; sz = 6; }
                            SelectObject(hdcMem, br); Ellipse(hdcMem, (LONG)round(shapes[i].ptsX[p]*scaleFactor)-sz, (LONG)round(shapes[i].ptsY[p]*scaleFactor)-sz, (LONG)round(shapes[i].ptsX[p]*scaleFactor)+sz, (LONG)round(shapes[i].ptsY[p]*scaleFactor)+sz); 
                        }
                    }
                }
                
                if ((currentMode == 7 || currentMode == 8) && hasAnySel) { 
                    SelectObject(hdcMem, hbSel); Ellipse(hdcMem, (LONG)round(rotCenterX*scaleFactor)-4, (LONG)round(rotCenterY*scaleFactor)-4, (LONG)round(rotCenterX*scaleFactor)+4, (LONG)round(rotCenterY*scaleFactor)+4);
                    if (isDraggingNodes && !isMovingInRotate) { HPEN pLine = CreatePen(PS_DASH, 1, RGB(255,165,0)); HGDIOBJ old = SelectObject(hdcMem, pLine); POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt); MoveToEx(hdcMem, (LONG)round(rotCenterX*scaleFactor), (LONG)round(rotCenterY*scaleFactor), NULL); LineTo(hdcMem, pt.x, pt.y); SelectObject(hdcMem, old); DeleteObject(pLine); }
                }
                if (currentMode == 6 && hoverSegShape != -1) { SelectObject(hdcMem, hbEdge); Ellipse(hdcMem, hoverProjX-5, hoverProjY-5, hoverProjX+5, hoverProjY+5); } 
                DeleteObject(hbNode); DeleteObject(hbHov); DeleteObject(hbSel); DeleteObject(hbEdge);
            }

            int cx = canvasSize + 15;
            HDC hPrv = CreateCompatibleDC(hdcMem); HBITMAP hbPrv = CreateCompatibleBitmap(hdcMem, GRID_SIZE, GRID_SIZE); SelectObject(hPrv, hbPrv);
            RECT rPrv = {0,0,GRID_SIZE,GRID_SIZE}; FillRect(hPrv, &rPrv, hbg); RenderShapes(hPrv, shapes, shapeCount, 1, NULL, 0);
            SetStretchBltMode(hdcMem, HALFTONE);
            DrawEdge(hdcMem, &(RECT){cx-1, 10-1, cx+16+1, 10+16+1}, EDGE_SUNKEN, BF_RECT); StretchBlt(hdcMem, cx, 10, 16, 16, hPrv, 0, 0, 32, 32, SRCCOPY);
            DrawEdge(hdcMem, &(RECT){cx+25-1, 10-1, cx+25+32+1, 10+32+1}, EDGE_SUNKEN, BF_RECT); BitBlt(hdcMem, cx+25, 10, 32, 32, hPrv, 0, 0, SRCCOPY);
            DrawEdge(hdcMem, &(RECT){cx+65-1, 10-1, cx+65+64+1, 10+64+1}, EDGE_SUNKEN, BF_RECT); StretchBlt(hdcMem, cx+65, 10, 64, 64, hPrv, 0, 0, 32, 32, SRCCOPY);
            DeleteObject(hbPrv); DeleteDC(hPrv);
            
            SetBkMode(hdcMem, TRANSPARENT); SetTextColor(hdcMem, RGB(0,0,0)); TextOut(hdcMem, cx, 30, "16", 2); TextOut(hdcMem, cx+25, 45, "32px", 4); TextOut(hdcMem, cx+65, 76, "64px (2x)", 9);
            TextOut(hdcMem, cx+5, 80, "L-Click: Fill", 13); TextOut(hdcMem, cx+125, 80, "R-Click: Stroke", 15);
            RECT rF = {cx, 95, cx+60, 108}; if(useFill) { HBRUSH fb=CreateSolidBrush(currentFill); FillRect(hdcMem,&rF,fb); DeleteObject(fb); } else DrawText(hdcMem,"NONE",4,&rF,DT_CENTER); FrameRect(hdcMem,&rF,(HBRUSH)GetStockObject(BLACK_BRUSH));
            RECT rS = {cx+120, 95, cx+180, 108}; if(useStroke) { HBRUSH sb=CreateSolidBrush(currentStroke); FillRect(hdcMem,&rS,sb); DeleteObject(sb); } else DrawText(hdcMem,"NONE",4,&rS,DT_CENTER); FrameRect(hdcMem,&rS,(HBRUSH)GetStockObject(BLACK_BRUSH));
            
            for(int i=0; i<16; i++) { 
                RECT r = {cx + (i%8)*32, 110 + (i/8)*16, cx + (i%8)*32 + 32, 110 + (i/8)*16 + 16}; 
                HBRUSH pb = CreateSolidBrush(palette[i]); 
                FillRect(hdcMem, &r, pb); 
                DeleteObject(pb); 
                if (currentFill == palette[i] && useFill) { 
                    FrameRect(hdcMem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH)); 
                    InflateRect(&r, -1, -1); 
                    FrameRect(hdcMem, &r, (HBRUSH)GetStockObject(BLACK_BRUSH)); 
                } 
            }
            
            RECT rT = {cx, 180, cx+256, 210}; FillRect(hdcMem, &rT, (HBRUSH)(COLOR_BTNFACE+1)); DrawEdge(hdcMem, &rT, EDGE_RAISED, BF_RECT); TextOut(hdcMem, cx+45, 188, "Click: Transparent / None", 25);
            SetTextColor(hdcMem, RGB(128,128,128)); if (shapeCount == 0 && !hRefBmp) TextOut(hdcMem, canvasSize/2 - 120, canvasSize/2 - 10, "Double-Click to open Image/SVG", 30);
            
            DeleteObject(hbg); BitBlt(hdc, 0, 0, clientW, clientH, hdcMem, 0, 0, SRCCOPY); DeleteObject(hbmMem); DeleteDC(hdcMem); EndPaint(hwnd, &ps); break;
        }
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    } return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASS wc = {0}; wc.style = CS_DBLCLKS; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "VecEdX"; RegisterClass(&wc);
    hMain = CreateWindowEx(0, "VecEdX", "Win32 C Pro Vector Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 860, 800, NULL, NULL, hInst, NULL);
    ShowWindow(hMain, show); 
    
    MSG msg; 
    while (GetMessage(&msg, NULL, 0, 0)) { 
        if (msg.message == WM_KEYDOWN) {
            if (msg.hwnd != hMain && msg.hwnd != hDistEdit) {
                if (msg.wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) { SendMessage(hMain, WM_KEYDOWN, msg.wParam, msg.lParam); continue; }
                if (msg.wParam == VK_DELETE || msg.wParam == VK_BACK || msg.wParam == VK_ESCAPE || msg.wParam == VK_RETURN) { SendMessage(hMain, WM_KEYDOWN, msg.wParam, msg.lParam); continue; }
            }
        }
        TranslateMessage(&msg); DispatchMessage(&msg); 
    }
    return msg.wParam;
}