/* ============================================================================
 * PUBLIC DOMAIN NOTICE
 * This software is free and unencumbered software released into the public domain.
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute this 
 * software, either in source code form or as a compiled binary, for any purpose, 
 * commercial or non-commercial, and by any means.
 * ============================================================================
 *
 * GCC COMPILE INSTRUCTIONS (MinGW):
 * gcc shell-icons.c -o shell-icons.exe -mwindows -lgdi32 -lcomctl32 -lcomdlg32
 *
 * ============================================================================ */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>

#define TOTAL_UNIQUE_ICONS 60
#define TOP_BAR_HEIGHT 50
#define IDM_FILE_OPEN 1001
#define IDM_FILE_EXIT 1002

// Icon Group Structure
typedef struct {
    const char* title;
    int startId;
    int count;
} IconGroup;

const IconGroup g_groups[] = {
    { "Files & Documents",          0,  9 },
    { "Hardware & Storage",         9, 12 },
    { "System Controls & Apps",    21, 11 },
    { "Actions & Editing Tools",   32, 11 },
    { "Network, Web & Mail",       43,  5 },
    { "Security, Media & Status",  48, 12 }
};
#define NUM_GROUPS (sizeof(g_groups) / sizeof(g_groups[0]))

// Global State
int g_scale = 3;
int g_scrollY = 0;
HWND hTrackbar;
HWND hLabel;
HWND g_hStatus;
char g_loadedFile[MAX_PATH] = "Win95 Scalable GDI Shell";

// Names for the 60 grid labels
const char* g_iconNames[TOTAL_UNIQUE_ICONS] = {
    // Group 1: Files & Documents
    "Folder", "Folder (Open)", "Text File", "Font File", "Audio File", 
    "Bitmap File", "Zip / Archive", "Certificate File", "Notepad Note", 

    // Group 2: Hardware & Storage
    "My Computer", "Floppy Disk", "Save Disk", "Hard Drive", "CD-ROM", 
    "USB Flash Drive", "Network Drive", "Printer", "Display Monitor", "Battery", 
    "Keyboard", "Mouse",

    // Group 3: System Controls & Apps
    "App Window", "Control Panel", "Terminal", "Briefcase", "Program Group", 
    "Settings / Gears", "Calculator", "Clock", "User Profile", "Calendar", 
    "Database / Server",

    // Group 4: Actions & Editing Tools
    "Search", "Cut", "Copy", "Paste", "Undo", 
    "Help", "Delete", "Refresh / Reload", "Pushpin", "Trash / Shredder", 
    "Chart / Analytics",

    // Group 5: Network, Web & Mail
    "Home", "Network", "Globe / Web", "Mail", "Open Mail",

    // Group 6: Security, Media & Status
    "Warning", "Error", "OK / Check", "Favorites", "Speaker", 
    "Volume Waves", "Security Key", "Security Lock", "Security Shield", "Movie / Video", 
    "Recycle Bin", "Recycle Bin (Full)"
};

// GDI Color Palette Theme
typedef struct {
    HBRUSH white, black, gray, ltGray, blue, cyan, red, green, yellow, dkYellow, brown, silver;
    HPEN blackPen, thickBlackPen, grayPen, thickWhitePen, whitePen;
} Win95Theme;

