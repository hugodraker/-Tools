/*
 * =====================================================================
 * This work is released into the public domain.
 * You are free to use, modify, copy, and distribute this code 
 * for any purpose, commercial or non-commercial, without restriction.
 * =====================================================================
 * gcc icons.c -o icons.exe -lgdi32
 */

#include <windows.h>
#include <stdlib.h>

// State: 0 = horizontal row, 1 = vertical column
static int isVertical = 0;

// Helper function to draw a polygon translated by (dx, dy)
void DrawTranslatedPolygon(HDC hdc, const POINT* pts, int count, int dx, int dy) {
    POINT* temp = (POINT*)malloc(sizeof(POINT) * count);
    for(int i = 0; i < count; ++i) {
        temp[i].x = pts[i].x + dx;
        temp[i].y = pts[i].y + dy;
    }
    Polygon(hdc, temp, count);
    free(temp);
}

// Macro to draw borderless, solid-colored polygons
#define DRAW_POLY(pts, count, r, g, b) \
    do { \
        COLORREF color = RGB(r, g, b); \
        HBRUSH hBr = CreateSolidBrush(color); \
        HPEN hPn = CreatePen(PS_SOLID, 1, color); \
        HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr); \
        HPEN hOldPn = (HPEN)SelectObject(hdc, hPn); \
        DrawTranslatedPolygon(hdc, pts, count, x, y); \
        SelectObject(hdc, hOldBr); \
        SelectObject(hdc, hOldPn); \
        DeleteObject(hBr); \
        DeleteObject(hPn); \
    } while(0)

// 1. Folder & Properties Window Icon
void DrawIcon0(HDC hdc, int x, int y) {
    POINT fBack[] = {{8,12}, {24,12}, {28,16}, {52,16}, {52,48}, {8,48}}; DRAW_POLY(fBack, 6, 230, 190, 40);
    POINT fIn[] = {{12,18}, {48,18}, {48,44}, {12,44}}; DRAW_POLY(fIn, 4, 200, 150, 20);
    POINT fFront[] = {{4,48}, {16,24}, {56,24}, {44,48}}; DRAW_POLY(fFront, 4, 255, 220, 70);
    POINT fEdge[] = {{4,48}, {16,24}, {20,24}, {10,48}}; DRAW_POLY(fEdge, 4, 255, 235, 120);

    POINT wShad[] = {{24,30}, {60,30}, {60,60}, {24,60}}; DRAW_POLY(wShad, 4, 150, 150, 150);
    POINT wBase[] = {{20,26}, {56,26}, {56,56}, {20,56}}; DRAW_POLY(wBase, 4, 245, 245, 250);
    POINT wTop[] = {{20,26}, {56,26}, {56,34}, {20,34}}; DRAW_POLY(wTop, 4, 60, 130, 210);
    
    POINT pRed[] = {{26,42}, {30,38}, {34,42}, {30,46}}; DRAW_POLY(pRed, 4, 220, 60, 60);
    POINT pGrn[] = {{36,42}, {40,38}, {44,42}, {40,46}}; DRAW_POLY(pGrn, 4, 40, 180, 80);
    POINT pBlu[] = {{46,42}, {50,38}, {54,42}, {50,46}}; DRAW_POLY(pBlu, 4, 40, 100, 220);
}

