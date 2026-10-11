/*
 * ============================================================================
 * barcodescan.c - Native Win32 1D (UPC/EAN), DataMatrix & PDF417 Scanner
 * ============================================================================
 * COMPILE INSTRUCTIONS (MinGW GCC):
 * gcc -O2 barcodescan.c -o barcodescan.exe -lgdi32 -luser32 -lole32 -loleaut32 -lstrmiids -luuid -lcomdlg32 -lwindowscodecs -lvfw32 -mwindows
 *
 * CLI OPTIONS:
 *   -vfw                 Use Video for Windows (VfW) capture driver
 *   -LF                  Toggle the 0.25s delayed RETURN key option
 *   -hwnd:<handle>       Set target HWND (dec or 0x hex) & start scanning
 *   -window:<pattern>    Match target window title (name, name*, *name*) & scan
 *   -hidden              Run with no UI or error popups, type barcode & exit
 *
 * REQUIRES: pdf417_clusters.h in the same directory.
 * 
 * 2026-10-11 Hugo
 * 
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================
 */

#include <windows.h>
#include <dshow.h>
#include <wincodec.h>
#include <vfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "pdf417_clusters.h"
#define IDC_CAM_COMBO    101
#define IDC_BTN_BROWSE   102
#define IDC_LBL_TARGET   103
#define IDC_BTN_TOGGLE   104
#define IDC_BTN_INJECT   105
#define IDC_CHK_RETURN   106
#define WM_APP_BARCODE_SCANNED (WM_APP + 1)
int g_iCmdCamIndex = -1;

// MinGW Explicit GUIDs
const GUID CLSID_WICImagingFactory_ = {0xcacaf262,0x9370,0x4615,{0xa1,0x3b,0x9f,0x55,0x39,0xda,0x4c,0x0a}};
const GUID IID_IWICImagingFactory_  = {0xec5ec8a9,0xc395,0x4314,{0x9c,0x77,0x54,0xd7,0xa9,0x35,0xff,0x70}};
const GUID GUID_WICPixelFormat32bppPBGRA_ = {0x6fddc324,0x4e03,0x4bfe,{0xb1,0x85,0x3d,0x77,0x76,0x8d,0xc9,0x10}};
const GUID CLSID_SampleGrabber_ = {0xC1F400A0,0x3F08,0x11D3,{0x9F,0x0B,0x00,0x60,0x08,0x03,0x9E,0x37}};
const GUID IID_ISampleGrabber_  = {0x6B652FFF,0x11FE,0x4FCE,{0x92,0xAD,0x02,0x66,0xB5,0xD7,0xC7,0x8F}};
const GUID IID_IAMStreamConfig_ = {0xC6E13340,0x30AC,0x11d0,{0xA1,0x8C,0x00,0xA0,0xC9,0x11,0x89,0x56}};

typedef struct ISampleGrabber ISampleGrabber;
typedef struct ISampleGrabberVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ISampleGrabber *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ISampleGrabber *This);
    ULONG (STDMETHODCALLTYPE *Release)(ISampleGrabber *This);
    HRESULT (STDMETHODCALLTYPE *SetOneShot)(ISampleGrabber *This, BOOL OneShot);
    HRESULT (STDMETHODCALLTYPE *SetMediaType)(ISampleGrabber *This, const AM_MEDIA_TYPE *pType);
    HRESULT (STDMETHODCALLTYPE *GetConnectedMediaType)(ISampleGrabber *This, AM_MEDIA_TYPE *pType);
    HRESULT (STDMETHODCALLTYPE *SetBufferSamples)(ISampleGrabber *This, BOOL BufferThem);
    HRESULT (STDMETHODCALLTYPE *GetCurrentBuffer)(ISampleGrabber *This, long *pBufferSize, long *pBuffer);
    HRESULT (STDMETHODCALLTYPE *GetCurrentSample)(ISampleGrabber *This, IMediaSample **ppSample);
    HRESULT (STDMETHODCALLTYPE *SetCallback)(ISampleGrabber *This, void *pCallback, long WhichMethodToCallback);
} ISampleGrabberVtbl;
struct ISampleGrabber { const ISampleGrabberVtbl *lpVtbl; };

typedef struct { short x, y; } Point2D;

HWND g_hMain, g_hVidView, g_hImgView, g_hCamCombo, g_hLblTarget, g_hBtnToggle, g_hBtnInject, g_hChkReturn = NULL;
HWND g_hTargetWnd = NULL, g_hLastHighlight = NULL, g_hCmdTargetWnd = NULL;
HBITMAP g_hLoadedImg = NULL;
BYTE* g_pStaticPixels = NULL;
UINT g_imgW = 0, g_imgH = 0;
BOOL g_bShowImage = FALSE, g_bDragging = FALSE, g_bBtnDown = FALSE, g_bScanning = FALSE;
BOOL g_bAddReturn = FALSE, g_bHidden = FALSE;
char g_szWindowPattern[256] = {0};
POINT g_ptClick;
WNDPROC OldToggleProc;
char g_szLastScan[1024] = {0};

// DirectShow state
IGraphBuilder *g_pGraph = NULL;
ICaptureGraphBuilder2 *g_pBuild = NULL;
IMediaControl *g_pControl = NULL;
IVideoWindow *g_pVidWin = NULL;
ISampleGrabber *g_pGrabber = NULL;
IMoniker *g_pCameras[10] = {0};
int g_iCameraCount = 0;

// Video for Windows (VfW) state
BOOL g_bUseVFW = FALSE;
HWND g_hCapWnd = NULL;
int g_vfwDriverMap[10] = {0};
BITMAPINFO g_vfwBmi = {0};
BYTE* g_pVfwPixels = NULL;
long g_vfwBufSize = 0;
int g_vfwBpp = 24;
BOOL g_vfwBottomUp = TRUE;

// ============================================================================
// 1D SUB-PIXEL L/G/R DECODER, UPSIDE-DOWN & STRICT CONSENSUS ENGINE
// ============================================================================

int MatchDigitStrict(float r1, float r2, float r3, float r4, int allow_g, int* out_is_g, float* out_err) {
    float s = r1 + r2 + r3 + r4;
    if (s < 4.5f) return -1;

    float n[4] = { r1 * 7.0f / s, r2 * 7.0f / s, r3 * 7.0f / s, r4 * 7.0f / s };

    static const float pats_L[10][4] = {
        {3,2,1,1},{2,2,2,1},{2,1,2,2},{1,4,1,1},{1,1,3,2},
        {1,2,3,1},{1,1,1,4},{1,3,1,2},{1,2,1,3},{3,1,1,2}
    };
    static const float pats_G[10][4] = {
        {1,1,2,3},{1,2,2,2},{2,2,1,2},{1,1,4,1},{2,3,1,1},
        {1,3,2,1},{4,1,1,1},{2,1,3,1},{3,1,2,1},{2,1,1,3}
    };

    int best_digit = -1, best_g = 0;
    float min_err = 999.0f, second_err = 999.0f;

    for (int i = 0; i < 10; i++) {
        float d0 = fabsf(n[0] - pats_L[i][0]), d1 = fabsf(n[1] - pats_L[i][1]);
        float d2 = fabsf(n[2] - pats_L[i][2]), d3 = fabsf(n[3] - pats_L[i][3]);
        if (d0 < 0.70f && d1 < 0.70f && d2 < 0.70f && d3 < 0.70f) {
            float errL = d0 + d1 + d2 + d3;
            if (errL < min_err) {
                second_err = min_err;
                min_err = errL;
                best_digit = i;
                best_g = 0;
            } else if (errL < second_err) {
                second_err = errL;
            }
        }

        if (allow_g) {
            float g0 = fabsf(n[0] - pats_G[i][0]), g1 = fabsf(n[1] - pats_G[i][1]);
            float g2 = fabsf(n[2] - pats_G[i][2]), g3 = fabsf(n[3] - pats_G[i][3]);
            if (g0 < 0.70f && g1 < 0.70f && g2 < 0.70f && g3 < 0.70f) {
                float errG = g0 + g1 + g2 + g3;
                if (errG < min_err) {
                    second_err = min_err;
                    min_err = errG;
                    best_digit = i;
                    best_g = 1;
                } else if (errG < second_err) {
                    second_err = errG;
                }
            }
        }
    }

    if (best_digit != -1 && min_err < 1.20f && (second_err - min_err) >= 0.25f) {
        if (out_is_g) *out_is_g = best_g;
        if (out_err)  *out_err  = min_err;
        return best_digit;
    }
    return -1;
}

static inline int SampleSingleGray(BYTE* pixels, int w, int h, int stride, int bpp, BOOL is_bottom_up, int x, int y) {
    if (y < 0 || y >= h || x < 0 || x >= w) return 255;
    BYTE* p = pixels + (is_bottom_up ? (h - 1 - y) : y) * stride + x * (bpp / 8);
    return (bpp >= 24) ? ((p[2] + p[1] + p[0]) / 3) : p[0];
}

int TryDecodeSequence(const float* runs59, float qz_left, float qz_right, int dir, char* out_code, float* out_total_err) {
    float r[59];
    for (int k = 0; k < 59; k++) r[k] = (dir == 1) ? runs59[k] : runs59[58 - k];

    float modS = (r[0] + r[1] + r[2]) / 3.0f;
    if (modS < 0.65f) return 0;

    float modM = (r[27] + r[28] + r[29] + r[30] + r[31]) / 5.0f;
    float modE = (r[56] + r[57] + r[58]) / 3.0f;
    if (modM > modS * 1.28f || modM < modS * 0.78f) return 0;
    if (modE > modS * 1.28f || modE < modS * 0.78f) return 0;

    float avg_mod = (modS + modM + modE) / 3.0f;

    // Require quiet zones on both ends of the 1D symbol
    if (qz_left < avg_mod * 4.5f || qz_right < avg_mod * 4.5f) return 0;

    // Verify individual guard elements (1-1-1 start, 1-1-1-1-1 middle, 1-1-1 stop)
    for (int k = 0; k < 3; k++) {
        if (r[k] < modS * 0.55f || r[k] > modS * 1.50f) return 0;
        if (r[56 + k] < modE * 0.55f || r[56 + k] > modE * 1.50f) return 0;
    }
    for (int k = 27; k < 32; k++) {
        if (r[k] < modM * 0.50f || r[k] > modM * 1.55f) return 0;
    }

    int digits[13] = {0}, g_mask = 0;
    float total_err = 0.0f, d_err = 0.0f;

    for (int d = 0; d < 6; d++) {
        int idx = 3 + d * 4;
        float w_dig = r[idx] + r[idx + 1] + r[idx + 2] + r[idx + 3];
        if (w_dig < avg_mod * 5.5f || w_dig > avg_mod * 8.6f) return 0;

        int is_g = 0;
        int val = MatchDigitStrict(r[idx], r[idx + 1], r[idx + 2], r[idx + 3], 1, &is_g, &d_err);
        if (val == -1) return 0;
        digits[d + 1] = val;
        g_mask = (g_mask << 1) | is_g;
        total_err += d_err;
    }

    static const int parity_table[10] = { 0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A };
    int first_digit = -1;
    for (int p = 0; p < 10; p++) {
        if (g_mask == parity_table[p]) { first_digit = p; break; }
    }
    if (first_digit == -1) return 0;
    digits[0] = first_digit;

    for (int d = 0; d < 6; d++) {
        int idx = 32 + d * 4;
        float w_dig = r[idx] + r[idx + 1] + r[idx + 2] + r[idx + 3];
        if (w_dig < avg_mod * 5.5f || w_dig > avg_mod * 8.6f) return 0;

        int val = MatchDigitStrict(r[idx], r[idx + 1], r[idx + 2], r[idx + 3], 0, NULL, &d_err);
        if (val == -1) return 0;
        digits[d + 7] = val;
        total_err += d_err;
    }

    if (total_err > 6.5f) return 0;

    int even_sum = 0, odd_sum = 0;
    for (int j = 0; j < 12; j++) {
        if (j % 2 == 0) even_sum += digits[j];
        else            odd_sum  += digits[j];
    }
    if (((10 - ((odd_sum * 3 + even_sum) % 10)) % 10) != digits[12]) return 0;

    if (digits[0] == 0) {
        for (int j = 0; j < 12; j++) out_code[j] = '0' + digits[j + 1];
        out_code[12] = '\0';
    } else {
        for (int j = 0; j < 13; j++) out_code[j] = '0' + digits[j];
        out_code[13] = '\0';
    }

    *out_total_err = total_err;
    return 1;
}

int ScanBarcodeRuns(float* runs, int num_runs, int first_color, int y_coord, char* out_code, float* out_err, RECT* out_rc) {
    int found = 0;
    float best_err = 999.0f;

    for (int i = 0; i <= num_runs - 59; i++) {
        if ((first_color + i) % 2 != 1) continue;

        float qz_left  = (i > 0) ? runs[i - 1] : 999.0f;
        float qz_right = (i + 59 < num_runs) ? runs[i + 59] : 999.0f;

        char cand[16] = {0};
        float err = 999.0f;

        if (TryDecodeSequence(&runs[i], qz_left, qz_right, 1, cand, &err) ||
            TryDecodeSequence(&runs[i], qz_left, qz_right, -1, cand, &err)) {
            if (err < best_err) {
                best_err = err;
                strcpy(out_code, cand);
                found = 1;
                if (out_rc) {
                    float x_start = 0.0f; for (int r = 0; r < i; r++) x_start += runs[r];
                    float x_end = x_start; for (int r = i; r < i + 59; r++) x_end += runs[r];
                    float modS = (runs[i] + runs[i+1] + runs[i+2]) / 3.0f;
                    out_rc->left   = (int)x_start;
                    out_rc->right  = (int)x_end;
                    out_rc->top    = y_coord - (int)(modS * 20.0f);
                    out_rc->bottom = y_coord + (int)(modS * 20.0f);
                }
            }
        }
    }
    if (found && out_err) *out_err = best_err;
    return found;
}

int ScanLineRobust(int* gray_pixels, int length, int y_coord, char* out_code, float* out_err, RECT* out_rc) {
    int* smooth = (int*)malloc(length * sizeof(int));
    for (int x = 0; x < length; x++) {
        smooth[x] = (x == 0 || x == length - 1) ? gray_pixels[x] : (gray_pixels[x-1] + gray_pixels[x]*2 + gray_pixels[x+1]) / 4;
    }

    int* thresh = (int*)malloc(length * sizeof(int));
    int window = length / 15; if (window < 5) window = 5;
    long sum = 0; for (int x = 0; x < window && x < length; x++) sum += smooth[x];
    for (int x = 0; x < length; x++) {
        thresh[x] = sum / window;
        int left = x - window/2, right = x + window/2 + 1;
        sum += (right < length ? smooth[right] : smooth[length-1]) - (left >= 0 ? smooth[left] : smooth[0]);
    }

    float* runs = (float*)malloc(length * sizeof(float));
    int num_runs = 0, first_color = -1, current_val = -1;
    float last_cross_x = 0.0f;

    for (int x = 0; x < length; x++) {
        float diff_curr = (float)smooth[x] - (float)(thresh[x] - 5);
        int val = (diff_curr < 0.0f) ? 1 : 0;

        if (first_color == -1) first_color = val;
        if (current_val == -1) current_val = val;

        if (val != current_val && x > 0) {
            float diff_prev = (float)smooth[x-1] - (float)(thresh[x-1] - 5);
            float denom = diff_prev - diff_curr;
            float frac = (fabsf(denom) > 0.001f) ? (diff_prev / denom) : 0.5f;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;

            float cross_x = (float)(x - 1) + frac;
            runs[num_runs++] = cross_x - last_cross_x;
            last_cross_x = cross_x;
            current_val = val;
        }
    }
    if (last_cross_x < (float)(length - 1)) {
        runs[num_runs++] = (float)(length - 1) - last_cross_x;
    }

    int ok = ScanBarcodeRuns(runs, num_runs, first_color, y_coord, out_code, out_err, out_rc);
    free(smooth); free(thresh); free(runs);
    return ok;
}

