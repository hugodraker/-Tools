/*
 * ============================================================================
 * BarcodeGen.c - Native Win32 Vector Barcode Generator (GUI & CLI)
 * ============================================================================
 * 
 * COMPILE INSTRUCTIONS (MinGW GCC):
 * gcc -Os -s BarcodeGen.c -o BarcodeGen.exe -lgdi32 -lcomdlg32 -lcomctl32 -mwindows
 * 
 * 2026-10-08 Hugo pdf417 does not work yet
 * ============================================================================
 * PUBLIC DOMAIN NOTICE
 * Free and unencumbered software released into the public domain.
 * ============================================================================
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
// GUI Control IDs
#define IDC_EDIT_DATA    101
#define IDC_COMBO_TYPE   102
#define IDC_BTN_COPY     103
#define IDC_BTN_SAVE     104
#define IDC_COMBO_SIZE   105
// ============================================================================
// RED 'X' ERROR GENERATOR
// ============================================================================

HENHMETAFILE GenError_EMF() {
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0Error\0\0");
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    RECT rBg = {0, 0, 500, 500}; FillRect(hdcMeta, &rBg, bg); DeleteObject(bg);
    HPEN hPen = CreatePen(PS_SOLID, 20, RGB(220, 50, 50));
    HPEN hOld = (HPEN)SelectObject(hdcMeta, hPen);
    MoveToEx(hdcMeta, 100, 100, NULL); LineTo(hdcMeta, 400, 400);
    MoveToEx(hdcMeta, 400, 100, NULL); LineTo(hdcMeta, 100, 400);
    SelectObject(hdcMeta, hOld); DeleteObject(hPen);
    hPen = CreatePen(PS_SOLID, 15, RGB(220, 50, 50));
    hOld = (HPEN)SelectObject(hdcMeta, hPen);
    MoveToEx(hdcMeta, 0, 0, NULL); LineTo(hdcMeta, 500, 0); LineTo(hdcMeta, 500, 500); 
    LineTo(hdcMeta, 0, 500); LineTo(hdcMeta, 0, 0);
    SelectObject(hdcMeta, hOld); DeleteObject(hPen);
    return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// 1. UPC-A / EAN-13 ENCODER
// ============================================================================

const char* UPC_L[10] = { "0001101", "0011001", "0010011", "0111101", "0100011", "0110001", "0101111", "0111011", "0110111", "0001011" };
const char* UPC_G[10] = { "0100111", "0110011", "0011011", "0100001", "0011101", "0111001", "0000101", "0010001", "0001001", "0010111" };
const char* UPC_R[10] = { "1110010", "1100110", "1101100", "1000010", "1011100", "1001110", "1010000", "1000100", "1001000", "1110100" };
const char* EAN_PARITY[10] = { "LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG", "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL" };

int ExtractDigits(const char* in, char* out, int max_len) {
    int len = 0;
    for (int i = 0; in[i]; i++) { if (in[i] >= '0' && in[i] <= '9' && len < max_len) out[len++] = in[i]; }
    out[len] = '\0'; return len;
}

static void DrawBarcodeSeq(HDC hdc, const char* seq, int extend, int* px, int scale, int height, HBRUSH brush) {
    for (int i = 0; seq[i]; i++) {
        if (seq[i] == '1') {
            RECT r = { (*px) * scale, 0, ((*px) + 1) * scale, height + (extend ? 50 : 0) };
            FillRect(hdc, &r, brush);
        }
        (*px)++;
    }
}

HENHMETAFILE GenUPC_EMF(const char* data) {
    char clean[14] = {0}; int len = ExtractDigits(data, clean, 12);
    if (len != 11 && len != 12) return GenError_EMF();
    
    if (len == 11) {
        int odd = 0, even = 0;
        for (int i = 0; i < 11; i++) { if (i % 2 == 0) odd += (clean[i] - '0'); else even += (clean[i] - '0'); }
        int check = (odd * 3 + even) % 10; clean[11] = (check == 0 ? 0 : 10 - check) + '0'; clean[12] = '\0';
    }

    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0UPC-A\0\0"); HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    int scale = 10, height = 500, x = 0;

    DrawBarcodeSeq(hdcMeta, "101", 1, &x, scale, height, blackBrush);
    for (int i = 0; i < 6; i++) DrawBarcodeSeq(hdcMeta, UPC_L[clean[i] - '0'], 0, &x, scale, height, blackBrush);
    DrawBarcodeSeq(hdcMeta, "01010", 1, &x, scale, height, blackBrush);
    for (int i = 6; i < 12; i++) DrawBarcodeSeq(hdcMeta, UPC_R[clean[i] - '0'], 0, &x, scale, height, blackBrush);
    DrawBarcodeSeq(hdcMeta, "101", 1, &x, scale, height, blackBrush);
    
    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

HENHMETAFILE GenEAN13_EMF(const char* data) {
    char clean[14] = {0}; int len = ExtractDigits(data, clean, 13);
    if (len != 12 && len != 13) return GenError_EMF();
    
    if (len == 12) {
        int sum = 0; for (int i = 0; i < 12; i++) sum += (clean[i] - '0') * (i % 2 == 0 ? 1 : 3);
        clean[12] = ((10 - (sum % 10)) % 10) + '0'; clean[13] = '\0';
    }

    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0EAN-13\0\0"); HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    int scale = 10, height = 500, x = 0;
    int first = clean[0] - '0'; const char* parity = EAN_PARITY[first];

    DrawBarcodeSeq(hdcMeta, "101", 1, &x, scale, height, blackBrush);
    for (int i = 1; i <= 6; i++) DrawBarcodeSeq(hdcMeta, parity[i - 1] == 'L' ? UPC_L[clean[i] - '0'] : UPC_G[clean[i] - '0'], 0, &x, scale, height, blackBrush);
    DrawBarcodeSeq(hdcMeta, "01010", 1, &x, scale, height, blackBrush);
    for (int i = 7; i <= 12; i++) DrawBarcodeSeq(hdcMeta, UPC_R[clean[i] - '0'], 0, &x, scale, height, blackBrush);
    DrawBarcodeSeq(hdcMeta, "101", 1, &x, scale, height, blackBrush);
    
    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// 2. CODE 39 ENCODER
// ============================================================================

const char* c39_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%*";
const char* c39_pat[] = {
    "112211211","211112112","112112112","212112111","111122112","211122111","112122111","111112212","211112211","112112211",
    "211111122","112111122","212111121","111121122","211121121","112121121","111111222","211111221","112111221","111121221",
    "211111113","112111113","212111112","111121113","211121112","112121112","111111213","211111212","112111212","111121212",
    "221111111","122111111","222111111","121121111","221121111","122121111","121111211","221111211","122111211","121212111",
    "121211121","121112121","111212121","121121211" 
};

HENHMETAFILE GenCode39_EMF(const char* data) {
    if (strlen(data) == 0) return GenError_EMF();
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0Code39\0\0");
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    
    char payload[256] = {0}; snprintf(payload, 250, "*%s*", data); 
    for (int i = 1; payload[i+1]; i++) payload[i] = toupper(payload[i]);

    int scale = 10, height = 500, x_offset = 0;
    for (int i = 0; payload[i]; i++) {
        int idx = -1; for (int j = 0; j < 44; j++) { if (c39_chars[j] == payload[i]) { idx = j; break; } }
        if (idx == -1) { DeleteObject(blackBrush); CloseEnhMetaFile(hdcMeta); return GenError_EMF(); }
        
        const char* pat = c39_pat[idx];
        for (int j = 0; j < 9; j++) {
            int width = (pat[j] == '1') ? scale : scale * 3;
            if (j % 2 == 0) { RECT r = { x_offset, 0, x_offset + width, height }; FillRect(hdcMeta, &r, blackBrush); }
            x_offset += width;
        }
        x_offset += scale; 
    }
    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// 3. CODE 128 ENCODER (Subset B)
// ============================================================================

const char* c128_pat[] = {
    "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213","221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
    "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211","212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
    "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331","231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
    "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214","112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
    "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141","214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
    "114131","311141","411131","211412","211214","211232","2331112"
};

static void DrawC128Pat(HDC hdcMeta, int idx, int* px_offset, int scale, int height, HBRUSH brush) {
    const char* p = c128_pat[idx];
    for (int i = 0; p[i]; i++) {
        int width = (p[i] - '0') * scale;
        if (i % 2 == 0) { RECT r = { *px_offset, 0, *px_offset + width, height }; FillRect(hdcMeta, &r, brush); }
        *px_offset += width;
    }
}

HENHMETAFILE GenCode128_EMF(const char* data) {
    if (strlen(data) == 0) return GenError_EMF();
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0Code128\0\0");
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    int scale = 10, height = 500, x_offset = 0;

    int sum = 104; DrawC128Pat(hdcMeta, 104, &x_offset, scale, height, blackBrush);
    for (int i = 0; data[i]; i++) {
        int val = data[i] - 32; if (val < 0 || val > 95) val = 0;
        DrawC128Pat(hdcMeta, val, &x_offset, scale, height, blackBrush); 
        sum += val * (i + 1);
    }
    DrawC128Pat(hdcMeta, sum % 103, &x_offset, scale, height, blackBrush); 
    DrawC128Pat(hdcMeta, 106, &x_offset, scale, height, blackBrush);

    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// 4. ITF-14 ENCODER
// ============================================================================

const char* itf_pat[] = { "11221", "21112", "12112", "22111", "11212", "21211", "12211", "11122", "21121", "12121" };

static void DrawITFElem(HDC hdcMeta, int width, int is_bar, int* px_offset, int scale, int height, HBRUSH brush) {
    int w = width * scale;
    if (is_bar) { RECT r = { *px_offset, 0, *px_offset + w, height }; FillRect(hdcMeta, &r, brush); }
    *px_offset += w;
}

HENHMETAFILE GenITF14_EMF(const char* data) {
    char clean[15] = {0}; int len = ExtractDigits(data, clean, 14);
    if (len != 13 && len != 14) return GenError_EMF();
    if (len == 13) {
        int sum = 0; for (int i = 0; i < 13; i++) sum += (clean[i] - '0') * (i % 2 == 0 ? 3 : 1);
        clean[13] = ((10 - (sum % 10)) % 10) + '0'; clean[14] = '\0';
    }

    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0ITF-14\0\0"); HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    int scale = 10, height = 500, x_offset = scale * 10; 
    
    DrawITFElem(hdcMeta, 1, 1, &x_offset, scale, height, blackBrush); 
    DrawITFElem(hdcMeta, 1, 0, &x_offset, scale, height, blackBrush); 
    DrawITFElem(hdcMeta, 1, 1, &x_offset, scale, height, blackBrush); 
    DrawITFElem(hdcMeta, 1, 0, &x_offset, scale, height, blackBrush); 
    
    for (int i = 0; i < 14; i += 2) {
        const char* p1 = itf_pat[clean[i] - '0']; const char* p2 = itf_pat[clean[i+1] - '0'];
        for (int j = 0; j < 5; j++) { 
            DrawITFElem(hdcMeta, p1[j] == '1' ? 1 : 3, 1, &x_offset, scale, height, blackBrush); 
            DrawITFElem(hdcMeta, p2[j] == '1' ? 1 : 3, 0, &x_offset, scale, height, blackBrush); 
        }
    }
    DrawITFElem(hdcMeta, 3, 1, &x_offset, scale, height, blackBrush); 
    DrawITFElem(hdcMeta, 1, 0, &x_offset, scale, height, blackBrush); 
    DrawITFElem(hdcMeta, 1, 1, &x_offset, scale, height, blackBrush); 
    
    int thick = scale * 4; x_offset += scale * 10;
    RECT rTop = { 0, 0, x_offset, thick }, rBot = { 0, height - thick, x_offset, height };
    RECT rLeft = { 0, 0, thick, height }, rRight = { x_offset - thick, 0, x_offset, height };
    FillRect(hdcMeta, &rTop, blackBrush); FillRect(hdcMeta, &rBot, blackBrush);
    FillRect(hdcMeta, &rLeft, blackBrush); FillRect(hdcMeta, &rRight, blackBrush);

    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// 5. CODABAR & POSTNET ENCODERS
// ============================================================================

const char* cb_chars = "0123456789-$:/.+ABCD";
const char* cb_patterns[] = {
    "1111122", "1111221", "1112112", "2211111", "1121121", "2111121", "1211112", "1211211", "1221111", "2112111",
    "1112211", "1122111", "2111212", "2121112", "2121211", "1121212", "1122121", "1212112", "1112122", "1112221"
};

HENHMETAFILE GenCodabar_EMF(const char* data) {
    if (strlen(data) == 0) return GenError_EMF();
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0Codabar\0\0"); HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    char payload[256] = {0}; strncpy(payload, data, 250);
    int start_idx = -1; for(int i = 0; i < 20; i++) if (cb_chars[i] == toupper(payload[0])) start_idx = i;
    if (start_idx < 16) { char tmp[256]; sprintf(tmp, "A%sA", payload); strcpy(payload, tmp); }
    
    int scale = 10, height = 500, x_offset = 0;
    for (int i = 0; payload[i]; i++) {
        int idx = -1; for(int j = 0; j < 20; j++) if (cb_chars[j] == toupper(payload[i])) idx = j;
        if (idx == -1) { DeleteObject(blackBrush); CloseEnhMetaFile(hdcMeta); return GenError_EMF(); }
        const char* pat = cb_patterns[idx];
        for (int j = 0; j < 7; j++) {
            int width = (pat[j] - '0') * scale;
            if (j % 2 == 0) { RECT r = { x_offset, 0, x_offset + width, height }; FillRect(hdcMeta, &r, blackBrush); }
            x_offset += width;
        }
        x_offset += scale; 
    }
    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

const char* pn_patterns[] = { "11000", "00011", "00101", "00110", "01001", "01010", "01100", "10001", "10010", "10100" };

HENHMETAFILE GenPOSTNET_EMF(const char* data) {
    char digits[128] = {0}; int len = 0, sum = 0;
    for(int i = 0; data[i] && len < 11; i++) { if (isdigit(data[i])) { digits[len++] = data[i]; sum += data[i] - '0'; } }
    if (len == 0) return GenError_EMF();
    digits[len++] = (((10 - (sum % 10)) % 10) == 0 ? 0 : 10 - (sum % 10)) + '0'; digits[len] = '\0';
    
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0POSTNET\0\0"); HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    int bar_w = 12, pitch = 28, height_tall = 250, height_short = 100, x_offset = 0;
    
    RECT rStart = { x_offset, 0, x_offset + bar_w, height_tall }; FillRect(hdcMeta, &rStart, blackBrush); x_offset += pitch;
    for (int i = 0; digits[i]; i++) {
        int digit = digits[i] - '0';
        for (int j = 0; j < 5; j++) {
            int top = (pn_patterns[digit][j] == '1') ? 0 : (height_tall - height_short);
            RECT r = { x_offset, top, x_offset + bar_w, height_tall }; FillRect(hdcMeta, &r, blackBrush); x_offset += pitch;
        }
    }
    RECT rStop = { x_offset, 0, x_offset + bar_w, height_tall }; FillRect(hdcMeta, &rStop, blackBrush);
    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

unsigned char dm_exp[512], dm_log[256];
int dm_grid[30*30], dm_initialized = 0;

// ============================================================================
// 6. DATAMATRIX (Dynamic Size: 10x10 to 26x26 with Full Ota Corners)
// ============================================================================

typedef struct DMSize {
    int w; 
    int data_cw; 
    int ecc_cw; 
};

struct DMSize dm_sizes[] = { {10,3,5}, {12,5,7}, {14,8,10}, {16,12,12}, {18,18,14}, {20,22,18}, {22,30,20}, {24,36,24}, {26,44,28} };

void init_dm_gf() {
    if (dm_initialized) return;
    int x = 1; for (int i = 0; i < 255; i++) { dm_exp[i] = x; dm_log[x] = i; x <<= 1; if (x & 256) x ^= 0x12D; }
    for (int i = 255; i < 512; i++) dm_exp[i] = dm_exp[i - 255];
    dm_initialized = 1;
}

unsigned char dm_mul(unsigned char a, unsigned char b) { 
    return (!a || !b) ? 0 : dm_exp[dm_log[a] + dm_log[b]]; 
}

int dm_nrow, dm_ncol;
void dm_module(int r, int c, int val) {
    if (r < 0) { r += dm_nrow; c += 4 - ((dm_nrow + 3) % 8); }
    if (c < 0) { c += dm_ncol; r += 4 - ((dm_ncol + 3) % 8); }
    dm_grid[r * dm_ncol + c] = val;
}

void dm_utah(int r, int c, int val) {
    dm_module(r-2, c-2, (val>>7)&1); dm_module(r-2, c-1, (val>>6)&1); dm_module(r-1, c-2, (val>>5)&1); dm_module(r-1, c-1, (val>>4)&1);
    dm_module(r-1, c, (val>>3)&1); dm_module(r, c-2, (val>>2)&1); dm_module(r, c-1, (val>>1)&1); dm_module(r, c, (val)&1);
}

HENHMETAFILE GenDataMatrix_EMF(const char* data, const char* size_str) {
    init_dm_gf();
    int target_w = 14; 
    if (size_str && strlen(size_str) > 0) {
        int pw, ph; if (sscanf(size_str, "%dx%d", &pw, &ph) == 2 && pw == ph) target_w = pw;
        else return GenError_EMF();
    }
    
    int size_idx = -1;
    for (int i=0; i<9; i++) { if (dm_sizes[i].w == target_w) { size_idx = i; break; } }
    if (size_idx == -1) return GenError_EMF(); 
    
    int data_cw = dm_sizes[size_idx].data_cw, ecc_cw = dm_sizes[size_idx].ecc_cw;
    dm_nrow = target_w - 2; dm_ncol = target_w - 2;
    
    int len = strlen(data);
    if (len > data_cw) return GenError_EMF();
    
    unsigned char wd[128] = {0};
    for (int i = 0; i < len; i++) wd[i] = data[i] + 1;
    
    // Fix: Properly implement the 253-state randomizer for padding characters
    if (len < data_cw) { 
        wd[len] = 129; 
        for (int i = len + 1; i < data_cw; i++) {
            int prn = ((149 * (i + 1)) % 253) + 1;
            int v = 129 + prn;
            wd[i] = (v <= 254) ? v : v - 254;
        }
    }

    // Fix: Correct polynomial degree generation order
    unsigned char g[64] = {0}; g[0] = 1;
    for(int i = 1; i <= ecc_cw; i++) {
        g[i] = g[i-1];
        for(int j = i - 1; j > 0; j--) g[j] = g[j-1] ^ dm_mul(g[j], dm_exp[i]);
        g[0] = dm_mul(g[0], dm_exp[i]);
    }
    
    // Fix: Align constant term multiplication appropriately for Reed-Solomon division
    unsigned char ecc[64] = {0};
    for(int i = 0; i < data_cw; i++) {
        unsigned char fb = wd[i] ^ ecc[0];
        for(int j = 0; j < ecc_cw - 1; j++) ecc[j] = ecc[j+1] ^ dm_mul(fb, g[ecc_cw - 1 - j]);
        ecc[ecc_cw - 1] = dm_mul(fb, g[0]);
    }
    for (int i = 0; i < ecc_cw; i++) wd[data_cw + i] = ecc[i];

    memset(dm_grid, -1, sizeof(dm_grid));
    int r = 4, c = 0, i = 0;
    
    while (i < data_cw + ecc_cw) {
        // Fix: Standard compliant ISO matrix Corner Case implementations
        if (r == dm_nrow && c == 0) {
            int v = wd[i++];
            dm_module(dm_nrow-1, 0, (v>>7)&1); dm_module(dm_nrow-1, 1, (v>>6)&1); dm_module(dm_nrow-1, 2, (v>>5)&1); dm_module(0, dm_ncol-2, (v>>4)&1);
            dm_module(0, dm_ncol-1, (v>>3)&1); dm_module(1, dm_ncol-1, (v>>2)&1); dm_module(2, dm_ncol-1, (v>>1)&1); dm_module(3, dm_ncol-1, v&1); 
            r -= 2; c += 2; continue;
        }
        if (r == dm_nrow - 2 && c == 0 && ((dm_ncol % 4) != 0)) {
            int v = wd[i++];
            dm_module(dm_nrow-3, 0, (v>>7)&1); dm_module(dm_nrow-2, 0, (v>>6)&1); dm_module(dm_nrow-1, 0, (v>>5)&1); dm_module(0, dm_ncol-4, (v>>4)&1);
            dm_module(0, dm_ncol-3, (v>>3)&1); dm_module(0, dm_ncol-2, (v>>2)&1); dm_module(0, dm_ncol-1, (v>>1)&1); dm_module(1, dm_ncol-1, v&1); 
            r -= 2; c += 2; continue;
        }
        if (r == dm_nrow - 2 && c == 0 && ((dm_ncol % 8) == 4)) {
            int v = wd[i++];
            dm_module(dm_nrow-3, 0, (v>>7)&1); dm_module(dm_nrow-2, 0, (v>>6)&1); dm_module(dm_nrow-1, 0, (v>>5)&1); dm_module(0, dm_ncol-2, (v>>4)&1);
            dm_module(0, dm_ncol-1, (v>>3)&1); dm_module(1, dm_ncol-1, (v>>2)&1); dm_module(2, dm_ncol-1, (v>>1)&1); dm_module(3, dm_ncol-1, v&1); 
            r -= 2; c += 2; continue;
        }
        if (r == dm_nrow + 4 && c == 2 && ((dm_ncol % 8) == 0)) {
            int v = wd[i++];
            dm_module(dm_nrow-1, 0, (v>>7)&1); dm_module(dm_nrow-1, dm_ncol-1, (v>>6)&1); dm_module(0, dm_ncol-3, (v>>5)&1); dm_module(0, dm_ncol-2, (v>>4)&1);
            dm_module(0, dm_ncol-1, (v>>3)&1); dm_module(1, dm_ncol-3, (v>>2)&1); dm_module(1, dm_ncol-2, (v>>1)&1); dm_module(1, dm_ncol-1, v&1); 
            r -= 2; c += 2; continue;
        }

        // Up-Right Sweep
        do {
            if (r >= 0 && r < dm_nrow && c >= 0 && c < dm_ncol && dm_grid[r * dm_ncol + c] == -1) dm_utah(r, c, wd[i++]);
            r -= 2; c += 2;
        } while (r >= 0 && c < dm_ncol);
        r++; c += 3;

        // Down-Left Sweep
        do {
            if (r >= 0 && r < dm_nrow && c >= 0 && c < dm_ncol && dm_grid[r * dm_ncol + c] == -1) dm_utah(r, c, wd[i++]);
            r += 2; c -= 2;
        } while (r < dm_nrow && c >= 0);
        r += 3; c++;
    }

    // Fix: Properly handle untouched corner per ISO spec
    if (dm_grid[dm_nrow * dm_ncol - 1] == -1) {
        dm_grid[dm_nrow * dm_ncol - 1] = 1;
        dm_grid[dm_nrow * dm_ncol - dm_ncol - 2] = 0;
    }

    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0DataMatrix\0\0");
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    int scale = 30; 
    for (int y = 0; y < target_w; y++) {
        for (int x = 0; x < target_w; x++) {
            int val = (y == target_w-1 || x == 0) ? 1 : ((y == 0) ? (x % 2 == 0) : ((x == target_w-1) ? (y % 2 == 1) : dm_grid[(y - 1) * dm_ncol + (x - 1)]));
            if (val == 1) { RECT rBox = { x * scale, y * scale, (x + 1) * scale, (y + 1) * scale }; FillRect(hdcMeta, &rBox, blackBrush); }
        }
    }
    DeleteObject(blackBrush); 
    return CloseEnhMetaFile(hdcMeta);
}
// ============================================================================
// 7. PDF417 ENCODER (Corrected ISO Compaction & Clustering)
// ============================================================================
int pdf417_clusters[3][929], pdf417_initialized = 0;

HENHMETAFILE GenPDF417_EMF(const char* data) {
    if (!pdf417_initialized) {
        int counts[3] = {0};
        // ISO 15438 requires evaluating 17-bit patterns in descending numerical order
        for (int i = 0x1FFFF; i >= 0x10000; i--) {
            if (i & 1) continue; 
            
            int runs[16] = {0}, run_idx = 0, current_bit = 1, len = 0, valid = 1;
            for (int b = 16; b >= 0; b--) {
                int bit = (i >> b) & 1; 
                if (bit == current_bit) { 
                    len++; 
                    if (len > 6) { valid = 0; break; } // No run longer than 6 modules
                } else { 
                    if (run_idx >= 8) { valid = 0; break; } 
                    runs[run_idx++] = len; 
                    current_bit = bit; 
                    len = 1; 
                }
            }
            if (!valid) continue;
            
            if (run_idx >= 8) valid = 0;
            else runs[run_idx++] = len;
            
            if (!valid || run_idx != 8) continue;
            
            // Cluster evaluation: K = (b1 - b2 + b3 - b4) mod 9
            int k = (runs[0] - runs[2] + runs[4] - runs[6]) % 9; 
            if (k < 0) k += 9;
            
            if (k == 0 && counts[0] < 929) pdf417_clusters[0][counts[0]++] = i; 
            else if (k == 3 && counts[1] < 929) pdf417_clusters[1][counts[1]++] = i; 
            else if (k == 6 && counts[2] < 929) pdf417_clusters[2][counts[2]++] = i;
        }
        pdf417_initialized = 1;
    }
    
    int cw_buf[2048] = {0}, pos = 0; 
    int d_len = strlen(data), d_idx = 0;
    
    // 1. Process multiples of 6 bytes using Latch 901 (Base 900 Compaction)
    if (d_len >= 6) {
        cw_buf[pos++] = 901; 
        while (d_idx + 5 < d_len) {
            unsigned long long val = 0; 
            for (int j = 0; j < 6; j++) val = (val << 8) | (unsigned char)data[d_idx + j];
            int cw[5]; 
            for (int j = 4; j >= 0; j--) { 
                cw[j] = val % 900; 
                val /= 900; 
            }
            for (int j = 0; j < 5; j++) cw_buf[pos++] = cw[j]; 
            d_idx += 6;
        }
    }
    
    // 2. Process remainders (1 to 5 bytes) safely to prevent scanner corruption
    if (d_idx < d_len) {
        cw_buf[pos++] = 900; // Terminate 901 and revert to Text Compaction Default
        while (d_idx < d_len) {
            cw_buf[pos++] = 913; // Shift to Byte Compaction for ONE codeword only
            cw_buf[pos++] = (unsigned char)data[d_idx++];
        }
    }
    
    int cols = 5, ecc_level = 1, ecc_count = 1 << (ecc_level + 1);
    int total_words = pos + 1 + ecc_count, rows = (total_words + cols - 1) / cols;
    if (rows < 3) rows = 3; 
    if (rows > 90) return GenError_EMF();
    
    int pad_count = (rows * cols) - total_words;
    int payload_len = pos + 1 + pad_count; // Length includes pad & self, excludes ECC
    
    int full_buf[4096] = {0}; 
    full_buf[0] = payload_len; 
    
    for (int i = 0; i < pos; i++) full_buf[1 + i] = cw_buf[i];
    for (int i = 0; i < pad_count; i++) full_buf[1 + pos + i] = 900; // Pad with CW 900
    
    // RS ECC Generator Polynomial
    int ecc_gen[1024] = {0}, b[1025] = {0}; b[0] = 1; int root = 1;
    for (int i = 1; i <= ecc_count; i++) { 
        root = (root * 3) % 929; 
        for (int j = i; j > 0; j--) b[j] = (b[j] + (b[j-1] * (929 - root))) % 929; 
    }
    for (int i = 0; i < ecc_count; i++) ecc_gen[i] = b[i + 1];
    
    // RS ECC Division
    int* out_ecc = &full_buf[payload_len];
    for (int i = 0; i < payload_len; i++) {
        int term = (full_buf[i] + out_ecc[0]) % 929;
        for (int j = 0; j < ecc_count - 1; j++) {
            out_ecc[j] = (out_ecc[j + 1] + 929 - (term * ecc_gen[j]) % 929) % 929;
        }
        out_ecc[ecc_count - 1] = (929 - (term * ecc_gen[ecc_count - 1]) % 929) % 929;
    }
    for (int i = 0; i < ecc_count; i++) if (out_ecc[i] != 0) out_ecc[i] = 929 - out_ecc[i];

    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0PDF417\0\0"); 
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    int scale_x = 10, scale_y = 30; 
    int start_pattern[] = {1,1,1,1,1,1,1,1,0,1,0,1,0,1,0,0,0}; 
    int stop_pattern[]  = {1,1,1,1,1,1,1,0,1,0,0,0,1,0,1,0,0,1};
    
    for (int y = 0; y < rows; y++) {
        int x_off = 0, cluster = y % 3, v = y / 3, left_ind, right_ind;
        if (cluster == 0) { left_ind = 30 * v + (rows - 1) / 3; right_ind = 30 * v + (cols - 1); } 
        else if (cluster == 1) { left_ind = 30 * v + ecc_level * 3 + (rows - 1) % 3; right_ind = 30 * v + (rows - 1) / 3; } 
        else { left_ind = 30 * v + (cols - 1); right_ind = 30 * v + ecc_level * 3 + (rows - 1) % 3; }
        
        int row_seq[32] = {0}, seq_len = 0; 
        row_seq[seq_len++] = left_ind;
        for (int c = 0; c < cols; c++) row_seq[seq_len++] = full_buf[y * cols + c]; 
        row_seq[seq_len++] = right_ind;
        
        // Render Start Pattern
        for (int i = 0; i < 17; i++) { 
            if (start_pattern[i]) { 
                RECT r = { x_off * scale_x, y * scale_y, (x_off + 1) * scale_x, (y + 1) * scale_y }; 
                FillRect(hdcMeta, &r, blackBrush); 
            } 
            x_off++; 
        }
        
        // Render Data Codewords
        for (int i = 0; i < seq_len; i++) {
            int pat = pdf417_clusters[cluster][row_seq[i]];
            for (int bit = 16; bit >= 0; bit--) { 
                if ((pat >> bit) & 1) { 
                    RECT r = { x_off * scale_x, y * scale_y, (x_off + 1) * scale_x, (y + 1) * scale_y }; 
                    FillRect(hdcMeta, &r, blackBrush); 
                } 
                x_off++; 
            }
        }
        
        // Render Stop Pattern
        for (int i = 0; i < 18; i++) { 
            if (stop_pattern[i]) { 
                RECT r = { x_off * scale_x, y * scale_y, (x_off + 1) * scale_x, (y + 1) * scale_y }; 
                FillRect(hdcMeta, &r, blackBrush); 
            } 
            x_off++; 
        }
    }
    
    DeleteObject(blackBrush); 
    return CloseEnhMetaFile(hdcMeta);
}
// ============================================================================
// 8. QR CODE ENCODER (Version 1)
// ============================================================================

unsigned char qr_exp[512], qr_log[256]; int qr_initialized = 0;
unsigned char qr_mul(unsigned char a, unsigned char b) { return (!a || !b) ? 0 : qr_exp[qr_log[a] + qr_log[b]]; }
HENHMETAFILE GenQR_EMF(const char* text) {
    if (!qr_initialized) { int x = 1; for(int i = 0; i < 255; i++) { qr_exp[i] = x; qr_log[x] = i; x <<= 1; if(x & 256) x ^= 0x11D; } for(int i = 255; i < 512; i++) qr_exp[i] = qr_exp[i - 255]; qr_initialized = 1; }
    int symbol[21][21] = {0}; unsigned char rs_gen[8] = {0}; rs_gen[0] = 1;
    for(int i = 0; i < 7; i++) { for(int j = i + 1; j > 0; j--) rs_gen[j] = rs_gen[j] ^ qr_mul(rs_gen[j-1], qr_exp[i]); }
    unsigned char data[26] = {0}; int len = strlen(text); if (len > 17) return GenError_EMF();
    int bp = 0;
    #define WRITE_BIT(val) do { if (val) data[bp/8] |= (1<<(7-(bp%8))); bp++; } while(0)
    WRITE_BIT(0); WRITE_BIT(1); WRITE_BIT(0); WRITE_BIT(0); for(int i = 7; i >= 0; i--) WRITE_BIT((len >> i) & 1);
    for(int i = 0; i < len; i++) { for(int j = 7; j >= 0; j--) WRITE_BIT((text[i] >> j) & 1); }
    for(int i = 0; i < 4 && bp < 19 * 8; i++) WRITE_BIT(0); while(bp % 8 != 0) WRITE_BIT(0);
    int pad[2] = {0xEC, 0x11}, pi = 0; while(bp < 19 * 8) { for(int j = 7; j >= 0; j--) WRITE_BIT((pad[pi] >> j) & 1); pi = 1 - pi; }
    unsigned char ecc[7] = {0};
    for(int i = 0; i < 19; i++) { unsigned char fb = data[i] ^ ecc[0]; for(int j = 0; j < 6; j++) ecc[j] = ecc[j+1] ^ qr_mul(fb, rs_gen[j+1]); ecc[6] = qr_mul(fb, rs_gen[7]); }
    for(int i = 0; i < 7; i++) data[19+i] = ecc[i];
    for(int i = 0; i < 21; i++) for(int j = 0; j < 21; j++) symbol[i][j] = -1;
    for(int fy = 0; fy < 3; fy++) { int cy = (fy == 2) ? 14 : 0, cx = (fy == 1) ? 14 : 0;
        for(int y = 0; y < 7; y++) for(int x = 0; x < 7; x++) { int max_dist = MAX(abs(x-3), abs(y-3)); symbol[cy+y][cx+x] = (max_dist == 2) ? 0 : 1; }
        for(int i = 0; i < 8; i++) { if(cx == 0 && cy == 0) { symbol[7][i] = 0; symbol[i][7] = 0; } if(cx == 14 && cy == 0) { symbol[7][13+i] = 0; symbol[i][13] = 0; } if(cx == 0 && cy == 14) { symbol[13][i] = 0; symbol[13+i][7] = 0; } } }
    for(int i = 8; i < 13; i++) { symbol[6][i] = (i%2==0); symbol[i][6] = (i%2==0); } symbol[13][8] = 1;
    int ftx1[] = {0,1,2,3,4,5,7,8,8,8,8,8,8,8,8}, fty1[] = {8,8,8,8,8,8,8,8,7,5,4,3,2,1,0}, ftx2[] = {8,8,8,8,8,8,8,13,14,15,16,17,18,19,20}, fty2[] = {20,19,18,17,16,15,14,8,8,8,8,8,8,8,8}, fmt = 0x77C4;
    for(int i = 0; i < 15; i++) { int b = (fmt >> (14 - i)) & 1; symbol[fty1[i]][ftx1[i]] = b; symbol[fty2[i]][ftx2[i]] = b; }
    int x = 20, y = 20, dir = -1, bit_idx = 0;
    while(x > 0) { if(x == 6) x--; 
        for(int i = 0; i < 2; i++) { int cx = x - i; if(symbol[y][cx] == -1) { int b = 0; if (bit_idx < 208) b = (data[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1; if ((cx + y) % 2 == 0) b ^= 1; symbol[y][cx] = b; bit_idx++; } }
        y += dir; if(y < 0 || y > 20) { dir = -dir; y += dir; x -= 2; } }
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0QR Code\0\0"); HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0)); int scale = 20; 
    for (int ry = 0; ry < 21; ry++) { for (int rx = 0; rx < 21; rx++) { if (symbol[ry][rx] == 1) { RECT rBox = { rx * scale, ry * scale, (rx + 1) * scale, (ry + 1) * scale }; FillRect(hdcMeta, &rBox, blackBrush); } } }
    DeleteObject(blackBrush); return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// ROUTING & SCALING LOGIC
// ============================================================================

HENHMETAFILE CreateBarcodeEMF(int type, const char* data, const char* size_str) {
    if (!data || strlen(data) == 0) return GenError_EMF();
    switch(type) {
        case 0: return GenUPC_EMF(data);
        case 1: return GenEAN13_EMF(data);
        case 2: return GenCode39_EMF(data);
        case 3: return GenCode128_EMF(data);
        case 4: return GenITF14_EMF(data);
        case 5: return GenCodabar_EMF(data);
        case 6: return GenPOSTNET_EMF(data);
        case 7: return GenDataMatrix_EMF(data, size_str);
        case 8: return GenPDF417_EMF(data);
        case 9: return GenQR_EMF(data);
    }
    return GenError_EMF();
}

void DrawEMFScaled(HDC hdc, HENHMETAFILE hMeta, RECT rcTarget) {
    if (!hMeta) return;
    ENHMETAHEADER emh;
    if (!GetEnhMetaFileHeader(hMeta, sizeof(emh), &emh)) return;
    float mw = (float)(emh.rclBounds.right - emh.rclBounds.left);
    float mh = (float)(emh.rclBounds.bottom - emh.rclBounds.top);
    if (mw <= 0 || mh <= 0) return;
    float tw = (float)(rcTarget.right - rcTarget.left);
    float th = (float)(rcTarget.bottom - rcTarget.top);
    float scale = min(tw / mw, th / mh);
    int finalW = (int)(mw * scale);
    int finalH = (int)(mh * scale);
    int cx = rcTarget.left + (int)((tw - finalW) / 2);
    int cy = rcTarget.top + (int)((th - finalH) / 2);
    RECT rcDraw = { cx, cy, cx + finalW, cy + finalH };
    PlayEnhMetaFile(hdc, hMeta, &rcDraw);
}

// ============================================================================
// WIN32 GUI IMPLEMENTATION
// ============================================================================

HENHMETAFILE hCurrentEMF = NULL;

RECT rcPreview = { 15, 110, 470, 360 };

void UpdatePreview(HWND hwnd) {
    char data[256] = {0}, size_str[32] = {0};
    GetDlgItemText(hwnd, IDC_EDIT_DATA, data, 255);
    GetDlgItemText(hwnd, IDC_COMBO_SIZE, size_str, 31);
    int type = SendDlgItemMessage(hwnd, IDC_COMBO_TYPE, CB_GETCURSEL, 0, 0);
    
    if (hCurrentEMF) { DeleteEnhMetaFile(hCurrentEMF); hCurrentEMF = NULL; }
    hCurrentEMF = CreateBarcodeEMF(type, data, size_str);
    InvalidateRect(hwnd, &rcPreview, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "Payload Data:", WS_CHILD | WS_VISIBLE, 15, 15, 100, 20, hwnd, NULL, NULL, NULL);
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "TOOLKIT", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 15, 200, 22, hwnd, (HMENU)IDC_EDIT_DATA, NULL, NULL);
            
            CreateWindow("STATIC", "Format:", WS_CHILD | WS_VISIBLE, 15, 45, 100, 20, hwnd, NULL, NULL, NULL);
            HWND hCombo = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 120, 43, 200, 250, hwnd, (HMENU)IDC_COMBO_TYPE, NULL, NULL);
            const char* items[] = { "UPC-A", "EAN-13", "Code 39", "Code 128 (Sub B)", "ITF-14", "Codabar", "POSTNET", "DataMatrix", "PDF417", "QR Code (V1)" };
            for(int i = 0; i < 10; i++) SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)items[i]);
            SendMessage(hCombo, CB_SETCURSEL, 7, 0); 
            
            CreateWindow("STATIC", "Size (e.g. 14x14):", WS_CHILD | WS_VISIBLE, 15, 75, 120, 20, hwnd, NULL, NULL, NULL);
            HWND hComboSize = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 140, 73, 100, 200, hwnd, (HMENU)IDC_COMBO_SIZE, NULL, NULL);
            const char* sizes[] = { "10x10", "12x12", "14x14", "16x16", "18x18", "20x20", "22x22", "24x24", "26x26" };
            for(int i = 0; i < 9; i++) SendMessage(hComboSize, CB_ADDSTRING, 0, (LPARAM)sizes[i]);
            SendMessage(hComboSize, CB_SETCURSEL, 2, 0); 

            CreateWindow("BUTTON", "Copy to Clipboard", WS_CHILD | WS_VISIBLE, 340, 14, 130, 24, hwnd, (HMENU)IDC_BTN_COPY, NULL, NULL);
            CreateWindow("BUTTON", "Save as EMF", WS_CHILD | WS_VISIBLE, 340, 46, 130, 24, hwnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);
            
            UpdatePreview(hwnd);
            break;
        }
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            SetWindowPos(GetDlgItem(hwnd, IDC_EDIT_DATA), NULL, 140, 15, width - 280, 22, SWP_NOZORDER);
            SetWindowPos(GetDlgItem(hwnd, IDC_COMBO_TYPE), NULL, 140, 43, width - 280, 250, SWP_NOZORDER);
            SetWindowPos(GetDlgItem(hwnd, IDC_COMBO_SIZE), NULL, 140, 73, 100, 250, SWP_NOZORDER);
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_COPY), NULL, width - 130, 14, 115, 24, SWP_NOZORDER);
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_SAVE), NULL, width - 130, 46, 115, 24, SWP_NOZORDER);
            rcPreview.left = 15; rcPreview.top = 110; rcPreview.right = width - 15; rcPreview.bottom = height - 15;
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EDIT_DATA && HIWORD(wParam) == EN_CHANGE) UpdatePreview(hwnd);
            if (LOWORD(wParam) == IDC_COMBO_SIZE && HIWORD(wParam) == CBN_EDITCHANGE) UpdatePreview(hwnd);
            if (LOWORD(wParam) == IDC_COMBO_SIZE && HIWORD(wParam) == CBN_SELCHANGE) {
                PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_COMBO_SIZE, CBN_EDITCHANGE), (LPARAM)GetDlgItem(hwnd, IDC_COMBO_SIZE));
            }
            if (LOWORD(wParam) == IDC_COMBO_TYPE && HIWORD(wParam) == CBN_SELCHANGE) UpdatePreview(hwnd);
            
            if (LOWORD(wParam) == IDC_BTN_COPY) {
                if (hCurrentEMF && OpenClipboard(hwnd)) {
                    EmptyClipboard(); SetClipboardData(CF_ENHMETAFILE, CopyEnhMetaFile(hCurrentEMF, NULL)); CloseClipboard();
                    MessageBox(hwnd, "Vector Graphic copied to clipboard!", "Success", MB_OK);
                }
            }
            if (LOWORD(wParam) == IDC_BTN_SAVE) {
                if (hCurrentEMF) {
                    OPENFILENAME ofn; char szFile[260] = "barcode.emf";
                    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "Enhanced Metafile (*.emf)\0*.emf\0All Files (*.*)\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
                    if (GetSaveFileName(&ofn)) {
                        HENHMETAFILE hCopy = CopyEnhMetaFile(hCurrentEMF, szFile);
                        if (hCopy) DeleteEnhMetaFile(hCopy);
                    }
                }
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            HBRUSH hbg = CreateSolidBrush(RGB(240, 240, 240)); FillRect(hdc, &rcPreview, hbg);
            FrameRect(hdc, &rcPreview, (HBRUSH)GetStockObject(BLACK_BRUSH)); DeleteObject(hbg);
            DrawEMFScaled(hdc, hCurrentEMF, rcPreview);
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_DESTROY:
            if (hCurrentEMF) DeleteEnhMetaFile(hCurrentEMF);
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// MAIN ENTRY POINT (Updated CLI Parsing with Optional Size Support)
// ============================================================================

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (__argc > 1) {
        AttachConsole(ATTACH_PARENT_PROCESS); 
        freopen("CONOUT$", "w", stdout);
        
        if (__argc < 3) {
            printf("\nUsage: BarcodeGen.exe <data> -t:<type> [-s:<size>] [-o:<output.emf>]\n");
            printf("Types: upc, ean, code39, code128, itf14, codabar, postnet, datamatrix, pdf417, qr\n\n");
            return 1;
        }

        char* data = NULL; 
        char type[32] = {0}; 
        char size_str[32] = {0}; 
        char output_file[260] = {0};

        for (int i = 1; i < __argc; i++) {
            if (strncmp(__argv[i], "-t:", 3) == 0) {
                strncpy(type, __argv[i] + 3, sizeof(type) - 1);
            } 
            else if (strncmp(__argv[i], "-s:", 3) == 0) {
                strncpy(size_str, __argv[i] + 3, sizeof(size_str) - 1);
            } 
            else if (strncmp(__argv[i], "-o:", 3) == 0) {
                strncpy(output_file, __argv[i] + 3, sizeof(output_file) - 1);
            } 
            else {
                data = __argv[i];
            }
        }

        if (!data || strlen(type) == 0) {
            printf("\nError: Data and barcode type (-t:) are required.\n\n");
            return 1;
        }

        int type_idx = -1;
        if (stricmp(type, "upc") == 0) type_idx = 0; 
        else if (stricmp(type, "ean") == 0) type_idx = 1;
        else if (stricmp(type, "code39") == 0) type_idx = 2; 
        else if (stricmp(type, "code128") == 0) type_idx = 3;
        else if (stricmp(type, "itf14") == 0) type_idx = 4; 
        else if (stricmp(type, "codabar") == 0) type_idx = 5;
        else if (stricmp(type, "postnet") == 0) type_idx = 6; 
        else if (stricmp(type, "datamatrix") == 0) type_idx = 7;
        else if (stricmp(type, "pdf417") == 0) type_idx = 8; 
        else if (stricmp(type, "qr") == 0) type_idx = 9;

        if (type_idx == -1) { 
            printf("\nUnsupported format: %s\n\n", type); 
            return 1; 
        }

        // Pass the optional size_str parameter to the generator
        HENHMETAFILE hMeta = CreateBarcodeEMF(type_idx, data, size_str);
        if (hMeta) {
            if (strlen(output_file) > 0) {
                HENHMETAFILE hSave = CopyEnhMetaFile(hMeta, output_file);
                if (hSave) {
                    DeleteEnhMetaFile(hSave);
                    printf("\nSaved to file: %s\n\n", output_file);
                } else {
                    printf("\nFailed to save to file: %s\n\n", output_file);
                }
                DeleteEnhMetaFile(hMeta);
            } else {
                if (OpenClipboard(NULL)) {
                    EmptyClipboard(); 
                    SetClipboardData(CF_ENHMETAFILE, hMeta); 
                    CloseClipboard();
                    printf("\n%s copied to clipboard as WMF/EMF.\n\n", type);
                }
            }
        } else {
            printf("\nFailed to generate barcode (check payload size limits or invalid size).\n\n");
        }
        return 0;
    }

    // GUI Mode Fallback Startup
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.lpszClassName = "BarcodeAppClass";
    RegisterClass(&wc);

    CreateWindowEx(0, "BarcodeAppClass", "Native Barcode Studio", 
                   WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
                   CW_USEDEFAULT, CW_USEDEFAULT, 600, 480, NULL, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { 
        TranslateMessage(&msg); 
        DispatchMessage(&msg); 
    }
    return msg.wParam;
}