// 2. Folder & Image Icon
void DrawIcon1(HDC hdc, int x, int y) {
    POINT fBack[] = {{8,12}, {24,12}, {28,16}, {52,16}, {52,48}, {8,48}}; DRAW_POLY(fBack, 6, 230, 190, 40);
    POINT fIn[] = {{12,18}, {48,18}, {48,44}, {12,44}}; DRAW_POLY(fIn, 4, 200, 150, 20);
    POINT fFront[] = {{4,48}, {16,24}, {56,24}, {44,48}}; DRAW_POLY(fFront, 4, 255, 220, 70);
    POINT fEdge[] = {{4,48}, {16,24}, {20,24}, {10,48}}; DRAW_POLY(fEdge, 4, 255, 235, 120);

    POINT iShad[] = {{28,32}, {60,32}, {60,60}, {28,60}}; DRAW_POLY(iShad, 4, 150, 150, 150);
    POINT iBase[] = {{24,28}, {56,28}, {56,56}, {24,56}}; DRAW_POLY(iBase, 4, 255, 255, 255);
    POINT iPic[] = {{26,30}, {54,30}, {54,54}, {26,54}}; DRAW_POLY(iPic, 4, 235, 240, 245);

    POINT m1[] = {{26,54}, {36,40}, {46,54}}; DRAW_POLY(m1, 3, 100, 170, 220);
    POINT m1S[] = {{36,40}, {46,54}, {36,54}}; DRAW_POLY(m1S, 3, 60, 130, 190);
    POINT m2[] = {{38,54}, {46,44}, {54,54}}; DRAW_POLY(m2, 3, 40, 100, 160);
    POINT m2S[] = {{46,44}, {54,54}, {46,54}}; DRAW_POLY(m2S, 3, 20, 60, 120);
}

// 3. Document Search Icon (Magnifying Glass)
void DrawIcon2(HDC hdc, int x, int y) {
    POINT pShad[] = {{16,12}, {44,12}, {52,20}, {52,60}, {16,60}}; DRAW_POLY(pShad, 5, 180, 180, 180);
    POINT pBase[] = {{12,8}, {40,8}, {48,16}, {48,56}, {12,56}}; DRAW_POLY(pBase, 5, 250, 250, 250);
    POINT pFold[] = {{40,8}, {40,16}, {48,16}}; DRAW_POLY(pFold, 3, 220, 220, 220);
    
    POINT t1[] = {{18,22}, {40,22}, {40,26}, {18,26}}; DRAW_POLY(t1, 4, 200, 200, 200);
    POINT t2[] = {{18,30}, {34,30}, {34,34}, {18,34}}; DRAW_POLY(t2, 4, 200, 200, 200);
    POINT t3[] = {{18,38}, {42,38}, {42,42}, {18,42}}; DRAW_POLY(t3, 4, 200, 200, 200);

    POINT hShad[] = {{38,38}, {58,58}, {52,64}, {32,44}}; DRAW_POLY(hShad, 4, 50, 50, 50);
    POINT hBase[] = {{36,36}, {56,56}, {50,62}, {30,42}}; DRAW_POLY(hBase, 4, 90, 90, 90);
    POINT hHigh[] = {{36,36}, {44,44}, {40,48}, {30,42}}; DRAW_POLY(hHigh, 4, 130, 130, 130);

    POINT rimOut[] = {{20,20}, {28,16}, {38,16}, {46,24}, {46,34}, {38,42}, {28,42}, {20,34}}; DRAW_POLY(rimOut, 8, 160, 160, 170);
    POINT rimIn[] = {{22,22}, {28,19}, {36,19}, {42,25}, {42,33}, {36,39}, {28,39}, {22,33}}; DRAW_POLY(rimIn, 8, 220, 220, 230);
    
    POINT glass[] = {{24,24}, {28,22}, {34,22}, {39,27}, {39,31}, {34,36}, {28,36}, {24,31}}; DRAW_POLY(glass, 8, 180, 230, 255);
    POINT gHigh[] = {{24,24}, {34,22}, {28,34}}; DRAW_POLY(gHigh, 3, 240, 250, 255);
}