static inline int SampleBandGray(BYTE* pixels, int w, int h, int stride, int bpp, BOOL is_bottom_up, int x, int y) {
    int sum = 0, count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        int cy = y + dy;
        if (cy >= 0 && cy < h) {
            BYTE* p = pixels + (is_bottom_up ? (h - 1 - cy) : cy) * stride + x * (bpp / 8);
            sum += (bpp >= 24) ? ((p[2] + p[1] + p[0]) / 3) : p[0];
            count++;
        }
    }
    return count ? (sum / count) : 255;
}

// ============================================================================
// NATIVE PDF417 DETECTOR & DECODER (STRICT CLUSTERS + GF(929) RS + COMPACTION)
// ============================================================================

static BYTE g_pdfRuns[3][929][8];
static BYTE g_pdfT[3][929][7];
static int  g_pdfInv929[929];
static BOOL g_pdfInit = FALSE;

static int gf929_mul(int a, int b) {
    return (int)(((long long)((a % 929 + 929) % 929) * ((b % 929 + 929) % 929)) % 929);
}

static int gf929_pow(int base, int exp) {
    int res = 1;
    base = (base % 929 + 929) % 929;
    while (exp > 0) {
        if (exp & 1) res = (res * base) % 929;
        base = (base * base) % 929;
        exp >>= 1;
    }
    return res;
}

void InitPDF417Tables() {
    if (g_pdfInit) return;
    g_pdfInv929[0] = 0;
    for (int i = 1; i < 929; i++) {
        g_pdfInv929[i] = gf929_pow(i, 927);
    }
    for (int c = 0; c < 3; c++) {
        for (int w = 0; w < 929; w++) {
            int pat = pdf417_clusters[c][w];
            int r_idx = 0, cur_bit = 1, count = 0;
            for (int b = 16; b >= 0; b--) {
                int bit = (pat >> b) & 1;
                if (bit == cur_bit) {
                    count++;
                } else {
                    if (r_idx < 8) g_pdfRuns[c][w][r_idx++] = (BYTE)count;
                    cur_bit = bit;
                    count = 1;
                }
            }
            if (r_idx < 8) g_pdfRuns[c][w][r_idx++] = (BYTE)count;
            for (int k = 0; k < 7; k++) {
                g_pdfT[c][w][k] = (BYTE)(g_pdfRuns[c][w][k] + g_pdfRuns[c][w][k + 1]);
            }
        }
    }
    g_pdfInit = TRUE;
}

static int MatchPDF417CodewordRuns(const float* r8, float expected_w, int cluster, float* out_err) {
    float s = 0.0f;
    for (int k = 0; k < 8; k++) s += r8[k];
    if (s < 4.5f) return -1;
    if (expected_w > 0.0f && (s < expected_w * 0.78f || s > expected_w * 1.22f)) return -1;

    float scale = 17.0f / s;
    float n[8], t[7];
    for (int k = 0; k < 8; k++) {
        n[k] = r8[k] * scale;
        if (n[k] < 0.35f || n[k] > 6.85f) return -1;
    }
    for (int k = 0; k < 7; k++) t[k] = n[k] + n[k + 1];

    int best_cw = -1;
    float best_err = 999.0f, second_err = 999.0f;
    const BYTE (*c_runs)[8] = g_pdfRuns[cluster];
    const BYTE (*c_t)[7]    = g_pdfT[cluster];

    for (int w = 0; w < 929; w++) {
        float t_err = 0.0f;
        int ok = 1;
        for (int k = 0; k < 7; k++) {
            float dt = fabsf(t[k] - (float)c_t[w][k]);
            if (dt > 0.80f) { ok = 0; break; }
            t_err += dt;
        }
        if (!ok || t_err >= second_err) continue;

        float r_err = 0.0f;
        for (int k = 0; k < 8; k++) {
            float dr = fabsf(n[k] - (float)c_runs[w][k]);
            if (dr > 0.80f) { ok = 0; break; }
            r_err += dr;
        }
        if (!ok) continue;

        float err = t_err + 0.50f * r_err;
        if (err < best_err) {
            second_err = best_err;
            best_err = err;
            best_cw = w;
        } else if (err < second_err) {
            second_err = err;
        }
    }

    if (best_cw != -1 && best_err < 2.20f && (second_err - best_err) >= 0.18f) {
        if (out_err) *out_err = best_err;
        return best_cw;
    }
    return -1;
}

static int MatchPDF417CodewordSpatial(const int* gray_line, int line_len, float x0, float x1, int cluster, float* out_err) {
    float cw_w = x1 - x0;
    if (cw_w < 6.0f || x0 < 0.0f || x1 >= (float)line_len) return -1;

    float mod_val[17];
    float min_g = 255.0f, max_g = 0.0f;
    for (int m = 0; m < 17; m++) {
        float fx = x0 + (m + 0.5f) * (cw_w / 17.0f);
        int ix = (int)fx;
        float frac = fx - (float)ix;
        float g = (ix + 1 < line_len)
            ? ((1.0f - frac) * gray_line[ix] + frac * gray_line[ix + 1])
            : (float)gray_line[ix];
        mod_val[m] = g;
        if (g < min_g) min_g = g;
        if (g > max_g) max_g = g;
    }
    if (max_g - min_g < 15.0f) return -1;

    float mid = (min_g + max_g) * 0.5f;
    float norm = 2.0f / (max_g - min_g);
    float bip[17];
    for (int m = 0; m < 17; m++) {
        bip[m] = (mid - mod_val[m]) * norm;
    }

    int best_cw = -1;
    float best_err = 999.0f;
    for (int w = 0; w < 929; w++) {
        int pat = pdf417_clusters[cluster][w];
        float err = 0.0f;
        for (int m = 0; m < 17; m++) {
            int bit = (pat >> (16 - m)) & 1;
            float target = bit ? 1.0f : -1.0f;
            err += fabsf(bip[m] - target);
        }
        err *= 0.25f;
        if (err < best_err) {
            best_err = err;
            best_cw = w;
        }
    }

    if (best_err < 3.8f) {
        if (out_err) *out_err = best_err;
        return best_cw;
    }
    return -1;
}

static BOOL CheckPDF417StartPattern(const float* r8, float* out_mod, float* out_err) {
    static const float pat[8] = {8, 1, 1, 1, 1, 1, 1, 3};
    float s = 0.0f;
    for (int k = 0; k < 8; k++) s += r8[k];
    float mod = s / 17.0f;
    if (mod < 0.45f) return FALSE;

    float n0 = r8[0] / mod, n7 = r8[7] / mod;
    if (n0 < 6.2f || n0 > 9.8f || n7 < 2.0f || n7 > 4.0f) return FALSE;

    float err = fabsf(n0 - 8.0f) + fabsf(n7 - 3.0f);
    for (int k = 1; k <= 6; k++) {
        float nk = r8[k] / mod;
        if (nk < 0.35f || nk > 1.85f) return FALSE;
        err += fabsf(nk - pat[k]);
    }
    if (err > 2.6f) return FALSE;
    if (out_mod) *out_mod = mod;
    if (out_err) *out_err = err;
    return TRUE;
}

static BOOL CheckPDF417StopPattern(const float* r9, int avail_runs, float* out_mod, float* out_err) {
    static const float pat[9] = {7, 1, 1, 3, 1, 1, 1, 2, 1};
    if (avail_runs < 9) return FALSE;
    float s = 0.0f;
    for (int k = 0; k < 9; k++) s += r9[k];
    float mod = s / 18.0f;
    if (mod < 0.45f) return FALSE;

    float n0 = r9[0] / mod, n3 = r9[3] / mod, n7 = r9[7] / mod, n8 = r9[8] / mod;
    if (n0 < 5.4f || n0 > 8.6f || n3 < 2.0f || n3 > 4.0f ||
        n7 < 1.2f || n7 > 2.8f || n8 < 0.35f || n8 > 1.85f) return FALSE;

    float err = fabsf(n0 - 7.0f) + fabsf(n3 - 3.0f) + fabsf(n7 - 2.0f) + fabsf(n8 - 1.0f);
    int ones_idx[5] = {1, 2, 4, 5, 6};
    for (int i = 0; i < 5; i++) {
        float nk = r9[ones_idx[i]] / mod;
        if (nk < 0.35f || nk > 1.85f) return FALSE;
        err += fabsf(nk - 1.0f);
    }
    if (err > 2.8f) return FALSE;
    if (out_mod) *out_mod = mod;
    if (out_err) *out_err = err;
    return TRUE;
}

static BOOL SolvePDF417KnownLocations(int* cw, int N, int K, const int* S, const int* err_idx, int E) {
    // Require at least 2 spare syndromes (K - E >= 2) so solutions are always verified
    if (E <= 0 || E > K - 2 || E > 32) return FALSE;
    int X[32];
    for (int m = 0; m < E; m++) {
        if (err_idx[m] < 0 || err_idx[m] >= N) return FALSE;
        X[m] = gf929_pow(3, N - 1 - err_idx[m]);
    }

    int trial[1024];
    memcpy(trial, cw, N * sizeof(int));

    for (int m = 0; m < E; m++) {
        int c[33] = {0};
        c[0] = 1;
        int deg = 0;
        int denom = X[m];

        for (int r = 0; r < E; r++) {
            if (r == m) continue;
            int neg_Xr = (929 - X[r]) % 929;
            for (int k = deg + 1; k >= 1; k--) {
                c[k] = (c[k - 1] + gf929_mul(c[k], neg_Xr)) % 929;
            }
            c[0] = gf929_mul(c[0], neg_Xr);
            deg++;
            int diff = (X[m] + 929 - X[r]) % 929;
            denom = gf929_mul(denom, diff);
        }
        if (denom == 0) return FALSE;

        int num = 0;
        for (int k = 0; k < E; k++) {
            num = (num + gf929_mul(c[k], S[k + 1])) % 929;
        }
        int err_val = gf929_mul(num, g_pdfInv929[denom]);
        trial[err_idx[m]] = (trial[err_idx[m]] + 929 - err_val) % 929;
        if (trial[err_idx[m]] < 0 || trial[err_idx[m]] >= 929) return FALSE;
    }

    for (int j = 1; j <= K; j++) {
        int syn = 0, a_j = gf929_pow(3, j);
        for (int i = 0; i < N; i++) {
            syn = (gf929_mul(syn, a_j) + trial[i]) % 929;
        }
        if (syn != 0) return FALSE;
    }

    memcpy(cw, trial, N * sizeof(int));
    return TRUE;
}

static BOOL VerifyAndCorrectPDF417RS(int* cw, int payload_len, int ecc_count, const int* erasures, int num_erasures) {
    InitPDF417Tables();
    int N = payload_len + ecc_count;
    if (payload_len < 2 || ecc_count < 2 || N > 928) return FALSE;

    // Reject if too many cells were missed (never guess when > ecc_count/4 cells are missing)
    int max_erasures = (ecc_count >= 8) ? (ecc_count / 4) : ((ecc_count == 4) ? 1 : 0);
    if (num_erasures > max_erasures) return FALSE;

    for (int i = 0; i < N; i++) {
        if (cw[i] < 0 || cw[i] >= 929) cw[i] = 0;
    }

    int S[520] = {0};
    BOOL has_err = FALSE;
    for (int j = 1; j <= ecc_count; j++) {
        int syn = 0, a_j = gf929_pow(3, j);
        for (int i = 0; i < N; i++) {
            syn = (gf929_mul(syn, a_j) + cw[i]) % 929;
        }
        S[j] = syn;
        if (syn != 0) has_err = TRUE;
    }
    if (!has_err) return (cw[0] == payload_len);

    // If ecc_count == 2 (ECC level 0), never attempt error correction (0 spare syndromes)
    if (ecc_count < 4) return FALSE;

    // 1. Try known erasure positions (with at least K - num_erasures >= 3 spare verification syndromes)
    if (num_erasures > 0 && num_erasures <= max_erasures) {
        if (SolvePDF417KnownLocations(cw, N, ecc_count, S, erasures, num_erasures)) {
            return (cw[0] == payload_len);
        }
        return FALSE;
    }

    // 2. Single-error algebraic solver (leaves K - 2 >= 2 independent check syndromes)
    if (ecc_count >= 4 && S[1] != 0) {
        int X0 = gf929_mul(S[2], g_pdfInv929[S[1]]);
        int p0 = -1, cur = 1;
        for (int p = 0; p < N; p++) {
            if (cur == X0) { p0 = p; break; }
            cur = (cur * 3) % 929;
        }
        if (p0 != -1) {
            int idx0 = N - 1 - p0;
            if (SolvePDF417KnownLocations(cw, N, ecc_count, S, &idx0, 1)) {
                return (cw[0] == payload_len);
            }
        }
    }

    // For ECC level 1 (ecc_count == 4), do not attempt > 1 unknown error
    if (ecc_count <= 4) return FALSE;

    // 3. Berlekamp-Massey + Chien search for ECC level >= 2 (reserving >= 4 spare syndromes)
    int K = (ecc_count > 128) ? 128 : ecc_count;
    int C[130] = {0}, B[130] = {0};
    C[0] = 1; B[0] = 1;
    int L = 0, m = 1, b = 1;

    for (int n = 0; n < K; n++) {
        int d = S[n + 1];
        for (int i = 1; i <= L; i++) {
            d = (d + gf929_mul(C[i], S[n + 1 - i])) % 929;
        }
        if (d == 0) {
            m++;
        } else {
            int T[130];
            memcpy(T, C, sizeof(C));
            int coef = gf929_mul(d, g_pdfInv929[b]);
            for (int i = m; i < 130; i++) {
                C[i] = (C[i] + 929 - gf929_mul(coef, B[i - m])) % 929;
            }
            if (2 * L <= n) {
                L = n + 1 - L;
                memcpy(B, T, sizeof(B));
                b = d;
                m = 1;
            } else {
                m++;
            }
        }
    }

    if (L <= 0 || 2 * L > K - 4 || L > 32) return FALSE;

    int err_idx[32], err_cnt = 0;
    for (int i = 0; i < N; i++) {
        int p = N - 1 - i;
        int x_inv = g_pdfInv929[gf929_pow(3, p)];
        int eval = 0, x_pow = 1;
        for (int j = 0; j <= L; j++) {
            eval = (eval + gf929_mul(C[j], x_pow)) % 929;
            x_pow = gf929_mul(x_pow, x_inv);
        }
        if (eval == 0 && err_cnt < 32) {
            err_idx[err_cnt++] = i;
        }
    }

    if (err_cnt != L) return FALSE;
    if (!SolvePDF417KnownLocations(cw, N, ecc_count, S, err_idx, err_cnt)) return FALSE;
    return (cw[0] == payload_len);
}

static void DecodePDF417TextCompaction(const int* cw, int* idx, int limit, char* out, int* out_len, int max_out) {
    static const char mixed_tbl[25] = "0123456789&\r\t,:#-.$/+%*=^";
    static const char punct_tbl[29] = ";<>@[\\]_`~!\r\t,:\n-.$/\"|*()?{}'";
    int mode = 0, saved_mode = 0, is_shift = 0;

    while (*idx < limit && *out_len < max_out - 1) {
        int w = cw[*idx];
        if (w >= 900) {
            if (w == 900) { (*idx)++; continue; }
            break;
        }
        (*idx)++;
        int sub[2] = { w / 30, w % 30 };
        for (int s = 0; s < 2 && *out_len < max_out - 1; s++) {
            int v = sub[s];
            int cur_mode = is_shift ? 3 : mode;
            if (is_shift == 2) cur_mode = 0;
            is_shift = 0;

            if (cur_mode == 0) {
                if (v < 26) out[(*out_len)++] = (char)('A' + v);
                else if (v == 26) out[(*out_len)++] = ' ';
                else if (v == 27) mode = 1;
                else if (v == 28) mode = 2;
                else if (v == 29) { saved_mode = mode; is_shift = 1; }
            } else if (cur_mode == 1) {
                if (v < 26) out[(*out_len)++] = (char)('a' + v);
                else if (v == 26) out[(*out_len)++] = ' ';
                else if (v == 27) { saved_mode = mode; is_shift = 2; }
                else if (v == 28) mode = 2;
                else if (v == 29) { saved_mode = mode; is_shift = 1; }
            } else if (cur_mode == 2) {
                if (v < 25) out[(*out_len)++] = mixed_tbl[v];
                else if (v == 25) mode = 3;
                else if (v == 26) out[(*out_len)++] = ' ';
                else if (v == 27) mode = 1;
                else if (v == 28) mode = 0;
                else if (v == 29) { saved_mode = mode; is_shift = 1; }
            } else if (cur_mode == 3) {
                if (v < 29) out[(*out_len)++] = punct_tbl[v];
                else if (v == 29) mode = 0;
                if (saved_mode != mode && is_shift) mode = saved_mode;
            }
        }
    }
}