// ---------------------------------------------------------
// ICON RENDERING ENGINE
// ---------------------------------------------------------
void DrawWin95Icon(HDC hdc, int x, int y, int s, int iconId, Win95Theme* t) {
    #define R(l, top, r, b) Rectangle(hdc, x+(l)*s, y+(top)*s, x+(r)*s, y+(b)*s)
    #define E(l, top, r, b) Ellipse(hdc, x+(l)*s, y+(top)*s, x+(r)*s, y+(b)*s)
    #define L(x1, y1, x2, y2) do { MoveToEx(hdc, x+(x1)*s, y+(y1)*s, NULL); LineTo(hdc, x+(x2)*s, y+(y2)*s); } while(0)
    #define PT(X, Y) {x + (X)*s, y + (Y)*s}
    #define POLY(pts) Polygon(hdc, pts, sizeof(pts)/sizeof(POINT))

    SelectObject(hdc, t->blackPen); // Default outline

    switch (iconId) {
        // --- Group 1: Files & Documents ---
        case 0: { // Folder
            SelectObject(hdc, t->dkYellow);
            POINT back[] = { PT(3,6), PT(12,6), PT(15,10), PT(29,10), PT(29,28), PT(3,28) }; POLY(back);
            SelectObject(hdc, t->yellow);
            POINT front[] = { PT(3,12), PT(29,12), PT(29,28), PT(3,28) }; POLY(front);
            break;
        }
        case 1: { // Folder (Open)
            SelectObject(hdc, t->dkYellow);
            POINT back[] = { PT(3,4), PT(12,4), PT(15,8), PT(27,8), PT(27,24), PT(3,24) }; POLY(back);
            SelectObject(hdc, t->white); R(7,8,23,22);
            SelectObject(hdc, t->yellow);
            POINT front[] = { PT(1,14), PT(25,14), PT(29,28), PT(5,28) }; POLY(front);
            break;
        }
        case 2: { // Text File
            SelectObject(hdc, t->white);
            POINT paper[] = { PT(6,2), PT(20,2), PT(26,8), PT(26,30), PT(6,30) }; POLY(paper);
            SelectObject(hdc, t->ltGray);
            POINT fold[] = { PT(20,2), PT(20,8), PT(26,8) }; POLY(fold);
            SelectObject(hdc, t->grayPen);
            L(10,12,22,12); L(10,16,22,16); L(10,20,22,20); L(10,24,18,24);
            break;
        }
        case 3: { // Font File
            SelectObject(hdc, t->white); POINT doc[] = { PT(6,2), PT(20,2), PT(26,8), PT(26,30), PT(6,30) }; POLY(doc);
            SelectObject(hdc, t->ltGray); POINT fold[] = { PT(20,2), PT(20,8), PT(26,8) }; POLY(fold);
            SelectObject(hdc, t->thickBlackPen); L(11,24,16,12); L(16,12,21,24); L(13,20,19,20);
            break;
        }
        case 4: { // Audio File
            SelectObject(hdc, t->white); POINT doc[] = { PT(6,2), PT(20,2), PT(26,8), PT(26,30), PT(6,30) }; POLY(doc);
            SelectObject(hdc, t->ltGray); POINT fold[] = { PT(20,2), PT(20,8), PT(26,8) }; POLY(fold);
            SelectObject(hdc, t->blue); E(10,20,15,25); E(18,18,23,23);
            SelectObject(hdc, t->thickBlackPen); L(14,22,14,12); L(22,20,22,10); L(14,12,22,10);
            break;
        }
        case 5: { // Bitmap File
            SelectObject(hdc, t->white); POINT doc[] = { PT(6,2), PT(20,2), PT(26,8), PT(26,30), PT(6,30) }; POLY(doc);
            SelectObject(hdc, t->ltGray); POINT fold[] = { PT(20,2), PT(20,8), PT(26,8) }; POLY(fold);
            SelectObject(hdc, t->cyan); R(10,12,22,24);
            SelectObject(hdc, t->yellow); E(18,14,21,17);
            SelectObject(hdc, t->green); POINT m[] = { PT(10,24), PT(15,17), PT(18,21), PT(20,19), PT(22,24) }; POLY(m);
            break;
        }
        case 6: { // Zip / Archive (NEW)
            SelectObject(hdc, t->yellow); R(6,4,26,28);
            SelectObject(hdc, t->gray); R(14,4,18,28);
            SelectObject(hdc, t->silver); R(12,18,20,24);
            SelectObject(hdc, t->blackPen); L(14,6,18,6); L(14,10,18,10); L(14,14,18,14);
            break;
        }
        case 7: { // Certificate File (NEW)
            SelectObject(hdc, t->white); POINT paper[] = { PT(6,2), PT(20,2), PT(26,8), PT(26,30), PT(6,30) }; POLY(paper);
            SelectObject(hdc, t->ltGray); POINT fold[] = { PT(20,2), PT(20,8), PT(26,8) }; POLY(fold);
            SelectObject(hdc, t->red); E(12,16,20,24);
            SelectObject(hdc, t->yellow); E(14,18,18,22);
            break;
        }
        case 8: { // Notepad Note (NEW)
            SelectObject(hdc, t->yellow); R(4,4,28,28);
            SelectObject(hdc, t->blue); R(4,4,28,8);
            SelectObject(hdc, t->grayPen); L(8,12,24,12); L(8,16,24,16); L(8,20,24,20); L(8,24,18,24);
            break;
        }

        // --- Group 2: Hardware & Storage ---
        case 9: { // My Computer
            SelectObject(hdc, t->ltGray); R(5,2,27,18);
            SelectObject(hdc, t->blue);   R(7,4,25,16);
            SelectObject(hdc, t->whitePen); L(8,5,15,5); SelectObject(hdc, t->blackPen);
            SelectObject(hdc, t->ltGray); R(12,18,20,21); R(9,21,23,23); R(4,23,28,30);
            SelectObject(hdc, t->black);  R(7,25,16,27);
            SelectObject(hdc, t->green);  E(24,25,26,27);
            break;
        }
        case 10: { // Floppy Disk
            SelectObject(hdc, t->black); R(6,6,26,26);
            SelectObject(hdc, t->white); R(10,14,22,26);
            SelectObject(hdc, t->silver); R(12,6,20,12);
            SelectObject(hdc, t->black); R(16,7,19,10);
            break;
        }
        case 11: { // Save Disk
            SelectObject(hdc, t->blue); R(5,4,27,28);
            SelectObject(hdc, t->silver); R(10,4,22,12);
            SelectObject(hdc, t->black); R(13,5,16,9);
            SelectObject(hdc, t->white); R(8,15,24,28);
            SelectObject(hdc, t->red); L(10,18,22,18);
            break;
        }
        case 12: { // Hard Drive
            SelectObject(hdc, t->gray); R(6,10,26,24);
            SelectObject(hdc, t->silver); R(8,12,24,20);
            SelectObject(hdc, t->grayPen); L(8,14,24,14); L(8,16,24,16); L(8,18,24,18);
            SelectObject(hdc, t->green); SelectObject(hdc, t->blackPen); E(10,20,14,24);
            break;
        }
        case 13: { // CD-ROM (Unfilled inner/outer circles removed)
            SelectObject(hdc, t->silver); E(3,3,29,29);
            SelectObject(hdc, t->ltGray); SelectObject(hdc, t->blackPen); E(11,11,21,21);
            SelectObject(hdc, t->white); E(13,13,19,19);
            break;
        }
        case 14: { // USB Flash Drive
            SelectObject(hdc, t->silver); R(11,2,21,10);
            SelectObject(hdc, t->black); R(13,4,15,7); R(17,4,19,7);
            SelectObject(hdc, t->blue); R(9,10,23,28);
            SelectObject(hdc, t->blackPen); L(9,14,23,14);
            SelectObject(hdc, t->green); E(15,22,17,24);
            break;
        }
        case 15: { // Network Drive (NEW)
            SelectObject(hdc, t->gray); R(6,10,26,22);
            SelectObject(hdc, t->silver); R(8,12,24,18);
            SelectObject(hdc, t->green); E(10,24,14,28);
            SelectObject(hdc, t->blackPen); L(12,22,12,24); L(4,28,28,28);
            break;
        }
        case 16: { // Printer
            SelectObject(hdc, t->white); R(10,4,22,12);
            SelectObject(hdc, t->ltGray); R(6,12,26,22);
            SelectObject(hdc, t->white); R(10,22,22,28);
            SelectObject(hdc, t->grayPen); L(12,24,20,24); L(12,26,18,26);
            break;
        }
        case 17: { // Display Monitor
            SelectObject(hdc, t->ltGray); R(3,4,29,22);
            SelectObject(hdc, t->cyan); R(5,6,27,20);
            SelectObject(hdc, t->gray); R(13,22,19,26);
            SelectObject(hdc, t->ltGray); R(8,26,24,29);
            break;
        }
        case 18: { // Battery
            SelectObject(hdc, t->gray); R(4,8,26,24);
            SelectObject(hdc, t->silver); R(26,12,30,20);
            SelectObject(hdc, t->green); R(6,10,12,22); R(14,10,20,22);
            break;
        }
        case 19: { // Keyboard
            SelectObject(hdc, t->ltGray); R(2,10,30,22);
            SelectObject(hdc, t->grayPen);
            L(6,14,8,14); L(10,14,12,14); L(14,14,16,14); L(18,14,20,14); L(22,14,26,14); L(8,18,24,18);
            break;
        }
        case 20: { // Mouse
            SelectObject(hdc, t->thickBlackPen); L(16,12,16,2);
            SelectObject(hdc, t->ltGray); SelectObject(hdc, t->blackPen); E(10,10,22,30);
            L(10,18,22,18); L(16,10,16,18);
            break;
        }

        // --- Group 3: System Controls & Apps ---
        case 21: { // App Window
            SelectObject(hdc, t->ltGray); R(4,6,28,26);
            SelectObject(hdc, t->blue);   R(4,6,28,10);
            SelectObject(hdc, t->white);  R(6,12,26,24);
            break;
        }
        case 22: { // Control Panel
            SelectObject(hdc, t->ltGray); R(4,4,28,28);
            SelectObject(hdc, t->gray); R(6,6,26,10);
            SelectObject(hdc, t->blackPen); L(9,12,9,24); L(16,12,16,24); L(23,12,23,24);
            SelectObject(hdc, t->blue); R(7,13,12,17);
            SelectObject(hdc, t->red); R(14,19,19,23);
            SelectObject(hdc, t->green); R(21,15,26,19);
            break;
        }
        case 23: { // Terminal
            SelectObject(hdc, t->black); R(4,6,28,26);
            SelectObject(hdc, t->white); R(4,6,28,10);
            SelectObject(hdc, t->green); R(6,14,10,16);
            break;
        }
        case 24: { // Briefcase
            SelectObject(hdc, t->brown); R(4,10,28,28);
            SelectObject(hdc, t->blackPen); L(11,10,11,28); L(21,10,21,28);
            SelectObject(hdc, t->ltGray); POINT handle[] = { PT(12,10), PT(12,5), PT(20,5), PT(20,10) }; Polyline(hdc, handle, 4);
            SelectObject(hdc, t->yellow); R(9,16,13,19); R(19,16,23,19);
            break;
        }
        case 25: { // Program Group
            SelectObject(hdc, t->ltGray); R(2,2,18,18); SelectObject(hdc, t->blue); R(2,2,18,6);
            SelectObject(hdc, t->ltGray); R(8,8,24,24); SelectObject(hdc, t->blue); R(8,8,24,12);
            SelectObject(hdc, t->ltGray); R(14,14,30,30); SelectObject(hdc, t->blue); R(14,14,30,18);
            SelectObject(hdc, t->white); R(16,20,28,28);
            break;
        }
        case 26: { // Settings / Gears
            SelectObject(hdc, t->gray); E(6,6,26,26);
            SelectObject(hdc, t->ltGray); E(11,11,21,21);
            SelectObject(hdc, t->white); E(13,13,19,19);
            SelectObject(hdc, t->blackPen);
            L(16,2,16,6); L(16,26,16,30); L(2,16,6,16); L(26,16,30,16);
            L(6,6,9,9); L(23,23,26,26); L(26,6,23,9); L(6,26,9,23);
            break;
        }
        case 27: { // Calculator
            SelectObject(hdc, t->gray); R(5,3,27,29);
            SelectObject(hdc, t->green); R(8,6,24,11);
            SelectObject(hdc, t->white); R(8,14,12,17); R(14,14,18,17); R(20,14,24,17);
            R(8,19,12,22); R(14,19,18,22); R(20,19,24,22);
            SelectObject(hdc, t->red); R(8,24,12,27);
            SelectObject(hdc, t->blue); R(14,24,24,27);
            break;
        }
        case 28: { // Clock
            SelectObject(hdc, t->ltGray); E(4,4,28,28);
            SelectObject(hdc, t->white); E(6,6,26,26);
            SelectObject(hdc, t->black); E(15,15,17,17);
            SelectObject(hdc, t->thickBlackPen); L(16,16,11,11); L(16,16,22,12);
            break;
        }
        case 29: { // User Profile
            SelectObject(hdc, t->ltGray); R(6,4,26,28);
            SelectObject(hdc, t->blue); E(12,8,20,16);
            POINT sShoulders[] = { PT(8,24), PT(11,18), PT(21,18), PT(24,24) }; POLY(sShoulders);
            break;
        }
        case 30: { // Calendar (NEW)
            SelectObject(hdc, t->white); R(4,6,28,28);
            SelectObject(hdc, t->red); R(4,6,28,12);
            SelectObject(hdc, t->blackPen); L(10,2,10,6); L(22,2,22,6);
            SelectObject(hdc, t->blue); R(8,16,12,20); R(14,16,18,20); R(20,16,24,20);
            break;
        }
        case 31: { // Database / Server (NEW)
            SelectObject(hdc, t->silver);
            E(6,4,26,10); R(6,7,26,15); E(6,12,26,18); R(6,15,26,23); E(6,20,26,26);
            SelectObject(hdc, t->green); E(20,8,22,10); E(20,16,22,18); E(20,23,22,25);
            break;
        }

        // --- Group 4: Actions & Editing Tools ---
        case 32: { // Search (Magnifying Glass - Ferrule square removed)
            SelectObject(hdc, t->cyan); E(3,3,19,19);
            SelectObject(hdc, t->whitePen); L(6,6,11,6); SelectObject(hdc, t->blackPen);
            SelectObject(hdc, t->brown);
            POINT handle[] = { PT(13,13), PT(16,10), PT(28,22), PT(25,25) }; POLY(handle);
            break;
        }
        case 33: { // Cut
            SelectObject(hdc, t->silver);
            POINT blade1[] = { PT(6,6), PT(24,20), PT(27,20), PT(16,14) }; POLY(blade1);
            POINT blade2[] = { PT(6,20), PT(24,6), PT(27,6), PT(16,12) }; POLY(blade2);
            SelectObject(hdc, t->blue); E(2,3,10,11); E(2,15,10,23);
            SelectObject(hdc, t->white); E(4,5,8,9); E(4,17,8,21);
            SelectObject(hdc, t->black); E(14,12,16,14);
            break;
        }
        case 34: { // Copy
            SelectObject(hdc, t->white);
            POINT p1[] = { PT(4,4), PT(14,4), PT(20,10), PT(20,26), PT(4,26) }; POLY(p1);
            POINT p2[] = { PT(12,8), PT(22,8), PT(28,14), PT(28,30), PT(12,30) }; POLY(p2);
            break;
        }
        case 35: { // Paste
            SelectObject(hdc, t->brown); R(6,4,26,30);
            SelectObject(hdc, t->silver); R(12,2,20,8);
            SelectObject(hdc, t->white); R(8,10,24,28);
            SelectObject(hdc, t->grayPen); L(10,14,22,14); L(10,18,22,18);
            break;
        }
        case 36: { // Undo
            SelectObject(hdc, t->blue);
            POINT arrow[] = { PT(14,10), PT(14,4), PT(2,12), PT(14,20), PT(14,14), PT(22,14), PT(22,24), PT(28,24), PT(28,10) };
            POLY(arrow);
            break;
        }
        case 37: { // Help
            SelectObject(hdc, t->blue);
            POINT bookL[] = { PT(3,12), PT(15,14), PT(15,28), PT(3,26) }; POLY(bookL);
            POINT bookR[] = { PT(17,14), PT(29,12), PT(29,26), PT(17,28) }; POLY(bookR);
            SelectObject(hdc, t->white);
            POINT pageL[] = { PT(5,13), PT(15,15), PT(15,27), PT(5,25) }; POLY(pageL);
            POINT pageR[] = { PT(17,15), PT(27,13), PT(27,25), PT(17,27) }; POLY(pageR);
            SelectObject(hdc, t->thickBlackPen); SelectObject(hdc, t->yellow);
            POINT q[] = { PT(12,5), PT(16,2), PT(20,5), PT(20,9), PT(16,13), PT(16,16) };
            Polyline(hdc, q, 6);
            R(15,18,18,21);
            break;
        }
        case 38: { // Delete
            SelectObject(hdc, t->red);
            POINT xpts[] = { PT(4,8), PT(8,4), PT(16,12), PT(24,4), PT(28,8), PT(20,16), PT(28,24), PT(24,28), PT(16,20), PT(8,28), PT(4,24), PT(12,16) };
            POLY(xpts);
            break;
        }
        case 39: { // Refresh / Reload
            SelectObject(hdc, t->blue);
            POINT arrow1[] = { PT(16,4), PT(26,4), PT(26,14), PT(21,9), PT(16,12) }; POLY(arrow1);
            POINT arrow2[] = { PT(16,28), PT(6,28), PT(6,18), PT(11,23), PT(16,20) }; POLY(arrow2);
            break;
        }
        case 40: { // Pushpin
            SelectObject(hdc, t->silver);
            POINT pinTip[] = { PT(6,26), PT(14,18), PT(12,16) }; POLY(pinTip);
            SelectObject(hdc, t->red);
            POINT body[] = { PT(12,16), PT(22,6), PT(26,10), PT(16,20) }; POLY(body);
            E(18,2,28,12);
            break;
        }
        case 41: { // Trash / Shredder (NEW)
            SelectObject(hdc, t->ltGray); R(6,6,26,14);
            SelectObject(hdc, t->black); R(8,10,24,12);
            SelectObject(hdc, t->whitePen); L(10,14,10,26); L(14,14,14,28); L(18,14,18,25); L(22,14,22,27);
            break;
        }
        case 42: { // Chart / Analytics (NEW)
            SelectObject(hdc, t->white); R(4,4,28,28);
            SelectObject(hdc, t->blue); R(8,18,12,24);
            SelectObject(hdc, t->red); R(14,12,18,24);
            SelectObject(hdc, t->green); R(20,8,24,24);
            SelectObject(hdc, t->blackPen); L(6,24,26,24); L(6,6,6,24);
            break;
        }

        // --- Group 5: Network, Web & Mail ---
        case 43: { // Home
            SelectObject(hdc, t->white); R(6,14,26,28);
            SelectObject(hdc, t->red); POINT roof[] = { PT(16,4), PT(30,14), PT(2,14) }; POLY(roof);
            SelectObject(hdc, t->brown); R(14,20,20,28);
            break;
        }
        case 44: { // Network
            SelectObject(hdc, t->thickBlackPen); L(16,16,16,28); L(8,28,24,28); L(8,28,8,24); L(24,28,24,24);
            SelectObject(hdc, t->blackPen);
            SelectObject(hdc, t->ltGray); R(10,4,22,14); SelectObject(hdc, t->blue); R(12,6,20,12);
            SelectObject(hdc, t->ltGray); R(2,16,10,24); SelectObject(hdc, t->blue); R(4,18,8,22);
            SelectObject(hdc, t->ltGray); R(22,16,30,24); SelectObject(hdc, t->blue); R(24,18,28,22);
            break;
        }
        case 45: { // Globe / Web
            SelectObject(hdc, t->blue); E(4,4,28,28);
            SelectObject(hdc, t->cyan); E(8,4,24,28);
            L(4,16,28,16); L(6,10,26,10); L(6,22,26,22);
            break;
        }
        case 46: { // Mail
            SelectObject(hdc, t->white); R(2,8,30,24);
            L(2,8,16,18); L(30,8,16,18);
            break;
        }
        case 47: { // Open Mail
            SelectObject(hdc, t->ltGray); R(4,12,28,28);
            SelectObject(hdc, t->white); R(7,5,25,20);
            SelectObject(hdc, t->grayPen); L(9,8,21,8); L(9,11,21,11); L(9,14,17,14);
            SelectObject(hdc, t->white); SelectObject(hdc, t->blackPen);
            POINT env[] = { PT(4,14), PT(16,22), PT(28,14), PT(28,28), PT(4,28) }; POLY(env);
            break;
        }

        // --- Group 6: Security, Media & Status ---
        case 48: { // Warning
            SelectObject(hdc, t->yellow);
            POINT tri[] = { PT(16,4), PT(30,26), PT(2,26) }; POLY(tri);
            SelectObject(hdc, t->black); R(14,10,18,18); R(14,21,18,24);
            break;
        }
        case 49: { // Error
            SelectObject(hdc, t->red); E(4,4,28,28);
            SelectObject(hdc, t->thickWhitePen); L(10,10,22,22); L(10,22,22,10);
            break;
        }
        case 50: { // OK / Checkmark
            SelectObject(hdc, t->green);
            POINT chk[] = { PT(10,18), PT(14,24), PT(28,6), PT(24,4), PT(13,19), PT(8,14) }; POLY(chk);
            break;
        }
        case 51: { // Favorites
            SelectObject(hdc, t->yellow);
            POINT star[] = { PT(16,2), PT(20,10), PT(30,10), PT(22,16), PT(26,26), PT(16,20), PT(6,26), PT(10,16), PT(2,10), PT(12,10) };
            POLY(star);
            break;
        }
        case 52: { // Speaker
            SelectObject(hdc, t->gray); R(6,12,12,20);
            POINT cone[] = { PT(12,12), PT(20,6), PT(20,26), PT(12,20) }; POLY(cone);
            SelectObject(hdc, t->thickBlackPen); L(24,10,24,22); L(28,6,28,26);
            break;
        }
        case 53: { // Volume Waves
            SelectObject(hdc, t->gray); R(3,12,8,20);
            POINT cone[] = { PT(8,12), PT(15,6), PT(15,26), PT(8,20) }; POLY(cone);
            SelectObject(hdc, t->thickBlackPen);
            POINT w1[] = { PT(19,10), PT(22,16), PT(19,22) }; Polyline(hdc, w1, 3);
            POINT w2[] = { PT(24,6), PT(28,16), PT(24,26) }; Polyline(hdc, w2, 3);
            break;
        }
        case 54: { // Security Key
            SelectObject(hdc, t->yellow); E(4,8,14,18);
            SelectObject(hdc, t->white); E(7,11,11,15);
            SelectObject(hdc, t->yellow); R(12,11,27,15); R(21,15,24,19); R(24,15,27,21);
            break;
        }
        case 55: { // Security Lock
            SelectObject(hdc, t->silver); SelectObject(hdc, t->thickBlackPen);
            POINT arch[] = { PT(10,16), PT(10,8), PT(22,8), PT(22,16) }; Polyline(hdc, arch, 4);
            SelectObject(hdc, t->blackPen); SelectObject(hdc, t->yellow); R(7,14,25,28);
            SelectObject(hdc, t->black); E(14,18,18,22); R(15,21,17,25);
            break;
        }
        case 56: { // Security Shield (NEW)
            SelectObject(hdc, t->blue);
            POINT shield[] = { PT(6,4), PT(26,4), PT(26,16), PT(16,28), PT(6,16) }; POLY(shield);
            SelectObject(hdc, t->yellow);
            POINT inner[] = { PT(10,7), PT(22,7), PT(22,15), PT(16,23), PT(10,15) }; POLY(inner);
            break;
        }
        case 57: { // Movie / Video Clip (NEW)
            SelectObject(hdc, t->black); R(4,6,28,26);
            SelectObject(hdc, t->white); R(6,8,26,24);
            SelectObject(hdc, t->blackPen); L(6,12,26,12); L(6,20,26,20);
            SelectObject(hdc, t->thickBlackPen); L(10,8,10,24); L(22,8,22,24);
            break;
        }
        case 58: { // Recycle Bin
            SelectObject(hdc, t->gray); E(8,25,24,29);
            SelectObject(hdc, t->ltGray);
            POINT bin[] = { PT(6,8), PT(26,8), PT(24,27), PT(8,27) }; POLY(bin);
            SelectObject(hdc, t->gray); E(6,6,26,10);
            SelectObject(hdc, t->blackPen); L(12,10,12,25); L(16,11,16,26); L(20,10,20,25);
            break;
        }
        case 59: { // Recycle Bin Full
            SelectObject(hdc, t->white); E(8,4,16,11); E(14,2,22,10); E(18,5,25,12);
            SelectObject(hdc, t->gray); E(8,25,24,29);
            SelectObject(hdc, t->ltGray); POINT bin[] = { PT(6,10), PT(26,10), PT(24,27), PT(8,27) }; POLY(bin);
            SelectObject(hdc, t->blackPen); L(12,12,12,25); L(16,13,16,26); L(20,12,20,25);
            break;
        }
    }
}

