/* ============================================================================
 * receipts_pdf - Lightweight PDF Generator for webp
 * COMPILATION: gcc -Os -s receipts_pdf.c -o receipts_pdf.exe -lole32
 * 2026 Hugo
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* --- Dynamic GDI+ Flat API --- */
typedef struct {
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

static HMODULE hGdiPlus = NULL;
static int (WINAPI *pGdiplusStartup)(ULONG_PTR*, const GdiplusStartupInput*, void*) = NULL;
static void (WINAPI *pGdiplusShutdown)(ULONG_PTR) = NULL;
static int (WINAPI *pGdipLoadImageFromFile)(const WCHAR*, void**) = NULL;
static int (WINAPI *pGdipDisposeImage)(void*) = NULL;
static int (WINAPI *pGdipGetImageWidth)(void*, UINT*) = NULL;
static int (WINAPI *pGdipGetImageHeight)(void*, UINT*) = NULL;
static int (WINAPI *pGdipSaveImageToStream)(void*, IStream*, const CLSID*, const void*) = NULL;

const GUID CLSID_ImageFormatJPEG = {0x557cf401,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};

void InitGDIPlus() {
    hGdiPlus = LoadLibraryA("gdiplus.dll");
    if (!hGdiPlus) return;
    pGdiplusStartup = (void*)GetProcAddress(hGdiPlus, "GdiplusStartup");
    pGdiplusShutdown = (void*)GetProcAddress(hGdiPlus, "GdiplusShutdown");
    pGdipLoadImageFromFile = (void*)GetProcAddress(hGdiPlus, "GdipLoadImageFromFile");
    pGdipDisposeImage = (void*)GetProcAddress(hGdiPlus, "GdipDisposeImage");
    pGdipGetImageWidth = (void*)GetProcAddress(hGdiPlus, "GdipGetImageWidth");
    pGdipGetImageHeight = (void*)GetProcAddress(hGdiPlus, "GdipGetImageHeight");
    pGdipSaveImageToStream = (void*)GetProcAddress(hGdiPlus, "GdipSaveImageToStream");
}

/* --- PDF Structures & Engine --- */
#define MAX_PAGES 500
#define MAX_IMAGES 2000
#define MAX_PDF_OBJS 8192

long pdf_offsets[MAX_PDF_OBJS];
int next_obj = 5; // 1:Cat, 2:Info, 3:Pages, 4:Font

typedef struct {
    int obj_id;
    int contents_obj_id;
    int images[100];
    int img_count;
    char stream_buf[65536];
    int stream_len;
} PdfPage;

typedef struct {
    int obj_id;
    int width, height;
    unsigned char* data;
    size_t size;
} PdfImage;

PdfPage pages[MAX_PAGES];
int page_cnt = 0;
PdfImage images[MAX_IMAGES];
int image_cnt = 0;
double cur_y = 756.0;
PdfPage* cur_page = NULL;

void begin_obj(FILE* f, int id) {
    pdf_offsets[id] = ftell(f);
    fprintf(f, "%d 0 obj\n", id);
}

void end_obj(FILE* f) {
    fprintf(f, "endobj\n");
}

void new_page() {
    if (page_cnt >= MAX_PAGES) return;
    cur_page = &pages[page_cnt++];
    cur_page->obj_id = next_obj++;
    cur_page->contents_obj_id = next_obj++;
    cur_page->img_count = 0;
    cur_page->stream_len = 0;
    cur_y = 756.0;
}

void add_stream(PdfPage* p, const char* fmt, ...) {
    char buf[1024]; va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (p->stream_len + n < (int)sizeof(p->stream_buf)) {
        memcpy(p->stream_buf + p->stream_len, buf, n);
        p->stream_len += n;
    }
}

void write_pdf_text(char* dst, const char* src) {
    *dst++ = '(';
    while (*src) {
        if (*src == '(' || *src == ')' || *src == '\\') *dst++ = '\\';
        *dst++ = *src++;
    }
    *dst++ = ')';
    *dst = '\0';
}

/* --- Main Entry --- */
int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: receipts_pdf.exe <output.pdf> <input_script.txt>\n");
        return 1;
    }

    InitGDIPlus();
    if (!hGdiPlus) return 1;

    ULONG_PTR gdiToken;
    GdiplusStartupInput gdiInput = {1, NULL, FALSE, FALSE};
    pGdiplusStartup(&gdiToken, &gdiInput, NULL);

    FILE* fin = fopen(argv[2], "r");
    if (!fin) return 1;

    new_page();
    char line[2048];

    while (fgets(line, sizeof(line), fin)) {
        char* p = strpbrk(line, "\r\n");
        if (p) *p = '\0';

        if (strncmp(line, "TXT|", 4) == 0) {
            if (cur_y - 16 < 36.0) new_page();
            cur_y -= 16.0;
            char escaped[2048];
            write_pdf_text(escaped, line + 4);
            add_stream(cur_page, "BT /F1 12 Tf 36.00 %.2f Td %s Tj ET\n", cur_y, escaped);
        } 
        else if (strncmp(line, "SPC|", 4) == 0) {
            cur_y -= atof(line + 4);
            if (cur_y < 36.0) new_page();
        } 
        else if (strncmp(line, "IMG|", 4) == 0) {
            WCHAR wpath[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, line + 4, -1, wpath, MAX_PATH);
            void* img = NULL;
            if (pGdipLoadImageFromFile(wpath, &img) == 0 && img) {
                UINT w = 0, h = 0;
                pGdipGetImageWidth(img, &w);
                pGdipGetImageHeight(img, &h);
                if (w > 0 && h > 0) {
                    IStream* pStream = NULL;
                    CreateStreamOnHGlobal(NULL, TRUE, &pStream);
                    if (pGdipSaveImageToStream(img, pStream, &CLSID_ImageFormatJPEG, NULL) == 0) {
                        STATSTG stg;
                        pStream->lpVtbl->Stat(pStream, &stg, STATFLAG_NONAME);
                        size_t sz = stg.cbSize.QuadPart;
                        unsigned char* buf = malloc(sz);
                        LARGE_INTEGER li = {0};
                        pStream->lpVtbl->Seek(pStream, li, STREAM_SEEK_SET, NULL);
                        ULONG read = 0;
                        pStream->lpVtbl->Read(pStream, buf, (ULONG)sz, &read);

                        images[image_cnt].obj_id = next_obj++;
                        images[image_cnt].width = w;
                        images[image_cnt].height = h;
                        images[image_cnt].data = buf;
                        images[image_cnt].size = sz;

                        double scale = 1.0;
                        if (w > 540.0) scale = 540.0 / w;
                        if (h * scale > 380.0) scale = 380.0 / h; // Fit at least 1-2 receipts
                        double dw = w * scale;
                        double dh = h * scale;

                        if (cur_y - dh < 36.0) new_page();
                        cur_y -= dh;

                        cur_page->images[cur_page->img_count++] = image_cnt;
                        add_stream(cur_page, "q\n%.2f 0 0 %.2f 36.00 %.2f cm\n/Img%d Do\nQ\n", dw, dh, cur_y, image_cnt);
                        image_cnt++;
                    }
                    pStream->lpVtbl->Release(pStream);
                }
                pGdipDisposeImage(img);
            }
        }
    }
    fclose(fin);

    /* Construct the Final PDF */
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "%%PDF-1.4\n");
    
    begin_obj(f, 1);
    fprintf(f, "<< /Type /Catalog /Pages 3 0 R >>\n");
    end_obj(f);

    begin_obj(f, 2);
    fprintf(f, "<< /Creator (DRA-Receipts) >>\n");
    end_obj(f);

    begin_obj(f, 3);
    fprintf(f, "<< /Type /Pages /Count %d /Kids [ ", page_cnt);
    for (int i = 0; i < page_cnt; i++) fprintf(f, "%d 0 R ", pages[i].obj_id);
    fprintf(f, "] >>\n");
    end_obj(f);

    begin_obj(f, 4);
    fprintf(f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n");
    end_obj(f);

    for (int i = 0; i < page_cnt; i++) {
        begin_obj(f, pages[i].obj_id);
        fprintf(f, "<< /Type /Page /Parent 3 0 R /MediaBox [0 0 612 792]\n");
        fprintf(f, "/Contents %d 0 R\n", pages[i].contents_obj_id);
        fprintf(f, "/Resources << /Font << /F1 4 0 R >> ");
        if (pages[i].img_count > 0) {
            fprintf(f, "/XObject << ");
            for (int j = 0; j < pages[i].img_count; j++) {
                fprintf(f, "/Img%d %d 0 R ", pages[i].images[j], images[pages[i].images[j]].obj_id);
            }
            fprintf(f, ">> ");
        }
        fprintf(f, ">> >>\n");
        end_obj(f);

        begin_obj(f, pages[i].contents_obj_id);
        fprintf(f, "<< /Length %d >>\nstream\n", pages[i].stream_len);
        fwrite(pages[i].stream_buf, 1, pages[i].stream_len, f);
        fprintf(f, "\nendstream\n");
        end_obj(f);
    }

    for (int i = 0; i < image_cnt; i++) {
        begin_obj(f, images[i].obj_id);
        fprintf(f, "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length %zu >>\nstream\n",
                images[i].width, images[i].height, images[i].size);
        fwrite(images[i].data, 1, images[i].size, f);
        fprintf(f, "\nendstream\n");
        end_obj(f);
        free(images[i].data);
    }

    long xref_pos = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", next_obj);
    for (int i = 1; i < next_obj; i++) fprintf(f, "%010ld 00000 n \n", pdf_offsets[i]);
    
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R /Info 2 0 R >>\nstartxref\n%ld\n%%%%EOF\n", next_obj, xref_pos);
    fclose(f);

    pGdiplusShutdown(gdiToken);
    FreeLibrary(hGdiPlus);
    return 0;
}