static BOOL DecodePDF417ByteCompaction(const int* cw, int* idx, int limit, int mode_latch, char* out, int* out_len, int max_out) {
    int end_idx = *idx;
    while (end_idx < limit && cw[end_idx] < 900) end_idx++;

    int total_cw = end_idx - *idx;
    if (total_cw <= 0) return FALSE;
    // Mode 924 requires exact multiples of 5 codewords (6 bytes per 5 codewords)
    if (mode_latch == 924 && (total_cw % 5) != 0) return FALSE;

    while (*idx < end_idx && *out_len < max_out - 1) {
        int rem = end_idx - *idx;
        if (rem >= 5) {
            BOOL use_base900 = TRUE;
            if (rem == 5 && mode_latch == 901) {
                unsigned long long test_val = 0;
                BOOL all_raw_bytes = TRUE;
                for (int j = 0; j < 5; j++) {
                    int w = cw[*idx + j];
                    if (w < 0 || w > 255) all_raw_bytes = FALSE;
                    test_val = test_val * 900ULL + (unsigned long long)w;
                }
                if (all_raw_bytes) {
                    BOOL b900_printable = (test_val <= 0xFFFFFFFFFFFFULL);
                    if (b900_printable) {
                        for (int j = 5; j >= 0; j--) {
                            unsigned char b = (unsigned char)((test_val >> (j * 8)) & 0xFF);
                            if (b < 32 || b > 126) { b900_printable = FALSE; break; }
                        }
                    }
                    if (!b900_printable) use_base900 = FALSE;
                }
            }

            if (use_base900) {
                unsigned long long val = 0;
                for (int j = 0; j < 5; j++) {
                    val = val * 900ULL + (unsigned long long)cw[(*idx)++];
                }
                // 6 bytes max is 0xFFFFFFFFFFFFULL; overflow means corrupted base-900 group
                if (val > 0xFFFFFFFFFFFFULL) return FALSE;
                for (int j = 5; j >= 0 && *out_len < max_out - 1; j--) {
                    out[(*out_len)++] = (char)((val >> (j * 8)) & 0xFF);
                }
            } else {
                for (int j = 0; j < 5 && *out_len < max_out - 1; j++) {
                    int w = cw[(*idx)++];
                    if (w < 0 || w > 255) return FALSE;
                    out[(*out_len)++] = (char)w;
                }
            }
        } else {
            // Trailing 1..4 codewords in mode 901 must each be a single byte (0..255)
            while (*idx < end_idx && *out_len < max_out - 1) {
                int w = cw[(*idx)++];
                if (w < 0 || w > 255) return FALSE;
                out[(*out_len)++] = (char)w;
            }
        }
    }
    return TRUE;
}

static void DecodePDF417NumericCompaction(const int* cw, int* idx, int limit, char* out, int* out_len, int max_out) {
    while (*idx < limit && *out_len < max_out - 1) {
        int group[15], count = 0;
        while (*idx < limit && cw[*idx] < 900 && count < 15) {
            group[count++] = cw[(*idx)++];
        }
        if (count == 0) break;

        int dec[64] = {0}, dec_len = 1;
        for (int i = 0; i < count; i++) {
            int carry = group[i];
            for (int j = 0; j < dec_len; j++) {
                int v = dec[j] * 900 + carry;
                dec[j] = v % 10;
                carry = v / 10;
            }
            while (carry > 0 && dec_len < 64) {
                dec[dec_len++] = carry % 10;
                carry /= 10;
            }
        }
        for (int j = dec_len - 2; j >= 0 && *out_len < max_out - 1; j--) {
            out[(*out_len)++] = (char)('0' + dec[j]);
        }
    }
}

static BOOL DecodePDF417Stream(const int* full_buf, int payload_len, char* out_str, int max_out) {
    if (payload_len <= 1 || payload_len > 928 || full_buf[0] != payload_len) return FALSE;

    int real_end = payload_len;
    while (real_end > 1 && full_buf[real_end - 1] == 900) real_end--;
    if (real_end <= 1) return FALSE;

    // If the barcode begins with a Byte Compaction latch (901 or 924) as in BarcodeGen.c,
    // no codeword >= 900 may appear inside [2 .. real_end - 1]
    if (full_buf[1] == 901 || full_buf[1] == 924) {
        for (int k = 2; k < real_end; k++) {
            if (full_buf[k] < 0 || full_buf[k] >= 900) return FALSE;
        }
    }

    int idx = 1, out_len = 0;

    while (idx < real_end && out_len < max_out - 1) {
        int w = full_buf[idx];
        if (w == 900) {
            idx++;
            DecodePDF417TextCompaction(full_buf, &idx, real_end, out_str, &out_len, max_out);
        } else if (w == 901 || w == 924) {
            idx++;
            if (!DecodePDF417ByteCompaction(full_buf, &idx, real_end, w, out_str, &out_len, max_out)) {
                return FALSE;
            }
        } else if (w == 902) {
            idx++;
            DecodePDF417NumericCompaction(full_buf, &idx, real_end, out_str, &out_len, max_out);
        } else if (w == 913) {
            idx++;
            if (idx < real_end && out_len < max_out - 1) {
                if (full_buf[idx] < 0 || full_buf[idx] > 255) return FALSE;
                out_str[out_len++] = (char)full_buf[idx++];
            }
        } else if (w >= 0 && w < 900) {
            DecodePDF417TextCompaction(full_buf, &idx, real_end, out_str, &out_len, max_out);
        } else {
            return FALSE;
        }
    }

    if (out_len <= 0) return FALSE;

    // Final sanity check: reject if any decoded character is non-printable garbage
    for (int i = 0; i < out_len; i++) {
        unsigned char c = (unsigned char)out_str[i];
        if ((c < 32 && c != '\t' && c != '\r' && c != '\n') || c >= 127) {
            return FALSE;
        }
    }

    out_str[out_len] = '\0';
    return TRUE;
}

typedef struct {
    int cols;
    int left_ind, right_ind;
    int cluster;
    int row_L, row_R;
    int cws[32];
    float errs[32];
    float x_left, x_right;
    float y_scan;
} PDF417RowScan;