// ---------------------------------------------------------
// APP LOGIC & WINDOW MANAGEMENT
// ---------------------------------------------------------

int CalculateTotalHeight(int winWidth) {
    int pad = 40;
    int cellW = (32 * g_scale) + pad;
    int cellH = (32 * g_scale) + pad + 15;
    int cols = max(1, winWidth / cellW);
    int curY = TOP_BAR_HEIGHT + 10;

    for (size_t g = 0; g < NUM_GROUPS; g++) {
        curY += 30; // Category Header Banner
        int rows = (g_groups[g].count + cols - 1) / cols;
        curY += rows * cellH + 15;
    }
    return curY;
}

int GetIconAtPoint(int mx, int my, int winWidth, int scrollY, int* outIconId) {
    int pad = 40;
    int iconSize = 32 * g_scale;
    int cellW = iconSize + pad;
    int cellH = iconSize + pad + 15;
    int cols = max(1, winWidth / cellW);
    int curY = TOP_BAR_HEIGHT + 10 - scrollY;

    for (size_t g = 0; g < NUM_GROUPS; g++) {
        curY += 30; // Header offset
        for (int i = 0; i < g_groups[g].count; i++) {
            int row = i / cols;
            int col = i % cols;
            int x = col * cellW + (pad / 2);
            int y = curY + row * cellH;

            if (mx >= x && mx <= x + iconSize + 10 && my >= y && my <= y + cellH) {
                *outIconId = g_groups[g].startId + i;
                return 1;
            }
        }
        int rows = (g_groups[g].count + cols - 1) / cols;
        curY += rows * cellH + 15;
    }
    return 0;
}

void UpdateLayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    RECT rcStatus = {0};
    if (g_hStatus) GetWindowRect(g_hStatus, &rcStatus);
    int statusH = rcStatus.bottom - rcStatus.top;

    int totalHeight = CalculateTotalHeight(rc.right);

    SCROLLINFO si = {0};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = totalHeight;
    si.nPage = max(0, rc.bottom - TOP_BAR_HEIGHT - statusH); 

    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(hwnd, SB_VERT, &si); 
    g_scrollY = si.nPos;
}

void OpenInputFile(HWND hwnd) {
    OPENFILENAME ofn;
    char szFileName[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "C Source Files (*.c)\0*.c\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "c";

    if (GetOpenFileName(&ofn)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Opened file for input:\n%s\n\n(File stream available for processing!)", szFileName);
        MessageBox(hwnd, msg, "File Opened", MB_OK | MB_ICONINFORMATION);
        
        snprintf(g_loadedFile, sizeof(g_loadedFile), "Win95 GDI Shell - Loaded: %s", szFileName);
        SetWindowText(hwnd, g_loadedFile);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HMENU hMenu = CreateMenu();
            HMENU hSubMenu = CreatePopupMenu();
            AppendMenu(hSubMenu, MF_STRING, IDM_FILE_OPEN, "&Open .C File...");
            AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hSubMenu, MF_STRING, IDM_FILE_EXIT, "E&xit");
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, "&File");
            SetMenu(hwnd, hMenu);

            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icex);

            // Status Bar
            g_hStatus = CreateWindowEx(0, STATUSCLASSNAME, " Ready - Click any icon to display name", 
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 
                0, 0, 0, 0, hwnd, (HMENU)1003, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            hTrackbar = CreateWindowEx(0, TRACKBAR_CLASS, "Trackbar",
                WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
                90, 10, 200, 30, hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            
            SendMessage(hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(1, 8));
            SendMessage(hTrackbar, TBM_SETPOS, TRUE, g_scale);

            hLabel = CreateWindowEx(0, "STATIC", "Icon Scale:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                15, 15, 75, 20, hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(FALSE, 0));
            return 0;
        }

        case WM_COMMAND: {
            switch(LOWORD(wParam)) {
                case IDM_FILE_OPEN: OpenInputFile(hwnd); break;
                case IDM_FILE_EXIT: PostQuitMessage(0); break;
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int clickedIconId = -1;
            if (GetIconAtPoint(mx, my, rc.right, g_scrollY, &clickedIconId)) {
                char buf[256];
                snprintf(buf, sizeof(buf), " Selected Icon: %s (ID: %d)", g_iconNames[clickedIconId], clickedIconId);
                SetWindowText(g_hStatus, buf);
            }
            return 0;
        }

        case WM_SIZE:
            SendMessage(g_hStatus, WM_SIZE, 0, 0);
            UpdateLayout(hwnd);
            return 0;

        case WM_VSCROLL: {
            SCROLLINFO si = {0};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int yPos = si.nPos;

            switch (LOWORD(wParam)) {
                case SB_TOP:        yPos = si.nMin; break;
                case SB_BOTTOM:     yPos = si.nMax; break;
                case SB_LINEUP:     yPos -= 30; break;
                case SB_LINEDOWN:   yPos += 30; break;
                case SB_PAGEUP:     yPos -= si.nPage; break;
                case SB_PAGEDOWN:   yPos += si.nPage; break;
                case SB_THUMBTRACK: yPos = si.nTrackPos; break;
            }

            si.fMask = SIF_POS;
            si.nPos = yPos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si); 
            
            if (si.nPos != g_scrollY) {
                g_scrollY = si.nPos;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_HSCROLL:
            if ((HWND)lParam == hTrackbar) {
                g_scale = SendMessage(hTrackbar, TBM_GETPOS, 0, 0);
                UpdateLayout(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right, h = rc.bottom;

            RECT rcStatus = {0};
            if (g_hStatus) GetWindowRect(g_hStatus, &rcStatus);
            int statusH = rcStatus.bottom - rcStatus.top;

            // Double Buffering
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBM = SelectObject(memDC, memBM);

            HBRUSH bgBrush = CreateSolidBrush(RGB(0, 128, 128)); // Win95 Teal
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);

            RECT topBar = {0, 0, w, TOP_BAR_HEIGHT};
            FillRect(memDC, &topBar, (HBRUSH)(COLOR_3DFACE + 1));

            // Theme Allocation
            Win95Theme t;
            t.white    = CreateSolidBrush(RGB(255, 255, 255));
            t.black    = CreateSolidBrush(RGB(0, 0, 0));
            t.gray     = CreateSolidBrush(RGB(128, 128, 128));
            t.ltGray   = CreateSolidBrush(RGB(192, 192, 192));
            t.blue     = CreateSolidBrush(RGB(0, 0, 128));
            t.cyan     = CreateSolidBrush(RGB(0, 255, 255));
            t.red      = CreateSolidBrush(RGB(255, 0, 0));
            t.green    = CreateSolidBrush(RGB(0, 128, 0));
            t.yellow   = CreateSolidBrush(RGB(255, 255, 0));
            t.dkYellow = CreateSolidBrush(RGB(192, 153, 0));
            t.brown    = CreateSolidBrush(RGB(128, 64, 0));
            t.silver   = CreateSolidBrush(RGB(224, 224, 224));
            
            int pScale = g_scale/4 < 1 ? 1 : g_scale/4;
            t.blackPen      = CreatePen(PS_SOLID, pScale, RGB(0, 0, 0));
            t.thickBlackPen = CreatePen(PS_SOLID, pScale * 2, RGB(0, 0, 0));
            t.thickWhitePen = CreatePen(PS_SOLID, pScale * 2, RGB(255, 255, 255));
            t.whitePen      = CreatePen(PS_SOLID, pScale, RGB(255, 255, 255));
            t.grayPen       = CreatePen(PS_SOLID, pScale, RGB(128, 128, 128));

            HFONT hFont = CreateFont(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                     DEFAULT_PITCH | FF_SWISS, "Arial");
            SelectObject(memDC, hFont);

            int pad = 40;
            int iconSize = 32 * g_scale;
            int cellW = iconSize + pad;
            int cellH = iconSize + pad + 15;
            int cols = max(1, w / cellW);
            int curY = TOP_BAR_HEIGHT + 10 - g_scrollY;

            for (size_t g = 0; g < NUM_GROUPS; g++) {
                // Draw Category Header Banner
                if (curY + 30 >= TOP_BAR_HEIGHT && curY <= h - statusH) {
                    RECT groupRect = { 10, curY, w - 10, curY + 24 };
                    HBRUSH headerBrush = CreateSolidBrush(RGB(0, 64, 128));
                    FillRect(memDC, &groupRect, headerBrush);
                    DeleteObject(headerBrush);
                    DrawEdge(memDC, &groupRect, EDGE_RAISED, BF_RECT);

                    SetTextColor(memDC, RGB(255, 255, 255));
                    SetBkMode(memDC, TRANSPARENT);
                    TextOut(memDC, groupRect.left + 10, groupRect.top + 4, g_groups[g].title, (int)strlen(g_groups[g].title));
                }

                curY += 30;

                // Draw Group Icons Grid
                for (int i = 0; i < g_groups[g].count; i++) {
                    int row = i / cols;
                    int col = i % cols;
                    int x = col * cellW + (pad / 2);
                    int y = curY + row * cellH;
                    int iconId = g_groups[g].startId + i;

                    if (y + iconSize + 30 >= TOP_BAR_HEIGHT && y <= h - statusH) {
                        DrawWin95Icon(memDC, x, y, g_scale, iconId, &t);

                        SetTextColor(memDC, RGB(255, 255, 255));
                        RECT textRect = {x - 20, y + iconSize + 5, x + iconSize + 20, y + iconSize + 25};
                        DrawText(memDC, g_iconNames[iconId], -1, &textRect, DT_CENTER | DT_SINGLELINE);
                    }
                }

                int rows = (g_groups[g].count + cols - 1) / cols;
                curY += rows * cellH + 15;
            }

            // Cleanup
            DeleteObject(t.white); DeleteObject(t.black); DeleteObject(t.gray);
            DeleteObject(t.ltGray); DeleteObject(t.blue); DeleteObject(t.cyan);
            DeleteObject(t.red); DeleteObject(t.green); DeleteObject(t.yellow);
            DeleteObject(t.dkYellow); DeleteObject(t.brown); DeleteObject(t.silver);
            DeleteObject(t.blackPen); DeleteObject(t.thickBlackPen); 
            DeleteObject(t.thickWhitePen); DeleteObject(t.whitePen); 
            DeleteObject(t.grayPen); DeleteObject(hFont);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBM); DeleteObject(memBM); DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "Win95Grid";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClass(&wc);
    HWND hwnd = CreateWindowEx(0, "Win95Grid", g_loadedFile, 
                               WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_VISIBLE, 
                               CW_USEDEFAULT, CW_USEDEFAULT, 900, 700, 
                               NULL, NULL, hInstance, NULL);
                               
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}