// 4. Folder, Document & Gear Icon (Refined)
void DrawIcon3(HDC hdc, int x, int y) {
    // Background Folder Back
    POINT fBack[] = {{8,12}, {24,12}, {28,16}, {50,16}, {50,44}, {8,44}}; DRAW_POLY(fBack, 6, 230, 190, 40);
    POINT fIn[] = {{12,18}, {48,18}, {48,42}, {12,42}}; DRAW_POLY(fIn, 4, 200, 150, 20);
    
    // Document (White paper with folded corner)
    POINT docDShad[] = {{32,20}, {48,20}, {56,28}, {56,56}, {32,56}}; DRAW_POLY(docDShad, 5, 180, 180, 180);
    POINT doc[] = {{30,18}, {46,18}, {54,26}, {54,54}, {30,54}}; DRAW_POLY(doc, 5, 250, 250, 250);
    POINT docFold[] = {{46,18}, {46,26}, {54,26}}; DRAW_POLY(docFold, 3, 210, 210, 210);

    // Foreground Folder Front
    POINT fFront[] = {{4,46}, {16,24}, {48,24}, {36,46}}; DRAW_POLY(fFront, 4, 255, 220, 70);

    // Grey Gear Base (Shadow/3D depth)
    POINT gShad[] = {
        {22,26}, {26,26}, {28,30}, {34,30}, {36,26}, {40,26}, {40,32}, {44,34}, {48,34}, {48,38}, 
        {44,40}, {44,44}, {40,46}, {40,52}, {36,52}, {34,48}, {28,48}, {26,52}, {22,52}, {22,46}, 
        {18,44}, {14,44}, {14,40}, {18,38}, {18,34}, {22,32}
    };
    DRAW_POLY(gShad, 26, 130, 130, 130); // Grey 3D Edge

    // Grey Gear Top Face
    POINT gTop[] = {
        {20,24}, {24,24}, {26,28}, {32,28}, {34,24}, {38,24}, {38,30}, {42,32}, {46,32}, {46,36}, 
        {42,38}, {42,42}, {38,44}, {38,50}, {34,50}, {32,46}, {26,46}, {24,50}, {20,50}, {20,44}, 
        {16,42}, {12,42}, {12,38}, {16,36}, {16,32}, {20,30}
    };
    DRAW_POLY(gTop, 26, 220, 220, 220); // Light Grey Base

    // Gear Inner Rim & Hole
    POINT gInRing[] = {{24,32}, {34,32}, {38,36}, {38,40}, {34,44}, {24,44}, {20,40}, {20,36}}; DRAW_POLY(gInRing, 8, 180, 180, 180);
    POINT gHole[] = {{26,35}, {32,35}, {35,38}, {32,41}, {26,41}, {23,38}}; DRAW_POLY(gHole, 6, 100, 100, 100);
}

// 5. Purple Help Book (Refined Perspective)
void DrawIcon4(HDC hdc, int x, int y) {
    // Dark Purple Spine & Back Cover Outline
    POINT spine[] = {{10,34}, {28,48}, {54,34}, {34,20}}; DRAW_POLY(spine, 4, 80, 20, 100);
    POINT spineSide[] = {{10,34}, {10,42}, {28,54}, {28,48}}; DRAW_POLY(spineSide, 4, 50, 10, 70);
    
    // Stacked White Pages
    POINT pagesBase[] = {{12,41}, {28,52}, {52,38}, {34,26}}; DRAW_POLY(pagesBase, 4, 180, 180, 180);
    POINT pagesTop[] = {{12,37}, {28,49}, {52,35}, {34,23}}; DRAW_POLY(pagesTop, 4, 240, 240, 240);
    POINT pageLine[] = {{28,49}, {28,52}, {52,38}, {52,35}}; DRAW_POLY(pageLine, 4, 200, 200, 200);

    // Purple Top Cover
    POINT coverTop[] = {{8,32}, {26,46}, {50,32}, {32,18}}; DRAW_POLY(coverTop, 4, 140, 50, 180);
    POINT coverHigh[] = {{8,32}, {26,46}, {28,44}, {10,30}}; DRAW_POLY(coverHigh, 4, 170, 80, 210);
    POINT coverSide[] = {{26,46}, {50,32}, {52,34}, {28,48}}; DRAW_POLY(coverSide, 4, 100, 30, 140);

    // 3D Golden Question Mark
    // Shadow layer
    POINT qS1[] = {{22,26}, {30,22}, {38,26}, {36,32}, {30,34}, {28,40}, {24,38}, {26,32}, {32,30}, {34,28}, {30,26}, {24,28}}; DRAW_POLY(qS1, 12, 180, 110, 0);
    POINT qSDot[] = {{26,42}, {30,44}, {28,46}, {24,44}}; DRAW_POLY(qSDot, 4, 180, 110, 0);
    // Top layer
    POINT qT1[] = {{22,24}, {30,20}, {38,24}, {36,30}, {30,32}, {28,38}, {24,36}, {26,30}, {32,28}, {34,26}, {30,24}, {24,26}}; DRAW_POLY(qT1, 12, 255, 220, 40);
    POINT qTDot[] = {{26,40}, {30,42}, {28,44}, {24,42}}; DRAW_POLY(qTDot, 4, 255, 220, 40);
}