static BOOL TryExtractPDF417RowFromRuns(
    const float* runs, const float* run_x, int num_runs, int first_color,
    int line_len, int dir, float y_coord, PDF417RowScan* out_row)
{
    for (int i = 0; i <= num_runs - 33; i++) {
        if ((first_color + i) % 2 != 1) continue;

        float modS = 0.0f, errS = 0.0f;
        if (!CheckPDF417StartPattern(&runs[i], &modS, &errS)) continue;

        float x_inner_start = run_x[i + 8];
        for (int j = i + 32; j <= num_runs - 9; j += 8) {
            if ((first_color + j) % 2 != 1) continue;

            int num_cw = (j - (i + 8)) / 8;
            if (num_cw < 3 || num_cw > 32) continue;

            float x_inner_end = run_x[j];
            float inner_w = x_inner_end - x_inner_start;
            if (inner_w < modS * 17.0f * (float)num_cw * 0.78f) continue;
            if (inner_w > modS * 17.0f * (float)num_cw * 1.25f) break;

            float modE = 0.0f, errE = 0.0f;
            if (!CheckPDF417StopPattern(&runs[j], num_runs - j, &modE, &errE)) continue;
            if (modE < modS * 0.78f || modE > modS * 1.28f) continue;

            float cw_span = inner_w / (float)num_cw;
            int cols_cand = num_cw - 2;

            int best_cluster = -1;
            float best_cluster_score = 9999.0f;
            int best_seq[34];
            float best_errs[34];

            for (int c = 0; c < 3; c++) {
                float eL = 99.0f, eR = 99.0f;
                int cwL = MatchPDF417CodewordRuns(&runs[i + 8], cw_span, c, &eL);
                int cwR = MatchPDF417CodewordRuns(&runs[j - 8], cw_span, c, &eR);

                // Both row indicators must be valid (< 900) and agree on the same row
                if (cwL < 0 || cwL >= 900 || cwR < 0 || cwR >= 900) continue;
                if ((cwL / 30) != (cwR / 30)) continue;

                // Validate indicator metadata for this cluster
                if (c == 0 && (cwR % 30) != (cols_cand - 1)) continue;
                if (c == 1 && (cwL % 30) >= 27) continue;
                if (c == 2 && ((cwL % 30) != (cols_cand - 1) || (cwR % 30) >= 27)) continue;

                float sum_err = eL + eR;
                int temp_seq[34];
                float temp_errs[34];
                temp_seq[0] = cwL; temp_errs[0] = eL;
                temp_seq[num_cw - 1] = cwR; temp_errs[num_cw - 1] = eR;

                int valid_data = 0;
                for (int k = 1; k < num_cw - 1; k++) {
                    float ek = 99.0f;
                    int cwk = MatchPDF417CodewordRuns(&runs[i + 8 + k * 8], cw_span, c, &ek);
                    temp_seq[k] = cwk;
                    temp_errs[k] = ek;
                    if (cwk >= 0) {
                        valid_data++;
                        sum_err += ek;
                    } else {
                        sum_err += 4.0f;
                    }
                }

                // Require at least 75% of data codewords on this scanline to match cleanly
                if (valid_data * 4 >= cols_cand * 3 && sum_err < best_cluster_score) {
                    best_cluster_score = sum_err;
                    best_cluster = c;
                    memcpy(best_seq, temp_seq, num_cw * sizeof(int));
                    memcpy(best_errs, temp_errs, num_cw * sizeof(float));
                }
            }

            if (best_cluster != -1) {
                int row_num = 3 * (best_seq[0] / 30) + best_cluster;
                out_row->cols      = cols_cand;
                out_row->cluster   = best_cluster;
                out_row->left_ind  = best_seq[0];
                out_row->right_ind = best_seq[num_cw - 1];
                out_row->row_L     = row_num;
                out_row->row_R     = row_num;
                for (int k = 0; k < cols_cand; k++) {
                    out_row->cws[k]  = best_seq[k + 1];
                    out_row->errs[k] = best_errs[k + 1];
                }
                int stop_end_idx = j + 9;
                float x0 = run_x[i], x1 = run_x[stop_end_idx];
                if (dir == -1) {
                    float tmp = (float)(line_len - 1) - x1;
                    x1 = (float)(line_len - 1) - x0;
                    x0 = tmp;
                }
                out_row->x_left  = x0;
                out_row->x_right = x1;
                out_row->y_scan  = y_coord;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BOOL ScanPDF417Line(const int* gray_pixels, int length, float y_coord, PDF417RowScan* out_row) {
    if (length < 60) return FALSE;
    int* smooth = (int*)malloc(length * sizeof(int));
    for (int x = 0; x < length; x++) {
        smooth[x] = (x == 0 || x == length - 1)
            ? gray_pixels[x]
            : (gray_pixels[x - 1] + gray_pixels[x] * 2 + gray_pixels[x + 1]) / 4;
    }

    int* thresh = (int*)malloc(length * sizeof(int));
    int window = length / 5;
    if (window < 24) window = 24;
    if (window > length) window = length;

    long sum = 0;
    for (int x = 0; x < window; x++) sum += smooth[x];
    for (int x = 0; x < length; x++) {
        thresh[x] = (int)(sum / window);
        int left = x - window / 2, right = x + window - window / 2;
        sum += (right < length ? smooth[right] : smooth[length - 1])
             - (left >= 0 ? smooth[left] : smooth[0]);
    }

    float* runs  = (float*)malloc((length + 2) * sizeof(float));
    float* run_x = (float*)malloc((length + 2) * sizeof(float));
    int num_runs = 0, first_color = -1, current_val = -1;
    float last_cross_x = 0.0f;

    for (int x = 0; x < length; x++) {
        float diff_curr = (float)smooth[x] - (float)(thresh[x] - 4);
        int val = (diff_curr < 0.0f) ? 1 : 0;

        if (first_color == -1) first_color = val;
        if (current_val == -1) current_val = val;

        if (val != current_val && x > 0) {
            float diff_prev = (float)smooth[x - 1] - (float)(thresh[x - 1] - 4);
            float denom = diff_prev - diff_curr;
            float frac = (fabsf(denom) > 0.001f) ? (diff_prev / denom) : 0.5f;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;

            float cross_x = (float)(x - 1) + frac;
            run_x[num_runs] = last_cross_x;
            runs[num_runs++] = cross_x - last_cross_x;
            last_cross_x = cross_x;
            current_val = val;
        }
    }
    if (last_cross_x < (float)(length - 1)) {
        run_x[num_runs] = last_cross_x;
        runs[num_runs++] = (float)(length - 1) - last_cross_x;
    }
    run_x[num_runs] = (float)(length - 1);

    BOOL ok = TryExtractPDF417RowFromRuns(runs, run_x, num_runs, first_color, length, 1, y_coord, out_row);
    if (!ok && num_runs >= 33) {
        float* rev_runs = (float*)malloc((num_runs + 2) * sizeof(float));
        float* rev_x    = (float*)malloc((num_runs + 2) * sizeof(float));
        int rev_first   = (first_color + num_runs - 1) % 2;
        float acc_x     = 0.0f;
        for (int k = 0; k < num_runs; k++) {
            rev_x[k] = acc_x;
            rev_runs[k] = runs[num_runs - 1 - k];
            acc_x += rev_runs[k];
        }
        rev_x[num_runs] = acc_x;
        ok = TryExtractPDF417RowFromRuns(rev_runs, rev_x, num_runs, rev_first, length, -1, y_coord, out_row);
        free(rev_runs);
        free(rev_x);
    }

    free(smooth); free(thresh); free(runs); free(run_x);
    return ok;
}

typedef struct {
    int cw[3];
    float err[3];
    int votes[3];
} PDF417CellTracker;

BOOL ScanPDF417Buffer(BYTE* pixels, int w, int h, int bpp, BOOL is_bottom_up, RECT* out_rc) {
    if (w < 60 || h < 15) return FALSE;
    InitPDF417Tables();

    int stride = ((w * bpp / 8) + 3) & ~3;
    int* line_gray = (int*)malloc(w * sizeof(int));

    int coarse_step = h / 120;
    if (coarse_step < 1) coarse_step = 1;
    if (coarse_step > 4) coarse_step = 4;

    int first_hit_y = -1, last_hit_y = -1;
    for (int y = 2; y < h - 2; y += coarse_step) {
        for (int x = 0; x < w; x++) {
            line_gray[x] = SampleBandGray(pixels, w, h, stride, bpp, is_bottom_up, x, y);
        }
        PDF417RowScan row_scan;
        if (ScanPDF417Line(line_gray, w, (float)y, &row_scan)) {
            if (first_hit_y == -1) first_hit_y = y;
            last_hit_y = y;
        }
    }

    if (first_hit_y == -1) {
        free(line_gray);
        return FALSE;
    }

    int y_start = first_hit_y - h / 3;
    int y_end   = last_hit_y  + h / 3;
    if (y_start < 1) y_start = 1;
    if (y_end >= h - 1) y_end = h - 2;

    PDF417CellTracker (*grid)[30] = (PDF417CellTracker (*)[30])calloc(90 * 30, sizeof(PDF417CellTracker));
    for (int r = 0; r < 90; r++) {
        for (int c = 0; c < 30; c++) {
            for (int k = 0; k < 3; k++) {
                grid[r][c].cw[k]  = -1;
                grid[r][c].err[k] = 999.0f;
            }
        }
    }

    int votes_cols[31] = {0};
    int votes_div3[30] = {0};
    int votes_mod3[3]  = {0};
    int votes_ecc[9]   = {0};
    int max_row_seen   = -1;
    float min_x = (float)w, max_x = 0.0f, min_y = (float)h, max_y = 0.0f;

    static const float slopes[5] = { 0.0f, 0.025f, -0.025f, 0.05f, -0.05f };

    for (int y = y_start; y <= y_end; y++) {
        for (int s_idx = 0; s_idx < 5; s_idx++) {
            float slope = slopes[s_idx];
            if (s_idx >= 3 && (y % 2 != 0)) continue;

            for (int band_mode = 0; band_mode < 2; band_mode++) {
                if (band_mode == 1 && s_idx > 0) continue;

                for (int x = 0; x < w; x++) {
                    int cy = (s_idx == 0) ? y : (y + (int)((x - w / 2) * slope));
                    line_gray[x] = (band_mode == 0)
                        ? SampleBandGray(pixels, w, h, stride, bpp, is_bottom_up, x, cy)
                        : SampleSingleGray(pixels, w, h, stride, bpp, is_bottom_up, x, cy);
                }

                PDF417RowScan rs;
                if (!ScanPDF417Line(line_gray, w, (float)y, &rs)) continue;

                int row_idx = rs.row_L;
                if (row_idx < 0 || row_idx >= 90 || rs.row_L != rs.row_R) continue;

                if (rs.cols >= 1 && rs.cols <= 30) votes_cols[rs.cols]++;
                if (row_idx > max_row_seen) max_row_seen = row_idx;

                if (rs.x_left < min_x)  min_x = rs.x_left;
                if (rs.x_right > max_x) max_x = rs.x_right;
                if (rs.y_scan < min_y)  min_y = rs.y_scan;
                if (rs.y_scan > max_y)  max_y = rs.y_scan;

                int remL = rs.left_ind % 30;
                int remR = rs.right_ind % 30;

                if (rs.cluster == 0) {
                    votes_div3[remL]++;
                    votes_cols[remR + 1]++;
                } else if (rs.cluster == 1) {
                    if (remL < 27) {
                        votes_ecc[remL / 3]++;
                        votes_mod3[remL % 3]++;
                    }
                    votes_div3[remR]++;
                } else if (rs.cluster == 2) {
                    votes_cols[remL + 1]++;
                    if (remR < 27) {
                        votes_ecc[remR / 3]++;
                        votes_mod3[remR % 3]++;
                    }
                }

                for (int c = 0; c < rs.cols && c < 30; c++) {
                    int cw = rs.cws[c];
                    float err = rs.errs[c];
                    if (cw < 0 || cw >= 929) continue;

                    PDF417CellTracker* cell = &grid[row_idx][c];
                    int slot = -1;
                    for (int k = 0; k < 3; k++) {
                        if (cell->cw[k] == cw) { slot = k; break; }
                    }
                    if (slot == -1) {
                        for (int k = 0; k < 3; k++) {
                            if (cell->cw[k] == -1) { slot = k; break; }
                        }
                    }
                    if (slot == -1) {
                        int min_v = 999999, worst_k = 0;
                        for (int k = 0; k < 3; k++) {
                            if (cell->votes[k] < min_v) { min_v = cell->votes[k]; worst_k = k; }
                        }
                        if (err < cell->err[worst_k]) {
                            slot = worst_k;
                            cell->votes[slot] = 0;
                            cell->err[slot] = 999.0f;
                        }
                    }
                    if (slot != -1) {
                        cell->cw[slot] = cw;
                        cell->votes[slot]++;
                        if (err < cell->err[slot]) cell->err[slot] = err;
                    }
                }
            }
        }
    }

    free(line_gray);

    int best_cols = -1, max_c_votes = 0;
    for (int c = 1; c <= 30; c++) {
        if (votes_cols[c] > max_c_votes) { max_c_votes = votes_cols[c]; best_cols = c; }
    }
    int best_div3 = -1, max_d_votes = 0;
    for (int d = 0; d < 30; d++) {
        if (votes_div3[d] > max_d_votes) { max_d_votes = votes_div3[d]; best_div3 = d; }
    }
    int best_mod3 = -1, max_m_votes = 0;
    for (int m = 0; m < 3; m++) {
        if (votes_mod3[m] > max_m_votes) { max_m_votes = votes_mod3[m]; best_mod3 = m; }
    }
    int best_ecc = -1, max_e_votes = 0;
    for (int e = 0; e <= 8; e++) {
        if (votes_ecc[e] > max_e_votes) { max_e_votes = votes_ecc[e]; best_ecc = e; }
    }

    // Require strong indicator consensus on cols, rows, and ecc_level
    if (best_cols < 1 || max_c_votes < 3 || best_div3 < 0 || max_d_votes < 2 ||
        best_mod3 < 0 || max_m_votes < 2 || best_ecc < 0 || max_e_votes < 2) {
        free(grid);
        return FALSE;
    }

    int rows = 3 * best_div3 + best_mod3 + 1;
    if (rows < 3 || rows > 90 || max_row_seen >= rows) {
        free(grid);
        return FALSE;
    }

    int ecc_count   = 1 << (best_ecc + 1);
    int total_cw    = rows * best_cols;
    int payload_len = total_cw - ecc_count;
    if (payload_len < 2 || total_cw > 928) {
        free(grid);
        return FALSE;
    }

    int work_buf[1024];
    int erasures[64], num_erasures = 0;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < best_cols; c++) {
            PDF417CellTracker* cell = &grid[r][c];
            int best_k = -1;
            for (int k = 0; k < 3; k++) {
                if (cell->cw[k] == -1) continue;
                if (best_k == -1 ||
                    cell->votes[k] > cell->votes[best_k] ||
                    (cell->votes[k] == cell->votes[best_k] && cell->err[k] < cell->err[best_k])) {
                    best_k = k;
                }
            }
            int idx = r * best_cols + c;
            if (best_k != -1) {
                work_buf[idx] = cell->cw[best_k];
            } else {
                work_buf[idx] = -1;
                if (num_erasures < 64) erasures[num_erasures++] = idx;
            }
        }
    }
    free(grid);

    if (VerifyAndCorrectPDF417RS(work_buf, payload_len, ecc_count, erasures, num_erasures)) {
        char decoded[1024] = {0};
        if (DecodePDF417Stream(work_buf, work_buf[0], decoded, sizeof(decoded))) {
            if (out_rc) {
                out_rc->left   = (int)min_x;
                out_rc->right  = (int)max_x;
                out_rc->top    = (int)min_y - 6;
                out_rc->bottom = (int)max_y + 6;
                if (out_rc->top < 0) out_rc->top = 0;
                if (out_rc->bottom >= h) out_rc->bottom = h - 1;
            }
            SendMessageA(g_hMain, WM_APP_BARCODE_SCANNED, 0, (LPARAM)strdup(decoded));
            return TRUE;
        }
    }

    return FALSE;
}
// ============================================================================
// NATIVE ECC 200 DATAMATRIX DETECTOR & DECODER (10x10..52x52 + 2x2 QUAD MODE)
// ============================================================================

typedef struct {
    int sym_r, sym_c, reg_r, reg_c, data_cw, ecc_cw, blocks;
} DMSymbolInfo;

static const DMSymbolInfo g_dmSymbols[] = {
    {10,10, 1,1,   3,  5, 1}, {12,12, 1,1,   5,  7, 1}, {14,14, 1,1,   8, 10, 1},
    {16,16, 1,1,  12, 12, 1}, {18,18, 1,1,  18, 14, 1}, {20,20, 1,1,  22, 18, 1},
    {22,22, 1,1,  30, 20, 1}, {24,24, 1,1,  36, 24, 1}, {26,26, 1,1,  44, 28, 1},
    {32,32, 2,2,  62, 36, 1}, {36,36, 2,2,  86, 42, 1}, {40,40, 2,2, 114, 48, 1},
    {44,44, 2,2, 144, 56, 1}, {48,48, 2,2, 174, 68, 1}, {52,52, 2,2, 204, 84, 2},
    { 8,18, 1,1,   5,  7, 1}, { 8,32, 1,2,  10, 11, 1}, {12,26, 1,1,  16, 14, 1},
    {12,36, 1,2,  22, 18, 1}, {16,36, 1,2,  32, 24, 1}, {16,48, 1,2,  49, 28, 1}
};
#define DM_NUM_SYMBOLS (sizeof(g_dmSymbols)/sizeof(g_dmSymbols[0]))
static BYTE g_dmExp[512], g_dmLog[256];
static BOOL g_dmGfInit = FALSE;

void InitDataMatrixGF() {
    if (g_dmGfInit) return;
    int v = 1;
    for (int i = 0; i < 255; i++) {
        g_dmExp[i] = g_dmExp[i + 255] = (BYTE)v;
        g_dmLog[v] = (BYTE)i;
        v <<= 1;
        if (v & 0x100) v ^= 0x12D;
    }
    g_dmExp[510] = g_dmExp[0];
    g_dmGfInit = TRUE;
}

static inline BYTE dm_mul(BYTE a, BYTE b) { return (a && b) ? g_dmExp[g_dmLog[a] + g_dmLog[b]] : 0; }
static inline BYTE dm_inv(BYTE a) { return a ? g_dmExp[255 - g_dmLog[a]] : 0; }

BOOL VerifyAndCorrectRS(BYTE* cw, int data_cw, int ecc_cw) {
    InitDataMatrixGF();
    int N = data_cw + ecc_cw;
    BYTE S[128] = {0};
    BOOL has_err = FALSE;

    for (int j = 0; j < ecc_cw; j++) {
        BYTE syn = 0, a_j = g_dmExp[j + 1];
        for (int i = 0; i < N; i++) syn = dm_mul(syn, a_j) ^ cw[i];
        S[j] = syn;
        if (syn != 0) has_err = TRUE;
    }
    if (!has_err) return TRUE;

    BYTE C[128] = {1}, B[128] = {1};
    int L = 0, m = 1;
    BYTE b = 1;
    for (int n = 0; n < ecc_cw; n++) {
        BYTE d = S[n];
        for (int i = 1; i <= L; i++) d ^= dm_mul(C[i], S[n - i]);
        if (d == 0) {
            m++;
        } else if (2 * L <= n) {
            BYTE T[128]; memcpy(T, C, 128);
            BYTE coef = dm_mul(d, dm_inv(b));
            for (int i = m; i < 128; i++) C[i] ^= dm_mul(coef, B[i - m]);
            L = n + 1 - L;
            memcpy(B, T, 128); b = d; m = 1;
        } else {
            BYTE coef = dm_mul(d, dm_inv(b));
            for (int i = m; i < 128; i++) C[i] ^= dm_mul(coef, B[i - m]);
            m++;
        }
    }
    if (L > ecc_cw / 2 || L > 16) return FALSE;

    int err_pos[32], err_cnt = 0;
    for (int i = 0; i < N; i++) {
        int p = N - 1 - i;
        BYTE x_inv = g_dmExp[(255 - (p % 255)) % 255], eval = 0, x_pow = 1;
        for (int j = 0; j <= L; j++) { eval ^= dm_mul(C[j], x_pow); x_pow = dm_mul(x_inv, x_pow); }
        if (eval == 0) err_pos[err_cnt++] = i;
    }
    if (err_cnt != L) return FALSE;

    BYTE Omega[128] = {0};
    for (int i = 0; i < ecc_cw; i++)
        for (int j = 0; j <= L && i + j < ecc_cw; j++)
            Omega[i + j] ^= dm_mul(S[i], C[j]);

    for (int e = 0; e < err_cnt; e++) {
        int idx = err_pos[e], p = N - 1 - idx;
        BYTE x_inv = g_dmExp[(255 - (p % 255)) % 255], num = 0, x_pow = 1;
        for (int j = 0; j < ecc_cw; j++) { num ^= dm_mul(Omega[j], x_pow); x_pow = dm_mul(x_pow, x_inv); }
        BYTE den = 0, x_inv2 = dm_mul(x_inv, x_inv);
        x_pow = 1;
        for (int j = 1; j <= L; j += 2) { den ^= dm_mul(C[j], x_pow); x_pow = dm_mul(x_pow, x_inv2); }
        if (den == 0) return FALSE;
        cw[idx] ^= dm_mul(num, dm_inv(den));
    }

    for (int j = 0; j < ecc_cw; j++) {
        BYTE syn = 0, a_j = g_dmExp[j + 1];
        for (int i = 0; i < N; i++) syn = dm_mul(syn, a_j) ^ cw[i];
        if (syn != 0) return FALSE;
    }
    return TRUE;
}

static BOOL VerifyAndCorrectDMBlocks(BYTE* cw, int data_cw, int ecc_cw, int blocks) {
    if (blocks <= 1) return VerifyAndCorrectRS(cw, data_cw, ecc_cw);

    int d_per_blk = data_cw / blocks;
    int e_per_blk = ecc_cw / blocks;
    BYTE blk[256];

    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < d_per_blk; i++) blk[i] = cw[i * blocks + b];
        for (int i = 0; i < e_per_blk; i++) blk[d_per_blk + i] = cw[data_cw + i * blocks + b];
        if (!VerifyAndCorrectRS(blk, d_per_blk, e_per_blk)) return FALSE;
        for (int i = 0; i < d_per_blk; i++) cw[i * blocks + b] = blk[i];
        for (int i = 0; i < e_per_blk; i++) cw[data_cw + i * blocks + b] = blk[d_per_blk + i];
    }
    return TRUE;
}

static void dm_place_bit(int* map, int r, int c, int nrow, int ncol, int cw_idx, int bit_shift, int wrap_off) {
    if (r < 0) { r += nrow; c += 4 - ((nrow + wrap_off) % 8); }
    if (c < 0) { c += ncol; r += 4 - ((ncol + wrap_off) % 8); }
    if (r >= 0 && r < nrow && c >= 0 && c < ncol) map[r * ncol + c] = (cw_idx << 3) | bit_shift;
}

static void dm_utah(int* map, int r, int c, int nrow, int ncol, int cw, int wrap_off) {
    dm_place_bit(map, r-2, c-2, nrow, ncol, cw, 7, wrap_off);
    dm_place_bit(map, r-2, c-1, nrow, ncol, cw, 6, wrap_off);
    dm_place_bit(map, r-1, c-2, nrow, ncol, cw, 5, wrap_off);
    dm_place_bit(map, r-1, c-1, nrow, ncol, cw, 4, wrap_off);
    dm_place_bit(map, r-1, c,   nrow, ncol, cw, 3, wrap_off);
    dm_place_bit(map, r,   c-2, nrow, ncol, cw, 2, wrap_off);
    dm_place_bit(map, r,   c-1, nrow, ncol, cw, 1, wrap_off);
    dm_place_bit(map, r,   c,   nrow, ncol, cw, 0, wrap_off);
}

