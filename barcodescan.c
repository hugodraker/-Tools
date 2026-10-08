/*
 * ============================================================================
 * Barcodescan.c - Native Win32 Vector Barcode scan 
 * ============================================================================
 * 
 * COMPILE INSTRUCTIONS (MinGW GCC):
 * gcc BarcodeScan.c -o BarcodeScan.exe -mwindows -lvfw32 -lgdi32 -lm
 * 
 * 2026-10-08 Hugo pdf417 does not work yet
 * ============================================================================
 * PUBLIC DOMAIN NOTICE
 * Free and unencumbered software released into the public domain.
 * ============================================================================
 */
#include <windows.h>
#include <vfw.h>
#include <stdio.h>

#define BTN_ON 101

HWND hMainWnd, hBtnOn, hCamWnd;
char szIniPath[MAX_PATH];
char szTargetTitle[256] = {0};
char szTargetClass[256] = {0};
BOOL bSelectingWindow = FALSE;

// UPC-A Standard Module Widths for digits 0-9
// Both L-digits (Left) and R-digits (Right) share this identical sequence of widths.
int L_PATTERNS[10][4] = {
    {3,2,1,1}, {2,2,2,1}, {2,1,2,2}, {1,4,1,1}, {1,1,3,2},
    {1,2,3,1}, {1,1,1,4}, {1,3,1,2}, {1,2,1,3}, {3,1,1,2}
};

// Check if a sequence of runs matches a guard pattern (Start/Middle/End)
int MatchGuard(int *r, int count) {
    int sum = 0;
    for (int i = 0; i < count; i++) sum += r[i];
    if (sum == 0) return 0;
    float avg = (float)sum / count;
    for (int i = 0; i < count; i++) {
        float ratio = (float)r[i] / avg;
        if (ratio < 0.4f || ratio > 2.5f) return 0; // Allow variance for webcam blur
    }
    return 1;
}

// Convert 4 bar/space runs into a standard UPC digit (7 modules total)
int MatchDigit(int *r) {
    int sum = r[0] + r[1] + r[2] + r[3];
    if (sum < 4) return -1;
    int m[4];
    for (int i = 0; i < 4; i++) {
        m[i] = (r[i] * 7 + (sum / 2)) / sum; // Round to nearest module width
    }
    for (int d = 0; d < 10; d++) {
        if (L_PATTERNS[d][0] == m[0] && L_PATTERNS[d][1] == m[1] &&
            L_PATTERNS[d][2] == m[2] && L_PATTERNS[d][3] == m[3]) {
            return d;
        }
    }
    return -1;
}

// Scans a single horizontal row of RGB pixels for a UPC-A pattern
BOOL DecodeScanline(unsigned char* row, int width, char* out_buf) {
    int runs[1024] = {0};
    int num_runs = 0;
    
    // Adaptive thresholding: calculate average brightness to counter glare
    int sum_lum = 0;
    for (int i = 0; i < width; i++) {
        sum_lum += (row[i * 3] + row[i * 3 + 1] + row[i * 3 + 2]) / 3;
    }
    int threshold = sum_lum / width;

    // Convert pixel row to alternating run-lengths
    int current_color = ((row[0] + row[1] + row[2]) / 3) < threshold ? 0 : 1;
    int current_len = 0;
    for (int i = 0; i < width; i++) {
        int color = ((row[i*3] + row[i*3+1] + row[i*3+2]) / 3) < threshold ? 0 : 1;
        if (color == current_color) {
            current_len++;
        } else {
            if (num_runs < 1024) runs[num_runs++] = current_len;
            current_color = color;
            current_len = 1;
        }
    }
    if (num_runs < 1024 && current_len > 0) runs[num_runs++] = current_len;

    // UPC-A requires exactly 59 distinct color blocks (runs)
    if (num_runs < 59) return FALSE;

    for (int i = 0; i < num_runs - 59; i++) {
        if (!MatchGuard(&runs[i], 3)) continue; // Start Guard (B-W-B)
        
        int digits[12];
        int run_idx = i + 3;
        BOOL valid = TRUE;
        
        for (int d = 0; d < 6; d++) { // 6 Left Digits
            digits[d] = MatchDigit(&runs[run_idx]);
            if (digits[d] == -1) { valid = FALSE; break; }
            run_idx += 4;
        }
        if (!valid || !MatchGuard(&runs[run_idx], 5)) continue; // Middle Guard (W-B-W-B-W)
        run_idx += 5;
        
        for (int d = 6; d < 12; d++) { // 6 Right Digits
            digits[d] = MatchDigit(&runs[run_idx]);
            if (digits[d] == -1) { valid = FALSE; break; }
            run_idx += 4;
        }
        if (!valid || !MatchGuard(&runs[run_idx], 3)) continue; // End Guard (B-W-B)
        
        // Verify Checksum
        int sum_odd = digits[0] + digits[2] + digits[4] + digits[6] + digits[8] + digits[10];
        int sum_even = digits[1] + digits[3] + digits[5] + digits[7] + digits[9];
        int checksum = (sum_odd * 3 + sum_even) % 10;
        checksum = (checksum == 0) ? 0 : 10 - checksum;
        
        if (checksum == digits[11]) {
            for (int d = 0; d < 12; d++) out_buf[d] = '0' + digits[d];
            out_buf[12] = '\0';
            return TRUE;
        }
    }
    return FALSE;
}