// 6. Hourglass & Colored Window (Refined)
void DrawIcon5(HDC hdc, int x, int y) {
    // Window Backing (Wider traditional rectangle dialog)
    POINT wBase[] = {{18,14}, {62,14}, {62,42}, {18,42}}; DRAW_POLY(wBase, 4, 245, 245, 245);
    POINT wTop[] = {{18,14}, {62,14}, {62,22}, {18,22}}; DRAW_POLY(wTop, 4, 50, 120, 200);

    // Colored data bubbles (Hexagons) in Window
    POINT bCyan[] = {{34,26}, {38,24}, {42,26}, {42,30}, {38,32}, {34,30}}; DRAW_POLY(bCyan, 6, 0, 180, 220);
    POINT bBlue[] = {{38,28}, {44,26}, {50,28}, {50,34}, {44,36}, {38,34}}; DRAW_POLY(bBlue, 6, 0, 0, 220);
    POINT bGrn[] = {{46,24}, {52,22}, {56,24}, {56,30}, {52,32}, {46,30}}; DRAW_POLY(bGrn, 6, 40, 180, 60);
    
    // Bottom dots (Adjusted slightly higher for the new rectangular bound)
    POINT dRed[] = {{36,36}, {40,34}, {44,36}, {40,38}}; DRAW_POLY(dRed, 4, 220, 40, 40);
    POINT dPurp[] = {{44,36}, {48,34}, {52,36}, {48,38}}; DRAW_POLY(dPurp, 4, 150, 50, 200);
    POINT dOrng[] = {{52,36}, {56,34}, {60,36}, {56,38}}; DRAW_POLY(dOrng, 4, 250, 120, 20);

    // Hourglass Base & Top Wood
    POINT hwTop[] = {{6,14}, {34,14}, {34,18}, {6,18}}; DRAW_POLY(hwTop, 4, 170, 110, 50);
    POINT hwTopL[] = {{6,12}, {34,12}, {34,14}, {6,14}}; DRAW_POLY(hwTopL, 4, 210, 150, 80);
    POINT hwBot[] = {{6,54}, {34,54}, {34,58}, {6,58}}; DRAW_POLY(hwBot, 4, 140, 90, 40);
    POINT hwBotL[] = {{6,52}, {34,52}, {34,54}, {6,54}}; DRAW_POLY(hwBotL, 4, 180, 130, 60);

    // Glass Bulb Back (Shadowed)
    POINT gBack[] = {{10,18}, {30,18}, {24,36}, {30,52}, {10,52}, {16,36}}; DRAW_POLY(gBack, 6, 200, 220, 230);

    // Sand
    POINT sandTop[] = {{12,24}, {28,24}, {24,34}, {16,34}}; DRAW_POLY(sandTop, 4, 220, 190, 120);
    POINT sandBot[] = {{20,38}, {24,46}, {28,52}, {12,52}, {16,46}}; DRAW_POLY(sandBot, 5, 200, 170, 100);
    POINT sandFlow[] = {{19,34}, {21,34}, {21,46}, {19,46}}; DRAW_POLY(sandFlow, 4, 220, 190, 120);

    // Glass Bulb Highlight (Front)
    POINT gFront1[] = {{10,18}, {16,18}, {20,30}, {18,36}, {14,30}}; DRAW_POLY(gFront1, 5, 240, 250, 255);
    POINT gFront2[] = {{18,36}, {22,46}, {28,52}, {24,52}, {16,42}}; DRAW_POLY(gFront2, 5, 230, 245, 255);
}