static void BuildUtahPlacementMap(int* map, int nrow, int ncol, int wrap_off) {
    for (int i = 0; i < nrow * ncol; i++) map[i] = -1;
    int cw = 0, r = 4, c = 0;
    while (r < nrow || c < ncol) {
        if (r == nrow && c == 0) {
            dm_place_bit(map, nrow-1, 0, nrow, ncol, cw, 7, wrap_off);
            dm_place_bit(map, nrow-1, 1, nrow, ncol, cw, 6, wrap_off);
            dm_place_bit(map, nrow-1, 2, nrow, ncol, cw, 5, wrap_off);
            dm_place_bit(map, 0, ncol-2, nrow, ncol, cw, 4, wrap_off);
            dm_place_bit(map, 0, ncol-1, nrow, ncol, cw, 3, wrap_off);
            dm_place_bit(map, 1, ncol-1, nrow, ncol, cw, 2, wrap_off);
            dm_place_bit(map, 2, ncol-1, nrow, ncol, cw, 1, wrap_off);
            dm_place_bit(map, 3, ncol-1, nrow, ncol, cw, 0, wrap_off);
            cw++; r -= 2; c += 2; continue;
        }
        if (r == nrow - 2 && c == 0 && ((ncol % 4) != 0)) {
            dm_place_bit(map, nrow-3, 0, nrow, ncol, cw, 7, wrap_off);
            dm_place_bit(map, nrow-2, 0, nrow, ncol, cw, 6, wrap_off);
            dm_place_bit(map, nrow-1, 0, nrow, ncol, cw, 5, wrap_off);
            dm_place_bit(map, 0, ncol-4, nrow, ncol, cw, 4, wrap_off);
            dm_place_bit(map, 0, ncol-3, nrow, ncol, cw, 3, wrap_off);
            dm_place_bit(map, 0, ncol-2, nrow, ncol, cw, 2, wrap_off);
            dm_place_bit(map, 0, ncol-1, nrow, ncol, cw, 1, wrap_off);
            dm_place_bit(map, 1, ncol-1, nrow, ncol, cw, 0, wrap_off);
            cw++; r -= 2; c += 2; continue;
        }
        if (r == nrow - 2 && c == 0 && ((ncol % 8) == 4)) {
            dm_place_bit(map, nrow-3, 0, nrow, ncol, cw, 7, wrap_off);
            dm_place_bit(map, nrow-2, 0, nrow, ncol, cw, 6, wrap_off);
            dm_place_bit(map, nrow-1, 0, nrow, ncol, cw, 5, wrap_off);
            dm_place_bit(map, 0, ncol-2, nrow, ncol, cw, 4, wrap_off);
            dm_place_bit(map, 0, ncol-1, nrow, ncol, cw, 3, wrap_off);
            dm_place_bit(map, 1, ncol-1, nrow, ncol, cw, 2, wrap_off);
            dm_place_bit(map, 2, ncol-1, nrow, ncol, cw, 1, wrap_off);
            dm_place_bit(map, 3, ncol-1, nrow, ncol, cw, 0, wrap_off);
            cw++; r -= 2; c += 2; continue;
        }
        if (r == nrow + 4 && c == 2 && ((ncol % 8) == 0)) {
            dm_place_bit(map, nrow-1, 0, nrow, ncol, cw, 7, wrap_off);
            dm_place_bit(map, nrow-1, ncol-1, nrow, ncol, cw, 6, wrap_off);
            dm_place_bit(map, 0, ncol-3, nrow, ncol, cw, 5, wrap_off);
            dm_place_bit(map, 0, ncol-2, nrow, ncol, cw, 4, wrap_off);
            dm_place_bit(map, 0, ncol-1, nrow, ncol, cw, 3, wrap_off);
            dm_place_bit(map, 1, ncol-3, nrow, ncol, cw, 2, wrap_off);
            dm_place_bit(map, 1, ncol-2, nrow, ncol, cw, 1, wrap_off);
            dm_place_bit(map, 1, ncol-1, nrow, ncol, cw, 0, wrap_off);
            cw++; r -= 2; c += 2; continue;
        }
        do {
            if (r >= 0 && r < nrow && c >= 0 && c < ncol && map[r * ncol + c] == -1)
                dm_utah(map, r, c, nrow, ncol, cw++, wrap_off);
            r -= 2; c += 2;
        } while (r >= 0 && c < ncol);
        r += 1; c += 3;
        do {
            if (r >= 0 && r < nrow && c >= 0 && c < ncol && map[r * ncol + c] == -1)
                dm_utah(map, r, c, nrow, ncol, cw++, wrap_off);
            r += 2; c -= 2;
        } while (r < nrow && c >= 0);
        r += 3; c += 1;
    }

    if (map[nrow * ncol - 1] == -1) {
        map[nrow * ncol - 1] = -2;
        map[nrow * ncol - ncol - 2] = -2;
    }
}

static void DecodeC40Text(const BYTE* cw, int* idx, int data_cw, BOOL is_text, char* out, int* out_len) {
    int shift = 0;
    while (*idx < data_cw && *out_len < 120) {
        int b1 = cw[(*idx)++];
        if (b1 == 254 || *idx >= data_cw) break;
        int b2 = cw[(*idx)++];
        int v = (b1 << 8) + b2 - 1;
        int c[3] = { v / 1600, (v / 40) % 40, v % 40 };
        for (int i = 0; i < 3 && *out_len < 120; i++) {
            int val = c[i];
            if (shift == 0) {
                if (val <= 2) shift = val + 1;
                else if (val == 3) out[(*out_len)++] = ' ';
                else if (val <= 13) out[(*out_len)++] = '0' + (val - 4);
                else if (val <= 39) out[(*out_len)++] = (is_text ? 'a' : 'A') + (val - 14);
            } else if (shift == 1) {
                out[(*out_len)++] = (char)val; shift = 0;
            } else if (shift == 2) {
                static const char s2[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_";
                if (val < 27) out[(*out_len)++] = s2[val];
                shift = 0;
            } else if (shift == 3) {
                if (val == 0) out[(*out_len)++] = '`';
                else if (val <= 26) out[(*out_len)++] = (is_text ? 'A' : 'a') + (val - 1);
                shift = 0;
            }
        }
    }
}

static BOOL DecodeDataMatrixStream(const BYTE* cw, int data_cw, char* out_str, int max_out, BOOL allow_empty) {
    if (allow_empty && cw[0] == 129) {
        out_str[0] = '\0';
        return TRUE;
    }
    int i = 0, len = 0, upper = 0;
    while (i < data_cw && len < max_out - 1) {
        BYTE b = cw[i++];
        if (b == 129) break;
        else if (b >= 1 && b <= 128) {
            char ch = (char)(b - 1 + (upper ? 128 : 0));
            upper = 0;
            if (ch >= 32 && ch <= 126) out_str[len++] = ch;
        } else if (b >= 130 && b <= 229) {
            int pair = b - 130;
            if (len + 2 < max_out) {
                out_str[len++] = '0' + (pair / 10);
                out_str[len++] = '0' + (pair % 10);
            }
        } else if (b == 230) DecodeC40Text(cw, &i, data_cw, FALSE, out_str, &len);
        else if (b == 239) DecodeC40Text(cw, &i, data_cw, TRUE, out_str, &len);
        else if (b == 235) upper = 1;
        else if (b == 231 && i < data_cw) {
            int d1 = cw[i] - (((149 * (i + 1)) % 255) + 1);
            if (d1 < 0) d1 += 256;
            i++;
            int count = (d1 == 0) ? (data_cw - i) : d1;
            for (int k = 0; k < count && i < data_cw && len < max_out - 1; k++, i++) {
                int ch = cw[i] - (((149 * (i + 1)) % 255) + 1);
                if (ch < 0) ch += 256;
                if (ch >= 32 && ch <= 126) out_str[len++] = (char)ch;
            }
        }
    }
    out_str[len] = '\0';
    return (len > 0);
}

static inline int SampleModuleBit(const BYTE* bin, int w, int h, float fx, float fy, float mw, float mh) {
    int x0 = (int)(fx - mw * 0.25f), x1 = (int)(fx + mw * 0.25f);
    int y0 = (int)(fy - mh * 0.25f), y1 = (int)(fy + mh * 0.25f);
    if (x0 < 0) x0 = 0; if (x1 >= w) x1 = w - 1;
    if (y0 < 0) y0 = 0; if (y1 >= h) y1 = h - 1;
    int dark = 0, total = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) { dark += bin[y * w + x]; total++; }
    return (total > 0) ? ((dark * 2 >= total) ? 1 : 0) : bin[((int)fy) * w + ((int)fx)];
}

static BOOL TightenBinaryBox(const BYTE* bin, int w, int h, RECT in_rc, RECT* out_rc) {
    int min_x = w, min_y = h, max_x = -1, max_y = -1;
    int x0 = (in_rc.left < 0) ? 0 : in_rc.left;
    int y0 = (in_rc.top < 0) ? 0 : in_rc.top;
    int x1 = (in_rc.right >= w) ? (w - 1) : in_rc.right;
    int y1 = (in_rc.bottom >= h) ? (h - 1) : in_rc.bottom;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (bin[y * w + x]) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }
    if (max_x - min_x < 8 || max_y - min_y < 8) return FALSE;
    out_rc->left = min_x; out_rc->top = min_y;
    out_rc->right = max_x; out_rc->bottom = max_y;
    return TRUE;
}

static BOOL TryDecodeCandidateBoxEx(const BYTE* bin, int w, int h, RECT box, char* out_code, int max_out, BOOL allow_empty) {
    int bw = box.right - box.left + 1, bh = box.bottom - box.top + 1;
    if (bw < 10 || bh < 10) return FALSE;

    int* place_map = (int*)malloc(64 * 64 * sizeof(int));
    BYTE* grid     = (BYTE*)malloc(64 * 64);

    for (int s = 0; s < (int)DM_NUM_SYMBOLS; s++) {
        int sr_cnt = g_dmSymbols[s].sym_r, sc_cnt = g_dmSymbols[s].sym_c;
        float aspect_box = (float)bw / (float)bh;

        for (int rot = 0; rot < 4; rot++) {
            int phys_r = (rot % 2 == 0) ? sr_cnt : sc_cnt;
            int phys_c = (rot % 2 == 0) ? sc_cnt : sr_cnt;
            float aspect_sym = (float)phys_c / (float)phys_r;
            if (aspect_box < aspect_sym * 0.65f || aspect_box > aspect_sym * 1.45f) continue;

            float mw = (float)bw / (float)phys_c, mh = (float)bh / (float)phys_r;
            if (mw < 1.2f || mh < 1.2f) continue;

            for (int r = 0; r < sr_cnt; r++) {
                for (int c = 0; c < sc_cnt; c++) {
                    int pr = r, pc = c;
                    if (rot == 1)      { pr = c; pc = sr_cnt - 1 - r; }
                    else if (rot == 2) { pr = sr_cnt - 1 - r; pc = sc_cnt - 1 - c; }
                    else if (rot == 3) { pr = sc_cnt - 1 - c; pc = r; }

                    float fx = box.left + (pc + 0.5f) * mw;
                    float fy = box.top  + (pr + 0.5f) * mh;
                    grid[r * sc_cnt + c] = (BYTE)SampleModuleBit(bin, w, h, fx, fy, mw, mh);
                }
            }

            int border_ok = 0, border_tot = 2 * (sr_cnt + sc_cnt);
            for (int c = 0; c < sc_cnt; c++) {
                if (grid[(sr_cnt - 1) * sc_cnt + c] == 1) border_ok++;
                if (grid[0 * sc_cnt + c] == ((c % 2 == 0) ? 1 : 0)) border_ok++;
            }
            for (int r = 0; r < sr_cnt; r++) {
                if (grid[r * sc_cnt + 0] == 1) border_ok++;
                if (grid[r * sc_cnt + (sc_cnt - 1)] == ((r % 2 == 1) ? 1 : 0)) border_ok++;
            }
            if (border_ok * 100 < border_tot * 78) continue;

            int reg_r = g_dmSymbols[s].reg_r, reg_c = g_dmSymbols[s].reg_c;
            int nrow = sr_cnt - 2 * reg_r, ncol = sc_cnt - 2 * reg_c;
            int sub_r = nrow / reg_r, sub_c = ncol / reg_c;

            static const int wrap_offsets[2] = { 3, 4 };
            for (int w_idx = 0; w_idx < 2; w_idx++) {
                BuildUtahPlacementMap(place_map, nrow, ncol, wrap_offsets[w_idx]);
                BYTE cw[512] = {0};

                for (int dr = 0; dr < nrow; dr++) {
                    int sr = 1 + dr + 2 * (dr / sub_r);
                    for (int dc = 0; dc < ncol; dc++) {
                        int sc = 1 + dc + 2 * (dc / sub_c);
                        int val = place_map[dr * ncol + dc];
                        if (val >= 0 && grid[sr * sc_cnt + sc]) cw[val >> 3] |= (1 << (val & 7));
                    }
                }

                if (VerifyAndCorrectDMBlocks(cw, g_dmSymbols[s].data_cw, g_dmSymbols[s].ecc_cw, g_dmSymbols[s].blocks)) {
                    if (DecodeDataMatrixStream(cw, g_dmSymbols[s].data_cw, out_code, max_out, allow_empty)) {
                        free(place_map); free(grid);
                        return TRUE;
                    }
                }
            }
        }
    }
    free(place_map); free(grid);
    return FALSE;
}

static BOOL TryDecodeCandidateBox(const BYTE* bin, int w, int h, RECT box, char* out_code) {
    return TryDecodeCandidateBoxEx(bin, w, h, box, out_code, 1024, FALSE);
}

static BOOL TryDecodeQuadBox(const BYTE* bin, int w, int h, RECT outer, char* out_code, int max_out) {
    int bw = outer.right - outer.left + 1;
    int bh = outer.bottom - outer.top + 1;
    if (bw < 22 || bh < 22) return FALSE;
    float aspect = (float)bw / (float)bh;
    if (aspect < 0.75f || aspect > 1.33f) return FALSE;

    int mid_x = (outer.left + outer.right) / 2;
    int mid_y = (outer.top + outer.bottom) / 2;
    int search_x = bw / 8; if (search_x < 2) search_x = 2;
    int search_y = bh / 8; if (search_y < 2) search_y = 2;

    int best_vx = mid_x, min_v_sum = 999999;
    for (int x = mid_x - search_x; x <= mid_x + search_x; x++) {
        int sum = 0;
        for (int y = outer.top; y <= outer.bottom; y++) sum += bin[y * w + x];
        if (sum < min_v_sum || (sum == min_v_sum && abs(x - mid_x) < abs(best_vx - mid_x))) {
            min_v_sum = sum;
            best_vx = x;
        }
    }

    int best_hy = mid_y, min_h_sum = 999999;
    for (int y = mid_y - search_y; y <= mid_y + search_y; y++) {
        int sum = 0;
        for (int x = outer.left; x <= outer.right; x++) sum += bin[y * w + x];
        if (sum < min_h_sum || (sum == min_h_sum && abs(y - mid_y) < abs(best_hy - mid_y))) {
            min_h_sum = sum;
            best_hy = y;
        }
    }

    if (min_v_sum > bh / 6 || min_h_sum > bw / 6) return FALSE;

    RECT q_raw[4] = {
        { outer.left,   outer.top,    best_vx - 1, best_hy - 1 },
        { best_vx + 1,  outer.top,    outer.right, best_hy - 1 },
        { outer.left,   best_hy + 1,  best_vx - 1, outer.bottom },
        { best_vx + 1,  best_hy + 1,  outer.right, outer.bottom }
    };

    out_code[0] = '\0';
    int total_len = 0;
    for (int q = 0; q < 4; q++) {
        RECT q_tight;
        if (!TightenBinaryBox(bin, w, h, q_raw[q], &q_tight)) return FALSE;
        char chunk[256] = {0};
        if (!TryDecodeCandidateBoxEx(bin, w, h, q_tight, chunk, sizeof(chunk), (q > 0))) return FALSE;
        int clen = (int)strlen(chunk);
        if (total_len + clen < max_out - 1) {
            memcpy(out_code + total_len, chunk, clen + 1);
            total_len += clen;
        }
    }
    return (total_len > 0);
}