// Simulates keystrokes into the specified target window
void TypeText(const char* text) {
    HWND hTarget = FindWindow(szTargetClass[0] ? szTargetClass : NULL, 
                              szTargetTitle[0] ? szTargetTitle : NULL);
    if (!hTarget) return;

    SetForegroundWindow(hTarget);
    Sleep(50); 

    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    while (*text) {
        SHORT key = VkKeyScan(*text);
        UINT vkey = key & 0xFF;
        inputs[0].ki.wVk = vkey;
        inputs[1].ki.wVk = vkey;
        SendInput(2, inputs, sizeof(INPUT));
        text++;
    }
    
    inputs[0].ki.wVk = VK_RETURN;
    inputs[1].ki.wVk = VK_RETURN;
    SendInput(2, inputs, sizeof(INPUT));
}

// VFW Camera Callback
LRESULT CALLBACK FrameCallback(HWND hWnd, LPVIDEOHDR lpVHdr) {
    if (!lpVHdr->lpData) return 0;

    int width = 640, height = 480;
    int stride = (width * 3 + 3) & ~3; // 4-byte aligned
    char barcode[32];

    // Scan the middle 20 rows of the webcam feed to catch the barcode
    for (int y = height / 2 - 10; y < height / 2 + 10; y += 2) {
        unsigned char* row = (unsigned char*)lpVHdr->lpData + (y * stride);
        
        if (DecodeScanline(row, width, barcode)) {
            SendMessage(hCamWnd, WM_CAP_SET_CALLBACK_FRAME, 0, 0); 
            TypeText(barcode);
            Sleep(2000); // 2-second debounce before scanning again
            SendMessage(hCamWnd, WM_CAP_SET_CALLBACK_FRAME, 0, (LPARAM)FrameCallback);
            break;
        }
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            hBtnOn = CreateWindow("BUTTON", "ON", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                  264, 8, 105, 170, hwnd, (HMENU)BTN_ON, NULL, NULL);

            GetModuleFileName(NULL, szIniPath, MAX_PATH);
            char *ext = strrchr(szIniPath, '.');
            if (ext) strcpy(ext, ".ini");

            GetPrivateProfileString("Settings", "TargetTitle", "", szTargetTitle, 256, szIniPath);
            GetPrivateProfileString("Settings", "TargetClass", "", szTargetClass, 256, szIniPath);

            hCamWnd = capCreateCaptureWindow("Webcam", WS_CHILD | WS_VISIBLE, 8, 8, 250, 170, hwnd, 0);
            SendMessage(hCamWnd, WM_CAP_DRIVER_CONNECT, 0, 0);

            // Force driver output to 24-bit RGB 640x480 so our C pointer math matches memory
            BITMAPINFO bmi = {0};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = 640;
            bmi.bmiHeader.biHeight = 480;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 24;
            bmi.bmiHeader.biCompression = BI_RGB;
            SendMessage(hCamWnd, WM_CAP_SET_VIDEOFORMAT, sizeof(bmi), (LPARAM)&bmi);

            SendMessage(hCamWnd, WM_CAP_SET_SCALE, TRUE, 0);
            SendMessage(hCamWnd, WM_CAP_SET_PREVIEWRATE, 66, 0);
            SendMessage(hCamWnd, WM_CAP_SET_PREVIEW, TRUE, 0);
            SendMessage(hCamWnd, WM_CAP_SET_CALLBACK_FRAME, 0, (LPARAM)FrameCallback);
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == BTN_ON) {
                bSelectingWindow = TRUE;
                SetCapture(hwnd);
                SetCursor(LoadCursor(NULL, IDC_CROSS));
            }
            break;

        case WM_LBUTTONUP:
            if (bSelectingWindow) {
                ReleaseCapture();
                bSelectingWindow = FALSE;
                SetCursor(LoadCursor(NULL, IDC_ARROW));

                POINT pt;
                GetCursorPos(&pt);
                HWND hit = WindowFromPoint(pt);
                HWND hTarget = GetAncestor(hit, GA_ROOTOWNER);

                if (hTarget != hwnd && hTarget != hBtnOn) {
                    GetWindowText(hTarget, szTargetTitle, 256);
                    GetClassName(hTarget, szTargetClass, 256);
                    WritePrivateProfileString("Settings", "TargetTitle", szTargetTitle, szIniPath);
                    WritePrivateProfileString("Settings", "TargetClass", szTargetClass, szIniPath);
                }
            }
            break;

        case WM_DESTROY:
            SendMessage(hCamWnd, WM_CAP_DRIVER_DISCONNECT, 0, 0);
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = "UPCApp";
    RegisterClass(&wc);

    hMainWnd = CreateWindowEx(WS_EX_TOPMOST, "UPCApp", "Native UPC Typer",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              100, 100, 400, 230, NULL, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}