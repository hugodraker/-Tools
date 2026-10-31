/* ============================================================================
 * AI LLAMA test 02 - OpenCL GPU Accelerated + Native Bubble Text Selection
 *
 * COMPILATION INSTRUCTIONS (64-bit GCC/MinGW):
 *   gcc "ai02.c" -Os -s -o "ai02.exe" -mwindows -Wl,--large-address-aware -lgdi32 -lshell32 -lcomdlg32 -lm
 *
 *  2026 HUGO
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdarg.h>
#include <time.h>
#define MAX_MSGS     500
#define ID_EDIT      101
#define ID_COMBO     102
#define IDM_COPY_SEL 2001
#define IDM_COPY_ALL 2002
#define IDM_SEL_ALL  2003
#define IDM_SAVE_PDF     2004
#define IDM_CLEAR_CHAT   2005
#define IDM_WIPE_MEMORY  2006
#define IDM_SET_COLOR_U  2007
#define IDM_SET_COLOR_AI 2008
#define MSG_USER    0
#define MSG_AI      1
#define MSG_SYS_OK  2
#define MSG_SYS_ERR 3
#define WM_UPDATE_CHAT (WM_APP + 1)
COLORREF color_user = RGB(10, 132, 255);
COLORREF color_ai = RGB(229, 229, 234);
COLORREF color_sys_ok = RGB(52, 199, 89);
COLORREF color_sys_err = RGB(255, 59, 48);

// --- Dynamic LLaMA Architecture Variables ---
int dim = 0;
int hidden_dim = 0;
int n_layers = 0;
int n_heads = 0;
int n_kv_heads = 0;
int vocab_size = 0;
int head_dim = 0;
int kv_dim = 0;
float rope_theta = 10000.0f;

// --- Configurable INI Parameters (with defaults matching original code) ---
int   param_max_gen_tokens = 80;
int   param_max_seq_len    = 512;
float param_temperature    = 0.0f;
float param_top_p          = 1.0f;
int   param_top_k          = 0;
float param_repeat_penalty = 1.0f;
float param_rope_theta     = 0.0f;

int use_gpu = 1;
volatile int is_generating = 0;
volatile int is_loading_model = 0;
char last_error_msg[1024] = "";
uint64_t safe_tensors_data_size = 0;

// --- Memory Tracker for Load-Time Dequantization ---
void* allocated_ptrs[4000];
int ptr_count = 0;

// --- Tokenizer Data ---
typedef struct { char* str; int id; } Token;
Token* vocab_map = NULL;
int current_vocab_size = 0;
int uses_g_prefix = 0;

// --- Neural Network Weights ---
float *tok_emb = NULL, *output_norm_w = NULL, *lm_head_w = NULL;

typedef struct {
    float *wq, *wk, *wv, *wo;
    float *ffn_gate, *ffn_down, *ffn_up;
    float *attn_norm, *ffn_norm;
} LlamaLayer;

LlamaLayer *layers = NULL;

// --- Forward Declarations to Prevent Implicit Declaration Errors ---
float* fetch_tensor(const char* name, size_t elements);
float  fp16_to_fp32(uint16_t h);
void   rmsnorm(float* o, float* x, float* weight, int size);
void   matmul(float* xout, float* x, float* w, int n, int d);
void   apply_rope(float* q, float* k, int pos);
void   swiglu(float* out, float* gate, float* up, int size);
int    find_token_id(const char* str);
int    encode_text_segment(const char* text, int* tokens, int count, int max_tokens);
void   detokenize_bpe(const char* src, char* dst);
void   probe_gpu_hardware(void);
void   load_ini_parameters(void);

// ============================================================================
// ZERO-DEPENDENCY DYNAMIC OPENCL GPU ENGINE
// ============================================================================
typedef int32_t  cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef void*    cl_platform_id;
typedef void*    cl_device_id;
typedef void*    cl_context;
typedef void*    cl_command_queue;
typedef void*    cl_mem;
typedef void*    cl_program;
typedef void*    cl_kernel;

#define CL_SUCCESS               0
#define CL_DEVICE_TYPE_GPU       (1 << 2)
#define CL_DEVICE_TYPE_ALL       0xFFFFFFFF
#define CL_DEVICE_NAME           0x102B
#define CL_MEM_READ_ONLY         (1 << 2)
#define CL_MEM_WRITE_ONLY        (1 << 1)
#define CL_MEM_COPY_HOST_PTR     (1 << 5)
#define CL_TRUE                  1
typedef cl_int           (WINAPI *pfn_clGetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
typedef cl_int           (WINAPI *pfn_clGetDeviceIDs)(cl_platform_id, cl_ulong, cl_uint, cl_device_id*, cl_uint*);
typedef cl_int           (WINAPI *pfn_clGetDeviceInfo)(cl_device_id, cl_uint, size_t, void*, size_t*);
typedef cl_context       (WINAPI *pfn_clCreateContext)(const void*, cl_uint, const cl_device_id*, void*, void*, cl_int*);
typedef cl_command_queue (WINAPI *pfn_clCreateCommandQueue)(cl_context, cl_device_id, cl_ulong, cl_int*);
typedef cl_mem           (WINAPI *pfn_clCreateBuffer)(cl_context, cl_ulong, size_t, void*, cl_int*);
typedef cl_int           (WINAPI *pfn_clEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, const void*, cl_uint, const void*, void*);
typedef cl_int           (WINAPI *pfn_clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_uint, size_t, size_t, void*, cl_uint, const void*, void*);
typedef cl_program       (WINAPI *pfn_clCreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
typedef cl_int           (WINAPI *pfn_clBuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*);
typedef cl_kernel        (WINAPI *pfn_clCreateKernel)(cl_program, const char*, cl_int*);
typedef cl_int           (WINAPI *pfn_clSetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
typedef cl_int           (WINAPI *pfn_clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, const size_t*, cl_uint, const void*, void*);
typedef cl_int           (WINAPI *pfn_clFinish)(cl_command_queue);
typedef cl_int           (WINAPI *pfn_clReleaseMemObject)(cl_mem);

HMODULE hOpenCL = NULL;
pfn_clGetPlatformIDs          f_clGetPlatformIDs = NULL;
pfn_clGetDeviceIDs            f_clGetDeviceIDs = NULL;
pfn_clGetDeviceInfo           f_clGetDeviceInfo = NULL;
pfn_clCreateContext           f_clCreateContext = NULL;
pfn_clCreateCommandQueue      f_clCreateCommandQueue = NULL;
pfn_clCreateBuffer            f_clCreateBuffer = NULL;
pfn_clEnqueueWriteBuffer      f_clEnqueueWriteBuffer = NULL;
pfn_clEnqueueReadBuffer       f_clEnqueueReadBuffer = NULL;
pfn_clCreateProgramWithSource f_clCreateProgramWithSource = NULL;
pfn_clBuildProgram            f_clBuildProgram = NULL;
pfn_clCreateKernel            f_clCreateKernel = NULL;
pfn_clSetKernelArg            f_clSetKernelArg = NULL;
pfn_clEnqueueNDRangeKernel    f_clEnqueueNDRangeKernel = NULL;
pfn_clFinish                  f_clFinish = NULL;
pfn_clReleaseMemObject        f_clReleaseMemObject = NULL;

cl_context       gpu_ctx = NULL;
cl_command_queue gpu_queue = NULL;
cl_program       gpu_prog = NULL;
cl_kernel        gpu_kernel = NULL;
cl_mem           gpu_buf_x = NULL;
cl_mem           gpu_buf_out = NULL;
size_t           gpu_max_x_bytes = 0;
size_t           gpu_max_out_bytes = 0;
int              gpu_ready = 0;
char             gpu_device_name[256] = "None";

typedef struct { const float* host_ptr; cl_mem dev_buf; size_t bytes; } GpuWeightEntry;
GpuWeightEntry gpu_weight_cache[512];
int gpu_weight_count = 0;
int gpu_vram_exhausted = 0;

const char* opencl_kernel_src =
    "__kernel void matvec_f32(__global const float* x, __global const float* w, __global float* out, int n, int d) {\n"
    "    int i = get_global_id(0);\n"
    "    if (i >= d) return;\n"
    "    float sum = 0.0f;\n"
    "    int row_off = i * n;\n"
    "    int j = 0;\n"
    "    for (; j + 3 < n; j += 4) {\n"
    "        sum += w[row_off + j] * x[j]\n"
    "             + w[row_off + j + 1] * x[j + 1]\n"
    "             + w[row_off + j + 2] * x[j + 2]\n"
    "             + w[row_off + j + 3] * x[j + 3];\n"
    "    }\n"
    "    for (; j < n; j++) sum += w[row_off + j] * x[j];\n"
    "    out[i] = sum;\n"
    "}\n";

void free_gpu_weights(void) {
    if (f_clReleaseMemObject) {
        for (int i = 0; i < gpu_weight_count; i++) {
            if (gpu_weight_cache[i].dev_buf) f_clReleaseMemObject(gpu_weight_cache[i].dev_buf);
        }
        if (gpu_buf_x) { f_clReleaseMemObject(gpu_buf_x); gpu_buf_x = NULL; gpu_max_x_bytes = 0; }
        if (gpu_buf_out) { f_clReleaseMemObject(gpu_buf_out); gpu_buf_out = NULL; gpu_max_out_bytes = 0; }
    }
    gpu_weight_count = 0;
    gpu_vram_exhausted = 0;
}

int init_opencl_gpu(void) {
    if (gpu_ready) return 1;
    if (!hOpenCL) hOpenCL = LoadLibraryA("OpenCL.dll");
    if (!hOpenCL) return 0;

    f_clGetPlatformIDs          = (pfn_clGetPlatformIDs)GetProcAddress(hOpenCL, "clGetPlatformIDs");
    f_clGetDeviceIDs            = (pfn_clGetDeviceIDs)GetProcAddress(hOpenCL, "clGetDeviceIDs");
    f_clGetDeviceInfo           = (pfn_clGetDeviceInfo)GetProcAddress(hOpenCL, "clGetDeviceInfo");
    f_clCreateContext           = (pfn_clCreateContext)GetProcAddress(hOpenCL, "clCreateContext");
    f_clCreateCommandQueue      = (pfn_clCreateCommandQueue)GetProcAddress(hOpenCL, "clCreateCommandQueue");
    f_clCreateBuffer            = (pfn_clCreateBuffer)GetProcAddress(hOpenCL, "clCreateBuffer");
    f_clEnqueueWriteBuffer      = (pfn_clEnqueueWriteBuffer)GetProcAddress(hOpenCL, "clEnqueueWriteBuffer");
    f_clEnqueueReadBuffer       = (pfn_clEnqueueReadBuffer)GetProcAddress(hOpenCL, "clEnqueueReadBuffer");
    f_clCreateProgramWithSource = (pfn_clCreateProgramWithSource)GetProcAddress(hOpenCL, "clCreateProgramWithSource");
    f_clBuildProgram            = (pfn_clBuildProgram)GetProcAddress(hOpenCL, "clBuildProgram");
    f_clCreateKernel            = (pfn_clCreateKernel)GetProcAddress(hOpenCL, "clCreateKernel");
    f_clSetKernelArg            = (pfn_clSetKernelArg)GetProcAddress(hOpenCL, "clSetKernelArg");
    f_clEnqueueNDRangeKernel    = (pfn_clEnqueueNDRangeKernel)GetProcAddress(hOpenCL, "clEnqueueNDRangeKernel");
    f_clFinish                  = (pfn_clFinish)GetProcAddress(hOpenCL, "clFinish");
    f_clReleaseMemObject        = (pfn_clReleaseMemObject)GetProcAddress(hOpenCL, "clReleaseMemObject");

    if (!f_clGetPlatformIDs || !f_clGetDeviceIDs || !f_clCreateContext || !f_clCreateCommandQueue ||
        !f_clCreateBuffer || !f_clCreateProgramWithSource || !f_clBuildProgram || !f_clCreateKernel ||
        !f_clSetKernelArg || !f_clEnqueueNDRangeKernel || !f_clEnqueueReadBuffer || !f_clEnqueueWriteBuffer) {
        return 0;
    }

    cl_platform_id platforms[8];
    cl_uint num_platforms = 0;
    if (f_clGetPlatformIDs(8, platforms, &num_platforms) != CL_SUCCESS || num_platforms == 0) return 0;

    cl_device_id dev = NULL;
    for (cl_uint p = 0; p < num_platforms && !dev; p++) {
        cl_uint num_devs = 0;
        if (f_clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 1, &dev, &num_devs) == CL_SUCCESS && num_devs > 0) break;
    }
    if (!dev) {
        for (cl_uint p = 0; p < num_platforms && !dev; p++) {
            cl_uint num_devs = 0;
            if (f_clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 1, &dev, &num_devs) == CL_SUCCESS && num_devs > 0) break;
        }
    }
    if (!dev) return 0;

    if (f_clGetDeviceInfo) {
        f_clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(gpu_device_name) - 1, gpu_device_name, NULL);
    }

    cl_int err = 0;
    gpu_ctx = f_clCreateContext(NULL, 1, &dev, NULL, NULL, &err);
    if (!gpu_ctx || err != CL_SUCCESS) return 0;

    gpu_queue = f_clCreateCommandQueue(gpu_ctx, dev, 0, &err);
    if (!gpu_queue || err != CL_SUCCESS) return 0;

    size_t src_len = strlen(opencl_kernel_src);
    gpu_prog = f_clCreateProgramWithSource(gpu_ctx, 1, &opencl_kernel_src, &src_len, &err);
    if (!gpu_prog || err != CL_SUCCESS) return 0;

    if (f_clBuildProgram(gpu_prog, 1, &dev, "-cl-fast-relaxed-math", NULL, NULL) != CL_SUCCESS) return 0;

    gpu_kernel = f_clCreateKernel(gpu_prog, "matvec_f32", &err);
    if (!gpu_kernel || err != CL_SUCCESS) return 0;

    gpu_ready = 1;
    return 1;
}

cl_mem get_or_upload_gpu_weight(const float* w, size_t bytes) {
    for (int i = 0; i < gpu_weight_count; i++) {
        if (gpu_weight_cache[i].host_ptr == w) return gpu_weight_cache[i].dev_buf;
    }
    if (gpu_vram_exhausted || gpu_weight_count >= 512) return NULL;

    cl_int err = 0;
    cl_mem buf = f_clCreateBuffer(gpu_ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, bytes, (void*)w, &err);
    if (!buf || err != CL_SUCCESS) {
        gpu_vram_exhausted = 1;
        return NULL;
    }
    gpu_weight_cache[gpu_weight_count].host_ptr = w;
    gpu_weight_cache[gpu_weight_count].dev_buf = buf;
    gpu_weight_cache[gpu_weight_count].bytes = bytes;
    gpu_weight_count++;
    return buf;
}

typedef struct {
    char text[262144];
    int msg_type;
    RECT hit_box;
    RECT text_box;
} ChatMsg;
ChatMsg chat_log[MAX_MSGS];
int msg_count = 0;
int scroll_y = 0;
HWND hMainWindow;
HWND hChatView;
HWND hEdit;
HWND hCombo;
HFONT hChatFont = NULL;
WNDPROC OldEditProc;
char ini_path[MAX_PATH];
char current_model_dir[MAX_PATH];
int total_height = 0;
int visible_space = 0;

int sel_bubble_idx = -1;
int sel_anchor_char = -1;
int sel_caret_char = -1;
int is_mouse_selecting = 0;

HANDLE hFile_ptr = INVALID_HANDLE_VALUE;
HANDLE hMap_ptr = NULL;
char* file_map_ptr = NULL;
char* safe_tensors_data = NULL;
char* json_header = NULL;

volatile int stop_generation = 0;

#define FETCH_TENS(ptr, name, elems) do { \
    ptr = fetch_tensor(name, elems); \
    if (!ptr) { \
        if (strncmp(last_error_msg, "Format", 6) != 0 && strncmp(last_error_msg, "Memory", 6) != 0) { \
            sprintf(last_error_msg, "Architecture Error: Missing '%s'. Engine strictly requires single-file LLaMA standard architecture.", name); \
        } \
        return 0; \
    } \
} while(0)
#define IDM_STOP_OUTPUT 2009
float fp16_to_fp32(uint16_t h) {
    uint32_t sign = (h >> 15) & 1;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    if (exp == 0 || exp == 31) return 0.0f;
    exp = exp + (127 - 15);
    uint32_t f = (sign << 31) | (exp << 23) | (mant << 13);
    float res;
    memcpy(&res, &f, sizeof(float));
    return res;
}

void copy_range_to_clipboard(HWND hwnd, const char* src, int start_idx, int end_idx) {
    if (!src || start_idx < 0 || end_idx <= start_idx) return;
    int len = (int)strlen(src);
    if (start_idx >= len) return;
    if (end_idx > len) end_idx = len;

    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    int slice_len = end_idx - start_idx;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (size_t)slice_len * 2 + 2);
    if (hg) {
        char* dst = (char*)GlobalLock(hg);
        int j = 0;
        for (int i = start_idx; i < end_idx; i++) {
            if (src[i] == '\n' && (i == start_idx || src[i - 1] != '\r')) dst[j++] = '\r';
            dst[j++] = src[i];
        }
        dst[j] = '\0';
        GlobalUnlock(hg);
        SetClipboardData(CF_TEXT, hg);
    }
    CloseClipboard();
}

void add_message(const char* text, int msg_type) {
    if (msg_count < MAX_MSGS) {
        strncpy(chat_log[msg_count].text, text, sizeof(chat_log[msg_count].text) - 1);
        chat_log[msg_count].text[sizeof(chat_log[msg_count].text) - 1] = '\0';
        chat_log[msg_count].msg_type = msg_type;
        SetRect(&chat_log[msg_count].hit_box, 0, 0, 0, 0);
        SetRect(&chat_log[msg_count].text_box, 0, 0, 0, 0);
        msg_count++;
    }
    scroll_y = -999999;
    PostMessage(hMainWindow, WM_UPDATE_CHAT, 0, 0);
}

void update_last_message(const char* append_text) {
    if (msg_count > 0) {
        size_t cur_len = strlen(chat_log[msg_count - 1].text);
        size_t app_len = strlen(append_text);
        if (cur_len + app_len < sizeof(chat_log[msg_count - 1].text) - 1) {
            memcpy(chat_log[msg_count - 1].text + cur_len, append_text, app_len + 1);
        }
        scroll_y = -999999;
        PostMessage(hMainWindow, WM_UPDATE_CHAT, 0, 0);
    }
}

// ============================================================================
// GDI WORD-WRAP LAYOUT & CHARACTER HIT-TESTING ENGINE FOR BUBBLE SELECTION
// ============================================================================
typedef struct {
    int start_idx;
    int len;
    int y_top;
    int line_height;
} WrappedLine;

int compute_wrapped_lines(HDC hdc, const char* text, int max_width, WrappedLine* out_lines, int max_lines) {
    int line_count = 0;
    int len = (int)strlen(text);
    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);
    int lh = tm.tmHeight;
    int y = 0;
    int pos = 0;

    if (len == 0) {
        if (out_lines && max_lines > 0) {
            out_lines[0] = (WrappedLine){0, 0, 0, lh};
        }
        return 1;
    }

    while (pos < len) {
        int hard_nl = pos;
        while (hard_nl < len && text[hard_nl] != '\n' && text[hard_nl] != '\r') hard_nl++;
        int seg_len = hard_nl - pos;

        if (seg_len == 0) {
            if (out_lines && line_count < max_lines) {
                out_lines[line_count] = (WrappedLine){pos, 0, y, lh};
            }
            line_count++;
            y += lh;
            if (text[pos] == '\r' && pos + 1 < len && text[pos + 1] == '\n') pos += 2;
            else pos++;
            continue;
        }

        int fit_chars = 0;
        SIZE sz;
        GetTextExtentExPointA(hdc, text + pos, seg_len, max_width, &fit_chars, NULL, &sz);
        if (fit_chars <= 0) fit_chars = 1;

        if (fit_chars < seg_len) {
            int break_at = fit_chars;
            while (break_at > 1 && text[pos + break_at - 1] != ' ') break_at--;
            if (break_at > 1) fit_chars = break_at;
        }

        if (out_lines && line_count < max_lines) {
            out_lines[line_count] = (WrappedLine){pos, fit_chars, y, lh};
        }
        line_count++;
        y += lh;
        pos += fit_chars;

        if (pos == hard_nl && pos < len) {
            if (text[pos] == '\r' && pos + 1 < len && text[pos + 1] == '\n') pos += 2;
            else if (text[pos] == '\n' || text[pos] == '\r') pos++;
        }
    }
    return line_count;
}

// --- PDF Generation & UI Helpers ---
typedef struct { char* data; int len; int cap; } Stream;

void s_app(Stream* s, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (s->len + n >= s->cap) {
        s->cap = (s->cap == 0 ? 4096 : s->cap * 2) + n + 1024;
        s->data = realloc(s->data, s->cap);
    }
    memcpy(s->data + s->len, buf, n);
    s->len += n;
    s->data[s->len] = 0;
}

void pdf_color(Stream* s, int is_stroke, COLORREF col) {
    float r = GetRValue(col) / 255.0f;
    float g = GetGValue(col) / 255.0f;
    float b = GetBValue(col) / 255.0f;
    s_app(s, "%.3f %.3f %.3f %s\n", r, g, b, is_stroke ? "RG" : "rg");
}

void pdf_rounded_rect(Stream* s, float x, float y, float w, float h, float r) {
    float k = 0.55228f * r;
    s_app(s, "%.2f %.2f m\n", x + r, y + h);
    s_app(s, "%.2f %.2f l\n", x + w - r, y + h); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + w - r + k, y + h, x + w, y + h - r + k, x + w, y + h - r);
    s_app(s, "%.2f %.2f l\n", x + w, y + r); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + w, y + r - k, x + w - r + k, y, x + w - r, y);
    s_app(s, "%.2f %.2f l\n", x + r, y); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + r - k, y, x, y + r - k, x, y + r);
    s_app(s, "%.2f %.2f l\n", x, y + h - r); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x, y + h - r + k, x + r - k, y + h, x + r, y + h);
    s_app(s, "f\n");
}

void save_chat_to_pdf(HWND hwnd) {
    char out_path[MAX_PATH] = "Chat_Log.pdf";
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PDF Files (*.pdf)\0*.pdf\0";
    ofn.lpstrFile = out_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&ofn)) return;

    FILE* f = fopen(out_path, "wb");
    if (!f) return;

    RECT clientRect; GetClientRect(hwnd, &clientRect);
    int win_width = clientRect.right - clientRect.left;
    HDC hdc = GetDC(hwnd);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hChatFont);
    TEXTMETRICA tm; GetTextMetricsA(hdc, &tm);
    int lh = tm.tmHeight;

    int y_cursor = 20, bubble_padding = 12, max_bubble_w = (win_width * 7) / 10;
    if (max_bubble_w < 120) max_bubble_w = 120;

    int pdf_total_height = 20;
    for (int i = 0; i < msg_count; i++) {
        int n_lines = compute_wrapped_lines(hdc, chat_log[i].text, max_bubble_w, NULL, 0);
        pdf_total_height += (n_lines * lh) + (bubble_padding * 2) + 15;
    }
    if (pdf_total_height < 500) pdf_total_height = 500;

    fprintf(f, "%%PDF-1.1\n");
    long pdf_objects[16];
    pdf_objects[1] = ftell(f); fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    pdf_objects[2] = ftell(f); fprintf(f, "2 0 obj\n<< /Type /Pages /Count 1 /Kids [ 3 0 R ] >>\nendobj\n");
    pdf_objects[4] = ftell(f); fprintf(f, "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");

    Stream s = {0};
    s_app(&s, "0.95 0.95 0.97 rg 0 0 %d %d re f\n", win_width, pdf_total_height);

    y_cursor = 20;
    for (int i = 0; i < msg_count; i++) {
        WrappedLine lines[1024];
        int n_lines = compute_wrapped_lines(hdc, chat_log[i].text, max_bubble_w, lines, 1024);
        int max_line_px = 20;
        for (int l = 0; l < n_lines; l++) {
            SIZE sz = {0, 0};
            if (lines[l].len > 0) GetTextExtentPoint32A(hdc, chat_log[i].text + lines[l].start_idx, lines[l].len, &sz);
            if (sz.cx > max_line_px) max_line_px = sz.cx;
        }

        int b_width = max_line_px + (bubble_padding * 2);
        int b_height = (n_lines * lh) + (bubble_padding * 2);

        RECT bRect = (chat_log[i].msg_type == MSG_USER)
            ? (RECT){win_width - b_width - 25, y_cursor, win_width - 25, y_cursor + b_height}
            : (RECT){25, y_cursor, 25 + b_width, y_cursor + b_height};

        COLORREF cur_col = color_ai;
        if (chat_log[i].msg_type == MSG_USER) cur_col = color_user;
        else if (chat_log[i].msg_type == MSG_SYS_OK) cur_col = color_sys_ok;
        else if (chat_log[i].msg_type == MSG_SYS_ERR) cur_col = color_sys_err;

        float pdf_y_bottom = pdf_total_height - bRect.bottom;
        
        pdf_color(&s, 0, cur_col);
        pdf_rounded_rect(&s, (float)bRect.left, pdf_y_bottom, (float)(bRect.right - bRect.left), (float)b_height, 10.0f);
        
        int r = GetRValue(cur_col), g = GetGValue(cur_col), b = GetBValue(cur_col);
        if ((0.299f*r + 0.587f*g + 0.114f*b) > 186.0f) s_app(&s, "0 0 0 rg\n");
        else s_app(&s, "1 1 1 rg\n");
        
        for (int l = 0; l < n_lines; l++) {
            char lbuf[4096], esc[8192]; 
            int len = lines[l].len;
            if (len > 4000) len = 4000;
            memcpy(lbuf, chat_log[i].text + lines[l].start_idx, len);
            
            int e_idx = 0;
            for (int c = 0; c < len; c++) {
                if (lbuf[c] == '(' || lbuf[c] == ')' || lbuf[c] == '\\') esc[e_idx++] = '\\';
                esc[e_idx++] = lbuf[c];
            }
            esc[e_idx] = '\0';

            float text_y = pdf_total_height - (bRect.top + bubble_padding + l * lh + lh - 4); 
            s_app(&s, "BT /F1 %d Tf %.2f %.2f Td (%s) Tj ET\n", 13, (float)(bRect.left + bubble_padding), text_y, esc);
        }
        y_cursor += b_height + 15;
    }

    SelectObject(hdc, hOldFont); ReleaseDC(hwnd, hdc);

    pdf_objects[3] = ftell(f);
    fprintf(f, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Contents 6 0 R /Resources << /Font << /F1 4 0 R >> >> >>\nendobj\n", win_width, pdf_total_height);
    pdf_objects[6] = ftell(f);
    fprintf(f, "6 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", s.len, s.data);

    long xref_pos = ftell(f);
    fprintf(f, "xref\n0 7\n0000000000 65535 f \n");
    for(int i = 1; i <= 6; i++) fprintf(f, "%010ld 00000 n \n", pdf_objects[i]);
    fprintf(f, "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", xref_pos);
    
    if (s.data) free(s.data);
    fclose(f);
}

void PickColor(HWND hwnd, COLORREF* current_color, const char* key) {
    CHOOSECOLOR cc = {0};
    static COLORREF acrCustClr[16];
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = *current_color;
    cc.lpCustColors = (LPDWORD)acrCustClr;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColor(&cc)) {
        *current_color = cc.rgbResult;
        char buf[32]; sprintf(buf, "%lu", *current_color);
        WritePrivateProfileStringA("Colors", key, buf, ini_path);
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

void handle_menu_cmd(HWND hwnd, int cmd) {
    if (cmd == IDM_SAVE_PDF) save_chat_to_pdf(hwnd);
    else if (cmd == IDM_STOP_OUTPUT) stop_generation = 1;
    else if (cmd == IDM_SET_COLOR_U) PickColor(hwnd, &color_user, "User");
    else if (cmd == IDM_SET_COLOR_AI) PickColor(hwnd, &color_ai, "AI");
    else if (cmd == IDM_CLEAR_CHAT) { msg_count = 0; scroll_y = 0; InvalidateRect(hwnd, NULL, TRUE); }
    else if (cmd == IDM_WIPE_MEMORY) { memset(chat_log, 0, sizeof(chat_log)); msg_count = 0; scroll_y = 0; InvalidateRect(hwnd, NULL, TRUE); }
}

int hit_test_bubble_char(HDC hdc, int bubble_idx, POINT pt) {
    if (bubble_idx < 0 || bubble_idx >= msg_count) return 0;
    const char* text = chat_log[bubble_idx].text;
    RECT r = chat_log[bubble_idx].text_box;
    int max_w = r.right - r.left;
    if (max_w <= 0) return 0;

    WrappedLine lines[1024];
    int n_lines = compute_wrapped_lines(hdc, text, max_w, lines, 1024);
    if (n_lines <= 0) return 0;

    int rel_x = pt.x - r.left;
    int rel_y = pt.y - r.top;

    int line_idx = 0;
    if (rel_y < 0) {
        line_idx = 0;
        rel_x = 0;
    } else {
        line_idx = n_lines - 1;
        for (int i = 0; i < n_lines; i++) {
            if (rel_y >= lines[i].y_top && rel_y < lines[i].y_top + lines[i].line_height) {
                line_idx = i;
                break;
            }
        }
    }

    WrappedLine* wl = &lines[line_idx];
    if (rel_x <= 0 || wl->len == 0) return wl->start_idx;

    int dx[1024];
    int count = (wl->len < 1024) ? wl->len : 1024;
    int fit = 0;
    SIZE sz;
    GetTextExtentExPointA(hdc, text + wl->start_idx, count, 32767, &fit, dx, &sz);

    int prev_x = 0;
    for (int i = 0; i < count; i++) {
        int cur_x = dx[i];
        int mid_x = (prev_x + cur_x) / 2;
        if (rel_x < mid_x) return wl->start_idx + i;
        prev_x = cur_x;
    }
    return wl->start_idx + count;
}

void draw_bubble_text_and_selection(HDC hdc, int bubble_idx) {
    const char* text = chat_log[bubble_idx].text;
    RECT r = chat_log[bubble_idx].text_box;
    int max_w = r.right - r.left;

    WrappedLine lines[1024];
    int n_lines = compute_wrapped_lines(hdc, text, max_w, lines, 1024);

    int has_sel = (sel_bubble_idx == bubble_idx && sel_anchor_char >= 0 && sel_caret_char >= 0);
    int sel_min = 0, sel_max = 0;
    if (has_sel) {
        sel_min = (sel_anchor_char < sel_caret_char) ? sel_anchor_char : sel_caret_char;
        sel_max = (sel_anchor_char > sel_caret_char) ? sel_anchor_char : sel_caret_char;
    }

    COLORREF bg_col = color_ai;
    if (chat_log[bubble_idx].msg_type == MSG_USER) bg_col = color_user;
    else if (chat_log[bubble_idx].msg_type == MSG_SYS_OK) bg_col = color_sys_ok;
    else if (chat_log[bubble_idx].msg_type == MSG_SYS_ERR) bg_col = color_sys_err;
    
    int c_r = GetRValue(bg_col), c_g = GetGValue(bg_col), c_b = GetBValue(bg_col);
    float luma = 0.299f*c_r + 0.587f*c_g + 0.114f*c_b;
    
    COLORREF normal_text_col = (luma > 186.0f) ? RGB(0, 0, 0) : RGB(255, 255, 255);
    HBRUSH selBrush = CreateSolidBrush((luma > 186.0f) ? RGB(0, 120, 215) : RGB(255, 214, 10));

    for (int i = 0; i < n_lines; i++) {
        WrappedLine* wl = &lines[i];
        int line_start = wl->start_idx;
        int line_end = wl->start_idx + wl->len;
        int draw_y = r.top + wl->y_top;

        if (has_sel && sel_max > sel_min && sel_max > line_start && sel_min < line_end) {
            int hl_start = (sel_min > line_start) ? sel_min : line_start;
            int hl_end   = (sel_max < line_end)   ? sel_max : line_end;

            SIZE sz_pre = {0, 0}, sz_hl = {0, 0};
            if (hl_start > line_start) {
                GetTextExtentPoint32A(hdc, text + line_start, hl_start - line_start, &sz_pre);
            }
            GetTextExtentPoint32A(hdc, text + hl_start, hl_end - hl_start, &sz_hl);

            RECT hlRect = { r.left + sz_pre.cx, draw_y, r.left + sz_pre.cx + sz_hl.cx, draw_y + wl->line_height };
            FillRect(hdc, &hlRect, selBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, normal_text_col);
            if (hl_start > line_start) {
                TextOutA(hdc, r.left, draw_y, text + line_start, hl_start - line_start);
            }
            SetTextColor(hdc, (luma > 186.0f) ? RGB(255, 255, 255) : RGB(0, 0, 0));
            TextOutA(hdc, r.left + sz_pre.cx, draw_y, text + hl_start, hl_end - hl_start);
            SetTextColor(hdc, normal_text_col);
            if (line_end > hl_end) {
                TextOutA(hdc, r.left + sz_pre.cx + sz_hl.cx, draw_y, text + hl_end, line_end - hl_end);
            }
        } else {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, normal_text_col);
            if (wl->len > 0) {
                TextOutA(hdc, r.left, draw_y, text + line_start, wl->len);
            }
        }

        if (has_sel && sel_caret_char >= line_start &&
            (sel_caret_char < line_end || (i == n_lines - 1 && sel_caret_char == line_end))) {
            SIZE sz_caret = {0, 0};
            if (sel_caret_char > line_start) {
                GetTextExtentPoint32A(hdc, text + line_start, sel_caret_char - line_start, &sz_caret);
            }
            RECT caretRect = { r.left + sz_caret.cx, draw_y, r.left + sz_caret.cx + 2, draw_y + wl->line_height };
            HBRUSH caretBrush = CreateSolidBrush((luma > 186.0f) ? RGB(0, 0, 0) : RGB(255, 255, 255));
            FillRect(hdc, &caretRect, caretBrush);
            DeleteObject(caretBrush);
        }
    }
    DeleteObject(selBrush);
}

void show_bubble_context_menu(HWND hwnd, int msg_idx, POINT screen_pt) {
    HMENU hMenu = CreatePopupMenu();
    int has_range = (sel_bubble_idx == msg_idx && sel_anchor_char != sel_caret_char && sel_anchor_char >= 0);
    AppendMenuA(hMenu, has_range ? MF_STRING : (MF_STRING | MF_GRAYED), IDM_COPY_SEL, "Copy Selection\tCtrl+C");
    AppendMenuA(hMenu, MF_STRING, IDM_COPY_ALL, "Copy Entire Bubble");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, IDM_SEL_ALL, "Select All in Bubble\tCtrl+A");
    
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    if (is_generating) {
        AppendMenuA(hMenu, MF_STRING, IDM_STOP_OUTPUT, "Stop Output");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuA(hMenu, MF_STRING, IDM_SAVE_PDF, "Save Chat to PDF");
    AppendMenuA(hMenu, MF_STRING, IDM_SET_COLOR_U, "Set User Bubble Color...");
    AppendMenuA(hMenu, MF_STRING, IDM_SET_COLOR_AI, "Set AI Bubble Color...");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, IDM_CLEAR_CHAT, "Clear Chat");
    AppendMenuA(hMenu, MF_STRING, IDM_WIPE_MEMORY, "Wipe Chat Memory");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_COPY_SEL && has_range) {
        int s_min = (sel_anchor_char < sel_caret_char) ? sel_anchor_char : sel_caret_char;
        int s_max = (sel_anchor_char > sel_caret_char) ? sel_anchor_char : sel_caret_char;
        copy_range_to_clipboard(hwnd, chat_log[msg_idx].text, s_min, s_max);
    } else if (cmd == IDM_COPY_ALL && msg_idx >= 0 && msg_idx < msg_count) {
        copy_range_to_clipboard(hwnd, chat_log[msg_idx].text, 0, (int)strlen(chat_log[msg_idx].text));
    } else if (cmd == IDM_SEL_ALL && msg_idx >= 0 && msg_idx < msg_count) {
        sel_bubble_idx = msg_idx;
        sel_anchor_char = 0;
        sel_caret_char = (int)strlen(chat_log[msg_idx].text);
        InvalidateRect(hwnd, NULL, FALSE);
    } else {
        handle_menu_cmd(hwnd, cmd);
    }
}

int encode_text_segment(const char* text, int* tokens, int count, int max_tokens) {
    size_t len = strlen(text);
    size_t bpe_cap = len * 3 + 10;
    char* bpe_buf = (char*)malloc(bpe_cap);
    if (!bpe_buf) return count;

    int b_idx = 0;
    for (int i = 0; text[i] != '\0' && b_idx < bpe_cap - 6; i++) {
        if (text[i] == ' ') {
            if (uses_g_prefix) {
                bpe_buf[b_idx++] = (char)0xC4; bpe_buf[b_idx++] = (char)0xA0;
            } else {
                bpe_buf[b_idx++] = (char)0xE2; bpe_buf[b_idx++] = (char)0x96; bpe_buf[b_idx++] = (char)0x81;
            }
        } else if (text[i] == '\n') {
            if (uses_g_prefix) {
                bpe_buf[b_idx++] = (char)0xC4; bpe_buf[b_idx++] = (char)0x8A;
            } else {
                bpe_buf[b_idx++] = '\n';
            }
        } else {
            bpe_buf[b_idx++] = text[i];
        }
    }
    bpe_buf[b_idx] = '\0';

    int pos = 0;
    while (pos < b_idx && count < max_tokens) {
        int best_len = 0;
        int best_id = -1;

        for (int i = 0; i < vocab_size; i++) {
            if (!vocab_map[i].str) continue;
            int t_len = (int)strlen(vocab_map[i].str);
            if (t_len > best_len && pos + t_len <= b_idx) {
                if (memcmp(bpe_buf + pos, vocab_map[i].str, t_len) == 0) {
                    best_len = t_len;
                    best_id = i;
                }
            }
        }

        if (best_id != -1 && best_len > 0) {
            tokens[count++] = best_id;
            pos += best_len;
        } else {
            pos++;
        }
    }
    free(bpe_buf);
    return count;
}
int find_token_id(const char* str) {
    for (int i = 0; i < vocab_size; i++) {
        if (vocab_map[i].str && strcmp(vocab_map[i].str, str) == 0) return i;
    }
    return -1;
}

void unescape_json_token(const char* start, int str_len, char* out, int max_out) {
    int u_idx = 0;
    for (int i = 0; i < str_len && u_idx < max_out - 4; i++) {
        if (start[i] == '\\' && (i + 1) < str_len) {
            i++;
            if (start[i] == 'n') out[u_idx++] = '\n';
            else if (start[i] == 'r') out[u_idx++] = '\r';
            else if (start[i] == 't') out[u_idx++] = '\t';
            else if (start[i] == '"' || start[i] == '\\' || start[i] == '/') out[u_idx++] = start[i];
            else if (start[i] == 'u' && (i + 4) < str_len) {
                char hex[5] = { start[i+1], start[i+2], start[i+3], start[i+4], 0 };
                unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                i += 4;
                if (cp < 0x80) {
                    out[u_idx++] = (char)cp;
                } else if (cp < 0x800) {
                    out[u_idx++] = (char)(0xC0 | (cp >> 6));
                    out[u_idx++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    out[u_idx++] = (char)(0xE0 | (cp >> 12));
                    out[u_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[u_idx++] = (char)(0x80 | (cp & 0x3F));
                }
            } else {
                out[u_idx++] = start[i];
            }
        } else {
            out[u_idx++] = start[i];
        }
    }
    out[u_idx] = '\0';
}

void parse_vocab_section(char* p) {
    char* brace = strchr(p, '{');
    if (!brace) return;
    p = brace + 1;

    while (*p) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) p++;
        if (!*p || *p == '}') break;

        if (*p != '"') { p++; continue; }
        p++;
        char* start = p;
        while (*p && *p != '"') {
            if (*p == '\\' && *(p + 1)) p += 2;
            else p++;
        }
        if (!*p) break;
        int str_len = (int)(p - start);
        p++;

        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (*p != ':') continue;
        p++;

        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (isdigit((unsigned char)*p) || *p == '-') {
            int id = (int)strtol(p, &p, 10);
            if (id >= 0 && id < vocab_size) {
                char unescaped[512];
                unescape_json_token(start, str_len, unescaped, sizeof(unescaped));
                if (vocab_map[id].str) free(vocab_map[id].str);
                vocab_map[id].str = strdup(unescaped);
                vocab_map[id].id = id;
                if ((unsigned char)unescaped[0] == 0xC4 &&
                   ((unsigned char)unescaped[1] == 0xA0 || (unsigned char)unescaped[1] == 0x8A)) {
                    uses_g_prefix = 1;
                }
            }
        }
    }
}

void* track_mem(void* ptr) {
    if (ptr && ptr_count < 4000) allocated_ptrs[ptr_count++] = ptr;
    return ptr;
}

void cleanup_memory(void) {
    free_gpu_weights();
    for (int i = 0; i < ptr_count; i++) free(allocated_ptrs[i]);
    ptr_count = 0;
}

int get_json_int(const char* json, const char* key) {
    char search[256];
    sprintf(search, "\"%s\"", key);
    char* pos = strstr(json, search);
    if (!pos) return 0;
    pos += strlen(search);
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' || *pos == ':')) pos++;
    return atoi(pos);
}

int load_config_json(const char* dir_path) {
    char path[MAX_PATH];
    sprintf(path, "%s\\config.json", dir_path);
    FILE* f = fopen(path, "rb");
    if (!f) { sprintf(last_error_msg, "File Missing: Could not find config.json in %s", dir_path); return 0; }

    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    char* json = (char*)malloc((size_t)fsize + 1);
    fread(json, 1, (size_t)fsize, f);
    json[fsize] = 0;
    fclose(f);

    dim = get_json_int(json, "hidden_size");
    hidden_dim = get_json_int(json, "intermediate_size");
    n_layers = get_json_int(json, "num_hidden_layers");
    n_heads = get_json_int(json, "num_attention_heads");
    n_kv_heads = get_json_int(json, "num_key_value_heads");
    if (n_kv_heads == 0) n_kv_heads = n_heads;
    vocab_size = get_json_int(json, "vocab_size");

    int rt = get_json_int(json, "rope_theta");
    if (rt > 0) rope_theta = (float)rt;
    if (param_rope_theta > 0.0f) rope_theta = param_rope_theta;

    if (n_heads > 0) { head_dim = dim / n_heads; kv_dim = n_kv_heads * head_dim; }
    free(json);

    if (dim == 0 || n_layers == 0 || n_heads == 0) {
        sprintf(last_error_msg, "Config Error: Invalid values (dim=%d, layers=%d, heads=%d).", dim, n_layers, n_heads);
        return 0;
    }
    return 1;
}

int load_bpe_tokenizer(const char* dir_path) {
    if (vocab_map) {
        for (int i = 0; i < current_vocab_size; i++) {
            if (vocab_map[i].str) free(vocab_map[i].str);
        }
        free(vocab_map);
        vocab_map = NULL;
    }
    uses_g_prefix = 0;
    current_vocab_size = vocab_size;
    vocab_map = (Token*)calloc((size_t)vocab_size, sizeof(Token));
    if (!vocab_map) return 0;

    int loaded_vocab = 0;
    char v_path[MAX_PATH];
    sprintf(v_path, "%s\\vocab.json", dir_path);
    FILE* f_voc = fopen(v_path, "rb");
    if (f_voc) {
        fseek(f_voc, 0, SEEK_END); long fsize = ftell(f_voc); fseek(f_voc, 0, SEEK_SET);
        char* json = (char*)malloc((size_t)fsize + 1);
        if (json) {
            fread(json, 1, (size_t)fsize, f_voc); json[fsize] = '\0';
            parse_vocab_section(json);
            free(json);
            loaded_vocab = 1;
        }
        fclose(f_voc);
    }

    char t_path[MAX_PATH];
    sprintf(t_path, "%s\\tokenizer.json", dir_path);
    FILE* f_tok = fopen(t_path, "rb");
    if (f_tok) {
        fseek(f_tok, 0, SEEK_END); long fsize = ftell(f_tok); fseek(f_tok, 0, SEEK_SET);
        char* json = (char*)malloc((size_t)fsize + 1);
        if (json) {
            fread(json, 1, (size_t)fsize, f_tok); json[fsize] = '\0';

            if (!loaded_vocab) {
                char* v_sec = json;
                while ((v_sec = strstr(v_sec, "\"vocab\"")) != NULL) {
                    char* after = v_sec + 7;
                    while (*after && (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')) after++;
                    if (*after == ':') {
                        after++;
                        while (*after && (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')) after++;
                        if (*after == '{') {
                            parse_vocab_section(after);
                            loaded_vocab = 1;
                            break;
                        }
                    }
                    v_sec += 7;
                }
            }

            char* added = strstr(json, "\"added_tokens\"");
            if (added) {
                char* cursor = added;
                char* model_pos = strstr(added, "\"model\"");
                while ((cursor = strstr(cursor, "\"id\"")) != NULL) {
                    if (model_pos && cursor > model_pos) break;
                    char* colon = strchr(cursor, ':');
                    if (!colon) break;
                    int id = atoi(colon + 1);

                    char* obj_end = strchr(colon, '}');
                    char* content = strstr(colon, "\"content\"");
                    if (content && obj_end && content < obj_end) {
                        char* q1 = strchr(content + 9, '"');
                        if (q1 && q1 < obj_end) {
                            q1++;
                            char* q2 = q1;
                            while (*q2 && *q2 != '"') {
                                if (*q2 == '\\' && *(q2 + 1)) q2 += 2;
                                else q2++;
                            }
                            if (*q2 == '"' && id >= 0 && id < vocab_size) {
                                char unescaped[256];
                                unescape_json_token(q1, (int)(q2 - q1), unescaped, sizeof(unescaped));
                                if (vocab_map[id].str) free(vocab_map[id].str);
                                vocab_map[id].str = strdup(unescaped);
                                vocab_map[id].id = id;
                            }
                        }
                    }
                    cursor = colon + 1;
                }
            }
            free(json);
        }
        fclose(f_tok);
    }

    if (!loaded_vocab) {
        sprintf(last_error_msg, "File Missing: Could not parse vocab from vocab.json or tokenizer.json in %s", dir_path);
        return 0;
    }
    return 1;
}

float* fetch_tensor(const char* name, size_t elements) {
    if (!json_header) return NULL;
    char search_str[256];
    sprintf(search_str, "\"%s\"", name);
    char* pos = strstr(json_header, search_str);

    if (!pos) {
        char temp[256];
        strcpy(temp, name);
        char* ext = strstr(temp, ".weight");
        if (ext) {
            *ext = '\0';
            sprintf(search_str, "\"%s\"", temp);
            pos = strstr(json_header, search_str);
        }
    }
    if (!pos && strncmp(name, "model.", 6) == 0) {
        sprintf(search_str, "\"%s\"", name + 6);
        pos = strstr(json_header, search_str);
    }
    if (!pos) return NULL;

    char* obj_start = strchr(pos, '{');
    char* obj_end = obj_start ? strchr(obj_start, '}') : NULL;
    if (!obj_start || !obj_end) return NULL;

    size_t entry_len = (size_t)(obj_end - obj_start + 1);
    char entry_buf[512];
    if (entry_len >= sizeof(entry_buf)) entry_len = sizeof(entry_buf) - 1;
    memcpy(entry_buf, obj_start, entry_len);
    entry_buf[entry_len] = '\0';

    char* off_pos = strstr(entry_buf, "\"data_offsets\"");
    if (!off_pos) return NULL;
    char* bracket_pos = strchr(off_pos, '[');
    if (!bracket_pos) return NULL;

    unsigned long long start = 0, end = 0;
    if (sscanf(bracket_pos + 1, "%llu , %llu", &start, &end) < 1) return NULL;

    int is_f16 = 0, is_bf16 = 0, is_f32 = 0;
    if (strstr(entry_buf, "\"F16\"") || strstr(entry_buf, "\"FLOAT16\"")) is_f16 = 1;
    else if (strstr(entry_buf, "\"BF16\"") || strstr(entry_buf, "\"BFLOAT16\"")) is_bf16 = 1;
    else if (strstr(entry_buf, "\"F32\"") || strstr(entry_buf, "\"FLOAT32\"")) is_f32 = 1;

    if (!is_f16 && !is_bf16 && !is_f32) {
        sprintf(last_error_msg, "Format Error: Tensor '%s' uses unsupported dtype.", name);
        return NULL;
    }

    uint64_t required_bytes = (uint64_t)elements * (is_f32 ? 4ULL : 2ULL);
    if (start + required_bytes > safe_tensors_data_size) {
        sprintf(last_error_msg, "Memory Error: Tensor '%s' exceeds file bounds.", name);
        return NULL;
    }

    if (is_f32) return (float*)(safe_tensors_data + start);

    float* out = (float*)track_mem(malloc(elements * sizeof(float)));
    if (!out) {
        sprintf(last_error_msg, "Memory Error: Out of RAM allocating '%s'.", name);
        return NULL;
    }

    uint16_t* raw = (uint16_t*)(safe_tensors_data + start);
    if (is_f16) {
        for (size_t i = 0; i < elements; i++) out[i] = fp16_to_fp32(raw[i]);
    } else {
        for (size_t i = 0; i < elements; i++) {
            uint32_t val = (uint32_t)raw[i] << 16;
            memcpy(&out[i], &val, sizeof(float));
        }
    }
    return out;
}

int load_llama_model(const char* target_path) {
    last_error_msg[0] = '\0';
    if (file_map_ptr) {
        UnmapViewOfFile(file_map_ptr);
        CloseHandle(hMap_ptr);
        CloseHandle(hFile_ptr);
        free(json_header);
        file_map_ptr = NULL;
        json_header = NULL;
    }
    cleanup_memory();

    char dir_path[MAX_PATH];
    char st_path[MAX_PATH] = "";
    DWORD attr = GetFileAttributesA(target_path);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        strcpy(dir_path, target_path);
        char* last_slash = strrchr(dir_path, '\\');
        if (last_slash) *last_slash = '\0';
        if (strstr(target_path, ".safetensors")) strcpy(st_path, target_path);
    } else {
        strcpy(dir_path, target_path);
    }

    load_ini_parameters();
    if (!load_config_json(dir_path)) return 0;
    if (!load_bpe_tokenizer(dir_path)) return 0;
    layers = (LlamaLayer*)realloc(layers, (size_t)n_layers * sizeof(LlamaLayer));

    if (strlen(st_path) == 0) {
        char search_pattern[MAX_PATH];
        sprintf(search_pattern, "%s\\*.safetensors", dir_path);
        WIN32_FIND_DATAA findFileData;
        HANDLE hFind = FindFirstFileA(search_pattern, &findFileData);

        if (hFind == INVALID_HANDLE_VALUE) {
            sprintf(last_error_msg, "File Error: Found config.json, but no .safetensors weights exist in %s", dir_path);
            return 0;
        }

        LARGE_INTEGER max_size; max_size.QuadPart = 0;
        char best_file[MAX_PATH] = "";
        do {
            LARGE_INTEGER fz; fz.LowPart = findFileData.nFileSizeLow; fz.HighPart = findFileData.nFileSizeHigh;
            if (fz.QuadPart > max_size.QuadPart) { max_size = fz; strcpy(best_file, findFileData.cFileName); }
        } while (FindNextFileA(hFind, &findFileData));
        FindClose(hFind);
        sprintf(st_path, "%s\\%s", dir_path, best_file);
    }

    hFile_ptr = CreateFileA(st_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile_ptr == INVALID_HANDLE_VALUE) { strcpy(last_error_msg, "OS Error: Could not open the .safetensors file."); return 0; }

    LARGE_INTEGER fsize; GetFileSizeEx(hFile_ptr, &fsize);
    if (fsize.QuadPart < 1024 * 1024) {
        sprintf(last_error_msg, "Download Error: File is only %lld bytes (Git LFS pointer).", fsize.QuadPart);
        CloseHandle(hFile_ptr); hFile_ptr = INVALID_HANDLE_VALUE; return 0;
    }

    hMap_ptr = CreateFileMappingA(hFile_ptr, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap_ptr) {
        sprintf(last_error_msg, "OS Error: CreateFileMapping failed (Error %lu).", GetLastError());
        CloseHandle(hFile_ptr); hFile_ptr = INVALID_HANDLE_VALUE; return 0;
    }

    file_map_ptr = (char*)MapViewOfFile(hMap_ptr, FILE_MAP_READ, 0, 0, 0);
    if (!file_map_ptr) {
        sprintf(last_error_msg, "Memory Error: MapViewOfFile failed (Error %lu).", GetLastError());
        CloseHandle(hMap_ptr); CloseHandle(hFile_ptr);
        hMap_ptr = NULL; hFile_ptr = INVALID_HANDLE_VALUE;
        return 0;
    }

    uint64_t header_size = *(uint64_t*)file_map_ptr;
    if (header_size > 100000000ULL || header_size < 2ULL || (8ULL + header_size) >= (uint64_t)fsize.QuadPart) {
        sprintf(last_error_msg, "Corrupt File: Invalid SafeTensors header size (%llu).", header_size);
        return 0;
    }

    json_header = (char*)malloc((size_t)header_size + 1);
    memcpy(json_header, file_map_ptr + 8, (size_t)header_size);
    json_header[header_size] = '\0';

    safe_tensors_data = file_map_ptr + 8 + header_size;
    safe_tensors_data_size = (uint64_t)fsize.QuadPart - (8ULL + header_size);

    FETCH_TENS(tok_emb, "model.embed_tokens.weight", (size_t)vocab_size * (size_t)dim);
    FETCH_TENS(output_norm_w, "model.norm.weight", (size_t)dim);
    lm_head_w = fetch_tensor("lm_head.weight", (size_t)vocab_size * (size_t)dim);
    if (!lm_head_w) lm_head_w = tok_emb;

    char key[256];
    for (int i = 0; i < n_layers; i++) {
        sprintf(key, "model.layers.%d.self_attn.q_proj.weight", i); FETCH_TENS(layers[i].wq, key, (size_t)dim * (size_t)dim);
        sprintf(key, "model.layers.%d.self_attn.k_proj.weight", i); FETCH_TENS(layers[i].wk, key, (size_t)kv_dim * (size_t)dim);
        sprintf(key, "model.layers.%d.self_attn.v_proj.weight", i); FETCH_TENS(layers[i].wv, key, (size_t)kv_dim * (size_t)dim);
        sprintf(key, "model.layers.%d.self_attn.o_proj.weight", i); FETCH_TENS(layers[i].wo, key, (size_t)dim * (size_t)dim);
        sprintf(key, "model.layers.%d.mlp.gate_proj.weight", i);    FETCH_TENS(layers[i].ffn_gate, key, (size_t)hidden_dim * (size_t)dim);
        sprintf(key, "model.layers.%d.mlp.down_proj.weight", i);    FETCH_TENS(layers[i].ffn_down, key, (size_t)dim * (size_t)hidden_dim);
        sprintf(key, "model.layers.%d.mlp.up_proj.weight", i);      FETCH_TENS(layers[i].ffn_up, key, (size_t)hidden_dim * (size_t)dim);
        sprintf(key, "model.layers.%d.input_layernorm.weight", i);  FETCH_TENS(layers[i].attn_norm, key, (size_t)dim);
        sprintf(key, "model.layers.%d.post_attention_layernorm.weight", i); FETCH_TENS(layers[i].ffn_norm, key, (size_t)dim);
    }

    if (use_gpu && init_opencl_gpu()) {
        for (int i = 0; i < n_layers && !gpu_vram_exhausted; i++) {
            get_or_upload_gpu_weight(layers[i].wq, (size_t)dim * (size_t)dim * sizeof(float));
            get_or_upload_gpu_weight(layers[i].wk, (size_t)kv_dim * (size_t)dim * sizeof(float));
            get_or_upload_gpu_weight(layers[i].wv, (size_t)kv_dim * (size_t)dim * sizeof(float));
            get_or_upload_gpu_weight(layers[i].wo, (size_t)dim * (size_t)dim * sizeof(float));
            get_or_upload_gpu_weight(layers[i].ffn_gate, (size_t)hidden_dim * (size_t)dim * sizeof(float));
            get_or_upload_gpu_weight(layers[i].ffn_down, (size_t)dim * (size_t)hidden_dim * sizeof(float));
            get_or_upload_gpu_weight(layers[i].ffn_up, (size_t)hidden_dim * (size_t)dim * sizeof(float));
        }
        get_or_upload_gpu_weight(lm_head_w, (size_t)vocab_size * (size_t)dim * sizeof(float));
    }
    return 1;
}

// ============================================================================
// LLAMA CORE MATH & SAMPLING
// ============================================================================
void rmsnorm(float* o, float* x, float* weight, int size) {
    float ss = 0.0f;
    for (int j = 0; j < size; j++) ss += x[j] * x[j];
    ss /= size; ss += 1e-5f; ss = 1.0f / sqrtf(ss);
    for (int j = 0; j < size; j++) o[j] = weight[j] * (ss * x[j]);
}

void matmul(float* xout, float* x, float* w, int n, int d) {
    if (use_gpu && gpu_ready) {
        size_t x_bytes = (size_t)n * sizeof(float);
        size_t out_bytes = (size_t)d * sizeof(float);
        size_t w_bytes = (size_t)n * (size_t)d * sizeof(float);

        cl_mem dev_w = get_or_upload_gpu_weight(w, w_bytes);
        if (dev_w) {
            cl_int err = CL_SUCCESS;
            if (x_bytes > gpu_max_x_bytes) {
                if (gpu_buf_x) f_clReleaseMemObject(gpu_buf_x);
                gpu_buf_x = f_clCreateBuffer(gpu_ctx, CL_MEM_READ_ONLY, x_bytes, NULL, &err);
                gpu_max_x_bytes = gpu_buf_x ? x_bytes : 0;
            }
            if (out_bytes > gpu_max_out_bytes) {
                if (gpu_buf_out) f_clReleaseMemObject(gpu_buf_out);
                gpu_buf_out = f_clCreateBuffer(gpu_ctx, CL_MEM_WRITE_ONLY, out_bytes, NULL, &err);
                gpu_max_out_bytes = gpu_buf_out ? out_bytes : 0;
            }
            if (gpu_buf_x && gpu_buf_out) {
                f_clEnqueueWriteBuffer(gpu_queue, gpu_buf_x, CL_TRUE, 0, x_bytes, x, 0, NULL, NULL);
                f_clSetKernelArg(gpu_kernel, 0, sizeof(cl_mem), &gpu_buf_x);
                f_clSetKernelArg(gpu_kernel, 1, sizeof(cl_mem), &dev_w);
                f_clSetKernelArg(gpu_kernel, 2, sizeof(cl_mem), &gpu_buf_out);
                f_clSetKernelArg(gpu_kernel, 3, sizeof(int), &n);
                f_clSetKernelArg(gpu_kernel, 4, sizeof(int), &d);

                size_t local_ws = 64;
                size_t global_ws = ((size_t)d + local_ws - 1) / local_ws * local_ws;
                if (f_clEnqueueNDRangeKernel(gpu_queue, gpu_kernel, 1, NULL, &global_ws, &local_ws, 0, NULL, NULL) == CL_SUCCESS) {
                    if (f_clEnqueueReadBuffer(gpu_queue, gpu_buf_out, CL_TRUE, 0, out_bytes, xout, 0, NULL, NULL) == CL_SUCCESS) {
                        return;
                    }
                }
            }
        }
    }

    for (int i = 0; i < d; i++) {
        float val = 0.0f;
        const float* row = w + (size_t)i * (size_t)n;
        int j = 0;
        for (; j + 3 < n; j += 4) {
            val += row[j] * x[j] + row[j+1] * x[j+1] + row[j+2] * x[j+2] + row[j+3] * x[j+3];
        }
        for (; j < n; j++) val += row[j] * x[j];
        xout[i] = val;
    }
}

void apply_rope(float* q, float* k, int pos) {
    for (int h = 0; h < n_heads; h++) {
        int head_offset = h * head_dim;
        for (int i = 0; i < head_dim / 2; i++) {
            float freq = 1.0f / powf(rope_theta, (float)(2 * i) / (float)head_dim);
            float val = (float)pos * freq;
            float fcr = cosf(val), fci = sinf(val);

            int i1 = head_offset + i;
            int i2 = head_offset + i + (head_dim / 2);
            float q1 = q[i1], q2 = q[i2];
            q[i1] = q1 * fcr - q2 * fci;
            q[i2] = q1 * fci + q2 * fcr;
        }
    }
    for (int h = 0; h < n_kv_heads; h++) {
        int head_offset = h * head_dim;
        for (int i = 0; i < head_dim / 2; i++) {
            float freq = 1.0f / powf(rope_theta, (float)(2 * i) / (float)head_dim);
            float val = (float)pos * freq;
            float fcr = cosf(val), fci = sinf(val);

            int i1 = head_offset + i;
            int i2 = head_offset + i + (head_dim / 2);
            float k1 = k[i1], k2 = k[i2];
            k[i1] = k1 * fcr - k2 * fci;
            k[i2] = k1 * fci + k2 * fcr;
        }
    }
}

void swiglu(float* out, float* gate, float* up, int size) {
    for (int i = 0; i < size; i++) {
        float val = gate[i];
        val *= (1.0f / (1.0f + expf(-val)));
        out[i] = val * up[i];
    }
}

typedef struct { float p; int id; } TokCandidate;

static int cand_descending_cmp(const void* a, const void* b) {
    float pa = ((const TokCandidate*)a)->p;
    float pb = ((const TokCandidate*)b)->p;
    if (pb > pa) return 1;
    if (pb < pa) return -1;
    return 0;
}

int sample_token(float* logits, int v_size, int* prev_tokens, int num_prev,
                 float temp, float top_p, int top_k, float rep_penalty) {
    // Exact default behavior: Greedy argmax
    if (temp <= 0.0001f) {
        int best_token = 0;
        float max_logit = logits[0];
        for (int v = 1; v < v_size; v++) {
            if (logits[v] > max_logit) {
                max_logit = logits[v];
                best_token = v;
            }
        }
        return best_token;
    }

    // Repetition penalty
    if (rep_penalty > 1.0001f && prev_tokens && num_prev > 0) {
        for (int i = 0; i < num_prev; i++) {
            int tid = prev_tokens[i];
            if (tid >= 0 && tid < v_size) {
                if (logits[tid] > 0.0f) logits[tid] /= rep_penalty;
                else logits[tid] *= rep_penalty;
            }
        }
    }

    // Temperature scaling
    for (int i = 0; i < v_size; i++) logits[i] /= temp;

    // Numerical softmax
    float max_l = logits[0];
    for (int i = 1; i < v_size; i++) {
        if (logits[i] > max_l) max_l = logits[i];
    }
    float sum_exp = 0.0f;
    for (int i = 0; i < v_size; i++) {
        logits[i] = expf(logits[i] - max_l);
        sum_exp += logits[i];
    }
    for (int i = 0; i < v_size; i++) logits[i] /= sum_exp;

    // Top-K and Top-P filtering
    if ((top_k > 0 && top_k < v_size) || (top_p > 0.0f && top_p < 1.0f)) {
        TokCandidate* cands = (TokCandidate*)malloc((size_t)v_size * sizeof(TokCandidate));
        if (cands) {
            for (int i = 0; i < v_size; i++) {
                cands[i].p = logits[i];
                cands[i].id = i;
            }
            qsort(cands, (size_t)v_size, sizeof(TokCandidate), cand_descending_cmp);

            int cand_count = v_size;
            if (top_k > 0 && top_k < cand_count) cand_count = top_k;

            if (top_p > 0.0f && top_p < 1.0f) {
                float cum = 0.0f;
                int cutoff = 0;
                for (int i = 0; i < cand_count; i++) {
                    cum += cands[i].p;
                    cutoff = i + 1;
                    if (cum >= top_p) break;
                }
                cand_count = cutoff;
            }

            float p_sum = 0.0f;
            for (int i = 0; i < cand_count; i++) p_sum += cands[i].p;
            float r = ((float)rand() / ((float)RAND_MAX + 1.0f)) * p_sum;
            float acc = 0.0f;
            int chosen = cands[0].id;
            for (int i = 0; i < cand_count; i++) {
                acc += cands[i].p;
                if (r <= acc) { chosen = cands[i].id; break; }
            }
            free(cands);
            return chosen;
        }
    }

    // Direct sampling fallback
    float r = ((float)rand() / ((float)RAND_MAX + 1.0f));
    float acc = 0.0f;
    for (int i = 0; i < v_size; i++) {
        acc += logits[i];
        if (r <= acc) return i;
    }
    return v_size - 1;
}

int tokenize_greedy(const char* text, int* tokens, int max_seq, int max_gen) {
    int count = 0;
    int im_start = find_token_id("<|im_start|>");
    int im_end   = find_token_id("<|im_end|>");

    int gen_reserve = (max_gen < max_seq / 2) ? max_gen : (max_seq / 2);
    int prompt_budget = max_seq - gen_reserve - 10;
    if (prompt_budget < 64) prompt_budget = 64;

    int history_budget = (prompt_budget * 4) / 5;

    if (im_start != -1 && im_end != -1) {
        int start_msg = msg_count - 6;
        if (start_msg < 0) start_msg = 0;

        for (int i = start_msg; i < msg_count - 2; i++) {
            if (chat_log[i].msg_type == MSG_USER) {
                tokens[count++] = im_start;
                count = encode_text_segment("user\n", tokens, count, history_budget);
                count = encode_text_segment(chat_log[i].text, tokens, count, history_budget);
                tokens[count++] = im_end;
                count = encode_text_segment("\n", tokens, count, history_budget);
            } else if (chat_log[i].msg_type == MSG_AI && strlen(chat_log[i].text) > 0) {
                tokens[count++] = im_start;
                count = encode_text_segment("assistant\n", tokens, count, history_budget);
                count = encode_text_segment(chat_log[i].text, tokens, count, history_budget);
                tokens[count++] = im_end;
                count = encode_text_segment("\n", tokens, count, history_budget);
            }
        }

        tokens[count++] = im_start;
        count = encode_text_segment("user\n", tokens, count, prompt_budget - 10);
        count = encode_text_segment(text, tokens, count, prompt_budget - 10);
        tokens[count++] = im_end;
        count = encode_text_segment("\n", tokens, count, prompt_budget - 10);

        tokens[count++] = im_start;
        count = encode_text_segment("assistant\n", tokens, count, prompt_budget);
    } else {
        int bos = find_token_id("<s>");
        if (bos != -1) tokens[count++] = bos;
        
        size_t pbuf_size = strlen(text) + 256;
        char* prompt_buf = malloc(pbuf_size);
        if (prompt_buf) {
            snprintf(prompt_buf, pbuf_size, "User: %s\nAssistant:", text);
            count = encode_text_segment(prompt_buf, tokens, count, prompt_budget);
            free(prompt_buf);
        }
    }
    return count;
}
void detokenize_bpe(const char* src, char* dst) {
    char* start = dst;
    while (*src && (dst - start) < 250) {
        if ((unsigned char)src[0] == 0xE2 && (unsigned char)src[1] == 0x96 && (unsigned char)src[2] == 0x81) {
            *dst++ = ' '; src += 3;
        } else if ((unsigned char)src[0] == 0xC4 && (unsigned char)src[1] == 0xA0) {
            *dst++ = ' '; src += 2;
        } else if ((unsigned char)src[0] == 0xC4 && (unsigned char)src[1] == 0x8A) {
            *dst++ = '\n'; src += 2;
        } else if ((unsigned char)src[0] == 0xC4 && (unsigned char)src[1] == 0x89) {
            *dst++ = ' '; src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void probe_gpu_hardware(void) {
    if (!use_gpu) {
        add_message("Hardware Configuration:\nGPU Acceleration Disabled in ai02.ini (Running on CPU).", MSG_SYS_OK);
        return;
    }
    if (init_opencl_gpu()) {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "Hardware Configuration:\nOpenCL GPU Kernel Compiled & Active!\nDevice: %s\nUploaded Tensors in VRAM: %d",
            gpu_device_name, gpu_weight_count);
        add_message(msg, MSG_SYS_OK);
    } else {
        add_message("Hardware Configuration:\nNo compatible OpenCL GPU runtime found. Falling back to 4x-unrolled CPU matrix multiplication.", MSG_SYS_ERR);
    }
}

DWORD WINAPI GenerationThread(LPVOID lpParam) {
    char* prompt = (char*)lpParam;
    if (!json_header) { add_message("Error: No LLaMA model loaded.", MSG_SYS_ERR); free(prompt); is_generating = 0; return 0; }
    add_message("", MSG_AI);

    load_ini_parameters();
    stop_generation = 0;

    int cur_max_gen = param_max_gen_tokens;
    if (cur_max_gen <= 0) cur_max_gen = 80;

    int cur_seq_len_cap = param_max_seq_len;
    if (cur_seq_len_cap < cur_max_gen + 128) {
        cur_seq_len_cap = cur_max_gen + 256;
    }

    int* tokens = (int*)calloc((size_t)cur_seq_len_cap, sizeof(int));
    if (!tokens) {
        add_message("Error: Out of Memory allocating token sequence buffer.", MSG_SYS_ERR);
        free(prompt); is_generating = 0; return 0;
    }

    int seq_len = tokenize_greedy(prompt, tokens, cur_seq_len_cap, cur_max_gen);

    float *x = calloc((size_t)dim, sizeof(float)), *xb = calloc((size_t)dim, sizeof(float));
    float *q = calloc((size_t)dim, sizeof(float)), *k = calloc((size_t)kv_dim, sizeof(float)), *v = calloc((size_t)kv_dim, sizeof(float));
    float *att = calloc((size_t)n_heads * (size_t)cur_seq_len_cap, sizeof(float)), *out = calloc((size_t)dim, sizeof(float));
    float *h_gate = calloc((size_t)hidden_dim, sizeof(float)), *h_up = calloc((size_t)hidden_dim, sizeof(float)), *h_swi = calloc((size_t)hidden_dim, sizeof(float));
    float *logits = calloc((size_t)vocab_size, sizeof(float));
    float *key_cache = calloc((size_t)n_layers * (size_t)cur_seq_len_cap * (size_t)kv_dim, sizeof(float));
    float *val_cache = calloc((size_t)n_layers * (size_t)cur_seq_len_cap * (size_t)kv_dim, sizeof(float));

    if (!x || !xb || !q || !k || !v || !att || !out || !h_gate || !h_up || !h_swi || !logits || !key_cache || !val_cache) {
        add_message("Error: Out of Memory allocating KV Cache.", MSG_SYS_ERR);
        free(tokens); free(x); free(xb); free(q); free(k); free(v); free(att); free(out);
        free(h_gate); free(h_up); free(h_swi); free(logits); free(key_cache); free(val_cache);
        free(prompt); is_generating = 0; return 0;
    }

    for (int pos = 0; pos < seq_len + cur_max_gen; pos++) {
        if (stop_generation) break;
        if (pos >= cur_seq_len_cap - 1) break;
        int current_token = tokens[pos];
        if (current_token < 0 || current_token >= vocab_size) current_token = 0;
        memcpy(x, &tok_emb[(size_t)current_token * (size_t)dim], (size_t)dim * sizeof(float));

        for (int l = 0; l < n_layers; l++) {
            rmsnorm(xb, x, layers[l].attn_norm, dim);
            matmul(q, xb, layers[l].wq, dim, dim);
            matmul(k, xb, layers[l].wk, dim, kv_dim);
            matmul(v, xb, layers[l].wv, dim, kv_dim);

            apply_rope(q, k, pos);

            int cache_offset = l * cur_seq_len_cap * kv_dim + pos * kv_dim;
            memcpy(&key_cache[cache_offset], k, (size_t)kv_dim * sizeof(float));
            memcpy(&val_cache[cache_offset], v, (size_t)kv_dim * sizeof(float));

            for (int h = 0; h < n_heads; h++) {
                int q_off = h * head_dim;
                int kv_h = h / (n_heads / n_kv_heads);

                float max_val = -1e9f;
                for (int t = 0; t <= pos; t++) {
                    float score = 0.0f;
                    int k_off = l * cur_seq_len_cap * kv_dim + t * kv_dim + kv_h * head_dim;
                    for (int i = 0; i < head_dim; i++) score += q[q_off + i] * key_cache[k_off + i];
                    score /= sqrtf((float)head_dim);
                    att[h * cur_seq_len_cap + t] = score;
                    if (score > max_val) max_val = score;
                }
                float exp_sum = 0.0f;
                for (int t = 0; t <= pos; t++) {
                    att[h * cur_seq_len_cap + t] = expf(att[h * cur_seq_len_cap + t] - max_val);
                    exp_sum += att[h * cur_seq_len_cap + t];
                }
                for (int t = 0; t <= pos; t++) att[h * cur_seq_len_cap + t] /= exp_sum;

                for (int i = 0; i < head_dim; i++) {
                    float val_sum = 0.0f;
                    for (int t = 0; t <= pos; t++) {
                        int v_off = l * cur_seq_len_cap * kv_dim + t * kv_dim + kv_h * head_dim;
                        val_sum += att[h * cur_seq_len_cap + t] * val_cache[v_off + i];
                    }
                    out[q_off + i] = val_sum;
                }
            }
            matmul(xb, out, layers[l].wo, dim, dim);
            for (int i = 0; i < dim; i++) x[i] += xb[i];

            rmsnorm(xb, x, layers[l].ffn_norm, dim);
            matmul(h_gate, xb, layers[l].ffn_gate, dim, hidden_dim);
            matmul(h_up, xb, layers[l].ffn_up, dim, hidden_dim);
            swiglu(h_swi, h_gate, h_up, hidden_dim);
            matmul(xb, h_swi, layers[l].ffn_down, hidden_dim, dim);
            for (int i = 0; i < dim; i++) x[i] += xb[i];
        }

        rmsnorm(xb, x, output_norm_w, dim);

        if (pos >= seq_len - 1) {
            matmul(logits, xb, lm_head_w, dim, vocab_size);

            int best_token = sample_token(logits, vocab_size, tokens, pos + 1,
                                          param_temperature, param_top_p, param_top_k, param_repeat_penalty);

            tokens[pos + 1] = best_token;
            if (best_token >= 0 && best_token < vocab_size && vocab_map[best_token].str) {
                char clean_str[256];
                detokenize_bpe(vocab_map[best_token].str, clean_str);
                if (strstr(clean_str, "<|im_end|>") != NULL ||
                    strstr(clean_str, "<|endoftext|>") != NULL ||
                    strstr(clean_str, "<|eot_id|>") != NULL) break;
                update_last_message(clean_str);
            }

            int im_end_id    = find_token_id("<|im_end|>");
            int eos_id       = find_token_id("</s>");
            int eot_id       = find_token_id("<|eot_id|>");
            int endoftext_id = find_token_id("<|endoftext|>");
            if (best_token == im_end_id ||
                (eos_id != -1 && best_token == eos_id) ||
                (eot_id != -1 && best_token == eot_id) ||
                (endoftext_id != -1 && best_token == endoftext_id)) break;
        }
    }

    free(tokens); free(x); free(xb); free(q); free(k); free(v); free(att); free(out);
    free(h_gate); free(h_up); free(h_swi); free(logits); free(key_cache); free(val_cache);
    free(prompt); is_generating = 0;
    return 0;
}

DWORD WINAPI BackgroundLoadThread(LPVOID lpParam) {
    char* filepath = (char*)lpParam;
    Sleep(100);

    if (load_llama_model(filepath)) {
        char dir_path[MAX_PATH]; strcpy(dir_path, filepath);
        DWORD attr = GetFileAttributesA(dir_path);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            char* ls = strrchr(dir_path, '\\'); if (ls) *ls = '\0';
        }
        strcpy(current_model_dir, dir_path);

        if (SendMessage(hCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)current_model_dir) == CB_ERR) {
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)current_model_dir);
            char model_list[2048];
            GetPrivateProfileStringA("Models", "List", "", model_list, sizeof(model_list), ini_path);
            if (strlen(model_list) > 0) strcat(model_list, ",");
            strcat(model_list, current_model_dir);
            WritePrivateProfileStringA("Models", "List", model_list, ini_path);
        }
        SendMessage(hCombo, CB_SELECTSTRING, -1, (LPARAM)current_model_dir);
        WritePrivateProfileStringA("Settings", "LastUsed", current_model_dir, ini_path);

        msg_count = 0;
        char msg[512];
        sprintf(msg, "Successfully Mapped LLaMA Architecture!\nPath: %s\nLayers: %d\nDimensions: %d\nTokens: %d", dir_path, n_layers, dim, vocab_size);
        add_message(msg, MSG_SYS_OK);
        probe_gpu_hardware();
    } else {
        add_message(last_error_msg, MSG_SYS_ERR);
    }

    is_loading_model = 0;
    free(filepath);
    return 0;
}

void load_ini_parameters(void) {
    char buf[64];
    param_max_gen_tokens = GetPrivateProfileIntA("Parameters", "MaxGenTokens", 80, ini_path);
    param_max_seq_len    = GetPrivateProfileIntA("Parameters", "MaxSeqLen", 512, ini_path);

    GetPrivateProfileStringA("Parameters", "Temperature", "0.0", buf, sizeof(buf), ini_path);
    param_temperature = (float)atof(buf);

    GetPrivateProfileStringA("Parameters", "TopP", "1.0", buf, sizeof(buf), ini_path);
    param_top_p = (float)atof(buf);

    param_top_k = GetPrivateProfileIntA("Parameters", "TopK", 0, ini_path);

    GetPrivateProfileStringA("Parameters", "RepeatPenalty", "1.0", buf, sizeof(buf), ini_path);
    param_repeat_penalty = (float)atof(buf);

    GetPrivateProfileStringA("Parameters", "RopeTheta", "0.0", buf, sizeof(buf), ini_path);
    param_rope_theta = (float)atof(buf);
    if (param_rope_theta > 0.0f) rope_theta = param_rope_theta;
}

void init_ini_and_combo(HWND hwnd) {
    GetModuleFileNameA(NULL, ini_path, MAX_PATH);
    char *last_slash = strrchr(ini_path, '\\');
    if (last_slash) *(last_slash + 1) = '\0';
    strcat(ini_path, "ai02.ini");

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA("Hardware", "UseGPU", "1", ini_path);
        WritePrivateProfileStringA("Models", "List", "C:\\", ini_path);
        WritePrivateProfileStringA("Settings", "LastUsed", "C:\\", ini_path);
        WritePrivateProfileStringA("Parameters", "MaxGenTokens", "80", ini_path);
        WritePrivateProfileStringA("Parameters", "MaxSeqLen", "512", ini_path);
        WritePrivateProfileStringA("Parameters", "Temperature", "0.0", ini_path);
        WritePrivateProfileStringA("Parameters", "TopP", "1.0", ini_path);
        WritePrivateProfileStringA("Parameters", "TopK", "0", ini_path);
        WritePrivateProfileStringA("Parameters", "RepeatPenalty", "1.0", ini_path);
        WritePrivateProfileStringA("Parameters", "RopeTheta", "0.0", ini_path);
    } else {
        char chk[32];
        if (GetPrivateProfileStringA("Parameters", "MaxGenTokens", "", chk, sizeof(chk), ini_path) == 0) {
            WritePrivateProfileStringA("Parameters", "MaxGenTokens", "80", ini_path);
            WritePrivateProfileStringA("Parameters", "MaxSeqLen", "512", ini_path);
            WritePrivateProfileStringA("Parameters", "Temperature", "0.0", ini_path);
            WritePrivateProfileStringA("Parameters", "TopP", "1.0", ini_path);
            WritePrivateProfileStringA("Parameters", "TopK", "0", ini_path);
            WritePrivateProfileStringA("Parameters", "RepeatPenalty", "1.0", ini_path);
            WritePrivateProfileStringA("Parameters", "RopeTheta", "0.0", ini_path);
        }
    }

    use_gpu = GetPrivateProfileIntA("Hardware", "UseGPU", 1, ini_path);
    load_ini_parameters();

    color_user = GetPrivateProfileIntA("Colors", "User", RGB(10, 132, 255), ini_path);
    color_ai = GetPrivateProfileIntA("Colors", "AI", RGB(229, 229, 234), ini_path);

    char model_list[2048];
    GetPrivateProfileStringA("Models", "List", "C:\\", model_list, sizeof(model_list), ini_path);
    GetPrivateProfileStringA("Settings", "LastUsed", "C:\\", current_model_dir, sizeof(current_model_dir), ini_path);

    hCombo = CreateWindowEx(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, 10, 200, 200, hwnd, (HMENU)ID_COMBO, GetModuleHandle(NULL), NULL);
    HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
    SendMessage(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

    char* token = strtok(model_list, ",");
    while (token) { SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)token); token = strtok(NULL, ","); }
    SendMessage(hCombo, CB_SELECTSTRING, -1, (LPARAM)current_model_dir);
}

LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        if (is_generating) return 0;
        char buf[1024]; GetWindowText(hwnd, buf, 1024);
        if (strlen(buf) > 0) {
            add_message(buf, MSG_USER); SetWindowText(hwnd, "");
            is_generating = 1;
            char* prompt_copy = strdup(buf);
            CreateThread(NULL, 0, GenerationThread, prompt_copy, 0, NULL);
        }
        return 0;
    }
    return CallWindowProc(OldEditProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ChatViewProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int hit = -1;
            for (int i = 0; i < msg_count; i++) {
                if (PtInRect(&chat_log[i].hit_box, pt)) { hit = i; break; }
            }
            if (hit != -1) {
                HDC hdc = GetDC(hwnd);
                HFONT hOld = (HFONT)SelectObject(hdc, hChatFont);
                int ch_idx = hit_test_bubble_char(hdc, hit, pt);
                SelectObject(hdc, hOld);
                ReleaseDC(hwnd, hdc);

                sel_bubble_idx = hit;
                sel_anchor_char = ch_idx;
                sel_caret_char = ch_idx;
                is_mouse_selecting = 1;
                SetCapture(hwnd);
            } else {
                sel_bubble_idx = -1;
                sel_anchor_char = -1;
                sel_caret_char = -1;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (is_mouse_selecting && sel_bubble_idx >= 0 && sel_bubble_idx < msg_count) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                HDC hdc = GetDC(hwnd);
                HFONT hOld = (HFONT)SelectObject(hdc, hChatFont);
                int ch_idx = hit_test_bubble_char(hdc, sel_bubble_idx, pt);
                SelectObject(hdc, hOld);
                ReleaseDC(hwnd, hdc);

                if (ch_idx != sel_caret_char) {
                    sel_caret_char = ch_idx;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (is_mouse_selecting) {
                is_mouse_selecting = 0;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            for (int i = 0; i < msg_count; i++) {
                if (PtInRect(&chat_log[i].hit_box, pt)) {
                    HDC hdc = GetDC(hwnd);
                    HFONT hOld = (HFONT)SelectObject(hdc, hChatFont);
                    int ch_idx = hit_test_bubble_char(hdc, i, pt);
                    SelectObject(hdc, hOld);
                    ReleaseDC(hwnd, hdc);

                    const char* txt = chat_log[i].text;
                    int len = (int)strlen(txt);
                    int w_start = ch_idx, w_end = ch_idx;
                    while (w_start > 0 && !isspace((unsigned char)txt[w_start - 1])) w_start--;
                    while (w_end < len && !isspace((unsigned char)txt[w_end])) w_end++;
                    sel_bubble_idx = i;
                    sel_anchor_char = w_start;
                    sel_caret_char = w_end;
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if ((GetKeyState(VK_CONTROL) & 0x8000) && sel_bubble_idx >= 0 && sel_bubble_idx < msg_count) {
                if (wParam == 'C') {
                    int s_min = (sel_anchor_char < sel_caret_char) ? sel_anchor_char : sel_caret_char;
                    int s_max = (sel_anchor_char > sel_caret_char) ? sel_anchor_char : sel_caret_char;
                    if (s_max > s_min) {
                        copy_range_to_clipboard(hwnd, chat_log[sel_bubble_idx].text, s_min, s_max);
                    }
                    return 0;
                } else if (wParam == 'A') {
                    sel_anchor_char = 0;
                    sel_caret_char = (int)strlen(chat_log[sel_bubble_idx].text);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            break;
        }

        case WM_RBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int hit = -1;
            for (int i = 0; i < msg_count; i++) {
                if (PtInRect(&chat_log[i].hit_box, pt)) { hit = i; break; }
            }
            POINT screen_pt = pt;
            ClientToScreen(hwnd, &screen_pt);

            if (hit != -1) {
                if (sel_bubble_idx != hit) {
                    sel_bubble_idx = hit;
                    sel_anchor_char = 0;
                    sel_caret_char = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                show_bubble_context_menu(hwnd, hit, screen_pt);
            } else {
                HMENU hMenu = CreatePopupMenu();
                if (is_generating) {
                    AppendMenuA(hMenu, MF_STRING, IDM_STOP_OUTPUT, "Stop Output");
                    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                }
                AppendMenuA(hMenu, MF_STRING, IDM_SAVE_PDF, "Save Chat to PDF");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, IDM_SET_COLOR_U, "Set User Bubble Color...");
                AppendMenuA(hMenu, MF_STRING, IDM_SET_COLOR_AI, "Set AI Bubble Color...");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, IDM_CLEAR_CHAT, "Clear Chat");
                AppendMenuA(hMenu, MF_STRING, IDM_WIPE_MEMORY, "Wipe Chat Memory");
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                handle_menu_cmd(hwnd, cmd);
            }
            return 0;
        }

        case WM_VSCROLL: {
            int action = LOWORD(wParam);
            if (action == SB_LINEUP) scroll_y += 40;
            else if (action == SB_LINEDOWN) scroll_y -= 40;
            else if (action == SB_PAGEUP) scroll_y += visible_space;
            else if (action == SB_PAGEDOWN) scroll_y -= visible_space;
            else if (action == SB_THUMBTRACK) {
                SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
                GetScrollInfo(hwnd, SB_VERT, &si);
                scroll_y = -si.nTrackPos;
            }
            if (scroll_y > 0) scroll_y = 0;
            if (scroll_y < -(total_height - visible_space) && total_height > visible_space)
                scroll_y = -(total_height - visible_space);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            scroll_y += (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 40 : -40;
            if (scroll_y > 0) scroll_y = 0;
            if (scroll_y < -(total_height - visible_space) && total_height > visible_space)
                scroll_y = -(total_height - visible_space);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int win_width = clientRect.right - clientRect.left;
            visible_space = clientRect.bottom - clientRect.top;

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
            HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

            HBRUSH bgBrush = CreateSolidBrush(RGB(242, 242, 247));
            FillRect(hdcMem, &clientRect, bgBrush);
            HBRUSH brushUser = CreateSolidBrush(color_user);
            HBRUSH brushAI = CreateSolidBrush(color_ai);
            HBRUSH brushSysOK = CreateSolidBrush(color_sys_ok);
            HBRUSH brushSysErr = CreateSolidBrush(color_sys_err);

            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hChatFont);
            TEXTMETRICA tm;
            GetTextMetricsA(hdcMem, &tm);
            int lh = tm.tmHeight;

            int y_cursor = 20, bubble_padding = 12, max_bubble_w = (win_width * 7) / 10;
            if (max_bubble_w < 120) max_bubble_w = 120;
            total_height = 20;

            for (int i = 0; i < msg_count; i++) {
                int n_lines = compute_wrapped_lines(hdcMem, chat_log[i].text, max_bubble_w, NULL, 0);
                total_height += (n_lines * lh) + (bubble_padding * 2) + 15;
            }

            if (scroll_y < -(total_height - visible_space) && total_height > visible_space)
                scroll_y = -(total_height - visible_space);
            if (total_height <= visible_space || scroll_y > 0)
                scroll_y = 0;
            y_cursor += scroll_y;

            for (int i = 0; i < msg_count; i++) {
                WrappedLine lines[1024];
                int n_lines = compute_wrapped_lines(hdcMem, chat_log[i].text, max_bubble_w, lines, 1024);
                int max_line_px = 20;
                for (int l = 0; l < n_lines; l++) {
                    SIZE sz = {0, 0};
                    if (lines[l].len > 0) {
                        GetTextExtentPoint32A(hdcMem, chat_log[i].text + lines[l].start_idx, lines[l].len, &sz);
                    }
                    if (sz.cx > max_line_px) max_line_px = sz.cx;
                }

                int b_width = max_line_px + (bubble_padding * 2);
                int b_height = (n_lines * lh) + (bubble_padding * 2);

                RECT bubbleRect = (chat_log[i].msg_type == MSG_USER)
                    ? (RECT){win_width - b_width - 25, y_cursor, win_width - 25, y_cursor + b_height}
                    : (RECT){25, y_cursor, 25 + b_width, y_cursor + b_height};

                RECT drawRect = {
                    bubbleRect.left + bubble_padding,
                    bubbleRect.top + bubble_padding,
                    bubbleRect.right - bubble_padding,
                    bubbleRect.bottom - bubble_padding
                };

                chat_log[i].hit_box = bubbleRect;
                chat_log[i].text_box = drawRect;

                SelectObject(hdcMem, GetStockObject(NULL_PEN));
                HBRUSH currentBrush = brushAI;
                if (chat_log[i].msg_type == MSG_USER) currentBrush = brushUser;
                else if (chat_log[i].msg_type == MSG_SYS_OK) currentBrush = brushSysOK;
                else if (chat_log[i].msg_type == MSG_SYS_ERR) currentBrush = brushSysErr;

                SelectObject(hdcMem, currentBrush);
                RoundRect(hdcMem, bubbleRect.left, bubbleRect.top, bubbleRect.right, bubbleRect.bottom, 20, 20);

                POINT tail[3];
                if (chat_log[i].msg_type == MSG_USER) {
                    tail[0].x = bubbleRect.right - 5;  tail[0].y = bubbleRect.bottom - 20;
                    tail[1].x = bubbleRect.right - 5;  tail[1].y = bubbleRect.bottom - 5;
                    tail[2].x = bubbleRect.right + 10; tail[2].y = bubbleRect.bottom - 5;
                } else {
                    tail[0].x = bubbleRect.left + 5;   tail[0].y = bubbleRect.bottom - 20;
                    tail[1].x = bubbleRect.left + 5;   tail[1].y = bubbleRect.bottom - 5;
                    tail[2].x = bubbleRect.left - 10;  tail[2].y = bubbleRect.bottom - 5;
                }
                Polygon(hdcMem, tail, 3);

                draw_bubble_text_and_selection(hdcMem, i);
                y_cursor += b_height + 15;
            }

            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            SelectObject(hdcMem, hOldFont);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
            DeleteObject(bgBrush);
            DeleteObject(brushUser);
            DeleteObject(brushAI);
            DeleteObject(brushSysOK);
            DeleteObject(brushSysErr);

            SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS, 0, total_height, (UINT)visible_space, -scroll_y, 0 };
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            srand((unsigned int)time(NULL));
            DragAcceptFiles(hwnd, TRUE);
            hChatFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                   DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            hChatView = CreateWindowEx(0, "ChatCanvasClass", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                       0, 0, 450, 550, hwnd, NULL, GetModuleHandle(NULL), NULL);

            init_ini_and_combo(hwnd);
            hEdit = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                   10, 10, 100, 30, hwnd, (HMENU)ID_EDIT, GetModuleHandle(NULL), NULL);
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hChatFont, TRUE);
            OldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

            if (load_llama_model(current_model_dir)) {
                msg_count = 0;
                char msg[512];
                sprintf(msg, "Successfully Loaded LLaMA!\nPath: %s\nLayers: %d\nDimensions: %d\nTokens: %d", current_model_dir, n_layers, dim, vocab_size);
                add_message(msg, MSG_SYS_OK);
                probe_gpu_hardware();
            } else {
                add_message("Drag and drop ANY file or folder from your downloaded LLaMA repository to load the model.", MSG_SYS_OK);
                if (strlen(last_error_msg) > 0) add_message(last_error_msg, MSG_SYS_ERR);
            }
            return 0;
        }

        case WM_UPDATE_CHAT: {
            InvalidateRect(hChatView, NULL, FALSE);
            return 0;
        }

        case WM_DROPFILES: {
            if (is_generating || is_loading_model) { DragFinish((HDROP)wParam); return 0; }
            HDROP hDrop = (HDROP)wParam;
            char filepath[MAX_PATH];
            DragQueryFileA(hDrop, 0, filepath, MAX_PATH);
            DragFinish(hDrop);

            DWORD attr = GetFileAttributesA(filepath);
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                FILE *f = fopen(filepath, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    
                    long read_size = fsize < 260000 ? fsize : 260000; 
                    char *buf = malloc(read_size + 256);
                    if (buf) {
                        int prefix_len = snprintf(buf, 256, "Attached file %s:\n\n", filepath);
                        fread(buf + prefix_len, 1, read_size, f);
                        buf[prefix_len + read_size] = '\0';
                        add_message(buf, MSG_USER);
                        free(buf);
                    }
                    fclose(f);
                } else {
                    add_message("Error: Could not read attached file.", MSG_SYS_ERR);
                }
            } else {
                add_message("Loading model weights into RAM & GPU VRAM...", MSG_SYS_OK);
                is_loading_model = 1;
                char* path_copy = strdup(filepath);
                CreateThread(NULL, 0, BackgroundLoadThread, path_copy, 0, NULL);
            }
            return 0;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_COMBO && HIWORD(wParam) == CBN_SELCHANGE) {
                if (is_generating) return 0;
                int idx = SendMessage(hCombo, CB_GETCURSEL, 0, 0);
                if (idx != CB_ERR) {
                    SendMessage(hCombo, CB_GETLBTEXT, idx, (LPARAM)current_model_dir);
                    WritePrivateProfileStringA("Settings", "LastUsed", current_model_dir, ini_path);
                    if (load_llama_model(current_model_dir)) {
                        msg_count = 0;
                        char msg[512];
                        sprintf(msg, "Switched LLaMA Architecture:\n%s\nLayers: %d", current_model_dir, n_layers);
                        add_message(msg, MSG_SYS_OK);
                        probe_gpu_hardware();
                    } else {
                        add_message(last_error_msg, MSG_SYS_ERR);
                    }
                }
            }
            return 0;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam), height = HIWORD(lParam);
            int bar_h = 50;
            int chat_h = (height > bar_h) ? (height - bar_h) : 0;

            MoveWindow(hChatView, 0, 0, width, chat_h, TRUE);
            MoveWindow(hCombo, 10, chat_h + 10, 200, 300, TRUE);
            MoveWindow(hEdit, 220, chat_h + 10, (width > 230) ? (width - 230) : 10, 30, TRUE);
            InvalidateRect(hChatView, NULL, TRUE);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            return SendMessage(hChatView, uMsg, wParam, lParam);
        }

        case WM_DESTROY:
            cleanup_memory();
            if (hChatFont) DeleteObject(hChatFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wcChat = {0};
    wcChat.style = CS_DBLCLKS;
    wcChat.lpfnWndProc = ChatViewProc;
    wcChat.hInstance = hInstance;
    wcChat.lpszClassName = "ChatCanvasClass";
    wcChat.hCursor = LoadCursor(NULL, IDC_IBEAM);
    RegisterClass(&wcChat);

    const char CLASS_NAME[] = "LlamaChatClass";
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(242, 242, 247));
    RegisterClass(&wc);

    hMainWindow = CreateWindowEx(0, CLASS_NAME, "Generative LLaMA Core (OpenCL GPU)",
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 500, 650,
                                 NULL, NULL, hInstance, NULL);
    if (hMainWindow == NULL) return 0;
    ShowWindow(hMainWindow, nCmdShow);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}