BOOL ScanDataMatrixBuffer(BYTE* pixels, int w, int h, int bpp, BOOL is_bottom_up, RECT* out_rc) {
    int scale = (w > 960) ? (w / 720) : 1;
    if (scale < 1) scale = 1;
    int sw = w / scale, sh = h / scale;
    int stride = ((w * bpp / 8) + 3) & ~3;

    BYTE* gray = (BYTE*)malloc(sw * sh);
    int* integ = (int*)calloc((sw + 1) * (sh + 1), sizeof(int));
    BYTE* bin  = (BYTE*)calloc(sw * sh, 1);
    BYTE* vis  = (BYTE*)calloc(sw * sh, 1);

    int min_g = 255, max_g = 0;
    for (int y = 0; y < sh; y++) {
        int row_sum = 0, src_y = is_bottom_up ? (h - 1 - y * scale) : (y * scale);
        BYTE* p_row = pixels + src_y * stride;
        for (int x = 0; x < sw; x++) {
            BYTE* p = p_row + (x * scale) * (bpp / 8);
            int g = (bpp >= 24) ? ((p[2] + p[1] + p[0]) / 3) : p[0];
            gray[y * sw + x] = (BYTE)g;
            if (g < min_g) min_g = g;
            if (g > max_g) max_g = g;
            row_sum += g;
            integ[(y + 1) * (sw + 1) + (x + 1)] = integ[y * (sw + 1) + (x + 1)] + row_sum;
        }
    }

    int global_mid = (min_g + max_g) / 2;
    int rad = (sw < sh ? sw : sh) / 6; if (rad < 16) rad = 16;
    int g_min_x = sw, g_min_y = sh, g_max_x = -1, g_max_y = -1;
    for (int y = 0; y < sh; y++) {
        int y0 = (y - rad < 0) ? 0 : y - rad, y1 = (y + rad >= sh) ? sh - 1 : y + rad;
        for (int x = 0; x < sw; x++) {
            int x0 = (x - rad < 0) ? 0 : x - rad, x1 = (x + rad >= sw) ? sw - 1 : x + rad;
            int area = (x1 - x0 + 1) * (y1 - y0 + 1);
            int sum = integ[(y1+1)*(sw+1)+(x1+1)] - integ[y0*(sw+1)+(x1+1)] - integ[(y1+1)*(sw+1)+x0] + integ[y0*(sw+1)+x0];
            int local_mean = sum / area;
            int thr = (local_mean + global_mid) / 2 - 6;
            if (gray[y * sw + x] < thr) {
                bin[y * sw + x] = 1;
                if (x < g_min_x) g_min_x = x; if (x > g_max_x) g_max_x = x;
                if (y < g_min_y) g_min_y = y; if (y > g_max_y) g_max_y = y;
            }
        }
    }

    char decoded[1024] = {0};

    if (g_max_x > g_min_x + 20 && g_max_y > g_min_y + 20) {
        RECT g_rc = { g_min_x, g_min_y, g_max_x, g_max_y };
        if (TryDecodeQuadBox(bin, sw, sh, g_rc, decoded, sizeof(decoded))) {
            if (out_rc) {
                out_rc->left   = g_rc.left   * scale;
                out_rc->top    = g_rc.top    * scale;
                out_rc->right  = g_rc.right  * scale;
                out_rc->bottom = g_rc.bottom * scale;
            }
            SendMessageA(g_hMain, WM_APP_BARCODE_SCANNED, 0, (LPARAM)strdup(decoded));
            free(gray); free(integ); free(bin); free(vis);
            return TRUE;
        }
    }

    RECT cands[32]; int num_cands = 0;
    if (g_max_x > g_min_x + 8 && g_max_y > g_min_y + 8) {
        cands[num_cands++] = (RECT){ g_min_x, g_min_y, g_max_x, g_max_y };
    }

    Point2D* q = (Point2D*)malloc(sw * sh * sizeof(Point2D));
    for (int y = 1; y < sh - 1; y++) {
        for (int x = 1; x < sw - 1; x++) {
            if (bin[y * sw + x] && !vis[y * sw + x]) {
                int head = 0, tail = 0;
                q[tail++] = (Point2D){(short)x, (short)y};
                vis[y * sw + x] = 1;
                int min_x = x, max_x = x, min_y = y, max_y = y;
                while (head < tail) {
                    Point2D pt = q[head++];
                    if (pt.x < min_x) min_x = pt.x; if (pt.x > max_x) max_x = pt.x;
                    if (pt.y < min_y) min_y = pt.y; if (pt.y > max_y) max_y = pt.y;
                    for (int dy = -2; dy <= 2; dy++) {
                        for (int dx = -2; dx <= 2; dx++) {
                            int nx = pt.x + dx, ny = pt.y + dy;
                            if (nx >= 0 && nx < sw && ny >= 0 && ny < sh && bin[ny * sw + nx] && !vis[ny * sw + nx]) {
                                vis[ny * sw + nx] = 1;
                                q[tail++] = (Point2D){(short)nx, (short)ny};
                            }
                        }
                    }
                }
                int bw = max_x - min_x + 1, bh = max_y - min_y + 1;
                if (bw >= 10 && bh >= 10 && tail >= 25) {
                    if (num_cands < 32) {
                        cands[num_cands++] = (RECT){ min_x, min_y, max_x, max_y };
                    } else {
                        int min_idx = 1, min_area = 9999999;
                        for (int k = 1; k < 32; k++) {
                            int a = (cands[k].right - cands[k].left) * (cands[k].bottom - cands[k].top);
                            if (a < min_area) { min_area = a; min_idx = k; }
                        }
                        if (bw * bh > min_area) cands[min_idx] = (RECT){ min_x, min_y, max_x, max_y };
                    }
                }
            }
        }
    }
    free(q);

    BOOL matched = FALSE;
    for (int i = 0; i < num_cands; i++) {
        if (TryDecodeQuadBox(bin, sw, sh, cands[i], decoded, sizeof(decoded)) ||
            TryDecodeCandidateBox(bin, sw, sh, cands[i], decoded)) {
            if (out_rc) {
                out_rc->left   = cands[i].left   * scale;
                out_rc->top    = cands[i].top    * scale;
                out_rc->right  = cands[i].right  * scale;
                out_rc->bottom = cands[i].bottom * scale;
            }
            SendMessageA(g_hMain, WM_APP_BARCODE_SCANNED, 0, (LPARAM)strdup(decoded));
            matched = TRUE;
            break;
        }
    }

    free(gray); free(integ); free(bin); free(vis);
    return matched;
}

// ============================================================================
// WINDOW WILDCARD MATCHER, PICKER & KEYBOARD INJECTOR
// ============================================================================

static BOOL WildcardMatchI(const char* pat, const char* str) {
    const char *s = str, *p = pat, *star = NULL, *ss = NULL;
    while (*s) {
        if (*p == '*') {
            star = p++;
            ss = s;
        } else if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*s)) {
            p++;
            s++;
        } else if (star) {
            p = star + 1;
            s = ++ss;
        } else {
            return FALSE;
        }
    }
    while (*p == '*') p++;
    return (*p == '\0');
}

typedef struct {
    const char* pattern;
    HWND hFound;
    BOOL requireVisible;
} FindWndCtx;

static BOOL CALLBACK EnumWndMatchProc(HWND hwnd, LPARAM lParam) {
    FindWndCtx* ctx = (FindWndCtx*)lParam;
    if (hwnd == g_hMain) return TRUE;
    if (ctx->requireVisible && !IsWindowVisible(hwnd)) return TRUE;

    char title[512] = {0};
    if (GetWindowTextA(hwnd, title, sizeof(title) - 1) > 0) {
        if (WildcardMatchI(ctx->pattern, title)) {
            ctx->hFound = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND FindWindowByPattern(const char* pattern) {
    if (!pattern || !pattern[0]) return NULL;
    FindWndCtx ctx = { pattern, NULL, TRUE };
    EnumWindows(EnumWndMatchProc, (LPARAM)&ctx);
    if (ctx.hFound) return ctx.hFound;

    ctx.requireVisible = FALSE;
    EnumWindows(EnumWndMatchProc, (LPARAM)&ctx);
    return ctx.hFound;
}

// ============================================================================
// UNIFIED HIGH-RES BUFFER SWEEPER (1D CONSENSUS + PDF417 + DATAMATRIX)
// ============================================================================

typedef struct {
    char code[16];
    int votes;
    float best_err;
    RECT rc;
} ScanCandidate;

BOOL ScanHighResBuffer(BYTE* pixels, int w, int h, int bpp, BOOL is_bottom_up, RECT* out_rc) {
    int stride = ((w * bpp / 8) + 3) & ~3;
    int step = h / 32; if (step < 1) step = 1;
    int* line_gray = (int*)malloc(w * sizeof(int));

    ScanCandidate cands[16] = {0};
    int num_cands = 0;
    float slopes[3] = { 0.0f, 0.176f, -0.176f };

    for (int y = step; y < h - step; y += step) {
        for (int s_idx = 0; s_idx < 3; s_idx++) {
            float slope = slopes[s_idx];
            for (int x = 0; x < w; x++) {
                int cy = y + (int)((x - w/2) * slope);
                line_gray[x] = (cy >= 0 && cy < h) ? SampleBandGray(pixels, w, h, stride, bpp, is_bottom_up, x, cy) : 255;
            }

            char code[16] = {0};
            float err = 999.0f;
            RECT rc = {0};

            if (ScanLineRobust(line_gray, w, y, code, &err, &rc)) {
                int idx = -1;
                for (int c = 0; c < num_cands; c++) {
                    if (strcmp(cands[c].code, code) == 0) { idx = c; break; }
                }
                if (idx == -1 && num_cands < 16) {
                    idx = num_cands++;
                    strcpy(cands[idx].code, code);
                    cands[idx].votes = 0;
                    cands[idx].best_err = 999.0f;
                }
                if (idx != -1) {
                    cands[idx].votes++;
                    if (err < cands[idx].best_err) {
                        cands[idx].best_err = err;
                        cands[idx].rc = rc;
                    }
                }
            }
        }
    }
    free(line_gray);

    int best_idx = -1;
    for (int c = 0; c < num_cands; c++) {
        if (cands[c].votes >= 2 || cands[c].best_err < 11.0f) {
            if (best_idx == -1 || cands[c].votes > cands[best_idx].votes ||
               (cands[c].votes == cands[best_idx].votes && cands[c].best_err < cands[best_idx].best_err)) {
                best_idx = c;
            }
        }
    }

    if (best_idx != -1) {
        if (out_rc) *out_rc = cands[best_idx].rc;
        SendMessageA(g_hMain, WM_APP_BARCODE_SCANNED, 0, (LPARAM)strdup(cands[best_idx].code));
        return TRUE;
    }

    if (ScanPDF417Buffer(pixels, w, h, bpp, is_bottom_up, out_rc)) {
        return TRUE;
    }

    return ScanDataMatrixBuffer(pixels, w, h, bpp, is_bottom_up, out_rc);
}

void ProcessFrameTimer() {
    if (!g_bScanning || (!g_bHidden && (IsIconic(g_hMain) || !IsWindowVisible(g_hMain)))) return;

    if ((!g_hTargetWnd || !IsWindow(g_hTargetWnd)) && g_szWindowPattern[0] != '\0') {
        HWND hFound = FindWindowByPattern(g_szWindowPattern);
        if (hFound) g_hTargetWnd = hFound;
    }

    BOOL matched = FALSE; RECT rc = {0};

    if (g_bShowImage && g_hLoadedImg && g_pStaticPixels) {
        if (strlen(g_szLastScan) > 0) return;
        matched = ScanHighResBuffer(g_pStaticPixels, g_imgW, g_imgH, 32, FALSE, &rc);
        if (matched && !g_bHidden) {
            float s = (320.0f / g_imgW < 240.0f / g_imgH) ? 320.0f / g_imgW : 240.0f / g_imgH;
            int nW = (int)(g_imgW * s), nH = (int)(g_imgH * s), oX = (320 - nW) / 2, oY = (240 - nH) / 2;
            rc.left = oX + (int)(rc.left * s); rc.right = oX + (int)(rc.right * s);
            rc.top = oY + (int)(rc.top * s); rc.bottom = oY + (int)(rc.bottom * s);
            HDC hdc = GetDC(g_hImgView); HPEN hG = CreatePen(PS_SOLID, 4, RGB(0,255,0)), hO = SelectObject(hdc, hG);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(hdc, hO); DeleteObject(hG); ReleaseDC(g_hImgView, hdc);
        }
    } else if (!g_bShowImage && g_bUseVFW && g_hCapWnd) {
        capGrabFrameNoStop(g_hCapWnd);
        int img_w = g_vfwBmi.bmiHeader.biWidth;
        int img_h = abs(g_vfwBmi.bmiHeader.biHeight);
        if (g_pVfwPixels && img_w > 0 && img_h > 0) {
            matched = ScanHighResBuffer(g_pVfwPixels, img_w, img_h, g_vfwBpp, g_vfwBottomUp, &rc);
            if (matched && !g_bHidden) {
                float s = (320.0f / img_w < 240.0f / img_h) ? 320.0f / img_w : 240.0f / img_h;
                int nW = (int)(img_w * s), nH = (int)(img_h * s), oX = (320 - nW) / 2, oY = (240 - nH) / 2;
                rc.left = oX + (int)(rc.left * s); rc.right = oX + (int)(rc.right * s);
                rc.top = oY + (int)(rc.top * s); rc.bottom = oY + (int)(rc.bottom * s);
                HDC hdc = GetDC(g_hVidView); HPEN hG = CreatePen(PS_SOLID, 4, RGB(0,255,0)), hO = SelectObject(hdc, hG);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, hO); DeleteObject(hG); ReleaseDC(g_hVidView, hdc);
            }
        }
        if (!matched && !g_bHidden) {
            HDC hdc = GetDC(g_hVidView); HPEN hR = CreatePen(PS_SOLID, 4, RGB(255,0,0)), hO = SelectObject(hdc, hR);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 2, 2, 318, 238);
            SelectObject(hdc, hO); DeleteObject(hR); ReleaseDC(g_hVidView, hdc);
        }
    } else if (!g_bShowImage && g_pGrabber) {
        long size = 0;
        if (SUCCEEDED(g_pGrabber->lpVtbl->GetCurrentBuffer(g_pGrabber, &size, NULL)) && size > 0) {
            BYTE* pBuf = (BYTE*)malloc(size);
            if (SUCCEEDED(g_pGrabber->lpVtbl->GetCurrentBuffer(g_pGrabber, &size, (long*)pBuf))) {
                AM_MEDIA_TYPE mt = {0};
                if (SUCCEEDED(g_pGrabber->lpVtbl->GetConnectedMediaType(g_pGrabber, &mt))) {
                    VIDEOINFOHEADER *pVih = (VIDEOINFOHEADER*)mt.pbFormat;
                    int img_w = pVih->bmiHeader.biWidth, img_h = pVih->bmiHeader.biHeight, is_btm = TRUE;
                    if (img_h < 0) { img_h = -img_h; is_btm = FALSE; }
                    matched = ScanHighResBuffer(pBuf, img_w, img_h, pVih->bmiHeader.biBitCount, is_btm, &rc);
                    if (matched && !g_bHidden) {
                        float s = (320.0f / img_w < 240.0f / img_h) ? 320.0f / img_w : 240.0f / img_h;
                        int nW = (int)(img_w * s), nH = (int)(img_h * s), oX = (320 - nW) / 2, oY = (240 - nH) / 2;
                        rc.left = oX + (int)(rc.left * s); rc.right = oX + (int)(rc.right * s);
                        rc.top = oY + (int)(rc.top * s); rc.bottom = oY + (int)(rc.bottom * s);
                        HDC hdc = GetDC(g_hVidView); HPEN hG = CreatePen(PS_SOLID, 4, RGB(0,255,0)), hO = SelectObject(hdc, hG);
                        SelectObject(hdc, GetStockObject(NULL_BRUSH));
                        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                        SelectObject(hdc, hO); DeleteObject(hG); ReleaseDC(g_hVidView, hdc);
                    }
                    if (mt.cbFormat != 0) CoTaskMemFree((PVOID)mt.pbFormat);
                    if (mt.pUnk != NULL) mt.pUnk->lpVtbl->Release(mt.pUnk);
                }
            }
            free(pBuf);
        }
        if (!matched && !g_bHidden) {
            InvalidateRect(g_hVidView, NULL, TRUE);
            HDC hdc = GetDC(g_hVidView); HPEN hR = CreatePen(PS_SOLID, 4, RGB(255,0,0)), hO = SelectObject(hdc, hR);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 2, 2, 318, 238);
            SelectObject(hdc, hO); DeleteObject(hR); ReleaseDC(g_hVidView, hdc);
        }
    }
}

