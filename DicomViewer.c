/* ============================================================================
 *
 *  RIS Multi-Protocol Viewer - Extended Format Edition
 *
 *  Supports:
 *    - DICOM Part 10 (Explicit & Implicit VR Little Endian)
 *    - DICOM Explicit VR Big Endian (1.2.840.10008.1.2.2 & GE 1.2.840.113619.5.2)
 *    - Preamble-less DICOM / ACR-NEMA 2.0 (Direct at Offset 0)
 *    - Signed 16-bit CT (PixelRepresentation = 1) & MONOCHROME1 Inversion
 *    - 24-bit RGB DICOM (SamplesPerPixel = 3)
 *    - GE Genesis / Signa & DAT ("IMGF" headers at offsets 0, 3178, 3180, 3228, 3240)
 *    - Siemens Magnetom Vision & Somatom ("SIEMENS" / "SOMATOM" headers)
 *    - Netpbm Binary PGM/PPM (P5 Grayscale & P6 RGB)
 *
 *  Build:
 *   gcc DicomViewer.c -o DicomViewer.exe -mwindows -lshell32 -lcomctl32
 *  -municode
 *  2026 Hugo
 *  
 *  THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 *  AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
/* UI Control IDs */
#define ID_EDIT_HEADER 103
/* Global State */
HWND hMainWnd, hEditHeader;

uint8_t* g_PixelData = NULL;
size_t g_PixelDataSize = 0;

uint32_t* g_RenderBuffer = NULL; 
size_t g_RenderBufferSize = 0;

int g_ImgWidth = 0, g_ImgHeight = 0;
int g_BitsAllocated = 16;
int g_PixelRepresentation = 0;        /* 0 = unsigned, 1 = signed 2's complement */
int g_SamplesPerPixel = 1;            /* 1 = Monochrome, 3 = RGB */
int g_PhotometricInterpretation = 2;  /* 1 = MONOCHROME1, 2 = MONOCHROME2, 3 = RGB */

float g_Zoom = 1.0f;
int g_PanX = 0, g_PanY = 0;
POINT g_LastMouse;

/* Mouse Interaction States */
bool g_IsPanning = false;
bool g_IsWindowLevelling = false;
bool g_IsDraggingBox = false;
POINT g_DragBoxOffset;

/* Feature Toggles */
bool g_ShowHeader = false;
bool g_BoxMode = false;

/* ROI Box in Image Coordinates */
int g_RoiX = 0, g_RoiY = 0, g_RoiW = 100, g_RoiH = 100;

/* DICOM parameters */
int g_WinCenter = 400;
int g_WinWidth = 2000;
int g_DefWinCenter = 400;
int g_DefWinWidth = 2000;
float g_RescaleSlope = 1.0f;
float g_RescaleIntercept = 0.0f;

wchar_t* g_DicomHeaderStr = NULL;
size_t g_HeaderCap = 0;
size_t g_HeaderLen = 0;

/* Helpers for Byte Swapping */
static inline uint16_t Swap16(uint16_t v) {
    return (v >> 8) | (v << 8);
}

static inline uint32_t Swap32(uint32_t v) {
    return ((v >> 24) & 0x000000FF) |
           ((v >> 8)  & 0x0000FF00) |
           ((v << 8)  & 0x00FF0000) |
           ((v << 24) & 0xFF000000);
}

static inline float SwapFloat(uint32_t v) {
    uint32_t s = Swap32(v);
    float f;
    memcpy(&f, &s, 4);
    return f;
}

/* Header string builder */
void AppendHeaderW(const wchar_t* str) {
    size_t len = wcslen(str);
    if (g_HeaderLen + len + 1 > g_HeaderCap) {
        g_HeaderCap = (g_HeaderCap == 0) ? 4096 : ((g_HeaderCap * 2) + len + 1);
        g_DicomHeaderStr = (wchar_t*)realloc(g_DicomHeaderStr, g_HeaderCap * sizeof(wchar_t));
    }
    wcscpy(g_DicomHeaderStr + g_HeaderLen, str);
    g_HeaderLen += len;
}