// 7. Computer Monitor (Refined - Facing Left)
void DrawIcon6(HDC hdc, int x, int y) {
    // CRT Back (Right Side, receding)
    POINT cRight[] = {{34,14}, {54,18}, {54,40}, {38,44}}; DRAW_POLY(cRight, 4, 140, 140, 140);
    
    // CRT Top
    POINT cTop[] = {{10,24}, {34,14}, {54,18}, {30,28}}; DRAW_POLY(cTop, 4, 230, 230, 230);

    // Monitor Stand
    POINT neck[] = {{24,46}, {30,44}, {30,54}, {24,56}}; DRAW_POLY(neck, 4, 120, 120, 120);
    POINT base[] = {{18,52}, {38,48}, {48,52}, {28,58}}; DRAW_POLY(base, 4, 180, 180, 180);
    POINT baseH[] = {{18,52}, {28,58}, {34,56}, {26,51}}; DRAW_POLY(baseH, 4, 210, 210, 210); // Base highlight

    // Front Bezel (Facing left)
    POINT mFront[] = {{8,26}, {32,16}, {36,46}, {12,54}}; DRAW_POLY(mFront, 4, 190, 190, 190);
    POINT mBezelIn[] = {{12,28}, {28,21}, {31,43}, {16,49}}; DRAW_POLY(mBezelIn, 4, 160, 160, 160);

    // Dark Blue Screen
    POINT screen[] = {{14,29}, {28,23}, {30,42}, {16,47}}; DRAW_POLY(screen, 4, 20, 40, 120);
    POINT screenGlow[] = {{14,29}, {28,23}, {24,28}, {16,33}}; DRAW_POLY(screenGlow, 4, 40, 80, 180); // Screen reflection

    // Cyan Wireframe Globe (Polygonal Lines)
    POINT gOut[] = {{18,34}, {22,29}, {26,29}, {28,34}, {24,39}, {20,39}}; DRAW_POLY(gOut, 6, 0, 200, 255);
    POINT gIn[] = {{20,35}, {22,31}, {24,31}, {26,35}, {24,37}, {22,37}}; DRAW_POLY(gIn, 6, 20, 40, 120); // Mask out center
    // Cross lines for globe
    POINT gVLine[] = {{22,28}, {24,28}, {25,40}, {23,40}}; DRAW_POLY(gVLine, 4, 0, 200, 255);
    POINT gHLine[] = {{17,34}, {29,29}, {29,31}, {17,36}}; DRAW_POLY(gHLine, 4, 0, 200, 255);
    POINT gCenter[] = {{22,33}, {24,32}, {25,35}, {23,36}}; DRAW_POLY(gCenter, 4, 20, 40, 120); // Mask intersection

    // Power LED (Green dot bottom right of bezel)
    POINT led[] = {{30,47}, {32,46}, {33,48}, {31,49}}; DRAW_POLY(led, 4, 50, 220, 50);
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_LBUTTONDOWN:
            isVertical = !isVertical;
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hbg = CreateSolidBrush(RGB(240, 240, 245));
            FillRect(hdc, &rc, hbg);
            DeleteObject(hbg);

            SetTextColor(hdc, RGB(100, 100, 100));
            SetBkMode(hdc, TRANSPARENT);
            TextOut(hdc, 30, 550, "Click anywhere in the window to toggle layout.", 45);

            void (*drawFuncs[7])(HDC, int, int) = {
                DrawIcon0, DrawIcon1, DrawIcon2, DrawIcon3, DrawIcon4, DrawIcon5, DrawIcon6
            };

            int startX = 50;
            int startY = 40;
            int spacing = 72;

            for(int i = 0; i < 7; ++i) {
                int x = isVertical ? startX : startX + (i * spacing);
                int y = isVertical ? startY + (i * spacing) : startY;
                drawFuncs[i](hdc, x, y);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Entry Point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = "PolygonIconsClass";

    if(!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        "PolygonIconsClass",
        "GDI Polygon Icons - Accurate Recreation",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 650,
        NULL, NULL, hInstance, NULL
    );

    if(hwnd == NULL) {
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}