void InvertWindowBorder(HWND hwnd) {
    if (!hwnd || g_bHidden) return;
    HDC hdc = GetWindowDC(hwnd); if (!hdc) return;
    RECT rc; GetWindowRect(hwnd, &rc); OffsetRect(&rc, -rc.left, -rc.top); int oldRop = SetROP2(hdc, R2_NOT);
    HPEN hPen = CreatePen(PS_INSIDEFRAME, 5, RGB(0,0,0)), hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, hOldPen); SelectObject(hdc, hOldBrush); SetROP2(hdc, oldRop); DeleteObject(hPen); ReleaseDC(hwnd, hdc);
}

void SetTargetWindow(HWND hTarget) {
    if (!hTarget || !IsWindow(hTarget)) return;
    g_hTargetWnd = hTarget;

    char t[256] = {0}, m[512];
    GetWindowTextA(g_hTargetWnd, t, 255);
    snprintf(m, 511, "Target: %s (%llu)", t[0] ? t : "[Unnamed Box]", (unsigned long long)(uintptr_t)g_hTargetWnd);
    SetWindowTextA(g_hLblTarget, m);

    g_bScanning = TRUE;
    g_szLastScan[0] = '\0';
    if (!g_bHidden) InvalidateRect(g_hBtnToggle, NULL, TRUE);
    SetTimer(g_hMain, 1, 1000, NULL);
    ProcessFrameTimer();
}

LRESULT CALLBACK ToggleBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN: GetCursorPos(&g_ptClick); g_bBtnDown = TRUE; g_bDragging = FALSE; SetCapture(hwnd); return 0;
        case WM_MOUSEMOVE:
            if (g_bBtnDown) {
                POINT pt; GetCursorPos(&pt);
                if (!g_bDragging && (abs(pt.x - g_ptClick.x) > 3 || abs(pt.y - g_ptClick.y) > 3)) g_bDragging = TRUE;
                if (g_bDragging) {
                    SetCursor(LoadCursor(NULL, IDC_CROSS)); HWND hHov = WindowFromPoint(pt);
                    if (hHov != g_hLastHighlight) {
                        if (g_hLastHighlight) InvertWindowBorder(g_hLastHighlight);
                        if (hHov) InvertWindowBorder(hHov);
                        g_hLastHighlight = hHov;
                    }
                }
            } return 0;
        case WM_LBUTTONUP:
            if (g_bBtnDown) {
                g_bBtnDown = FALSE; ReleaseCapture();
                if (g_bDragging) {
                    g_bDragging = FALSE;
                    if (g_hLastHighlight) {
                        InvertWindowBorder(g_hLastHighlight);
                        HWND hPicked = g_hLastHighlight;
                        g_hLastHighlight = NULL;
                        SetTargetWindow(hPicked);
                    }
                } else {
                    g_bScanning = !g_bScanning; InvalidateRect(hwnd, NULL, TRUE);
                    if (g_bScanning) {
                        g_szLastScan[0] = '\0';
                        SetTimer(g_hMain, 1, 1000, NULL);
                        ProcessFrameTimer();
                    } else {
                        KillTimer(g_hMain, 1);
                        InvalidateRect(g_hVidView, NULL, TRUE); InvalidateRect(g_hImgView, NULL, TRUE);
                    }
                }
            } return 0;
    }
    return CallWindowProc(OldToggleProc, hwnd, msg, wParam, lParam);
}

void TypeBarcode(const char* txt) {
    if ((!g_hTargetWnd || !IsWindow(g_hTargetWnd)) && g_szWindowPattern[0] != '\0') {
        g_hTargetWnd = FindWindowByPattern(g_szWindowPattern);
    }
    if (!g_hTargetWnd || !IsWindow(g_hTargetWnd)) {
        if (!g_bHidden) {
            MessageBoxA(g_hMain, "Drag ON button to target or pass -hwnd: / -window: first.", "No Target", MB_ICONWARNING);
        }
        return;
    }

    HWND hRoot = GetAncestor(g_hTargetWnd, GA_ROOT);
    if (!hRoot) hRoot = g_hTargetWnd;

    // Bypass Windows foreground lock when running in -hidden mode
    HWND hFg = GetForegroundWindow();
    DWORD dwCurThread = GetCurrentThreadId();
    DWORD dwFgThread  = hFg ? GetWindowThreadProcessId(hFg, NULL) : 0;
    DWORD dwTgtThread = GetWindowThreadProcessId(hRoot, NULL);

    if (dwFgThread && dwFgThread != dwCurThread) AttachThreadInput(dwCurThread, dwFgThread, TRUE);
    if (dwTgtThread && dwTgtThread != dwCurThread) AttachThreadInput(dwCurThread, dwTgtThread, TRUE);

    if (IsIconic(hRoot)) ShowWindow(hRoot, SW_RESTORE);
    BringWindowToTop(hRoot);
    SetForegroundWindow(hRoot);
    if (g_hTargetWnd != hRoot) SetFocus(g_hTargetWnd);

    if (dwTgtThread && dwTgtThread != dwCurThread) AttachThreadInput(dwCurThread, dwTgtThread, FALSE);
    if (dwFgThread && dwFgThread != dwCurThread) AttachThreadInput(dwCurThread, dwFgThread, FALSE);

    // Wait for the target window to finish activating before typing
    DWORD_PTR dwRes = 0;
    SendMessageTimeoutA(hRoot, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 500, &dwRes);
    Sleep(150);

    // Send characters one by one and synchronize with the target's message queue
    int len = (int)strlen(txt);
    for (int i = 0; i < len; i++) {
        INPUT inp[2] = {0};
        inp[0].type = INPUT_KEYBOARD;
        inp[0].ki.wScan = (WORD)(unsigned char)txt[i];
        inp[0].ki.dwFlags = KEYEVENTF_UNICODE;

        inp[1].type = INPUT_KEYBOARD;
        inp[1].ki.wScan = (WORD)(unsigned char)txt[i];
        inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(2, inp, sizeof(INPUT));
        Sleep(15);
        SendMessageTimeoutA(g_hTargetWnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 250, &dwRes);
    }

    if (SendMessageA(g_hChkReturn, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        Sleep(250);
        INPUT ret[2] = {0};
        ret[0].type = INPUT_KEYBOARD;
        ret[0].ki.wVk = VK_RETURN;
        ret[1].type = INPUT_KEYBOARD;
        ret[1].ki.wVk = VK_RETURN;
        ret[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, ret, sizeof(INPUT));
    }

    // Final flush so hidden mode never exits before the target window processes all keys
    SendMessageTimeoutA(g_hTargetWnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 500, &dwRes);
    Sleep(200);
}

// ============================================================================
// DIRECTSHOW WEBCAM, VFW & WIC / EMF IMAGE LOADER
// ============================================================================

LRESULT CALLBACK VfwFrameCallback(HWND hCap, LPVIDEOHDR lpVHdr) {
    if (!lpVHdr || !lpVHdr->lpData || lpVHdr->dwBytesUsed == 0) return 0;
    int w = g_vfwBmi.bmiHeader.biWidth;
    int h = abs(g_vfwBmi.bmiHeader.biHeight);
    DWORD comp = g_vfwBmi.bmiHeader.biCompression;
    int bpp = g_vfwBmi.bmiHeader.biBitCount;

    if (comp == BI_RGB && (bpp == 24 || bpp == 32)) {
        long needed = (long)lpVHdr->dwBytesUsed;
        if (g_vfwBufSize < needed) {
            g_pVfwPixels = (BYTE*)realloc(g_pVfwPixels, needed);
            g_vfwBufSize = needed;
        }
        if (g_pVfwPixels) memcpy(g_pVfwPixels, lpVHdr->lpData, needed);
        g_vfwBpp = bpp;
        g_vfwBottomUp = (g_vfwBmi.bmiHeader.biHeight > 0);
    } else if ((comp == 0x32595559 || comp == 0x59565955) && lpVHdr->dwBytesUsed >= (DWORD)(w * h * 2)) {
        int y_off = (comp == 0x32595559) ? 0 : 1;
        int stride = (w + 3) & ~3;
        long needed = stride * h;
        if (g_vfwBufSize < needed) {
            g_pVfwPixels = (BYTE*)realloc(g_pVfwPixels, needed);
            g_vfwBufSize = needed;
        }
        if (g_pVfwPixels) {
            for (int y = 0; y < h; y++) {
                const BYTE* src = lpVHdr->lpData + y * w * 2;
                BYTE* dst = g_pVfwPixels + y * stride;
                for (int x = 0; x < w; x++) dst[x] = src[x * 2 + y_off];
            }
            g_vfwBpp = 8;
            g_vfwBottomUp = FALSE;
        }
    }
    return 1;
}

void CleanGraph() {
    if (g_hCapWnd) {
        capSetCallbackOnFrame(g_hCapWnd, NULL);
        capPreview(g_hCapWnd, FALSE);
        capDriverDisconnect(g_hCapWnd);
        DestroyWindow(g_hCapWnd);
        g_hCapWnd = NULL;
    }
    if (g_pVfwPixels) {
        free(g_pVfwPixels);
        g_pVfwPixels = NULL;
        g_vfwBufSize = 0;
    }
    if (g_pControl) { g_pControl->lpVtbl->Stop(g_pControl); g_pControl->lpVtbl->Release(g_pControl); g_pControl = NULL; }
    if (g_pGrabber) { g_pGrabber->lpVtbl->Release(g_pGrabber); g_pGrabber = NULL; }
    if (g_pVidWin)  { g_pVidWin->lpVtbl->put_Visible(g_pVidWin, 0); g_pVidWin->lpVtbl->put_Owner(g_pVidWin, 0); g_pVidWin->lpVtbl->Release(g_pVidWin); g_pVidWin = NULL; }
    if (g_pBuild)   { g_pBuild->lpVtbl->Release(g_pBuild); g_pBuild = NULL; }
    if (g_pGraph)   { g_pGraph->lpVtbl->Release(g_pGraph); g_pGraph = NULL; }
}

void StartWebcam(int index) {
    CleanGraph(); g_bShowImage = FALSE; ShowWindow(g_hImgView, SW_HIDE);
    if (index < 0 || index >= g_iCameraCount) return;

    if (g_bUseVFW) {
        g_hCapWnd = capCreateCaptureWindowA("VFWCap", WS_CHILD | WS_VISIBLE, 0, 0, 320, 240, g_hVidView, 0);
        if (g_hCapWnd && capDriverConnect(g_hCapWnd, g_vfwDriverMap[index])) {
            memset(&g_vfwBmi, 0, sizeof(g_vfwBmi));
            capGetVideoFormat(g_hCapWnd, &g_vfwBmi, sizeof(g_vfwBmi));

            BITMAPINFO bmiReq = g_vfwBmi;
            bmiReq.bmiHeader.biCompression = BI_RGB;
            bmiReq.bmiHeader.biBitCount = 24;
            bmiReq.bmiHeader.biSizeImage = bmiReq.bmiHeader.biWidth * abs(bmiReq.bmiHeader.biHeight) * 3;
            if (capSetVideoFormat(g_hCapWnd, &bmiReq, sizeof(BITMAPINFOHEADER))) {
                g_vfwBmi = bmiReq;
            } else {
                capGetVideoFormat(g_hCapWnd, &g_vfwBmi, sizeof(g_vfwBmi));
            }

            capSetCallbackOnFrame(g_hCapWnd, VfwFrameCallback);
            capPreviewScale(g_hCapWnd, TRUE);
            capPreviewRate(g_hCapWnd, 66);
            capPreview(g_hCapWnd, TRUE);
            if (!g_bHidden) ShowWindow(g_hVidView, SW_SHOW);
        }
        return;
    }

    CoCreateInstance(&CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, &IID_ICaptureGraphBuilder2, (void**)&g_pBuild);
    CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void**)&g_pGraph);
    g_pBuild->lpVtbl->SetFiltergraph(g_pBuild, g_pGraph);

    IBaseFilter *pCap = NULL;
    if (SUCCEEDED(g_pCameras[index]->lpVtbl->BindToObject(g_pCameras[index], NULL, NULL, &IID_IBaseFilter, (void**)&pCap))) {
        g_pGraph->lpVtbl->AddFilter(g_pGraph, pCap, L"Cap");

        IAMStreamConfig *pCfg = NULL;
        if (SUCCEEDED(g_pBuild->lpVtbl->FindInterface(g_pBuild, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pCap, &IID_IAMStreamConfig_, (void**)&pCfg))) {
            int iCount = 0, iSize = 0;
            if (SUCCEEDED(pCfg->lpVtbl->GetNumberOfCapabilities(pCfg, &iCount, &iSize)) && iSize == sizeof(VIDEO_STREAM_CONFIG_CAPS)) {
                AM_MEDIA_TYPE *pBestMT = NULL; int bestPixels = 0;
                for (int i = 0; i < iCount; i++) {
                    VIDEO_STREAM_CONFIG_CAPS scc; AM_MEDIA_TYPE *pmt = NULL;
                    if (SUCCEEDED(pCfg->lpVtbl->GetStreamCaps(pCfg, i, &pmt, (BYTE*)&scc))) {
                        if (IsEqualGUID(&pmt->formattype, &FORMAT_VideoInfo) && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
                            VIDEOINFOHEADER *pVih = (VIDEOINFOHEADER*)pmt->pbFormat;
                            int w = pVih->bmiHeader.biWidth, h = abs(pVih->bmiHeader.biHeight);
                            if (w * h > bestPixels && w <= 1280) {
                                if (pBestMT) { if (pBestMT->cbFormat) CoTaskMemFree(pBestMT->pbFormat); CoTaskMemFree(pBestMT); }
                                pBestMT = pmt; bestPixels = w * h; pmt = NULL;
                            }
                        }
                        if (pmt) { if (pmt->cbFormat) CoTaskMemFree(pmt->pbFormat); CoTaskMemFree(pmt); }
                    }
                }
                if (pBestMT) { pCfg->lpVtbl->SetFormat(pCfg, pBestMT); if (pBestMT->cbFormat) CoTaskMemFree(pBestMT->pbFormat); CoTaskMemFree(pBestMT); }
            }
            pCfg->lpVtbl->Release(pCfg);
        }

        IBaseFilter *pF = NULL;
        if (SUCCEEDED(CoCreateInstance(&CLSID_SampleGrabber_, NULL, CLSCTX_INPROC_SERVER, &IID_IBaseFilter, (void**)&pF))) {
            g_pGraph->lpVtbl->AddFilter(g_pGraph, pF, L"Grab");
            pF->lpVtbl->QueryInterface(pF, &IID_ISampleGrabber_, (void**)&g_pGrabber);
            AM_MEDIA_TYPE mt = {0}; mt.majortype = MEDIATYPE_Video; mt.subtype = MEDIASUBTYPE_RGB32;
            g_pGrabber->lpVtbl->SetMediaType(g_pGrabber, &mt);
            g_pGrabber->lpVtbl->SetBufferSamples(g_pGrabber, TRUE);
            g_pBuild->lpVtbl->RenderStream(g_pBuild, &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, (IUnknown*)pCap, pF, NULL);
            pF->lpVtbl->Release(pF);
        } else {
            g_pBuild->lpVtbl->RenderStream(g_pBuild, &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, (IUnknown*)pCap, NULL, NULL);
        }

        g_pGraph->lpVtbl->QueryInterface(g_pGraph, &IID_IVideoWindow, (void**)&g_pVidWin);
        if (g_pVidWin) {
            g_pVidWin->lpVtbl->put_Owner(g_pVidWin, (LONG_PTR)g_hVidView);
            g_pVidWin->lpVtbl->put_WindowStyle(g_pVidWin, WS_CHILD | (g_bHidden ? 0 : WS_VISIBLE) | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
            g_pVidWin->lpVtbl->SetWindowPosition(g_pVidWin, 0, 0, 320, 240);
            if (g_bHidden) g_pVidWin->lpVtbl->put_Visible(g_pVidWin, 0);
        }
        g_pGraph->lpVtbl->QueryInterface(g_pGraph, &IID_IMediaControl, (void**)&g_pControl);
        if (g_pControl) g_pControl->lpVtbl->Run(g_pControl);
        pCap->lpVtbl->Release(pCap);
        if (!g_bHidden) ShowWindow(g_hVidView, SW_SHOW);
    }
}