/* Recompute the 32-bit ARGB render buffer based on Window Level/Width and format */
void UpdateRenderBuffer(void) {
    if (g_ImgWidth <= 0 || g_ImgHeight <= 0 || g_PixelData == NULL) return;

    size_t neededPixels = (size_t)g_ImgWidth * (size_t)g_ImgHeight;
    size_t bytesPerPixel = (g_BitsAllocated == 8) ? 1 : 2;
    if (g_SamplesPerPixel == 3) bytesPerPixel *= 3;

    /* SECURITY FIX: Prevent Out-Of-Bounds read if PixelData is compressed (JPEG/RLE) or truncated. 
       Without a decoder, we cannot safely render encapsulated streams as raw bytes. */
    if (g_PixelDataSize < neededPixels * (g_SamplesPerPixel == 3 && g_BitsAllocated == 8 ? 3 : bytesPerPixel)) {
        return;
    }

    if (g_RenderBufferSize < neededPixels) {
        if (g_RenderBuffer) free(g_RenderBuffer);
        g_RenderBuffer = (uint32_t*)malloc(neededPixels * sizeof(uint32_t));
        g_RenderBufferSize = neededPixels;
    }

    /* Direct 24-bit RGB rendering */
    if (g_SamplesPerPixel == 3 && g_BitsAllocated == 8) {
        uint8_t* p = g_PixelData;
        for (size_t i = 0; i < neededPixels; ++i) {
            uint8_t r = p[i * 3];
            uint8_t g = p[i * 3 + 1];
            uint8_t b = p[i * 3 + 2];
            g_RenderBuffer[i] = (255u << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
        return;
    }

    float minV = g_WinCenter - 0.5f - (g_WinWidth - 1) / 2.0f;
    float maxV = g_WinCenter - 0.5f + (g_WinWidth - 1) / 2.0f;

    uint16_t* p16u = (uint16_t*)g_PixelData;
    int16_t*  p16s = (int16_t*)g_PixelData;
    uint8_t*  p8u  = (uint8_t*)g_PixelData;
    int8_t*   p8s  = (int8_t*)g_PixelData;

    for (size_t i = 0; i < neededPixels; ++i) {
        float val = 0.0f;
        if (g_BitsAllocated == 8) {
            if (g_PixelRepresentation == 1)
                val = (float)p8s[i] * g_RescaleSlope + g_RescaleIntercept;
            else
                val = (float)p8u[i] * g_RescaleSlope + g_RescaleIntercept;
        } else {
            if (g_PixelRepresentation == 1)
                val = (float)p16s[i] * g_RescaleSlope + g_RescaleIntercept;
            else
                val = (float)p16u[i] * g_RescaleSlope + g_RescaleIntercept;
        }

        int p = 0;
        if (val <= minV) p = 0;
        else if (val > maxV) p = 255;
        else if (g_WinWidth > 1) p = (int)(((val - (g_WinCenter - 0.5f)) / (float)(g_WinWidth - 1)) * 255.0f);
        else p = (val > minV) ? 255 : 0;
        
        if (p < 0) p = 0;
        if (p > 255) p = 255;

        /* MONOCHROME1: minimum pixel value is displayed as white */
        if (g_PhotometricInterpretation == 1) {
            p = 255 - p;
        }
        
        g_RenderBuffer[i] = (255u << 24) | ((uint32_t)p << 16) | ((uint32_t)p << 8) | (uint32_t)p;
    }
}

/* Calculate Window Level/Width dynamically based on pixels inside the ROI Box */
void AutoLevelROI(void) {
    if (!g_PixelData || g_ImgWidth <= 0 || g_ImgHeight <= 0) return;
    float minVal = 1e9f;
    float maxVal = -1e9f;
    bool found = false;

    uint16_t* p16u = (uint16_t*)g_PixelData;
    int16_t*  p16s = (int16_t*)g_PixelData;
    uint8_t*  p8u  = (uint8_t*)g_PixelData;
    int8_t*   p8s  = (int8_t*)g_PixelData;

    for (int y = g_RoiY; y < g_RoiY + g_RoiH; y++) {
        if (y < 0 || y >= g_ImgHeight) continue;
        for (int x = g_RoiX; x < g_RoiX + g_RoiW; x++) {
            if (x < 0 || x >= g_ImgWidth) continue;
            
            int idx = y * g_ImgWidth + x;
            float val = 0.0f;
            if (g_BitsAllocated == 8) {
                val = (g_PixelRepresentation == 1) ? 
                      ((float)p8s[idx] * g_RescaleSlope + g_RescaleIntercept) : 
                      ((float)p8u[idx] * g_RescaleSlope + g_RescaleIntercept);
            } else {
                val = (g_PixelRepresentation == 1) ? 
                      ((float)p16s[idx] * g_RescaleSlope + g_RescaleIntercept) : 
                      ((float)p16u[idx] * g_RescaleSlope + g_RescaleIntercept);
            }
            
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
            found = true;
        }
    }
    if (found) {
        g_WinWidth = (int)(maxVal - minVal);
        if (g_WinWidth < 1) g_WinWidth = 1;
        g_WinCenter = (int)(minVal + g_WinWidth / 2.0f);
    }
}

/* Reset view to fit the window */
void ResetView(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int viewW = rect.right - (g_ShowHeader ? 350 : 0);
    int viewH = rect.bottom;

    if (viewW > 0 && viewH > 0 && g_ImgWidth > 0 && g_ImgHeight > 0) {
        float zoomX = (float)viewW / (float)g_ImgWidth;
        float zoomY = (float)viewH / (float)g_ImgHeight;
        g_Zoom = (zoomX < zoomY) ? zoomX : zoomY;
        g_Zoom *= 0.95f; 
        g_PanX = (viewW - (int)((float)g_ImgWidth * g_Zoom)) / 2;
        g_PanY = (viewH - (int)((float)g_ImgHeight * g_Zoom)) / 2;
    } else {
        g_Zoom = 1.0f;
        g_PanX = 0; g_PanY = 0;
    }
}

/* ============================================================================
 * Format Loader: GE Genesis / Signa ("IMGF" magic) - ported from read_ge_data
 * ============================================================================ */
static bool TryLoadGEGenesis(uint8_t* buf, size_t fileSize, const wchar_t* filepath) {
    if (fileSize < 3252) return false;

    static const size_t testOffsets[] = { 3228, 3240, 0, 3178, 3180 };
    size_t initOff = (size_t)-1;

    for (int i = 0; i < 5; i++) {
        size_t o = testOffsets[i];
        if (o + 4 <= fileSize && memcmp(&buf[o], "IMGF", 4) == 0) {
            initOff = o;
            break;
        }
    }
    if (initOff == (size_t)-1) return false;

    uint32_t imgOffsetRel = Swap32(*(uint32_t*)&buf[initOff + 4]);
    uint32_t width = Swap32(*(uint32_t*)&buf[initOff + 8]);
    uint32_t height = Swap32(*(uint32_t*)&buf[initOff + 12]);
    uint32_t bits = Swap32(*(uint32_t*)&buf[initOff + 16]);
    uint32_t compress = Swap32(*(uint32_t*)&buf[initOff + 20]);

    if (compress != 0 || width == 0 || height == 0 || (bits != 8 && bits != 16)) return false;

    size_t imgStart = initOff + imgOffsetRel;
    size_t bytesPerPixel = bits / 8;
    size_t pixelBytes = (size_t)width * height * bytesPerPixel;
    if (imgStart + pixelBytes > fileSize) return false;

    g_ImgWidth = width;
    g_ImgHeight = height;
    g_BitsAllocated = bits;
    g_PixelRepresentation = 0;
    g_SamplesPerPixel = 1;
    g_PhotometricInterpretation = 2;
    g_RescaleSlope = 1.0f;
    g_RescaleIntercept = 0.0f;

    if (g_PixelData) free(g_PixelData);
    g_PixelData = (uint8_t*)malloc(pixelBytes);
    memcpy(g_PixelData, &buf[imgStart], pixelBytes);
    g_PixelDataSize = pixelBytes;

    /* GE Genesis is Big Endian: swap 16-bit words to host order */
    if (bits == 16) {
        uint16_t* p = (uint16_t*)g_PixelData;
        size_t count = pixelBytes / 2;
        for (size_t i = 0; i < count; i++) p[i] = Swap16(p[i]);
    }

    g_HeaderLen = 0;
    if (g_DicomHeaderStr) g_DicomHeaderStr[0] = L'\0';
    wchar_t tempStr[1024];
    swprintf(tempStr, 1024, L"File: %s\r\nFormat: GE Genesis / Signa (IMGF)\r\nMatrix: %u x %u x %u bits\r\nImage Offset: %zu\r\n", 
             filepath, width, height, bits, imgStart);
    AppendHeaderW(tempStr);

    uint32_t examHdr = Swap32(*(uint32_t*)&buf[initOff + 132]);
    if (examHdr > 0 && examHdr + 120 < fileSize) {
        char pId[16] = {0}, pName[32] = {0};
        memcpy(pId, &buf[examHdr + 84], 13);
        memcpy(pName, &buf[examHdr + 97], 25);
        swprintf(tempStr, 1024, L"Patient ID: %hs\r\nPatient Name: %hs\r\n", pId, pName);
        AppendHeaderW(tempStr);
    }

    g_WinWidth = -1; /* Auto-calculate */
    return true;
}

/* ============================================================================
 * Format Loader: Siemens Magnetom Vision & Somatom - ported from read_siemens_data
 * ============================================================================ */
static bool TryLoadSiemens(uint8_t* buf, size_t fileSize, const wchar_t* filepath) {
    if (fileSize < 6144) return false;

    size_t imgStart = 0;
    uint32_t width = 0, height = 0;
    bool isSiemens = false;

    /* Check Magnetom Vision */
    if (memcmp(&buf[96], "SIEMENS", 7) == 0) {
        isSiemens = true;
        imgStart = 6144;
        width = Swap32(*(uint32_t*)&buf[2864]);
        height = width;
        if (width == 0 || width > 2048) {
            width = 256; height = 256;
            if (fileSize >= 6144 + 512 * 512 * 2) { width = 512; height = 512; }
        }
    } 
    /* Check Somatom */
    else if (fileSize == 530432 || (fileSize > 300 && memcmp(&buf[281], "SOMATOM", 7) == 0)) {
        isSiemens = true;
        imgStart = 6144;
        width = 512; height = 512;
    } 
    /* Check Somatom Plus */
    else if (memcmp(&buf[0], "SIEMENS", 7) == 0) {
        isSiemens = true;
        imgStart = 8192;
        width = Swap32(*(uint32_t*)&buf[5340]);
        height = Swap32(*(uint32_t*)&buf[5344]);
    }

    if (!isSiemens || width == 0 || height == 0) return false;

    size_t pixelBytes = (size_t)width * height * 2;
    if (imgStart + pixelBytes > fileSize) return false;

    g_ImgWidth = width;
    g_ImgHeight = height;
    g_BitsAllocated = 16;
    g_PixelRepresentation = 0;
    g_SamplesPerPixel = 1;
    g_PhotometricInterpretation = 2;
    g_RescaleSlope = 1.0f;
    g_RescaleIntercept = 0.0f;

    if (g_PixelData) free(g_PixelData);
    g_PixelData = (uint8_t*)malloc(pixelBytes);
    memcpy(g_PixelData, &buf[imgStart], pixelBytes);
    g_PixelDataSize = pixelBytes;

    /* Siemens raw is Big Endian */
    uint16_t* p = (uint16_t*)g_PixelData;
    for (size_t i = 0; i < pixelBytes / 2; i++) p[i] = Swap16(p[i]);

    g_HeaderLen = 0;
    if (g_DicomHeaderStr) g_DicomHeaderStr[0] = L'\0';
    wchar_t tempStr[1024];
    swprintf(tempStr, 1024, L"File: %s\r\nFormat: Siemens Magnetom / Somatom\r\nMatrix: %u x %u x 16-bit Big-Endian\r\nOffset: %zu\r\n", 
             filepath, width, height, imgStart);
    AppendHeaderW(tempStr);

    g_WinWidth = -1; /* Auto-calculate */
    return true;
}

/* ============================================================================
 * Format Loader: Netpbm PGM / PPM (P5 / P6 binary)
 * ============================================================================ */
static bool TryLoadPGM(uint8_t* buf, size_t fileSize, const wchar_t* filepath) {
    if (fileSize < 16 || buf[0] != 'P' || (buf[1] != '5' && buf[1] != '6')) return false;

    int type = buf[1] - '0';
    size_t pos = 2;

    int w = 0, h = 0, maxVal = 0;
    int tokensRead = 0;

    while (tokensRead < 3 && pos < fileSize) {
        while (pos < fileSize && isspace(buf[pos])) pos++;
        if (pos >= fileSize) break;
        if (buf[pos] == '#') {
            while (pos < fileSize && buf[pos] != '\n') pos++;
            continue;
        }
        int val = 0;
        while (pos < fileSize && isdigit(buf[pos])) {
            val = val * 10 + (buf[pos] - '0');
            pos++;
        }
        if (tokensRead == 0) w = val;
        else if (tokensRead == 1) h = val;
        else if (tokensRead == 2) maxVal = val;
        tokensRead++;
    }
    if (tokensRead < 3 || w <= 0 || h <= 0 || maxVal <= 0) return false;
    if (pos < fileSize && isspace(buf[pos])) pos++; /* Skip single delimiter */

    int bpp = (maxVal > 255) ? 16 : 8;
    int channels = (type == 6) ? 3 : 1;
    size_t pixelBytes = (size_t)w * h * channels * (bpp / 8);
    if (pos + pixelBytes > fileSize) return false;

    g_ImgWidth = w;
    g_ImgHeight = h;
    g_BitsAllocated = bpp;
    g_PixelRepresentation = 0;
    g_SamplesPerPixel = channels;
    g_PhotometricInterpretation = (channels == 3) ? 3 : 2;
    g_RescaleSlope = 1.0f;
    g_RescaleIntercept = 0.0f;

    if (g_PixelData) free(g_PixelData);
    g_PixelData = (uint8_t*)malloc(pixelBytes);
    memcpy(g_PixelData, &buf[pos], pixelBytes);
    g_PixelDataSize = pixelBytes;

    if (bpp == 16) {
        uint16_t* p = (uint16_t*)g_PixelData;
        for (size_t i = 0; i < pixelBytes / 2; i++) p[i] = Swap16(p[i]);
    }

    g_HeaderLen = 0;
    if (g_DicomHeaderStr) g_DicomHeaderStr[0] = L'\0';
    wchar_t tempStr[1024];
    swprintf(tempStr, 1024, L"File: %s\r\nFormat: Netpbm (%hs)\r\nMatrix: %d x %d (%d-bit, max: %d)\r\n",
             filepath, (type == 6) ? "PPM RGB P6" : "PGM Grayscale P5", w, h, bpp, maxVal);
    AppendHeaderW(tempStr);

    g_WinCenter = maxVal / 2;
    g_WinWidth = maxVal;
    return true;
}

/* ============================================================================
 * Enhanced DICOM Loader: Standard Part 10, Big-Endian, Preamble-less, & JPEG
 * ============================================================================ */
static bool TryLoadDicom(uint8_t* buf, size_t fileSize, const wchar_t* filepath) {
    if (fileSize < 32) return false;

    size_t offset = 0;
    bool isExplicit = true;
    bool isBigEndian = false;
    bool isDicom = false;

    /* 1. Check for standard DICM magic at offset 128 */
    if (fileSize >= 136 && memcmp(&buf[128], "DICM", 4) == 0) {
        offset = 132;
        isDicom = true;
    }
    /* 2. Check for DICM magic at offset 0 */
    else if (memcmp(&buf[0], "DICM", 4) == 0) {
        offset = 4;
        isDicom = true;
    }
    /* 3. Check for preamble-less DICOM / ACR-NEMA at offset 0 */
    else {
        uint16_t grp_le = *(uint16_t*)&buf[0];
        uint16_t grp_be = Swap16(grp_le);
        if (grp_le == 0x0002 || grp_le == 0x0008 || grp_le == 0x0020 || grp_le == 0x0028) {
            offset = 0;
            isBigEndian = false;
            isDicom = true;
            if (buf[4] >= 'A' && buf[4] <= 'Z' && buf[5] >= 'A' && buf[5] <= 'Z') isExplicit = true;
            else isExplicit = false;
        } else if (grp_be == 0x0002 || grp_be == 0x0008 || grp_be == 0x0020 || grp_be == 0x0028) {
            offset = 0;
            isBigEndian = true;
            isDicom = true;
            if (buf[4] >= 'A' && buf[4] <= 'Z' && buf[5] >= 'A' && buf[5] <= 'Z') isExplicit = true;
            else isExplicit = false;
        }
    }

    if (!isDicom) return false;

    g_HeaderLen = 0;
    if (g_DicomHeaderStr) g_DicomHeaderStr[0] = L'\0';

    wchar_t tempStr[1024];
    swprintf(tempStr, 1024, L"File: %s\r\nMode: %s\r\n-----------------------------------\r\n", 
             filepath, isBigEndian ? L"DICOM (Big-Endian)" : L"DICOM (Little-Endian)");
    AppendHeaderW(tempStr);

    g_ImgWidth = 0; g_ImgHeight = 0;
    g_BitsAllocated = 16;
    g_PixelRepresentation = 0;
    g_SamplesPerPixel = 1;
    g_PhotometricInterpretation = 2;
    g_RescaleSlope = 1.0f; 
    g_RescaleIntercept = 0.0f;
    g_WinWidth = -1; /* Sentinel */

    if (g_PixelData) { free(g_PixelData); g_PixelData = NULL; }
    g_PixelDataSize = 0;

    while (offset + 8 <= fileSize) {
        uint16_t group = isBigEndian ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
        uint16_t elem  = isBigEndian ? Swap16(*(uint16_t*)&buf[offset + 2]) : *(uint16_t*)&buf[offset + 2];
        offset += 4;

        uint32_t length = 0;
        char vr[3] = {0};
        bool curExplicit = isExplicit;

        /* Group 0x0002 is always Explicit VR Little Endian in standard DICOM */
        if (group == 0x0002) {
            curExplicit = true;
        }

        if (curExplicit && offset + 2 <= fileSize &&
            buf[offset] >= 'A' && buf[offset] <= 'Z' && buf[offset+1] >= 'A' && buf[offset+1] <= 'Z') {
            vr[0] = buf[offset]; vr[1] = buf[offset+1];
            offset += 2;
            if ((vr[0] == 'O' && vr[1] == 'B') || (vr[0] == 'O' && vr[1] == 'W') || 
                (vr[0] == 'S' && vr[1] == 'Q') || (vr[0] == 'U' && vr[1] == 'N') || 
                (vr[0] == 'O' && vr[1] == 'F') || (vr[0] == 'U' && vr[1] == 'T')) {
                if (offset + 2 > fileSize) break;
                offset += 2; /* 2 reserved bytes */
                if (offset + 4 > fileSize) break;
                length = (isBigEndian && group != 0x0002) ? Swap32(*(uint32_t*)&buf[offset]) : *(uint32_t*)&buf[offset];
                offset += 4;
            } else {
                if (offset + 2 > fileSize) break;
                length = (isBigEndian && group != 0x0002) ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
                offset += 2;
            }
        } else {
            if (offset + 4 > fileSize) break;
            length = (isBigEndian && group != 0x0002) ? Swap32(*(uint32_t*)&buf[offset]) : *(uint32_t*)&buf[offset];
            offset += 4;
        }

        bool isEncapsulated = false;
        if (length == 0xFFFFFFFF) {
            if (group == 0x7FE0 && elem == 0x0010) {
                isEncapsulated = true;
                length = (uint32_t)(fileSize - offset); /* Safe bounding limit */
            } else {
                length = 0; /* Unknown sequence, safely skip 0 bytes to prevent crash */
            }
        }

        if (!isEncapsulated && (offset + length < offset || offset + length > fileSize)) {
            if (group == 0x7FE0 && elem == 0x0010) length = (uint32_t)(fileSize - offset);
            else break;
        }

        char valStr[256] = {0};
        bool isText = false;
        if (!isEncapsulated && length > 0 && length < 255 && offset + length <= fileSize) {
            isText = true;
            for (uint32_t k = 0; k < length; k++) {
                char c = buf[offset + k];
                if ((c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\0') || (unsigned char)c > 126) {
                    isText = false;
                    break;
                }
            }
            if (isText) {
                memcpy(valStr, &buf[offset], length);
                valStr[length] = '\0';
                for (int k = (int)length - 1; k >= 0; k--) {
                    if (valStr[k] == ' ' || valStr[k] == '\0' || valStr[k] == '\n' || valStr[k] == '\r') {
                        valStr[k] = '\0';
                    } else break;
                }
            }
        }

        wchar_t wValStr[256] = {0};
        if (isText && strlen(valStr) > 0) MultiByteToWideChar(CP_ACP, 0, valStr, -1, wValStr, 256);

        if (curExplicit && vr[0]) {
            if (isText && wValStr[0] != L'\0') swprintf(tempStr, 1024, L"(%04X,%04X) VR: %c%c Len: %u [%s]\r\n", group, elem, vr[0], vr[1], length, wValStr);
            else swprintf(tempStr, 1024, L"(%04X,%04X) VR: %c%c Len: %u\r\n", group, elem, vr[0], vr[1], length);
        } else {
            if (isText && wValStr[0] != L'\0') swprintf(tempStr, 1024, L"(%04X,%04X) Len: %u [%s]\r\n", group, elem, length, wValStr);
            else swprintf(tempStr, 1024, L"(%04X,%04X) Len: %u\r\n", group, elem, length);
        }
        AppendHeaderW(tempStr);

        /* Transfer Syntax Tracking */
        if (group == 0x0002 && elem == 0x0010 && isText) {
            if (strcmp(valStr, "1.2.840.10008.1.2") == 0) {
                isExplicit = false;
                isBigEndian = false;
            } else if (strcmp(valStr, "1.2.840.10008.1.2.2") == 0 || strcmp(valStr, "1.2.840.113619.5.2") == 0) {
                isExplicit = true;
                isBigEndian = true;
            } else if (strcmp(valStr, "1.2.840.10008.1.2.1") == 0) {
                isExplicit = true;
                isBigEndian = false;
            }
        }
        else if (group == 0x0028 && elem == 0x0002) { 
            g_SamplesPerPixel = isBigEndian ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
        }
        else if (group == 0x0028 && elem == 0x0004 && isText) { 
            if (strstr(valStr, "MONOCHROME1")) g_PhotometricInterpretation = 1;
            else if (strstr(valStr, "RGB") || strstr(valStr, "YBR")) g_PhotometricInterpretation = 3;
            else g_PhotometricInterpretation = 2;
        }
        else if (group == 0x0028 && elem == 0x0010) { 
            g_ImgHeight = isBigEndian ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
        }
        else if (group == 0x0028 && elem == 0x0011) { 
            g_ImgWidth = isBigEndian ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
        }
        else if (group == 0x0028 && elem == 0x0100) { 
            g_BitsAllocated = isBigEndian ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
        }
        else if (group == 0x0028 && elem == 0x0103) { 
            g_PixelRepresentation = isBigEndian ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
        }
        else if (group == 0x0028 && elem == 0x1050 && isText) { 
            g_WinCenter = (int)strtof(valStr, NULL);
        }
        else if (group == 0x0028 && elem == 0x1051 && isText) { 
            g_WinWidth = (int)strtof(valStr, NULL);
        }
        else if (group == 0x0028 && elem == 0x1052 && isText) { 
            g_RescaleIntercept = strtof(valStr, NULL);
        }
        else if (group == 0x0028 && elem == 0x1053 && isText) { 
            g_RescaleSlope = strtof(valStr, NULL);
        }
        else if (group == 0x7FE0 && elem == 0x0010) { 
            if (isEncapsulated) {
                /* Skip Basic Offset Table Item (FFFE, E000) */
                uint32_t botLen = isBigEndian ? Swap32(*(uint32_t*)&buf[offset+4]) : *(uint32_t*)&buf[offset+4];
                offset += 8 + botLen;

                /* Read First Pixel Fragment Item (FFFE, E000) */
                uint16_t fragGrp = isBigEndian ? Swap16(*(uint16_t*)&buf[offset]) : *(uint16_t*)&buf[offset];
                uint16_t fragElm = isBigEndian ? Swap16(*(uint16_t*)&buf[offset+2]) : *(uint16_t*)&buf[offset+2];
                uint32_t fragLen = isBigEndian ? Swap32(*(uint32_t*)&buf[offset+4]) : *(uint32_t*)&buf[offset+4];
                offset += 8;

                if (fragGrp == 0xFFFE && fragElm == 0xE000 && fragLen > 0 && offset + fragLen <= fileSize) {
                    /* Decode the JPEG stream into memory */
                    int w, h, channels;
                    uint8_t* rgb = stbi_load_from_memory(&buf[offset], fragLen, &w, &h, &channels, 3);
                    if (rgb) {
                        g_PixelDataSize = (size_t)w * h * 3;
                        g_PixelData = rgb;
                        g_ImgWidth = w;
                        g_ImgHeight = h;
                        g_BitsAllocated = 8;
                        g_SamplesPerPixel = 3;
                        g_PhotometricInterpretation = 3; /* Render straight to RGB */
                    }
                }
                break;
            } else {
                /* Standard Raw Uncompressed Pixel Data */
                g_PixelDataSize = length;
                g_PixelData = (uint8_t*)malloc(length);
                if (g_PixelData) {
                    memcpy(g_PixelData, &buf[offset], length);
                    if (isBigEndian && g_BitsAllocated == 16) {
                        uint16_t* p = (uint16_t*)g_PixelData;
                        size_t count = length / 2;
                        for (size_t i = 0; i < count; i++) p[i] = Swap16(p[i]);
                    }
                }
                offset += length;
                break;
            }
        }

        offset += length;
    }

    return (g_PixelData != NULL && g_ImgWidth > 0 && g_ImgHeight > 0);
}

/* Master Load function supporting all formats */
bool LoadDicom(HWND hwnd, const wchar_t* filepath) {
    FILE* f = _wfopen(filepath, L"rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize < 16) { fclose(f); return false; }

    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) { fclose(f); return false; }

    if (fread(buf, 1, fileSize, f) != (size_t)fileSize) {
        free(buf);
        fclose(f);
        return false;
    }
    fclose(f);

    bool loaded = false;
    if (TryLoadGEGenesis(buf, fileSize, filepath)) loaded = true;
    else if (TryLoadSiemens(buf, fileSize, filepath)) loaded = true;
    else if (TryLoadPGM(buf, fileSize, filepath)) loaded = true;
    else if (TryLoadDicom(buf, fileSize, filepath)) loaded = true;

    if (!loaded) {
        MessageBoxW(hMainWnd, 
                    L"Unsupported image format.\n(Checked DICOM Part 10, Preamble-less DICOM, Big-Endian DICOM, GE Genesis/Signa, Siemens Somatom/Magnetom, Netpbm).", 
                    L"Error", MB_ICONERROR);
        free(buf);
        return false;
    }

    SetWindowTextW(hEditHeader, g_DicomHeaderStr ? g_DicomHeaderStr : L"");

    /* Default Window Center/Width if absent from tags */
    if (g_WinWidth <= 0) {
        AutoLevelROI();
        g_DefWinCenter = g_WinCenter;
        g_DefWinWidth = g_WinWidth;
    } else {
        g_DefWinCenter = g_WinCenter;
        g_DefWinWidth = g_WinWidth;
    }

    /* Initialize ROI Box to image center */
    g_RoiW = g_ImgWidth / 4;
g_RoiH = g_ImgHeight / 4;
    if (g_RoiW < 50) g_RoiW = 50;
    if (g_RoiH < 50) g_RoiH = 50;
    if (g_RoiW > g_ImgWidth) g_RoiW = g_ImgWidth;
    if (g_RoiH > g_ImgHeight) g_RoiH = g_ImgHeight;
    g_RoiX = (g_ImgWidth - g_RoiW) / 2;
    g_RoiY = (g_ImgHeight - g_RoiH) / 2;

    ResetView(hwnd);
    UpdateRenderBuffer();
    free(buf);
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            InitCommonControls();
            
            hEditHeader = CreateWindowW(L"EDIT", L"Drag & Drop a medical image / DICOM file here...",
                WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_HEADER, NULL, NULL);
            
            DragAcceptFiles(hwnd, TRUE);
            break;
        }

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (g_ShowHeader) {
                MoveWindow(hEditHeader, w - 350, 0, 350, h, TRUE);
            }
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            wchar_t filepath[MAX_PATH];
            if (DragQueryFileW(hDrop, 0, filepath, MAX_PATH)) {
                LoadDicom(hwnd, filepath);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            DragFinish(hDrop);
            break;
        }

        case WM_KEYDOWN: {
            if (!g_PixelData) break;
            bool update = true;
            switch (wParam) {
                case '1': g_WinCenter = g_DefWinCenter; g_WinWidth = g_DefWinWidth; break;
                case '2': g_WinCenter = -600; g_WinWidth = 1500; break; /* Lung */
                case '3': g_WinCenter = 300; g_WinWidth = 1500; break;  /* Bone */
                case '4': g_WinCenter = 40; g_WinWidth = 80; break;     /* Brain */
                case '5': g_WinCenter = 50; g_WinWidth = 400; break;    /* Abdomen */
                case 'B': {
                    g_BoxMode = !g_BoxMode;
                    if (g_BoxMode) AutoLevelROI();
                    break;
                }
                case 'H': {
                    g_ShowHeader = !g_ShowHeader;
                    ShowWindow(hEditHeader, g_ShowHeader ? SW_SHOW : SW_HIDE);
                    RECT rect; GetClientRect(hwnd, &rect);
                    SendMessage(hwnd, WM_SIZE, 0, MAKELPARAM(rect.right, rect.bottom));
                    break;
                }
                case 'R': {
                    ResetView(hwnd);
                    break;
                }
                default: update = false; break;
            }
            if (update) {
                UpdateRenderBuffer();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            if (g_RenderBuffer == NULL) break;
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            float zoomFactor = (zDelta > 0) ? 1.1f : 0.9f;
            
            int w = (int)((float)g_ImgWidth * g_Zoom);
            int h = (int)((float)g_ImgHeight * g_Zoom);
            g_PanX -= (int)(((float)w * zoomFactor - (float)w) / 2.0f);
            g_PanY -= (int)(((float)h * zoomFactor - (float)h) / 2.0f);
            
            g_Zoom *= zoomFactor;
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (g_BoxMode) {
                int sx = (int)(g_RoiX * g_Zoom) + g_PanX;
                int sy = (int)(g_RoiY * g_Zoom) + g_PanY;
                int sw = (int)(g_RoiW * g_Zoom);
                int sh = (int)(g_RoiH * g_Zoom);

                if (mx >= sx && mx <= sx + sw && my >= sy && my <= sy + sh) {
                    g_IsDraggingBox = true;
                    g_DragBoxOffset.x = mx - sx;
                    g_DragBoxOffset.y = my - sy;
                    SetCapture(hwnd);
                    break;
                }
            }

            g_IsPanning = true;
            g_LastMouse.x = mx;
            g_LastMouse.y = my;
            SetCapture(hwnd);
            break;
        }

        case WM_RBUTTONDOWN: {
            g_IsWindowLevelling = true;
            g_LastMouse.x = GET_X_LPARAM(lParam);
            g_LastMouse.y = GET_Y_LPARAM(lParam);
            SetCapture(hwnd);
            break;
        }

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (g_IsDraggingBox) {
                int newSx = mx - g_DragBoxOffset.x;
                int newSy = my - g_DragBoxOffset.y;
                g_RoiX = (int)((newSx - g_PanX) / g_Zoom);
                g_RoiY = (int)((newSy - g_PanY) / g_Zoom);
                
                if (g_RoiX < 0) g_RoiX = 0;
                if (g_RoiY < 0) g_RoiY = 0;
                if (g_RoiX + g_RoiW > g_ImgWidth) g_RoiX = g_ImgWidth - g_RoiW;
                if (g_RoiY + g_RoiH > g_ImgHeight) g_RoiY = g_ImgHeight - g_RoiH;

                AutoLevelROI();
                UpdateRenderBuffer();
                InvalidateRect(hwnd, NULL, FALSE);

            } else if (g_IsPanning) {
                g_PanX += (mx - g_LastMouse.x);
                g_PanY += (my - g_LastMouse.y);
                g_LastMouse.x = mx;
                g_LastMouse.y = my;
                InvalidateRect(hwnd, NULL, FALSE);

            } else if (g_IsWindowLevelling) {
                int dx = mx - g_LastMouse.x;
                int dy = my - g_LastMouse.y;
                
                g_WinWidth += dx * 2;
                g_WinCenter += dy * 2;
                if (g_WinWidth < 1) g_WinWidth = 1;

                g_LastMouse.x = mx;
                g_LastMouse.y = my;
                UpdateRenderBuffer();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONUP: {
            g_IsPanning = false;
            g_IsDraggingBox = false;
            ReleaseCapture();
            break;
        }

        case WM_RBUTTONUP: {
            g_IsWindowLevelling = false;
            ReleaseCapture();
            break;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            if (g_ShowHeader) clientRect.right -= 350;
            FillRect(hdc, &clientRect, (HBRUSH)(COLOR_WINDOW + 2));

            if (g_RenderBuffer != NULL) {
                BITMAPINFO bmi = {0};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = g_ImgWidth;
                bmi.bmiHeader.biHeight = -g_ImgHeight; /* Top-down */
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchDIBits(hdc, 
                    g_PanX, g_PanY, (int)((float)g_ImgWidth * g_Zoom), (int)((float)g_ImgHeight * g_Zoom),
                    0, 0, g_ImgWidth, g_ImgHeight, 
                    g_RenderBuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);

                if (g_BoxMode) {
                    int sx = (int)(g_RoiX * g_Zoom) + g_PanX;
                    int sy = (int)(g_RoiY * g_Zoom) + g_PanY;
                    int sw = (int)(g_RoiW * g_Zoom);
                    int sh = (int)(g_RoiH * g_Zoom);

                    HBRUSH hNullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
                    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 0));
                    HGDIOBJ oldBrush = SelectObject(hdc, hNullBrush);
                    HGDIOBJ oldPen = SelectObject(hdc, hPen);

                    Rectangle(hdc, sx, sy, sx + sw, sy + sh);

                    SelectObject(hdc, oldBrush);
                    SelectObject(hdc, oldPen);
                    DeleteObject(hPen);
                }

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(0, 255, 0));
                
                wchar_t hudText[512];
                swprintf(hudText, 512, 
                         L"WW: %d | WC: %d | %dx%d (%d-bit %ls)\n\n"
                         L"Shortcuts:\n"
                         L"1-5: Window Presets\n"
                         L"R-Drag: Window/Level\n"
                         L"L-Drag: Pan\n"
                         L"Scroll: Zoom\n"
                         L"B: Toggle ROI Box WL\n"
                         L"H: Toggle Header Sidebar\n"
                         L"R: Reset View", 
                         g_WinWidth, g_WinCenter, g_ImgWidth, g_ImgHeight, g_BitsAllocated,
                         g_PixelRepresentation ? L"Signed" : L"Unsigned");
                
                RECT textRect = {10, 10, 350, 320};
                DrawTextW(hdc, hudText, -1, &textRect, DT_LEFT | DT_TOP);
            }

            EndPaint(hwnd, &ps);
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DESTROY:
            if (g_PixelData) free(g_PixelData);
            if (g_RenderBuffer) free(g_RenderBuffer);
            if (g_DicomHeaderStr) free(g_DicomHeaderStr);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"DicomViewerClass";

    WNDCLASSW wc = {0};
wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    hMainWnd = CreateWindowExW(
        0, CLASS_NAME, L"Native Win32 Multi-Protocol DICOM Viewer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        NULL, NULL, hInstance, NULL);

    if (hMainWnd == NULL) return 0;

    ShowWindow(hMainWnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}