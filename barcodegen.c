/*
 * ============================================================================
 * BarcodeGen.c - Native Win32 Vector Barcode Generator (GUI & CLI)
 * ============================================================================
 * 
 * COMPILE INSTRUCTIONS (MinGW GCC):
 * gcc -Os -s BarcodeGen.c -o BarcodeGen.exe -lgdi32 -lcomdlg32 -lcomctl32 -mwindows
 * 
 * 2026-10-11 Hugo
 * 
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
#include "pdf417_clusters.h"

#define MAX(a,b) ((a)>(b)?(a):(b))

// GUI Control IDs
#define IDC_EDIT_DATA    101
#define IDC_COMBO_TYPE   102
#define IDC_BTN_COPY     103
#define IDC_BTN_SAVE     104
#define IDC_COMBO_SIZE   105
#define IDC_COMBO_COLS   106
#define IDC_COMBO_ECC    107

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

// ============================================================================
// 6. DATAMATRIX (10x10 to 52x52 + Automatic 2x2 Stacked Quad Mode)
// ============================================================================

unsigned char dm_exp[512], dm_log[256];
int dm_grid[64*64], dm_initialized = 0;

struct DMSize {
    int w; 
    int data_cw; 
    int ecc_cw; 
    int regions; // 1 = single region (10x10..26x26), 2 = 2x2 quad regions (32x32..52x52)
    int blocks;  // RS interleaving blocks
};

#define DM_SIZE_COUNT 15
struct DMSize dm_sizes[DM_SIZE_COUNT] = {
    {10,   3,  5, 1, 1}, {12,   5,  7, 1, 1}, {14,   8, 10, 1, 1},
    {16,  12, 12, 1, 1}, {18,  18, 14, 1, 1}, {20,  22, 18, 1, 1},
    {22,  30, 20, 1, 1}, {24,  36, 24, 1, 1}, {26,  44, 28, 1, 1},
    {32,  62, 36, 2, 1}, {36,  86, 42, 2, 1}, {40, 114, 48, 2, 1},
    {44, 144, 56, 2, 1}, {48, 174, 68, 2, 1}, {52, 204, 84, 2, 2}
};

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

static void BuildSingleDataMatrix(const char* chunk, int len, int size_idx, int out_bitmap[64][64]) {
    int target_w = dm_sizes[size_idx].w;
    int data_cw  = dm_sizes[size_idx].data_cw;
    int ecc_cw   = dm_sizes[size_idx].ecc_cw;
    int regions  = dm_sizes[size_idx].regions;
    int blocks   = dm_sizes[size_idx].blocks;

    dm_nrow = target_w - 2 * regions;
    dm_ncol = target_w - 2 * regions;

    unsigned char wd[512] = {0};
    for (int i = 0; i < len; i++) wd[i] = (unsigned char)chunk[i] + 1;

    if (len < data_cw) { 
        wd[len] = 129; 
        for (int i = len + 1; i < data_cw; i++) {
            int prn = ((149 * (i + 1)) % 253) + 1;
            int v = 129 + prn;
            wd[i] = (v <= 254) ? v : v - 254;
        }
    }

    int d_per_blk = data_cw / blocks;
    int e_per_blk = ecc_cw / blocks;

    unsigned char g[128] = {0}; g[0] = 1;
    for (int i = 1; i <= e_per_blk; i++) {
        g[i] = g[i - 1];
        for (int j = i - 1; j > 0; j--) g[j] = g[j - 1] ^ dm_mul(g[j], dm_exp[i]);
        g[0] = dm_mul(g[0], dm_exp[i]);
    }
    
    for (int b = 0; b < blocks; b++) {
        unsigned char ecc[128] = {0};
        for (int i = 0; i < d_per_blk; i++) {
            unsigned char fb = wd[i * blocks + b] ^ ecc[0];
            for (int j = 0; j < e_per_blk - 1; j++) ecc[j] = ecc[j + 1] ^ dm_mul(fb, g[e_per_blk - 1 - j]);
            ecc[e_per_blk - 1] = dm_mul(fb, g[0]);
        }
        for (int i = 0; i < e_per_blk; i++) wd[data_cw + i * blocks + b] = ecc[i];
    }

    memset(dm_grid, -1, sizeof(dm_grid));
    int r = 4, c = 0, i = 0;
    
    while (i < data_cw + ecc_cw) {
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

        do {
            if (r >= 0 && r < dm_nrow && c >= 0 && c < dm_ncol && dm_grid[r * dm_ncol + c] == -1) dm_utah(r, c, wd[i++]);
            r -= 2; c += 2;
        } while (r >= 0 && c < dm_ncol);
        r++; c += 3;

        do {
            if (r >= 0 && r < dm_nrow && c >= 0 && c < dm_ncol && dm_grid[r * dm_ncol + c] == -1) dm_utah(r, c, wd[i++]);
            r += 2; c -= 2;
        } while (r < dm_nrow && c >= 0);
        r += 3; c++;
    }

    if (dm_grid[dm_nrow * dm_ncol - 1] == -1) {
        dm_grid[dm_nrow * dm_ncol - 1] = 1;
        dm_grid[dm_nrow * dm_ncol - dm_ncol - 2] = 0;
    }

    int reg_w = target_w / regions;
    int reg_data = reg_w - 2;
    for (int y = 0; y < target_w; y++) {
        for (int x = 0; x < target_w; x++) {
            int rx = x / reg_w, ry = y / reg_w;
            int lx = x % reg_w, ly = y % reg_w;
            int val;
            if (ly == reg_w - 1 || lx == 0) val = 1;
            else if (ly == 0) val = (lx % 2 == 0);
            else if (lx == reg_w - 1) val = (ly % 2 == 1);
            else val = dm_grid[(ry * reg_data + (ly - 1)) * dm_ncol + (rx * reg_data + (lx - 1))];
            out_bitmap[y][x] = (val == 1) ? 1 : 0;
        }
    }
}

HENHMETAFILE GenDataMatrix_EMF(const char* data, const char* size_str) {
    init_dm_gf();
    int target_w = 14; 
    if (size_str && strlen(size_str) > 0) {
        int pw, ph;
        if (sscanf(size_str, "%dx%d", &pw, &ph) == 2 && pw == ph) target_w = pw;
        else return GenError_EMF();
    }
    
    int size_idx = -1;
    for (int i = 0; i < DM_SIZE_COUNT; i++) {
        if (dm_sizes[i].w == target_w) { size_idx = i; break; }
    }
    if (size_idx == -1) return GenError_EMF(); 
    
    int len = (int)strlen(data);
    int data_cw = dm_sizes[size_idx].data_cw;
    int scale = 25, qz = 2;

    // 1. Single DataMatrix if payload fits the selected symbol size
    if (len <= data_cw) {
        int bmp[64][64] = {{0}};
        BuildSingleDataMatrix(data, len, size_idx, bmp);

        HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0DataMatrix\0\0");
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        RECT rBg = { 0, 0, (target_w + qz * 2) * scale, (target_w + qz * 2) * scale };
        FillRect(hdcMeta, &rBg, whiteBrush);

        for (int y = 0; y < target_w; y++) {
            for (int x = 0; x < target_w; x++) {
                if (bmp[y][x] == 1) {
                    RECT rBox = { (x + qz) * scale, (y + qz) * scale, (x + qz + 1) * scale, (y + qz + 1) * scale };
                    FillRect(hdcMeta, &rBox, blackBrush);
                }
            }
        }
        DeleteObject(blackBrush);
        DeleteObject(whiteBrush);
        return CloseEnhMetaFile(hdcMeta);
    }

    // 2. Stacked 2x2 Quad Mode when payload exceeds single symbol capacity
    // If payload exceeds 4x of the selected size, step up to the smallest size where 4x fits
    while (size_idx < DM_SIZE_COUNT && len > dm_sizes[size_idx].data_cw * 4) {
        size_idx++;
    }
    if (size_idx >= DM_SIZE_COUNT) return GenError_EMF();

    target_w = dm_sizes[size_idx].w;
    data_cw  = dm_sizes[size_idx].data_cw;

    int gap = 2; // 2-module quiet zone separating the 4 stacked quadrants
    int total_mods = qz * 2 + target_w * 2 + gap;

    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0DataMatrix Quad\0\0");
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    RECT rBg = { 0, 0, total_mods * scale, total_mods * scale };
    FillRect(hdcMeta, &rBg, whiteBrush);

    for (int q = 0; q < 4; q++) {
        int offset = q * data_cw;
        int chunk_len = 0;
        if (offset < len) {
            chunk_len = len - offset;
            if (chunk_len > data_cw) chunk_len = data_cw;
        }

        int bmp[64][64] = {{0}};
        BuildSingleDataMatrix(data + offset, chunk_len, size_idx, bmp);

        int qx_mod = qz + (q % 2) * (target_w + gap);
        int qy_mod = qz + (q / 2) * (target_w + gap);

        for (int y = 0; y < target_w; y++) {
            for (int x = 0; x < target_w; x++) {
                if (bmp[y][x] == 1) {
                    RECT rBox = {
                        (qx_mod + x) * scale,
                        (qy_mod + y) * scale,
                        (qx_mod + x + 1) * scale,
                        (qy_mod + y + 1) * scale
                    };
                    FillRect(hdcMeta, &rBox, blackBrush);
                }
            }
        }
    }

    DeleteObject(blackBrush);
    DeleteObject(whiteBrush);
    return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// 7. PDF417 ENCODER (Configurable Columns 1-30 & ECC Level 0-8)
// ============================================================================

HENHMETAFILE GenPDF417_EMF(const char* data, int cols, int ecc_level) {
    if (cols <= 0) cols = 5; // "Auto" defaults to 5 columns
    if (cols < 1 || cols > 30) return GenError_EMF();
    if (ecc_level < 0 || ecc_level > 8) return GenError_EMF();

    int cw_buf[2048] = {0}, pos = 0; 
    int d_len = (int)strlen(data), d_idx = 0;
    
    // Latch 924 if payload length is an exact multiple of 6; Latch 901 otherwise
    cw_buf[pos++] = (d_len % 6 == 0) ? 924 : 901;
    
    // Encode full 6-byte groups into 5 base-900 codewords
    while (d_idx + 5 < d_len) {
        unsigned long long val = 0; 
        for (int j = 0; j < 6; j++) val = (val << 8) | (unsigned char)data[d_idx + j];
        int cw[5]; 
        for (int j = 4; j >= 0; j--) { 
            cw[j] = (int)(val % 900); 
            val /= 900; 
        }
        for (int j = 0; j < 5; j++) cw_buf[pos++] = cw[j]; 
        d_idx += 6;
    }
    
    // Remaining 1..5 bytes in mode 901 are encoded directly (1 byte per codeword)
    while (d_idx < d_len) {
        cw_buf[pos++] = (unsigned char)data[d_idx++];
    }
    
    int ecc_count = 1 << (ecc_level + 1);
    int total_words = pos + 1 + ecc_count;
    int rows = (total_words + cols - 1) / cols;
    if (rows < 3) rows = 3; 
    if (rows > 90) return GenError_EMF();
    
    int pad_count = (rows * cols) - total_words;
    int payload_len = pos + 1 + pad_count; 
    if (payload_len + ecc_count > 928) return GenError_EMF();
    
    int full_buf[4096] = {0}; 
    full_buf[0] = payload_len; 
    
    for (int i = 0; i < pos; i++) full_buf[1 + i] = cw_buf[i];
    for (int i = 0; i < pad_count; i++) full_buf[1 + pos + i] = 900; 
    
    // RS ECC Division Math over GF(929)
    int ecc_gen[1024] = {0}, b[1025] = {0}; b[0] = 1; int root = 1;
    for (int i = 1; i <= ecc_count; i++) { 
        root = (root * 3) % 929; 
        for (int j = i; j > 0; j--) b[j] = (b[j] + (b[j - 1] * (929 - root))) % 929; 
    }
    for (int i = 0; i < ecc_count; i++) ecc_gen[i] = b[i + 1];
    
    int* out_ecc = &full_buf[payload_len];
    for (int i = 0; i < payload_len; i++) {
        int term = (full_buf[i] + out_ecc[0]) % 929;
        for (int j = 0; j < ecc_count - 1; j++) {
            out_ecc[j] = (out_ecc[j + 1] + 929 - (term * ecc_gen[j]) % 929) % 929;
        }
        out_ecc[ecc_count - 1] = (929 - (term * ecc_gen[ecc_count - 1]) % 929) % 929;
    }
    for (int i = 0; i < ecc_count; i++) if (out_ecc[i] != 0) out_ecc[i] = 929 - out_ecc[i];

    // Graphics Render Initialization
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0PDF417\0\0"); 
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    
    int scale_x = 5, scale_y = 15; // 1:3 standard PDF417 module aspect ratio
    int qx = 2, qy = 2;            // Strict 2X Quiet Zone padding
    int modules_per_row = 17 + 17 + (cols * 17) + 17 + 18; 
    
    RECT rBg = { 0, 0, (modules_per_row + qx * 2) * scale_x, (rows + qy * 2) * scale_y };
    FillRect(hdcMeta, &rBg, whiteBrush);
    
    int start_pattern[] = {1,1,1,1,1,1,1,1,0,1,0,1,0,1,0,0,0}; 
    int stop_pattern[]  = {1,1,1,1,1,1,1,0,1,0,0,0,1,0,1,0,0,1};
    
    for (int y = 0; y < rows; y++) {
        int x_off = 0, cluster = y % 3, v = y / 3, left_ind, right_ind;
        if (cluster == 0) { left_ind = 30 * v + (rows - 1) / 3; right_ind = 30 * v + (cols - 1); } 
        else if (cluster == 1) { left_ind = 30 * v + ecc_level * 3 + (rows - 1) % 3; right_ind = 30 * v + (rows - 1) / 3; } 
        else { left_ind = 30 * v + (cols - 1); right_ind = 30 * v + ecc_level * 3 + (rows - 1) % 3; }
        
        int row_seq[64] = {0}, seq_len = 0; 
        row_seq[seq_len++] = left_ind;
        for (int c = 0; c < cols; c++) row_seq[seq_len++] = full_buf[y * cols + c]; 
        row_seq[seq_len++] = right_ind;
        
        for (int i = 0; i < 17; i++) { 
            if (start_pattern[i]) { 
                RECT r = { (x_off + qx) * scale_x, (y + qy) * scale_y, (x_off + qx + 1) * scale_x, (y + qy + 1) * scale_y }; 
                FillRect(hdcMeta, &r, blackBrush); 
            } 
            x_off++; 
        }
        
        for (int i = 0; i < seq_len; i++) {
            int pat = pdf417_clusters[cluster][row_seq[i]];
            for (int bit = 16; bit >= 0; bit--) { 
                if ((pat >> bit) & 1) { 
                    RECT r = { (x_off + qx) * scale_x, (y + qy) * scale_y, (x_off + qx + 1) * scale_x, (y + qy + 1) * scale_y }; 
                    FillRect(hdcMeta, &r, blackBrush); 
                } 
                x_off++; 
            }
        }
        
        for (int i = 0; i < 18; i++) { 
            if (stop_pattern[i]) { 
                RECT r = { (x_off + qx) * scale_x, (y + qy) * scale_y, (x_off + qx + 1) * scale_x, (y + qy + 1) * scale_y }; 
                FillRect(hdcMeta, &r, blackBrush); 
            } 
            x_off++; 
        }
    }
    
    DeleteObject(blackBrush); 
    DeleteObject(whiteBrush);
    return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// 8. QR CODE ENCODER (Versions 1-10, ECC Levels L/M/Q/H, Multi-Block RS)
// ============================================================================

unsigned char qr_exp[512], qr_log[256]; int qr_initialized = 0;
unsigned char qr_mul(unsigned char a, unsigned char b) { return (!a || !b) ? 0 : qr_exp[qr_log[a] + qr_log[b]]; }

static const int qr_total_cw[10] = { 26, 44, 70, 100, 134, 172, 196, 242, 292, 346 };
static const int qr_ecc_pb[10][4] = {
    { 7, 10, 13, 17}, {10, 16, 22, 28}, {15, 26, 18, 22}, {20, 18, 26, 16}, {26, 24, 18, 22},
    {18, 16, 24, 28}, {20, 18, 18, 26}, {24, 22, 22, 26}, {30, 22, 20, 24}, {18, 26, 24, 28}
};
static const int qr_blocks[10][4] = {
    {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 2, 2}, {1, 2, 2, 4}, {1, 2, 4, 4},
    {2, 4, 4, 4}, {2, 4, 6, 5}, {2, 4, 6, 6}, {2, 5, 8, 8}, {4, 5, 8, 8}
};
static const int qr_fmt_mask0[4] = { 0x77C4, 0x5412, 0x355F, 0x1689 }; // Mask 0 for L(0), M(1), Q(2), H(3)

HENHMETAFILE GenQR_EMF(const char* text, int req_ver, int ecc_level) {
    if (!qr_initialized) {
        int x = 1;
        for (int i = 0; i < 255; i++) { qr_exp[i] = x; qr_log[x] = i; x <<= 1; if (x & 256) x ^= 0x11D; }
        for (int i = 255; i < 512; i++) qr_exp[i] = qr_exp[i - 255];
        qr_initialized = 1;
    }

    if (ecc_level < 0) ecc_level = 0;
    if (ecc_level > 3) ecc_level = 3; // Clamp 4..8 to Level H (3)

    int len = (int)strlen(text);
    int ver = req_ver;

    if (ver <= 0) {
        // Auto-select smallest QR version (1..10) that fits the payload
        ver = -1;
        for (int v = 1; v <= 10; v++) {
            int data_cw = qr_total_cw[v - 1] - qr_ecc_pb[v - 1][ecc_level] * qr_blocks[v - 1][ecc_level];
            int max_bytes = data_cw - ((v <= 9) ? 2 : 3);
            if (len <= max_bytes) { ver = v; break; }
        }
        if (ver == -1) return GenError_EMF();
    } else {
        if (ver > 10) return GenError_EMF();
        int data_cw = qr_total_cw[ver - 1] - qr_ecc_pb[ver - 1][ecc_level] * qr_blocks[ver - 1][ecc_level];
        int max_bytes = data_cw - ((ver <= 9) ? 2 : 3);
        if (len > max_bytes) return GenError_EMF();
    }

    int size = 17 + 4 * ver;
    int total_cw = qr_total_cw[ver - 1];
    int ecc_pb = qr_ecc_pb[ver - 1][ecc_level];
    int num_blocks = qr_blocks[ver - 1][ecc_level];
    int total_data_cw = total_cw - ecc_pb * num_blocks;

    // 1. Pack Byte Mode (0100) bitstream
    unsigned char data_cw[350] = {0};
    int bp = 0;
    #define QR_WRITE_BIT(val) do { if (val) data_cw[bp / 8] |= (1 << (7 - (bp % 8))); bp++; } while(0)

    QR_WRITE_BIT(0); QR_WRITE_BIT(1); QR_WRITE_BIT(0); QR_WRITE_BIT(0);
    int cci_bits = (ver <= 9) ? 8 : 16;
    for (int i = cci_bits - 1; i >= 0; i--) QR_WRITE_BIT((len >> i) & 1);
    for (int i = 0; i < len; i++) {
        for (int j = 7; j >= 0; j--) QR_WRITE_BIT(((unsigned char)text[i] >> j) & 1);
    }
    for (int i = 0; i < 4 && bp < total_data_cw * 8; i++) QR_WRITE_BIT(0);
    while (bp % 8 != 0 && bp < total_data_cw * 8) QR_WRITE_BIT(0);

    int pad[2] = {0xEC, 0x11}, pi = 0;
    while (bp < total_data_cw * 8) {
        for (int j = 7; j >= 0; j--) QR_WRITE_BIT((pad[pi] >> j) & 1);
        pi = 1 - pi;
    }
    #undef QR_WRITE_BIT

    // 2. Build RS Generator Polynomial of degree ecc_pb
    unsigned char rs_gen[32] = {0};
    rs_gen[0] = 1;
    for (int i = 0; i < ecc_pb; i++) {
        for (int j = i + 1; j > 0; j--) rs_gen[j] = rs_gen[j] ^ qr_mul(rs_gen[j - 1], qr_exp[i]);
    }

    // 3. Split into Group 1 & Group 2 blocks, compute RS ECC, and interleave
    int g2_blocks = total_data_cw % num_blocks;
    int g1_blocks = num_blocks - g2_blocks;
    int g1_len = total_data_cw / num_blocks;
    int g2_len = g1_len + 1;

    unsigned char blk_data[8][128] = {{0}};
    unsigned char blk_ecc[8][32] = {{0}};
    int src_idx = 0;

    for (int b = 0; b < num_blocks; b++) {
        int blen = (b < g1_blocks) ? g1_len : g2_len;
        for (int i = 0; i < blen; i++) blk_data[b][i] = data_cw[src_idx++];
        for (int i = 0; i < blen; i++) {
            unsigned char fb = blk_data[b][i] ^ blk_ecc[b][0];
            for (int j = 0; j < ecc_pb - 1; j++) blk_ecc[b][j] = blk_ecc[b][j + 1] ^ qr_mul(fb, rs_gen[j + 1]);
            blk_ecc[b][ecc_pb - 1] = qr_mul(fb, rs_gen[ecc_pb]);
        }
    }

    unsigned char final_cw[400] = {0};
    int out_idx = 0;
    for (int col = 0; col < g1_len; col++) {
        for (int b = 0; b < num_blocks; b++) final_cw[out_idx++] = blk_data[b][col];
    }
    if (g2_blocks > 0) {
        for (int b = g1_blocks; b < num_blocks; b++) final_cw[out_idx++] = blk_data[b][g1_len];
    }
    for (int col = 0; col < ecc_pb; col++) {
        for (int b = 0; b < num_blocks; b++) final_cw[out_idx++] = blk_ecc[b][col];
    }

    // 4. Construct QR Matrix
    int symbol[64][64];
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) symbol[y][x] = -1;

    // Finder patterns + separators
    for (int fy = 0; fy < 3; fy++) {
        int cy = (fy == 2) ? (size - 7) : 0;
        int cx = (fy == 1) ? (size - 7) : 0;
        for (int dy = -1; dy <= 7; dy++) {
            for (int dx = -1; dx <= 7; dx++) {
                int py = cy + dy, px = cx + dx;
                if (py >= 0 && py < size && px >= 0 && px < size) {
                    if (dy == -1 || dy == 7 || dx == -1 || dx == 7) symbol[py][px] = 0;
                    else symbol[py][px] = (MAX(abs(dx - 3), abs(dy - 3)) == 2) ? 0 : 1;
                }
            }
        }
    }

    // Alignment patterns (V2..V10)
    if (ver >= 2) {
        int ap_coords[3] = { 6, (ver >= 7) ? ((size - 1) / 2) : (size - 7), size - 7 };
        int ap_cnt = (ver >= 7) ? 3 : 2;
        for (int iy = 0; iy < ap_cnt; iy++) {
            for (int ix = 0; ix < ap_cnt; ix++) {
                if ((iy == 0 && ix == 0) || (iy == 0 && ix == ap_cnt - 1) || (iy == ap_cnt - 1 && ix == 0)) continue;
                int ay = ap_coords[iy], ax = ap_coords[ix];
                for (int dy = -2; dy <= 2; dy++) {
                    for (int dx = -2; dx <= 2; dx++) {
                        symbol[ay + dy][ax + dx] = (MAX(abs(dx), abs(dy)) == 1) ? 0 : 1;
                    }
                }
            }
        }
    }

    // Timing patterns & Dark Module
    for (int i = 8; i < size - 8; i++) {
        symbol[6][i] = (i % 2 == 0);
        symbol[i][6] = (i % 2 == 0);
    }
    symbol[size - 8][8] = 1;

    // Version Information (V7..V10)
    if (ver >= 7) {
        int rem = ver << 12;
        for (int i = 5; i >= 0; i--) if (rem & (1 << (i + 12))) rem ^= (0x1F25 << i);
        int vinfo = (ver << 12) | rem;
        for (int i = 0; i < 18; i++) {
            int bit = (vinfo >> i) & 1;
            symbol[size - 11 + (i % 3)][i / 3] = bit;
            symbol[i / 3][size - 11 + (i % 3)] = bit;
        }
    }

    // Format Information (Mask 0)
    int ftx1[] = {0,1,2,3,4,5,7,8,8,8,8,8,8,8,8};
    int fty1[] = {8,8,8,8,8,8,8,8,7,5,4,3,2,1,0};
    int fmt = qr_fmt_mask0[ecc_level];
    for (int i = 0; i < 15; i++) {
        int b = (fmt >> (14 - i)) & 1;
        symbol[fty1[i]][ftx1[i]] = b;
        if (i < 7) symbol[size - 1 - i][8] = b;
        else       symbol[8][size - 15 + i] = b;
    }

    // Zig-zag data placement with Mask 0
    int x = size - 1, y = size - 1, dir = -1, bit_idx = 0, total_bits = total_cw * 8;
    while (x > 0) {
        if (x == 6) x--;
        for (int i = 0; i < 2; i++) {
            int cx = x - i;
            if (symbol[y][cx] == -1) {
                int b = 0;
                if (bit_idx < total_bits) b = (final_cw[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1;
                if ((cx + y) % 2 == 0) b ^= 1;
                symbol[y][cx] = b;
                bit_idx++;
            }
        }
        y += dir;
        if (y < 0 || y >= size) { dir = -dir; y += dir; x -= 2; }
    }

    // Render EMF with 2-module white quiet zone
    HDC hdcMeta = CreateEnhMetaFile(NULL, NULL, NULL, "BarcodeGen\0QR Code\0\0");
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    int scale = 20, qz = 2;
    RECT rBg = { 0, 0, (size + qz * 2) * scale, (size + qz * 2) * scale };
    FillRect(hdcMeta, &rBg, whiteBrush);

    for (int ry = 0; ry < size; ry++) {
        for (int rx = 0; rx < size; rx++) {
            if (symbol[ry][rx] == 1) {
                RECT rBox = { (rx + qz) * scale, (ry + qz) * scale, (rx + qz + 1) * scale, (ry + qz + 1) * scale };
                FillRect(hdcMeta, &rBox, blackBrush);
            }
        }
    }
    DeleteObject(blackBrush);
    DeleteObject(whiteBrush);
    return CloseEnhMetaFile(hdcMeta);
}

// ============================================================================
// ROUTING & SCALING LOGIC
// ============================================================================

HENHMETAFILE CreateBarcodeEMF(int type, const char* data, const char* size_str, int cols_or_ver, int ecc_level) {
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
        case 8: return GenPDF417_EMF(data, cols_or_ver, ecc_level);
        case 9: return GenQR_EMF(data, cols_or_ver, ecc_level);
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
    char data[1024] = {0}, size_str[32] = {0}, cols_str[16] = {0}, ecc_str[16] = {0};
    GetDlgItemText(hwnd, IDC_EDIT_DATA, data, 1023);
    GetDlgItemText(hwnd, IDC_COMBO_SIZE, size_str, 31);
    GetDlgItemText(hwnd, IDC_COMBO_COLS, cols_str, 15);
    GetDlgItemText(hwnd, IDC_COMBO_ECC, ecc_str, 15);
    int type = SendDlgItemMessage(hwnd, IDC_COMBO_TYPE, CB_GETCURSEL, 0, 0);

    int cols_or_ver = (strlen(cols_str) > 0) ? atoi(cols_str) : 0; // "Auto" -> 0
    int ecc_level   = (strlen(ecc_str) > 0)  ? atoi(ecc_str)  : 1;
    
    if (hCurrentEMF) { DeleteEnhMetaFile(hCurrentEMF); hCurrentEMF = NULL; }
    hCurrentEMF = CreateBarcodeEMF(type, data, size_str, cols_or_ver, ecc_level);
    InvalidateRect(hwnd, &rcPreview, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "Payload Data:", WS_CHILD | WS_VISIBLE, 15, 15, 100, 20, hwnd, NULL, NULL, NULL);
            CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "TOOLKIT", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 140, 15, 200, 22, hwnd, (HMENU)IDC_EDIT_DATA, NULL, NULL);
            
            CreateWindow("STATIC", "Format:", WS_CHILD | WS_VISIBLE, 15, 45, 100, 20, hwnd, NULL, NULL, NULL);
            HWND hCombo = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 140, 43, 200, 250, hwnd, (HMENU)IDC_COMBO_TYPE, NULL, NULL);
            const char* items[] = { "UPC-A", "EAN-13", "Code 39", "Code 128 (Sub B)", "ITF-14", "Codabar", "POSTNET", "DataMatrix (Single / Quad)", "PDF417", "QR Code (V1-V10)" };
            for(int i = 0; i < 10; i++) SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)items[i]);
            SendMessage(hCombo, CB_SETCURSEL, 7, 0); // Default to DataMatrix
            
            CreateWindow("STATIC", "DM Size:", WS_CHILD | WS_VISIBLE, 15, 75, 60, 20, hwnd, NULL, NULL, NULL);
            HWND hComboSize = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 78, 73, 82, 250, hwnd, (HMENU)IDC_COMBO_SIZE, NULL, NULL);
            const char* sizes[] = {
                "10x10", "12x12", "14x14", "16x16", "18x18", "20x20", "22x22", "24x24", "26x26",
                "32x32", "36x36", "40x40", "44x44", "48x48", "52x52"
            };
            for(int i = 0; i < DM_SIZE_COUNT; i++) SendMessage(hComboSize, CB_ADDSTRING, 0, (LPARAM)sizes[i]);
            SendMessage(hComboSize, CB_SETCURSEL, 2, 0); 

            CreateWindow("STATIC", "Cols / Ver:", WS_CHILD | WS_VISIBLE, 172, 75, 75, 20, hwnd, NULL, NULL, NULL);
            HWND hComboCols = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 248, 73, 68, 250, hwnd, (HMENU)IDC_COMBO_COLS, NULL, NULL);
            SendMessage(hComboCols, CB_ADDSTRING, 0, (LPARAM)"Auto");
            for(int i = 1; i <= 30; i++) {
                char buf[8]; sprintf(buf, "%d", i);
                SendMessage(hComboCols, CB_ADDSTRING, 0, (LPARAM)buf);
            }
            SendMessage(hComboCols, CB_SETCURSEL, 0, 0); // Default "Auto"

            CreateWindow("STATIC", "ECC:", WS_CHILD | WS_VISIBLE, 328, 75, 35, 20, hwnd, NULL, NULL, NULL);
            HWND hComboEcc = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 365, 73, 95, 250, hwnd, (HMENU)IDC_COMBO_ECC, NULL, NULL);
            const char* ecc_labels[] = { "0 (QR: L)", "1 (QR: M)", "2 (QR: Q)", "3 (QR: H)", "4", "5", "6", "7", "8" };
            for(int i = 0; i <= 8; i++) SendMessage(hComboEcc, CB_ADDSTRING, 0, (LPARAM)ecc_labels[i]);
            SendMessage(hComboEcc, CB_SETCURSEL, 1, 0); // Default ECC 1

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
            if (LOWORD(wParam) == IDC_COMBO_COLS && HIWORD(wParam) == CBN_SELCHANGE) UpdatePreview(hwnd);
            if (LOWORD(wParam) == IDC_COMBO_ECC  && HIWORD(wParam) == CBN_SELCHANGE) UpdatePreview(hwnd);
            
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
// MAIN ENTRY POINT (CLI & GUI)
// ============================================================================

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (__argc > 1) {
        AttachConsole(ATTACH_PARENT_PROCESS); 
        freopen("CONOUT$", "w", stdout);
        
        if (__argc < 3) {
            printf("\nUsage: BarcodeGen.exe <data> -t:<type> [-s:<size>] [-c:<cols/ver>] [-e:<ecc>] [-o:<output.emf>]\n");
            printf("Types: upc, ean, code39, code128, itf14, codabar, postnet, datamatrix, pdf417, qr\n\n");
            return 1;
        }

        char* data = NULL; 
        char type[32] = {0}; 
        char size_str[32] = {0}; 
        char output_file[260] = {0};
        int cols_or_ver = 0, ecc_level = 1;

        for (int i = 1; i < __argc; i++) {
            if (strncmp(__argv[i], "-t:", 3) == 0) {
                strncpy(type, __argv[i] + 3, sizeof(type) - 1);
            } 
            else if (strncmp(__argv[i], "-s:", 3) == 0) {
                strncpy(size_str, __argv[i] + 3, sizeof(size_str) - 1);
            } 
            else if (strncmp(__argv[i], "-c:", 3) == 0) {
                cols_or_ver = atoi(__argv[i] + 3);
            } 
            else if (strncmp(__argv[i], "-e:", 3) == 0) {
                ecc_level = atoi(__argv[i] + 3);
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

        HENHMETAFILE hMeta = CreateBarcodeEMF(type_idx, data, size_str, cols_or_ver, ecc_level);
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