void EnumerateCameras() {
    if (g_bUseVFW) {
        char szName[128], szVer[128];
        for (int i = 0; i < 10; i++) {
            if (capGetDriverDescriptionA(i, szName, sizeof(szName), szVer, sizeof(szVer))) {
                SendMessageA(g_hCamCombo, CB_ADDSTRING, 0, (LPARAM)szName);
                g_vfwDriverMap[g_iCameraCount++] = i;
            }
        }
        int sel = (g_iCmdCamIndex >= 0 && g_iCmdCamIndex < g_iCameraCount)
                  ? g_iCmdCamIndex
                  : ((g_iCameraCount > 1) ? 1 : 0);
        if (g_iCameraCount > 0) {
            SendMessageA(g_hCamCombo, CB_SETCURSEL, sel, 0);
            StartWebcam(sel);
        }
        return;
    }

    ICreateDevEnum *pE = NULL;
    CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, &IID_ICreateDevEnum, (void**)&pE);
    if (!pE) return;
    IEnumMoniker *pM = NULL;
    pE->lpVtbl->CreateClassEnumerator(pE, &CLSID_VideoInputDeviceCategory, &pM, 0);
    if (pM) {
        IMoniker *pMon = NULL;
        while (pM->lpVtbl->Next(pM, 1, &pMon, NULL) == S_OK && g_iCameraCount < 10) {
            IPropertyBag *pB = NULL;
            if (SUCCEEDED(pMon->lpVtbl->BindToStorage(pMon, 0, 0, &IID_IPropertyBag, (void**)&pB))) {
                VARIANT v; VariantInit(&v);
                if (SUCCEEDED(pB->lpVtbl->Read(pB, L"FriendlyName", &v, 0))) {
                    char n[256]; WideCharToMultiByte(CP_ACP, 0, v.bstrVal, -1, n, sizeof(n), NULL, NULL);
                    SendMessageA(g_hCamCombo, CB_ADDSTRING, 0, (LPARAM)n); VariantClear(&v);
                }
                pB->lpVtbl->Release(pB);
            }
            g_pCameras[g_iCameraCount++] = pMon;
        }
        pM->lpVtbl->Release(pM);
    }
    pE->lpVtbl->Release(pE);

    int sel = (g_iCmdCamIndex >= 0 && g_iCmdCamIndex < g_iCameraCount)
              ? g_iCmdCamIndex
              : ((g_iCameraCount > 1) ? 1 : 0);
    if (g_iCameraCount > 0) {
        SendMessageA(g_hCamCombo, CB_SETCURSEL, sel, 0);
        StartWebcam(sel);
    }
}

void LoadStaticImage() {
    OPENFILENAMEW ofn = {0}; WCHAR szF[260] = {0};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_hMain; ofn.lpstrFile = szF; ofn.nMaxFile = 260;
    ofn.lpstrFilter = L"Images (*.jpg;*.png;*.bmp;*.emf)\0*.jpg;*.png;*.bmp;*.emf\0All\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        CleanGraph(); ShowWindow(g_hVidView, SW_HIDE); ShowWindow(g_hImgView, SW_SHOW);
        g_szLastScan[0] = '\0';

        HENHMETAFILE hEmf = GetEnhMetaFileW(szF);
        if (hEmf) {
            ENHMETAHEADER emh = {0};
            GetEnhMetaFileHeader(hEmf, sizeof(emh), &emh);
            int mw = emh.rclBounds.right - emh.rclBounds.left;
            int mh = emh.rclBounds.bottom - emh.rclBounds.top;
            if (mw <= 0) mw = 800;
            if (mh <= 0) mh = 400;
            int pad = 40;
            g_imgW = (UINT)(mw + pad * 2);
            g_imgH = (UINT)(mh + pad * 2);

            BITMAPINFO bmi = {0};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = (LONG)g_imgW;
            bmi.bmiHeader.biHeight = -(LONG)g_imgH;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void *pv = NULL;
            HDC hS = GetDC(NULL);
            HDC hMem = CreateCompatibleDC(hS);
            HBITMAP hB = CreateDIBSection(hS, &bmi, DIB_RGB_COLORS, &pv, NULL, 0);
            HBITMAP hOld = (HBITMAP)SelectObject(hMem, hB);

            RECT rcFull = {0, 0, (LONG)g_imgW, (LONG)g_imgH};
            FillRect(hMem, &rcFull, (HBRUSH)GetStockObject(WHITE_BRUSH));
            RECT rcPlay = {pad, pad, pad + mw, pad + mh};
            PlayEnhMetaFile(hMem, hEmf, &rcPlay);
            GdiFlush();

            if (g_pStaticPixels) free(g_pStaticPixels);
            g_pStaticPixels = (BYTE*)malloc(g_imgW * g_imgH * 4);
            memcpy(g_pStaticPixels, pv, g_imgW * g_imgH * 4);

            SelectObject(hMem, hOld);
            DeleteDC(hMem);
            ReleaseDC(NULL, hS);
            DeleteEnhMetaFile(hEmf);

            if (g_hLoadedImg) DeleteObject(g_hLoadedImg);
            g_hLoadedImg = hB;
            g_bShowImage = TRUE;
            InvalidateRect(g_hImgView, NULL, TRUE);
            UpdateWindow(g_hImgView);
            SendMessageA(g_hCamCombo, CB_SETCURSEL, -1, 0);
            if (g_bScanning) ProcessFrameTimer();
            return;
        }

        IWICImagingFactory *pF = NULL;
        if (SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory_, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory_, (void**)&pF))) {
            IWICBitmapDecoder *pD = NULL;
            if (SUCCEEDED(pF->lpVtbl->CreateDecoderFromFilename(pF, szF, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pD))) {
                IWICBitmapFrameDecode *pFr = NULL; pD->lpVtbl->GetFrame(pD, 0, &pFr);
                IWICFormatConverter *pC = NULL; pF->lpVtbl->CreateFormatConverter(pF, &pC);
                pC->lpVtbl->Initialize(pC, (IWICBitmapSource*)pFr, &GUID_WICPixelFormat32bppPBGRA_, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom);
                pC->lpVtbl->GetSize(pC, &g_imgW, &g_imgH);
                BITMAPINFO bmi = {0}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = g_imgW; bmi.bmiHeader.biHeight = -(LONG)g_imgH; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
                if (g_pStaticPixels) free(g_pStaticPixels);
                g_pStaticPixels = (BYTE*)malloc(g_imgW * g_imgH * 4);
                WICRect rc = {0, 0, g_imgW, g_imgH};
                pC->lpVtbl->CopyPixels(pC, &rc, g_imgW * 4, g_imgW * g_imgH * 4, g_pStaticPixels);
                void *pv = NULL; HDC hS = GetDC(NULL); HBITMAP hB = CreateDIBSection(hS, &bmi, DIB_RGB_COLORS, &pv, NULL, 0); ReleaseDC(NULL, hS);
                memcpy(pv, g_pStaticPixels, g_imgW * g_imgH * 4);
                if (g_hLoadedImg) DeleteObject(g_hLoadedImg);
                g_hLoadedImg = hB; g_bShowImage = TRUE; InvalidateRect(g_hImgView, NULL, TRUE); UpdateWindow(g_hImgView);
                pC->lpVtbl->Release(pC); pFr->lpVtbl->Release(pFr); pD->lpVtbl->Release(pD);
            }
            pF->lpVtbl->Release(pF);
        }
        SendMessageA(g_hCamCombo, CB_SETCURSEL, -1, 0);
        if (g_bScanning) ProcessFrameTimer();
    }
}

// ============================================================================
// GUI WINDOW PROCEDURES & WINMAIN
// ============================================================================

LRESULT CALLBACK ImgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT && g_bShowImage && g_hLoadedImg) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); HDC hM = CreateCompatibleDC(hdc); SelectObject(hM, g_hLoadedImg);
        float s = (320.0f / g_imgW < 240.0f / g_imgH) ? 320.0f / g_imgW : 240.0f / g_imgH;
        int nW = (int)(g_imgW * s), nH = (int)(g_imgH * s), oX = (320 - nW) / 2, oY = (240 - nH) / 2;
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 20)); RECT rc = {0, 0, 320, 240}; FillRect(hdc, &rc, bg); DeleteObject(bg);
        SetStretchBltMode(hdc, HALFTONE); StretchBlt(hdc, oX, oY, nW, nH, hM, 0, 0, g_imgW, g_imgH, SRCCOPY);
        DeleteDC(hM); EndPaint(hwnd, &ps); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hMain = hwnd;
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            InitDataMatrixGF();
            InitPDF417Tables();
            WNDCLASS wci = {0}; wci.lpfnWndProc = ImgProc; wci.hInstance = GetModuleHandle(NULL); wci.hbrBackground = (HBRUSH)(COLOR_WINDOW); wci.lpszClassName = "ImgAppClass"; RegisterClass(&wci);
            g_hImgView   = CreateWindowA("ImgAppClass", "", WS_CHILD | WS_VISIBLE, 15, 45, 320, 240, hwnd, NULL, NULL, NULL);
            g_hVidView   = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 15, 45, 320, 240, hwnd, NULL, NULL, NULL);
            g_hCamCombo  = CreateWindowA("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 15, 15, 230, 200, hwnd, (HMENU)IDC_CAM_COMBO, NULL, NULL);
            CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE, 255, 14, 80, 24, hwnd, (HMENU)IDC_BTN_BROWSE, NULL, NULL);
            g_hLblTarget = CreateWindowA("STATIC", "Target: None Selected", WS_CHILD | WS_VISIBLE, 350, 45, 220, 20, hwnd, (HMENU)IDC_LBL_TARGET, NULL, NULL);
            g_hBtnToggle = CreateWindowA("BUTTON", "SCANNER OFF (Drag to Pick)", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 350, 75, 220, 60, hwnd, (HMENU)IDC_BTN_TOGGLE, NULL, NULL);
            OldToggleProc = (WNDPROC)SetWindowLongPtr(g_hBtnToggle, GWLP_WNDPROC, (LONG_PTR)ToggleBtnProc);
            g_hBtnInject = CreateWindowA("BUTTON", "Inject: [None]", WS_CHILD | WS_VISIBLE, 350, 150, 220, 60, hwnd, (HMENU)IDC_BTN_INJECT, NULL, NULL);
            g_hChkReturn = CreateWindowA("BUTTON", "Add RETURN key (0.25s delay)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 350, 220, 220, 24, hwnd, (HMENU)IDC_CHK_RETURN, NULL, NULL);
            SendMessageA(g_hChkReturn, BM_SETCHECK, g_bAddReturn ? BST_CHECKED : BST_UNCHECKED, 0);

            EnumerateCameras();

            if (!g_hCmdTargetWnd && g_szWindowPattern[0] != '\0') {
                g_hCmdTargetWnd = FindWindowByPattern(g_szWindowPattern);
            }
            if (g_hCmdTargetWnd && IsWindow(g_hCmdTargetWnd)) {
                SetTargetWindow(g_hCmdTargetWnd);
            } else if (g_bHidden || g_szWindowPattern[0] != '\0') {
                g_bScanning = TRUE;
                SetTimer(g_hMain, 1, 1000, NULL);
            }
            break;
        }
        case WM_DRAWITEM: {
            if (wParam == IDC_BTN_TOGGLE) {
                LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lParam;
                HBRUSH bg = CreateSolidBrush(g_bScanning ? RGB(50, 200, 50) : RGB(200, 50, 50));
                FillRect(d->hDC, &d->rcItem, bg); DeleteObject(bg);
                SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, RGB(255, 255, 255));
                HFONT hF = CreateFontA(13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Arial"), hO = (HFONT)SelectObject(d->hDC, hF);
                DrawTextA(d->hDC, g_bScanning ? "SCANNING (1D / DM / PDF417)" : "SCANNER OFF (Drag to Target)", -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(d->hDC, hO); DeleteObject(hF); return TRUE;
            }
            break;
        }
        case WM_TIMER: ProcessFrameTimer(); break;
        case WM_APP_BARCODE_SCANNED: {
            char* b = (char*)lParam; strncpy(g_szLastScan, b, sizeof(g_szLastScan) - 1);
            g_szLastScan[sizeof(g_szLastScan) - 1] = '\0';
            char tx[1100]; snprintf(tx, sizeof(tx) - 1, "Inject: %s", g_szLastScan);
            SetWindowTextA(g_hBtnInject, tx); free(b);
            g_bScanning = FALSE; KillTimer(g_hMain, 1);
            if (!g_bHidden) InvalidateRect(g_hBtnToggle, NULL, TRUE);

            if ((g_hTargetWnd && IsWindow(g_hTargetWnd)) || g_szWindowPattern[0] != '\0') {
                TypeBarcode(g_szLastScan);
            }
            if (g_bHidden) {
                PostMessageA(g_hMain, WM_CLOSE, 0, 0);
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_BTN_INJECT) { if (strlen(g_szLastScan) > 0) TypeBarcode(g_szLastScan); }
            if (LOWORD(wParam) == IDC_BTN_BROWSE) LoadStaticImage();
            if (LOWORD(wParam) == IDC_CAM_COMBO && HIWORD(wParam) == CBN_SELCHANGE) StartWebcam((int)SendMessageA(g_hCamCombo, CB_GETCURSEL, 0, 0));
            break;
        case WM_DESTROY:
            KillTimer(g_hMain, 1); CleanGraph();
            if (!g_bUseVFW) {
                for (int i = 0; i < g_iCameraCount; i++) g_pCameras[i]->lpVtbl->Release(g_pCameras[i]);
            }
            if (g_hLoadedImg) DeleteObject(g_hLoadedImg);
            if (g_pStaticPixels) free(g_pStaticPixels);
            CoUninitialize(); PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, int nC) {
    for (int i = 1; i < __argc; i++) {
        if (_stricmp(__argv[i], "-vfw") == 0) {
            g_bUseVFW = TRUE;
        } else if (_stricmp(__argv[i], "-LF") == 0) {
            g_bAddReturn = !g_bAddReturn;
        } else if (_stricmp(__argv[i], "-hidden") == 0) {
            g_bHidden = TRUE;
        } else if (_strnicmp(__argv[i], "-cam:", 5) == 0) {
            g_iCmdCamIndex = atoi(__argv[i] + 5);
        } else if (_strnicmp(__argv[i], "-c:", 3) == 0) {
            g_iCmdCamIndex = atoi(__argv[i] + 3);
        } else if (_strnicmp(__argv[i], "-hwnd:", 6) == 0) {
            uintptr_t val = (uintptr_t)strtoull(__argv[i] + 6, NULL, 0);
            g_hCmdTargetWnd = (HWND)val;
        } else if (_strnicmp(__argv[i], "-window:", 8) == 0) {
            strncpy(g_szWindowPattern, __argv[i] + 8, sizeof(g_szWindowPattern) - 1);
            g_szWindowPattern[sizeof(g_szWindowPattern) - 1] = '\0';
        }
    }

    WNDCLASS wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hI; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW); wc.lpszClassName = "ScannerAppClass"; RegisterClass(&wc);
    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    if (!g_bHidden) dwStyle |= WS_VISIBLE;

    g_hMain = CreateWindowExA(0, "ScannerAppClass", "Native 1D, DataMatrix & PDF417 Scanner", dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, 600, 335, NULL, NULL, hI, NULL);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}
