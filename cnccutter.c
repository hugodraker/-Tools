/* ============================================================================
 * CNC SVG DXF Cutter - C Implementation with Delta Sync
 *
 * COMPILATION INSTRUCTIONS:
 *   gcc -Os -s cnccutter.c -o cnccutter.exe -mwindows -lgdi32 -lshell32 -luser32 -lcomdlg32 -lm
 *
 * 2026 HUGO
 *
 * REQUIREMENTS: Windows XP or later
 * DEPENDENCIES: Win32 API only
 * ============================================================================ */
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MAX_SHAPES 1024
#define MAX_HISTORY 32
#define SIDEBAR_WIDTH 200
#define MAX_SIM_SEGS 20000
#define MAX_SHAPE_LINES 256
enum {
    ID_BTN_PACK = 101, ID_BTN_ADDFILE, ID_BTN_UNDO, ID_BTN_REDO, ID_BTN_SAVE, ID_BTN_LOAD, 
    ID_BTN_EXPSVG, ID_BTN_EXPDXF, ID_TXT_W, ID_TXT_H, ID_CHK_LOCK, ID_BTN_APPLY,
    ID_TXT_TOOLR, ID_TXT_CLEARZ, ID_TXT_CUTZ, ID_CHK_CENTER, ID_CHK_VINYL, ID_BTN_EXPGCODE,
    ID_BTN_SIM, ID_BTN_SIM_PLAY, ID_BTN_SIM_PREV, ID_BTN_SIM_NEXT, ID_BTN_SIM_START, ID_BTN_SIM_END,
    ID_TXT_SIM_SPD, ID_BTN_ZOOM_EXT, ID_BTN_ZOOM_IN, ID_BTN_ZOOM_OUT, ID_BTN_CLONE,
    ID_LIST_ORDER, ID_CHK_ENVELOPE, ID_LBL_C, ID_LBL_E, ID_LBL_I, ID_CHK_REPOS
};

typedef struct { float x1, y1, x2, y2; } ShapeLine;
typedef struct { 
    char filepath[MAX_PATH]; 
    float orig_w, orig_h; 
    float x, y, w, h; 
    int is_oval;
    ShapeLine lines[MAX_SHAPE_LINES];
    int num_lines;
    int cut_type; // 0 = Center, 1 = External, 2 = Internal Inner
} Shape;

typedef struct { float x1, y1, x2, y2, len; int type; } SimSeg;
Shape shapes[MAX_SHAPES];
int num_shapes = 0;
float sheet_w = 800.0f, sheet_h = 600.0f;

// View State
float zoom = 0.5f, pan_x = 10.0f, pan_y = 10.0f;
int is_dragging = 0, is_panning = 0, is_shift_zooming = 0;
int selected_shape = -1, resize_target = -2, dim_target = -2;
float drag_offset_x = 0, drag_offset_y = 0, pan_start_px = 0, pan_start_py = 0, zoom_start = 0;
int pan_start_x = 0, pan_start_y = 0;
float edit_orig_w = 0, edit_orig_h = 0;

// Simulation State
int sim_active = 0, sim_playing = 0;
SimSeg sim_segs[MAX_SIM_SEGS];
int num_sim_segs = 0;
float sim_total_dist = 0, sim_current_dist = 0, sim_speed = 10.0f;
float sim_tool_r = 0;

// History
typedef struct { Shape shapes[MAX_SHAPES]; int num_shapes; float sheet_w, sheet_h; } HistoryState;
typedef struct { float x1, y1, x2, y2; int used; } TempLine;
HistoryState history[MAX_HISTORY];
int history_pos = -1, history_count = 0;

HWND hBtn[46];

WNDPROC OldListProc;
int lb_drag_active = 0;
POINT lb_drag_pt;

TempLine tlines[MAX_SHAPE_LINES * 10];
int num_tlines = 0;

char* Attr(char* tagStr, char* tagEnd, const char* attrName) {
    char search[32]; char* a; int i;
    const char* quotes = "\"'";
    for (i = 0; i < 2; i++) {
        sprintf(search, " %s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "\n%s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "\t%s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
        sprintf(search, "<%s=%c", attrName, quotes[i]);
        a = strstr(tagStr, search); if (a && a < tagEnd) return a + strlen(search);
    }
    return NULL;
}

double AttrF(char* tagStr, char* tagEnd, const char* attrName, double defVal) {
    char* valStr = Attr(tagStr, tagEnd, attrName);
    if (valStr) return atof(valStr);
    return defVal;
}

int GetNextSVGFloat(char** pp, double* val) {
    char* p = *pp;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) p++;
    if (!*p || *p == '"' || *p == '\'' || *p == ')' || *p == '<' || *p == '>') return 0;
    if (isalpha(*p) && *p != 'e' && *p != 'E') return 0; 
    *val = strtod(p, &p);
    *pp = p;
    return 1;
}

void MatMul(double* A, double* B, double* out) {
    out[0] = A[0]*B[0] + A[2]*B[1];
    out[1] = A[1]*B[0] + A[3]*B[1];
    out[2] = A[0]*B[2] + A[2]*B[3];
    out[3] = A[1]*B[2] + A[3]*B[3];
    out[4] = A[0]*B[4] + A[2]*B[5] + A[4];
    out[5] = A[1]*B[4] + A[3]*B[5] + A[5];
}

void ParseTransform(char* str, double* mat, char limitChar) {
    char* p = str; double t[6], temp[6]; int i;
    while (p && *p && *p != limitChar) {
        if (strncmp(p, "matrix", 6) == 0) {
            p += 6; while(*p && *p != '(' && *p != limitChar) p++; if(*p=='(') p++;
            for(i=0; i<6; i++) { if (!GetNextSVGFloat(&p, &t[i])) t[i] = (i==0||i==3)?1:0; }
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else if (strncmp(p, "translate", 9) == 0) {
            p += 9; while(*p && *p != '(' && *p != limitChar) p++; if(*p=='(') p++;
            for(i=0; i<6; i++) t[i] = (i==0||i==3)?1:0;
            GetNextSVGFloat(&p, &t[4]);
            if (!GetNextSVGFloat(&p, &t[5])) t[5] = 0;
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5; while(*p && *p != '(' && *p != limitChar) p++; if(*p=='(') p++;
            for(i=0; i<6; i++) t[i] = (i==0||i==3)?1:0;
            GetNextSVGFloat(&p, &t[0]);
            if (!GetNextSVGFloat(&p, &t[3])) t[3] = t[0];
            MatMul(mat, t, temp); for(i=0; i<6; i++) mat[i] = temp[i];
        } else {
            p++;
        }
    }
}

void save_state() {
    if (history_pos < history_count - 1) history_count = history_pos + 1;
    if (history_count == MAX_HISTORY) { memmove(&history[0], &history[1], sizeof(HistoryState) * (MAX_HISTORY - 1)); history_pos--; history_count--; }
    history_pos++; history_count++;
    history[history_pos].num_shapes = num_shapes; history[history_pos].sheet_w = sheet_w; history[history_pos].sheet_h = sheet_h;
    memcpy(history[history_pos].shapes, shapes, sizeof(Shape) * num_shapes);
}
void load_state(int pos) {
    if (pos >= 0 && pos < history_count) {
        history_pos = pos; num_shapes = history[pos].num_shapes; sheet_w = history[pos].sheet_w; sheet_h = history[pos].sheet_h;
        memcpy(shapes, history[pos].shapes, sizeof(Shape) * num_shapes); selected_shape = -1;
    }
}

void save_job() {
    FILE* f = fopen("job.nest", "wb"); if (!f) return;
    fwrite(&num_shapes, sizeof(int), 1, f); fwrite(&sheet_w, sizeof(float), 1, f); fwrite(&sheet_h, sizeof(float), 1, f);
    fwrite(shapes, sizeof(Shape), num_shapes, f); fclose(f);
}
void load_job() {
    FILE* f = fopen("job.nest", "rb"); if (!f) return;
    fread(&num_shapes, sizeof(int), 1, f); fread(&sheet_w, sizeof(float), 1, f); fread(&sheet_h, sizeof(float), 1, f);
    fread(shapes, sizeof(Shape), num_shapes, f); fclose(f); save_state();
}

void export_dxf() {
    FILE* out = fopen("layout.dxf", "w"); if(!out) return;
    fprintf(out, "0\nSECTION\n2\nENTITIES\n");
    for (int i = 0; i < num_shapes; i++) {
        if (shapes[i].is_oval) {
            fprintf(out, "0\nCIRCLE\n8\n0\n10\n%f\n20\n%f\n40\n%f\n", shapes[i].x+shapes[i].w/2, shapes[i].y+shapes[i].h/2, shapes[i].w/2);
        } else {
            fprintf(out, "0\nLWPOLYLINE\n8\n0\n90\n4\n70\n1\n");
            fprintf(out, "10\n%f\n20\n%f\n10\n%f\n20\n%f\n", shapes[i].x, shapes[i].y, shapes[i].x+shapes[i].w, shapes[i].y);
            fprintf(out, "10\n%f\n20\n%f\n10\n%f\n20\n%f\n", shapes[i].x+shapes[i].w, shapes[i].y+shapes[i].h, shapes[i].x, shapes[i].y+shapes[i].h);
        }
    }
    fprintf(out, "0\nLWPOLYLINE\n8\nSHEET\n90\n4\n70\n1\n10\n0.0\n20\n0.0\n10\n%f\n20\n0.0\n10\n%f\n20\n%f\n10\n0.0\n20\n%f\n", sheet_w, sheet_w, sheet_h, sheet_h);
    fprintf(out, "0\nENDSEC\n0\nEOF\n"); fclose(out);
}

void update_listbox(HWND hwnd) {
    HWND hLb = hBtn[ID_LIST_ORDER];
    SendMessage(hLb, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < num_shapes; i++) {
        char buf[MAX_PATH + 32];
        sprintf(buf, "%d: %s", i + 1, shapes[i].filepath);
        SendMessage(hLb, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

int cmp_box(const void* a, const void* b) { return (((Shape*)b)->h > ((Shape*)a)->h) - (((Shape*)b)->h < ((Shape*)a)->h); }

void pack_shapes(HWND hwnd) {
    if (num_shapes == 0) return;
    
    if (SendMessage(hBtn[ID_CHK_REPOS], BM_GETCHECK, 0, 0) != BST_CHECKED) {
        update_listbox(hwnd);
        save_state(); dim_target = -1; SetTimer(hwnd, 1, 2000, NULL); InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    char buf[64]; GetWindowText(hBtn[ID_TXT_TOOLR], buf, 64);
    float tr = atof(buf);
    if (SendMessage(hBtn[ID_CHK_VINYL], BM_GETCHECK, 0, 0) == BST_CHECKED) tr = 0.0f;
    float padding = tr * 4.0f; 

    qsort(shapes, num_shapes, sizeof(Shape), cmp_box);
    float step = sheet_w / 500.0f; if (step < 1.0f) step = 1.0f;

    for (int i = 0; i < num_shapes; i++) {
        float R = (shapes[i].w > shapes[i].h ? shapes[i].w : shapes[i].h) / 2.0f;
        float pad_R = R + padding/2.0f;
        float bx = 0, by = 999999.0f;
        
        if (shapes[i].is_oval) {
            float mx = sheet_w - pad_R * 2.0f; if (mx < 0) mx = 0;
            bx = pad_R;
            for (float sx = pad_R; sx <= mx + pad_R; sx += step) {
                float cy = pad_R;
                for (int j = 0; j < i; j++) {
                    if (shapes[j].is_oval) {
                        float pr = (shapes[j].w > shapes[j].h ? shapes[j].w : shapes[j].h) / 2.0f + padding/2.0f;
                        float dx = sx - (shapes[j].x + shapes[j].w/2.0f);
                        float dsq = (pad_R + pr) * (pad_R + pr);
                        if (dx * dx < dsq) {
                            float hit = (shapes[j].y + shapes[j].h/2.0f) + sqrt(dsq - (dx * dx));
                            if (hit > cy) cy = hit;
                        }
                    } else {
                        float rw = shapes[j].w + padding, rh = shapes[j].h + padding;
                        float rx = shapes[j].x - padding/2.0f, ry = shapes[j].y - padding/2.0f;
                        if (sx + pad_R > rx && sx - pad_R < rx + rw) {
                            float hit = ry + rh + pad_R;
                            if (hit > cy) cy = hit;
                        }
                    }
                }
                if (cy < by) { by = cy; bx = sx; }
            }
            shapes[i].x = bx - pad_R + padding/2.0f;
            shapes[i].y = by - pad_R + padding/2.0f;
        } else {
            float w_pad = shapes[i].w + padding, h_pad = shapes[i].h + padding;
            float mx = sheet_w - w_pad; if (mx < 0) mx = 0;
            for (float sx = 0; sx <= mx; sx += step) {
                float cy = 0;
                for (int j = 0; j < i; j++) {
                    float jx = shapes[j].x - padding/2.0f;
                    float jy = shapes[j].y - padding/2.0f;
                    float jw = shapes[j].w + padding;
                    float jh = shapes[j].h + padding;
                    if (sx + w_pad > jx && sx < jx + jw) {
                        float hit = jy + jh;
                        if (hit > cy) cy = hit;
                    }
                }
                if (cy < by) { by = cy; bx = sx; }
            }
            shapes[i].x = bx + padding/2.0f;
            shapes[i].y = by + padding/2.0f;
        }
    }
    update_listbox(hwnd);
    save_state(); dim_target = -1; SetTimer(hwnd, 1, 2000, NULL); InvalidateRect(hwnd, NULL, TRUE);
}

void add_seg(float x1, float y1, float x2, float y2, int type) {
    if (num_sim_segs >= MAX_SIM_SEGS) return;
    float dx = x2 - x1, dy = y2 - y1;
    sim_segs[num_sim_segs++] = (SimSeg){x1, y1, x2, y2, sqrt(dx*dx + dy*dy), type};
}

void generate_toolpath(FILE* f) { 
    char buf[64];
    int is_vinyl = (SendMessage(hBtn[ID_CHK_VINYL], BM_GETCHECK, 0, 0) == BST_CHECKED);
    int use_envelope = (SendMessage(hBtn[ID_CHK_ENVELOPE], BM_GETCHECK, 0, 0) == BST_CHECKED);
    GetWindowText(hBtn[ID_TXT_TOOLR], buf, 64); float tr = is_vinyl ? 0.0f : atof(buf); sim_tool_r = tr;
    GetWindowText(hBtn[ID_TXT_CLEARZ], buf, 64); float cz = is_vinyl ? 0.0f : atof(buf);
    GetWindowText(hBtn[ID_TXT_CUTZ], buf, 64); float depth = atof(buf);
    int center = (SendMessage(hBtn[ID_CHK_CENTER], BM_GETCHECK, 0, 0) == BST_CHECKED);

    if(f) { fprintf(f, "G21\nG90\nG0 Z%.3f\n", cz); }
    num_sim_segs = 0; float cur_x = 0, cur_y = 0;

    for (int i = 0; i < num_shapes; i++) {
        float cx = shapes[i].x + shapes[i].w / 2.0f, cy = shapes[i].y + shapes[i].h / 2.0f;
        float shape_offset = 0.0f;
        if (shapes[i].cut_type == 1) shape_offset = tr; 
        else if (shapes[i].cut_type == 2) shape_offset = -tr; 
        
        if (use_envelope) {
            if (shapes[i].is_oval) {
                float r = (shapes[i].w / 2.0f) + shape_offset;
                if (r < 0.1f) r = 0.1f;
                if (center) {
                    if(f) { fprintf(f, "G0 X%.3f Y%.3f\nG1 Z%.3f F300\nG1 X%.3f Y%.3f F800\n", cx, cy, depth, cx + r, cy); }
                    add_seg(cur_x, cur_y, cx, cy, 0); add_seg(cx, cy, cx, cy, 1); 
                    add_seg(cx, cy, cx + r, cy, 2); cur_x = cx + r; cur_y = cy;
                } else {
                    if(f) { fprintf(f, "G0 X%.3f Y%.3f\nG1 Z%.3f F300\n", cx + r, cy, depth); }
                    add_seg(cur_x, cur_y, cx + r, cy, 0); add_seg(cx + r, cy, cx + r, cy, 1); cur_x = cx + r; cur_y = cy;
                }
                if(f) { fprintf(f, "G2 X%.3f Y%.3f I%.3f J0.000 F1000\n", cx + r, cy, -r); }
                int segs = fmax(16, r * 2);
                for(int s=1; s<=segs; s++) {
                    float a = (float)s / segs * 2.0 * M_PI;
                    float nx = cx + r * cos(a), ny = cy + r * sin(a);
                    add_seg(cur_x, cur_y, nx, ny, 2); cur_x = nx; cur_y = ny;
                }
            } else {
                float L = shapes[i].x - shape_offset, R = shapes[i].x + shapes[i].w + shape_offset;
                float T = shapes[i].y - shape_offset, B = shapes[i].y + shapes[i].h + shape_offset;
                if (center) {
                    if(f) { fprintf(f, "G0 X%.3f Y%.3f\nG1 Z%.3f F300\nG1 X%.3f Y%.3f F800\n", cx, cy, depth, L, T); }
                    add_seg(cur_x, cur_y, cx, cy, 0); add_seg(cx, cy, cx, cy, 1); add_seg(cx, cy, L, T, 2); cur_x = L; cur_y = T;
                } else {
                    if(f) { fprintf(f, "G0 X%.3f Y%.3f\nG1 Z%.3f F300\n", L, T, depth); }
                    add_seg(cur_x, cur_y, L, T, 0); add_seg(L, T, L, T, 1); cur_x = L; cur_y = T;
                }
                if(f) { fprintf(f, "G1 X%.3f Y%.3f\nG1 X%.3f Y%.3f\nG1 X%.3f Y%.3f\nG1 X%.3f Y%.3f\n", R, T, R, B, L, B, L, T); }
                add_seg(cur_x, cur_y, R, T, 2); add_seg(R, T, R, B, 2); add_seg(R, B, L, B, 2); add_seg(L, B, L, T, 2); cur_x = L; cur_y = T;
            }
        } else {
            int shape_center_done = 0;
            for (int j = 0; j < shapes[i].num_lines; j++) {
                float sx = shapes[i].w / shapes[i].orig_w;
                float sy = shapes[i].h / shapes[i].orig_h;
                float x1 = shapes[i].x + shapes[i].lines[j].x1 * sx;
                float y1 = shapes[i].y + shapes[i].lines[j].y1 * sy;
                float x2 = shapes[i].x + shapes[i].lines[j].x2 * sx;
                float y2 = shapes[i].y + shapes[i].lines[j].y2 * sy;

                float dx = x2 - x1, dy = y2 - y1;
                float len = sqrt(dx*dx + dy*dy);
                if (len > 0.001f && shape_offset != 0.0f) {
                    float nx = -dy / len;
                    float ny = dx / len;
                    x1 += nx * shape_offset; y1 += ny * shape_offset;
                    x2 += nx * shape_offset; y2 += ny * shape_offset;
                }

                if (center && !shape_center_done) {
                    if(f) { fprintf(f, "G0 X%.3f Y%.3f\nG1 Z%.3f F300\nG1 X%.3f Y%.3f F800\n", cx, cy, depth, x1, y1); }
                    add_seg(cur_x, cur_y, cx, cy, 0); add_seg(cx, cy, cx, cy, 1);
                    add_seg(cx, cy, x1, y1, 2); cur_x = x1; cur_y = y1;
                    shape_center_done = 1;
                } else {
                    if(f) { fprintf(f, "G0 X%.3f Y%.3f\nG1 Z%.3f F300\n", x1, y1, depth); }
                    add_seg(cur_x, cur_y, x1, y1, 0); add_seg(x1, y1, x1, y1, 1); cur_x = x1; cur_y = y1;
                }

                if(f) { fprintf(f, "G1 X%.3f Y%.3f\n", x2, y2); }
                add_seg(cur_x, cur_y, x2, y2, 2); cur_x = x2; cur_y = y2;
                if(f && !is_vinyl) { fprintf(f, "G0 Z%.3f\n", cz); }
            }
        }
        if(f && !is_vinyl) { fprintf(f, "G0 Z%.3f\n", cz); }
    }
    if(f) { fprintf(f, "M30\n"); fclose(f); MessageBox(NULL, "Exported layout.nc", "Success", MB_OK); }
    
    sim_total_dist = 0;
    for(int i=0; i<num_sim_segs; i++) {
        if (sim_segs[i].type == 1) sim_segs[i].len = 50.0f;
        sim_total_dist += sim_segs[i].len;
    }
}

void zoom_extents(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int cw = rc.right - SIDEBAR_WIDTH - 20, ch = rc.bottom - 20;
    if (cw < 100) cw = 100; if (ch < 100) ch = 100;
    float zx = cw / sheet_w, zy = ch / sheet_h;
    zoom = (zx < zy) ? zx : zy; pan_x = 10; pan_y = 10;
    InvalidateRect(hwnd, NULL, FALSE);
}

void add_shape_from_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    char* d = (char*)calloc(1, sz + 1);
    fread(d, 1, sz, f); d[sz] = '\0'; fclose(f);
    
    num_tlines = 0;
    
    double matStack[10][6], cMat[6]; int sp = 0, i;
    for(i=0;i<6;i++) matStack[0][i] = (i==0||i==3)?1:0;
    char* p = d;
    
    while ((p = strchr(p, '<')) != NULL) {
        p++;
        if (strncmp(p, "/g", 2) == 0) { if (sp > 0) sp--; continue; }
        char* tagEnd = strchr(p, '>'); if (!tagEnd) break;

        for(i=0;i<6;i++) cMat[i] = matStack[sp][i];
        char* transStr = Attr(p, tagEnd, "transform");
        if (transStr) ParseTransform(transStr, cMat, '"');

        if (p[0] == 'g' && (isspace((unsigned char)p[1]) || p[1] == '>')) {
            if (sp < 9) { sp++; for(i=0;i<6;i++) matStack[sp][i] = cMat[i]; }
        } else if (strncmp(p, "circle", 6) == 0 || strncmp(p, "ellipse", 7) == 0) {
            double cx = AttrF(p, tagEnd, "cx", 0.0), cy = AttrF(p, tagEnd, "cy", 0.0);
            double r = AttrF(p, tagEnd, "r", 0.0);
            if (r == 0) {
                double rx = AttrF(p, tagEnd, "rx", 0.0), ry = AttrF(p, tagEnd, "ry", 0.0);
                r = (rx+ry)/2.0;
            }
            if (r > 0 && num_shapes < MAX_SHAPES) {
                Shape* s = &shapes[num_shapes];
                memset(s, 0, sizeof(Shape));
                const char* fn = strrchr(path, '\\'); fn = fn ? fn + 1 : path;
                sprintf(s->filepath, "%s (Oval)", fn);
                float ccx = cMat[0]*cx + cMat[2]*cy + cMat[4];
                float ccy = cMat[1]*cx + cMat[3]*cy + cMat[5];
                float ccr = r * sqrt(cMat[0]*cMat[0] + cMat[1]*cMat[1]);
                s->is_oval = 1;
                s->x = ccx - ccr; s->y = ccy - ccr;
                s->w = ccr*2; s->h = ccr*2;
                s->orig_w = s->w; s->orig_h = s->h;
                s->cut_type = 0;
                num_shapes++;
            }
        } else if (strncmp(p, "rect", 4) == 0) {
            double rx = AttrF(p, tagEnd, "x", 0.0), ry = AttrF(p, tagEnd, "y", 0.0);
            double rw = AttrF(p, tagEnd, "width", 0.0), rh = AttrF(p, tagEnd, "height", 0.0);
            if (rw > 0 && rh > 0 && num_tlines < MAX_SHAPE_LINES * 10 - 4) {
                double pts[8] = {rx,ry, rx+rw,ry, rx+rw,ry+rh, rx,ry+rh};
                for(i=0; i<4; i++) {
                    double x1 = pts[i*2], y1 = pts[i*2+1];
                    double x2 = pts[((i+1)%4)*2], y2 = pts[((i+1)%4)*2+1];
                    tlines[num_tlines++] = (TempLine){
                        (float)(cMat[0]*x1 + cMat[2]*y1 + cMat[4]), (float)(cMat[1]*x1 + cMat[3]*y1 + cMat[5]),
                        (float)(cMat[0]*x2 + cMat[2]*y2 + cMat[4]), (float)(cMat[1]*x2 + cMat[3]*y2 + cMat[5]), 0
                    };
                }
            }
        } else if (strncmp(p, "line", 4) == 0) {
            double x1 = AttrF(p, tagEnd, "x1", 0.0), y1 = AttrF(p, tagEnd, "y1", 0.0);
            double x2 = AttrF(p, tagEnd, "x2", 0.0), y2 = AttrF(p, tagEnd, "y2", 0.0);
            if (num_tlines < MAX_SHAPE_LINES * 10) {
                tlines[num_tlines++] = (TempLine){
                    (float)(cMat[0]*x1 + cMat[2]*y1 + cMat[4]), (float)(cMat[1]*x1 + cMat[3]*y1 + cMat[5]),
                    (float)(cMat[0]*x2 + cMat[2]*y2 + cMat[4]), (float)(cMat[1]*x2 + cMat[3]*y2 + cMat[5]), 0
                };
            }
        } else if (strncmp(p, "poly", 4) == 0) {
            char* ptsStr = Attr(p, tagEnd, "points");
            if (ptsStr) {
                double firstX, firstY, prevX, prevY, px, py;
                if (GetNextSVGFloat(&ptsStr, &firstX) && GetNextSVGFloat(&ptsStr, &firstY)) {
                    prevX = firstX; prevY = firstY;
                    while (GetNextSVGFloat(&ptsStr, &px) && GetNextSVGFloat(&ptsStr, &py)) {
                        if (num_tlines < MAX_SHAPE_LINES * 10) {
                            tlines[num_tlines++] = (TempLine){
                                (float)(cMat[0]*prevX + cMat[2]*prevY + cMat[4]), (float)(cMat[1]*prevX + cMat[3]*prevY + cMat[5]),
                                (float)(cMat[0]*px + cMat[2]*py + cMat[4]), (float)(cMat[1]*px + cMat[3]*py + cMat[5]), 0
                            };
                        }
                        prevX = px; prevY = py;
                    }
                    if (strncmp(p, "polygon", 7) == 0 && num_tlines < MAX_SHAPE_LINES * 10) {
                        tlines[num_tlines++] = (TempLine){
                            (float)(cMat[0]*prevX + cMat[2]*prevY + cMat[4]), (float)(cMat[1]*prevX + cMat[3]*prevY + cMat[5]),
                            (float)(cMat[0]*firstX + cMat[2]*firstY + cMat[4]), (float)(cMat[1]*firstX + cMat[3]*firstY + cMat[5]), 0
                        };
                    }
                }
            }
        } else if (strncmp(p, "path", 4) == 0) {
            char* dStr = Attr(p, tagEnd, "d");
            if (dStr) {
                double curX = 0, curY = 0, startX = 0, startY = 0; char cmd = 'M';
                while (*dStr && *dStr != '"') {
                    while (*dStr && isspace((unsigned char)*dStr)) dStr++;
                    if (isalpha((unsigned char)*dStr)) { cmd = *dStr; dStr++; }
                    if (toupper(cmd) == 'Z') {
                        if (num_tlines < MAX_SHAPE_LINES * 10) {
                            tlines[num_tlines++] = (TempLine){
                                (float)(cMat[0]*curX + cMat[2]*curY + cMat[4]), (float)(cMat[1]*curX + cMat[3]*curY + cMat[5]),
                                (float)(cMat[0]*startX + cMat[2]*startY + cMat[4]), (float)(cMat[1]*startX + cMat[3]*startY + cMat[5]), 0
                            };
                        }
                        curX = startX; curY = startY;
                        break;
                    }
                    double argX, argY;
                    if (toupper(cmd) == 'M' || toupper(cmd) == 'L') {
                        if (!GetNextSVGFloat(&dStr, &argX) || !GetNextSVGFloat(&dStr, &argY)) break;
                        if (cmd == 'm' || cmd == 'l') { argX += curX; argY += curY; }
                        if (toupper(cmd) == 'M') { startX = argX; startY = argY; curX = argX; curY = argY; cmd = (cmd == 'M') ? 'L' : 'l'; continue; }
                        if (num_tlines < MAX_SHAPE_LINES * 10) {
                            tlines[num_tlines++] = (TempLine){
                                (float)(cMat[0]*curX + cMat[2]*curY + cMat[4]), (float)(cMat[1]*curX + cMat[3]*curY + cMat[5]),
                                (float)(cMat[0]*argX + cMat[2]*argY + cMat[4]), (float)(cMat[1]*argX + cMat[3]*argY + cMat[5]), 0
                            };
                        }
                        curX = argX; curY = argY;
                    } else {
                        GetNextSVGFloat(&dStr, &argX);
                    }
                }
            }
        }
    }
    free(d);
    
    int cut_idx = 1;
    for(int j = 0; j < num_tlines; j++) {
        if (tlines[j].used || num_shapes >= MAX_SHAPES) continue;
        
        Shape* s = &shapes[num_shapes];
        memset(s, 0, sizeof(Shape));
        const char* fn = strrchr(path, '\\'); fn = fn ? fn + 1 : path;
        sprintf(s->filepath, "%s (Cut %d)", fn, cut_idx++);
        s->is_oval = 0; s->cut_type = 0;
        
        s->lines[s->num_lines++] = (ShapeLine){tlines[j].x1, tlines[j].y1, tlines[j].x2, tlines[j].y2};
        tlines[j].used = 1;
        
        int added;
        do {
            added = 0;
            for(int k = 0; k < num_tlines; k++) {
                if (tlines[k].used || s->num_lines >= MAX_SHAPE_LINES) continue;
                
                int connects = 0;
                for(int l = 0; l < s->num_lines; l++) {
                    float tol = 0.1f;
                    if ((fabs(tlines[k].x1 - s->lines[l].x1) < tol && fabs(tlines[k].y1 - s->lines[l].y1) < tol) ||
                        (fabs(tlines[k].x1 - s->lines[l].x2) < tol && fabs(tlines[k].y1 - s->lines[l].y2) < tol) ||
                        (fabs(tlines[k].x2 - s->lines[l].x1) < tol && fabs(tlines[k].y2 - s->lines[l].y1) < tol) ||
                        (fabs(tlines[k].x2 - s->lines[l].x2) < tol && fabs(tlines[k].y2 - s->lines[l].y2) < tol)) {
                        connects = 1; break;
                    }
                }
                if (connects) {
                    s->lines[s->num_lines++] = (ShapeLine){tlines[k].x1, tlines[k].y1, tlines[k].x2, tlines[k].y2};
                    tlines[k].used = 1; added = 1;
                }
            }
        } while (added && s->num_lines < MAX_SHAPE_LINES);
        
        float min_x = 99999, min_y = 99999, max_x = -99999, max_y = -99999;
        for (int k = 0; k < s->num_lines; k++) {
            if(s->lines[k].x1 < min_x) min_x = s->lines[k].x1; if(s->lines[k].x1 > max_x) max_x = s->lines[k].x1;
            if(s->lines[k].y1 < min_y) min_y = s->lines[k].y1; if(s->lines[k].y1 > max_y) max_y = s->lines[k].y1;
            if(s->lines[k].x2 < min_x) min_x = s->lines[k].x2; if(s->lines[k].x2 > max_x) max_x = s->lines[k].x2;
            if(s->lines[k].y2 < min_y) min_y = s->lines[k].y2; if(s->lines[k].y2 > max_y) max_y = s->lines[k].y2;
        }
        
        for (int k = 0; k < s->num_lines; k++) {
            s->lines[k].x1 -= min_x; s->lines[k].y1 -= min_y;
            s->lines[k].x2 -= min_x; s->lines[k].y2 -= min_y;
        }
        
        s->x = min_x; s->y = min_y; 
        s->w = max_x - min_x; if(s->w < 1) s->w = 1;
        s->h = max_y - min_y; if(s->h < 1) s->h = 1;
        s->orig_w = s->w; s->orig_h = s->h;
        num_shapes++;
    }
}

HWND create_ctl(const char* cls, const char* txt, int style, int id, HWND p) {
    HWND h = CreateWindow(cls, txt, WS_CHILD|WS_VISIBLE|style, 0,0,10,10, p, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0); return h;
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            int click_x = GET_X_LPARAM(lParam);
            int idx = SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            
            if (HIWORD(idx) == 0 && click_x >= 55) {
                int item = LOWORD(idx);
                if (click_x >= 55 && click_x <= 69) { shapes[item].cut_type = 0; }
                else if (click_x >= 85 && click_x <= 99) { shapes[item].cut_type = 1; }
                else if (click_x >= 115 && click_x <= 129) { shapes[item].cut_type = 2; }
                else { break; } // didn't click exactly on a box
                
                InvalidateRect(hwnd, NULL, FALSE);
                InvalidateRect(GetParent(hwnd), NULL, FALSE);
                return 0; // Handled the checkbox click
            }
            
            if (HIWORD(idx) == 0 && SendMessage(hwnd, LB_GETSEL, LOWORD(idx), 0) > 0 && !(wParam & (MK_SHIFT|MK_CONTROL))) {
                lb_drag_active = 1;
                lb_drag_pt.x = GET_X_LPARAM(lParam); lb_drag_pt.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
                return 0; 
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (lb_drag_active && (wParam & MK_LBUTTON)) {
                int dx = GET_X_LPARAM(lParam) - lb_drag_pt.x;
                int dy = GET_Y_LPARAM(lParam) - lb_drag_pt.y;
                if (abs(dx) > 3 || abs(dy) > 3) {
                    lb_drag_active = 2; 
                }
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (lb_drag_active) {
                int was_dragging = (lb_drag_active == 2);
                lb_drag_active = 0;
                ReleaseCapture();
                if (was_dragging) {
                    int drop_idx = SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lParam);
                    if (HIWORD(drop_idx) == 0) {
                        drop_idx = LOWORD(drop_idx);
                        int selCount = SendMessage(hwnd, LB_GETSELCOUNT, 0, 0);
                        if (selCount > 0) {
                            int* sels = malloc(selCount * sizeof(int));
                            SendMessage(hwnd, LB_GETSELITEMS, selCount, (LPARAM)sels);
                            Shape* temp = malloc(num_shapes * sizeof(Shape));
                            int t_idx = 0;
                            
                            for (int i = 0; i <= num_shapes; i++) {
                                if (i == drop_idx) {
                                    for (int j = 0; j < selCount; j++) temp[t_idx++] = shapes[sels[j]];
                                }
                                if (i < num_shapes) {
                                    int is_sel = 0;
                                    for (int j = 0; j < selCount; j++) { if (sels[j] == i) { is_sel = 1; break; } }
                                    if (!is_sel) temp[t_idx++] = shapes[i];
                                }
                            }
                            memcpy(shapes, temp, num_shapes * sizeof(Shape));
                            free(temp); free(sels);
                            
                            HWND hParent = GetParent(hwnd);
                            SendMessage(hwnd, LB_SETSEL, FALSE, -1);
                            update_listbox(hParent);
                            save_state();
                            InvalidateRect(hParent, NULL, TRUE);
                        }
                    }
                } else {
                    SendMessage(hwnd, LB_SETSEL, FALSE, -1);
                    int idx = SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lParam);
                    if (HIWORD(idx) == 0) SendMessage(hwnd, LB_SETSEL, TRUE, LOWORD(idx));
                    InvalidateRect(GetParent(hwnd), NULL, FALSE);
                }
                return 0;
            }
            break;
        }
    }
    return CallWindowProc(OldListProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            hBtn[ID_BTN_PACK] = create_ctl("BUTTON", "Pack", BS_PUSHBUTTON, ID_BTN_PACK, hwnd);
            hBtn[ID_CHK_REPOS] = create_ctl("BUTTON", "Auto Pack", BS_AUTOCHECKBOX, ID_CHK_REPOS, hwnd);
            hBtn[ID_BTN_ADDFILE] = create_ctl("BUTTON", "Add File...", BS_PUSHBUTTON, ID_BTN_ADDFILE, hwnd);
            hBtn[ID_BTN_UNDO] = create_ctl("BUTTON", "Undo", BS_PUSHBUTTON, ID_BTN_UNDO, hwnd);
            hBtn[ID_BTN_REDO] = create_ctl("BUTTON", "Redo", BS_PUSHBUTTON, ID_BTN_REDO, hwnd);
            hBtn[ID_BTN_CLONE] = create_ctl("BUTTON", "Clone Sel", BS_PUSHBUTTON, ID_BTN_CLONE, hwnd);
            
            hBtn[ID_BTN_SAVE] = create_ctl("BUTTON", "Save Job", BS_PUSHBUTTON, ID_BTN_SAVE, hwnd);
            hBtn[ID_BTN_LOAD] = create_ctl("BUTTON", "Load Job", BS_PUSHBUTTON, ID_BTN_LOAD, hwnd);
            hBtn[ID_BTN_EXPSVG] = create_ctl("BUTTON", "Export SVG", BS_PUSHBUTTON, ID_BTN_EXPSVG, hwnd);
            hBtn[ID_BTN_EXPDXF] = create_ctl("BUTTON", "Export DXF", BS_PUSHBUTTON, ID_BTN_EXPDXF, hwnd);
            
            hBtn[28] = create_ctl("STATIC", "Resize Item/Sheet:", 0, 0, hwnd);
            hBtn[ID_TXT_W] = create_ctl("EDIT", "", WS_BORDER, ID_TXT_W, hwnd);
            hBtn[ID_TXT_H] = create_ctl("EDIT", "", WS_BORDER, ID_TXT_H, hwnd);
            hBtn[ID_CHK_LOCK] = create_ctl("BUTTON", "Lock", BS_AUTOCHECKBOX, ID_CHK_LOCK, hwnd);
            hBtn[ID_BTN_APPLY] = create_ctl("BUTTON", "Apply", BS_PUSHBUTTON, ID_BTN_APPLY, hwnd);
            SendMessage(hBtn[ID_CHK_LOCK], BM_SETCHECK, BST_CHECKED, 0);

            hBtn[27] = create_ctl("STATIC", "Tool R | ClearZ | CutZ", 0, 0, hwnd);
            hBtn[ID_TXT_TOOLR] = create_ctl("EDIT", "3.175", WS_BORDER, ID_TXT_TOOLR, hwnd);
            hBtn[ID_TXT_CLEARZ] = create_ctl("EDIT", "5.0", WS_BORDER, ID_TXT_CLEARZ, hwnd);
            hBtn[ID_TXT_CUTZ] = create_ctl("EDIT", "-1.0", WS_BORDER, ID_TXT_CUTZ, hwnd);
            
            hBtn[ID_CHK_CENTER] = create_ctl("BUTTON", "Center", BS_AUTOCHECKBOX, ID_CHK_CENTER, hwnd);
            hBtn[ID_CHK_VINYL] = create_ctl("BUTTON", "Vinyl", BS_AUTOCHECKBOX, ID_CHK_VINYL, hwnd);
            hBtn[ID_CHK_ENVELOPE] = create_ctl("BUTTON", "Env Only", BS_AUTOCHECKBOX, ID_CHK_ENVELOPE, hwnd);
            
            hBtn[ID_BTN_EXPGCODE] = create_ctl("BUTTON", "Exp GCode", BS_PUSHBUTTON, ID_BTN_EXPGCODE, hwnd);
            hBtn[ID_BTN_SIM] = create_ctl("BUTTON", "Simulate Mode", BS_PUSHBUTTON, ID_BTN_SIM, hwnd);
            
            hBtn[ID_BTN_SIM_START] = create_ctl("BUTTON", "|<", BS_PUSHBUTTON, ID_BTN_SIM_START, hwnd);
            hBtn[ID_BTN_SIM_PREV] = create_ctl("BUTTON", "<", BS_PUSHBUTTON, ID_BTN_SIM_PREV, hwnd);
            hBtn[ID_BTN_SIM_PLAY] = create_ctl("BUTTON", "Play/Pse", BS_PUSHBUTTON, ID_BTN_SIM_PLAY, hwnd);
            hBtn[ID_BTN_SIM_NEXT] = create_ctl("BUTTON", ">", BS_PUSHBUTTON, ID_BTN_SIM_NEXT, hwnd);
            hBtn[ID_BTN_SIM_END] = create_ctl("BUTTON", ">|", BS_PUSHBUTTON, ID_BTN_SIM_END, hwnd);
            hBtn[26] = create_ctl("STATIC", "Spd:", 0, 0, hwnd);
            hBtn[ID_TXT_SIM_SPD] = create_ctl("EDIT", "10", WS_BORDER, ID_TXT_SIM_SPD, hwnd);

            hBtn[ID_BTN_ZOOM_EXT] = create_ctl("BUTTON", "Zoom Fit", BS_PUSHBUTTON, ID_BTN_ZOOM_EXT, hwnd);
            hBtn[ID_BTN_ZOOM_IN] = create_ctl("BUTTON", "+", BS_PUSHBUTTON, ID_BTN_ZOOM_IN, hwnd);
            hBtn[ID_BTN_ZOOM_OUT] = create_ctl("BUTTON", "-", BS_PUSHBUTTON, ID_BTN_ZOOM_OUT, hwnd);
            
            hBtn[ID_LBL_C] = create_ctl("STATIC", "Cent", 0, ID_LBL_C, hwnd);
            hBtn[ID_LBL_E] = create_ctl("STATIC", "Ext", 0, ID_LBL_E, hwnd);
            hBtn[ID_LBL_I] = create_ctl("STATIC", "Inn", 0, ID_LBL_I, hwnd);
            
            hBtn[ID_LIST_ORDER] = CreateWindowEx(0, "LISTBOX", NULL, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_EXTENDEDSEL|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS, 0, 0, 10, 10, hwnd, (HMENU)ID_LIST_ORDER, GetModuleHandle(NULL), NULL);
            OldListProc = (WNDPROC)SetWindowLongPtr(hBtn[ID_LIST_ORDER], GWLP_WNDPROC, (LONG_PTR)ListProc);
            
            save_state(); return 0;
        }

        case WM_SIZE: {
            int cx = LOWORD(lParam), cy = HIWORD(lParam), pX = cx - SIDEBAR_WIDTH + 5, y = 5, w3=60;
            MoveWindow(hBtn[ID_BTN_PACK], pX, y, 45, 25, TRUE); 
            MoveWindow(hBtn[ID_CHK_REPOS], pX+50, y, 75, 25, TRUE);
            MoveWindow(hBtn[ID_BTN_ADDFILE], pX+130, y, 60, 25, TRUE); y+=28;
            
            MoveWindow(hBtn[ID_BTN_UNDO], pX, y, w3, 25, TRUE); MoveWindow(hBtn[ID_BTN_REDO], pX+w3+2, y, w3, 25, TRUE); MoveWindow(hBtn[ID_BTN_CLONE], pX+w3*2+4, y, 65, 25, TRUE); y+=30;
            
            MoveWindow(hBtn[ID_BTN_ZOOM_EXT], pX, y, 90, 25, TRUE); MoveWindow(hBtn[ID_BTN_ZOOM_IN], pX+95, y, 40, 25, TRUE); MoveWindow(hBtn[ID_BTN_ZOOM_OUT], pX+140, y, 40, 25, TRUE); y+=30;
            
            int w2=90;
            MoveWindow(hBtn[ID_BTN_SAVE], pX, y, w2, 25, TRUE); MoveWindow(hBtn[ID_BTN_LOAD], pX+w2+5, y, w2, 25, TRUE); y+=28;
            MoveWindow(hBtn[ID_BTN_EXPSVG], pX, y, w2, 25, TRUE); MoveWindow(hBtn[ID_BTN_EXPDXF], pX+w2+5, y, w2, 25, TRUE); y+=32;
            
            MoveWindow(hBtn[28], pX, y, 190, 20, TRUE); y+=20;
            MoveWindow(hBtn[ID_TXT_W], pX, y, 55, 22, TRUE); MoveWindow(hBtn[ID_TXT_H], pX+60, y, 55, 22, TRUE); 
            MoveWindow(hBtn[ID_CHK_LOCK], pX+120, y, 60, 22, TRUE); y+=25;
            MoveWindow(hBtn[ID_BTN_APPLY], pX, y, 190, 25, TRUE); y+=32;

            MoveWindow(hBtn[27], pX, y, 190, 20, TRUE); y+=20;
            MoveWindow(hBtn[ID_TXT_TOOLR], pX, y, 60, 22, TRUE); MoveWindow(hBtn[ID_TXT_CLEARZ], pX+65, y, 60, 22, TRUE); MoveWindow(hBtn[ID_TXT_CUTZ], pX+130, y, 60, 22, TRUE); y+=25;
            
            MoveWindow(hBtn[ID_CHK_CENTER], pX, y, 60, 22, TRUE); MoveWindow(hBtn[ID_CHK_VINYL], pX+65, y, 50, 22, TRUE); MoveWindow(hBtn[ID_CHK_ENVELOPE], pX+120, y, 70, 22, TRUE); y+=25;
            
            MoveWindow(hBtn[ID_BTN_EXPGCODE], pX, y, 90, 25, TRUE); MoveWindow(hBtn[ID_BTN_SIM], pX+95, y, 90, 25, TRUE); y+=30;

            MoveWindow(hBtn[ID_BTN_SIM_START], pX, y, 30, 25, TRUE); MoveWindow(hBtn[ID_BTN_SIM_PREV], pX+32, y, 30, 25, TRUE); 
            MoveWindow(hBtn[ID_BTN_SIM_PLAY], pX+64, y, 65, 25, TRUE); MoveWindow(hBtn[ID_BTN_SIM_NEXT], pX+131, y, 30, 25, TRUE); MoveWindow(hBtn[ID_BTN_SIM_END], pX+163, y, 30, 25, TRUE); y+=28;
            MoveWindow(hBtn[26], pX, y, 35, 22, TRUE); MoveWindow(hBtn[ID_TXT_SIM_SPD], pX+40, y, 50, 22, TRUE); y+=30;
            
            MoveWindow(hBtn[ID_LBL_C], pX + 55, y, 30, 20, TRUE);
            MoveWindow(hBtn[ID_LBL_E], pX + 85, y, 30, 20, TRUE);
            MoveWindow(hBtn[ID_LBL_I], pX + 115, y, 30, 20, TRUE); y+=20;
            MoveWindow(hBtn[ID_LIST_ORDER], pX, y, 190, cy - y - 10, TRUE);

            int sh = sim_active ? SW_SHOW : SW_HIDE;
            for(int i=ID_BTN_SIM_START; i<=ID_TXT_SIM_SPD; i++) ShowWindow(hBtn[i], sh); ShowWindow(hBtn[26], sh);
            return 0;
        }

        case WM_DRAWITEM: {
            if (wParam == ID_LIST_ORDER) {
                DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
                if (dis->itemID == (UINT)-1) return 1;
                
                if (dis->itemState & ODS_SELECTED) {
                    FillRect(dis->hDC, &dis->rcItem, (HBRUSH)(COLOR_HIGHLIGHT+1));
                    SetTextColor(dis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    FillRect(dis->hDC, &dis->rcItem, (HBRUSH)(COLOR_WINDOW+1));
                    SetTextColor(dis->hDC, GetSysColor(COLOR_WINDOWTEXT));
                }
                
                SetBkMode(dis->hDC, TRANSPARENT);
                
                char buf[256];
                SendMessage(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)buf);
                
                RECT rText = dis->rcItem; 
                rText.left += 2; rText.right = rText.left + 50;
                DrawText(dis->hDC, buf, -1, &rText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                
                int ct = shapes[dis->itemID].cut_type;
                
                RECT rC = dis->rcItem; rC.left = 55; rC.right = rC.left + 14; rC.top += 2; rC.bottom -= 2;
                DrawFrameControl(dis->hDC, &rC, DFC_BUTTON, DFCS_BUTTONCHECK | (ct==0?DFCS_CHECKED:0));
                
                RECT rE = dis->rcItem; rE.left = 85; rE.right = rE.left + 14; rE.top += 2; rE.bottom -= 2;
                DrawFrameControl(dis->hDC, &rE, DFC_BUTTON, DFCS_BUTTONCHECK | (ct==1?DFCS_CHECKED:0));
                
                RECT rI = dis->rcItem; rI.left = 115; rI.right = rI.left + 14; rI.top += 2; rI.bottom -= 2;
                DrawFrameControl(dis->hDC, &rI, DFC_BUTTON, DFCS_BUTTONCHECK | (ct==2?DFCS_CHECKED:0));
                
                return 1;
            }
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_LIST_ORDER && HIWORD(wParam) == LBN_SELCHANGE) {
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            
            if (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_CHANGE) {
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
            }
            
            if (HIWORD(wParam) == EN_KILLFOCUS) {
                if (LOWORD(wParam) == ID_TXT_W || LOWORD(wParam) == ID_TXT_H) {
                    SendMessage(hwnd, WM_COMMAND, ID_BTN_APPLY, 0);
                }
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
            }

            switch (LOWORD(wParam)) {
                case ID_BTN_PACK: pack_shapes(hwnd); break;
                case ID_BTN_ADDFILE: {
                    OPENFILENAME ofn; char szFile[260] = {0};
                    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "SVG and DXF\0*.svg;*.dxf\0All\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn) == TRUE) { add_shape_from_file(szFile); update_listbox(hwnd); pack_shapes(hwnd); }
                    break;
                }
                case ID_BTN_UNDO: load_state(history_pos - 1); update_listbox(hwnd); InvalidateRect(hwnd, NULL, TRUE); break;
                case ID_BTN_REDO: load_state(history_pos + 1); update_listbox(hwnd); InvalidateRect(hwnd, NULL, TRUE); break;
                case ID_BTN_SAVE: {
                    OPENFILENAME ofn; char szFile[260] = "job.nest";
                    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "Nest Jobs\0*.nest\0All\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
                    if (GetSaveFileName(&ofn)) {
                        FILE* f = fopen(szFile, "wb");
                        if (f) {
                            fwrite(&num_shapes, sizeof(int), 1, f);
                            fwrite(&sheet_w, sizeof(float), 1, f);
                            fwrite(&sheet_h, sizeof(float), 1, f);
                            fwrite(shapes, sizeof(Shape), num_shapes, f);
                            fclose(f);
                        }
                    }
                    break;
                }
                case ID_BTN_LOAD: {
                    OPENFILENAME ofn; char szFile[260] = {0};
                    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "Nest Jobs\0*.nest\0All\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) {
                        FILE* f = fopen(szFile, "rb");
                        if (f) {
                            fread(&num_shapes, sizeof(int), 1, f);
                            fread(&sheet_w, sizeof(float), 1, f);
                            fread(&sheet_h, sizeof(float), 1, f);
                            fread(shapes, sizeof(Shape), num_shapes, f);
                            fclose(f);
                            save_state(); update_listbox(hwnd); InvalidateRect(hwnd, NULL, TRUE);
                        }
                    }
                    break;
                }
                case ID_BTN_EXPSVG: {
                    OPENFILENAME ofn; char szFile[260] = "layout.svg";
                    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "SVG Files\0*.svg\0All\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
                    if (GetSaveFileName(&ofn)) {
                        FILE* out = fopen(szFile, "w");
                        if(out) {
                            fprintf(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%f\" height=\"%f\">\n", sheet_w, sheet_h);
                            fprintf(out, "  <rect x=\"0\" y=\"0\" width=\"%f\" height=\"%f\" fill=\"none\" stroke=\"red\" />\n", sheet_w, sheet_h);
                            for (int i = 0; i < num_shapes; i++) {
                                if (shapes[i].is_oval) {
                                    fprintf(out, "  <ellipse cx=\"%f\" cy=\"%f\" rx=\"%f\" ry=\"%f\" fill=\"none\" stroke=\"blue\" />\n", shapes[i].x + shapes[i].w/2, shapes[i].y + shapes[i].h/2, shapes[i].w/2, shapes[i].h/2);
                                } else {
                                    for(int j=0; j<shapes[i].num_lines; j++) {
                                        float sx = shapes[i].w / shapes[i].orig_w;
                                        float sy = shapes[i].h / shapes[i].orig_h;
                                        fprintf(out, "  <line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" stroke=\"blue\" />\n", 
                                            shapes[i].x + shapes[i].lines[j].x1*sx, shapes[i].y + shapes[i].lines[j].y1*sy,
                                            shapes[i].x + shapes[i].lines[j].x2*sx, shapes[i].y + shapes[i].lines[j].y2*sy);
                                    }
                                }
                            }
                            fprintf(out, "</svg>\n"); fclose(out);
                        }
                    }
                    break;
                }
                case ID_BTN_EXPDXF: {
                    OPENFILENAME ofn; char szFile[260] = "layout.dxf";
                    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "DXF Files\0*.dxf\0All\0*.*\0";
                    ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
                    if (GetSaveFileName(&ofn)) {
                        FILE* out = fopen(szFile, "w");
                        if(out) {
                            fprintf(out, "0\nSECTION\n2\nENTITIES\n");
                            for (int i = 0; i < num_shapes; i++) {
                                if (shapes[i].is_oval) {
                                    fprintf(out, "0\nCIRCLE\n8\n0\n10\n%f\n20\n%f\n40\n%f\n", shapes[i].x+shapes[i].w/2, shapes[i].y+shapes[i].h/2, shapes[i].w/2);
                                } else {
                                    for(int j=0; j<shapes[i].num_lines; j++) {
                                        float sx = shapes[i].w / shapes[i].orig_w;
                                        float sy = shapes[i].h / shapes[i].orig_h;
                                        fprintf(out, "0\nLINE\n8\n0\n10\n%f\n20\n%f\n11\n%f\n21\n%f\n", 
                                            shapes[i].x + shapes[i].lines[j].x1*sx, shapes[i].y + shapes[i].lines[j].y1*sy,
                                            shapes[i].x + shapes[i].lines[j].x2*sx, shapes[i].y + shapes[i].lines[j].y2*sy);
                                    }
                                }
                            }
                            fprintf(out, "0\nLWPOLYLINE\n8\nSHEET\n90\n4\n70\n1\n10\n0.0\n20\n0.0\n10\n%f\n20\n0.0\n10\n%f\n20\n%f\n10\n0.0\n20\n%f\n", sheet_w, sheet_w, sheet_h, sheet_h);
                            fprintf(out, "0\nENDSEC\n0\nEOF\n"); fclose(out);
                        }
                    }
                    break;
                }
                case ID_BTN_APPLY: {
                    if (resize_target < -1) break; char buf[64];
                    GetWindowText(hBtn[ID_TXT_W], buf, 64); float nw = atof(buf); GetWindowText(hBtn[ID_TXT_H], buf, 64); float nh = atof(buf);
                    
                    if (SendMessage(hBtn[ID_CHK_LOCK], BM_GETCHECK, 0, 0) == BST_CHECKED) {
                        if (nw != edit_orig_w && edit_orig_w > 0) nh = nw * (edit_orig_h / edit_orig_w); else if (nh != edit_orig_h && edit_orig_h > 0) nw = nh * (edit_orig_w / edit_orig_h);
                        sprintf(buf, "%.2f", nw); SetWindowText(hBtn[ID_TXT_W], buf); sprintf(buf, "%.2f", nh); SetWindowText(hBtn[ID_TXT_H], buf);
                    }
                    if (resize_target == -1) {
                        sheet_w = nw; sheet_h = nh;
                    } else if (resize_target >= 0) {
                        shapes[resize_target].w = nw; shapes[resize_target].h = nh;
                    }
                    edit_orig_w = nw; edit_orig_h = nh; save_state(); dim_target = resize_target; SetTimer(hwnd, 1, 2000, NULL); InvalidateRect(hwnd, NULL, TRUE); 
                    break;
                }
                case ID_BTN_CLONE: {
                    if (selected_shape >= 0 && num_shapes < MAX_SHAPES) { shapes[num_shapes] = shapes[selected_shape]; shapes[num_shapes].x += 20; shapes[num_shapes].y += 20; selected_shape = num_shapes++; save_state(); update_listbox(hwnd); InvalidateRect(hwnd, NULL, TRUE); } break;
                }
                case ID_BTN_ZOOM_EXT: zoom_extents(hwnd); break;
                case ID_BTN_ZOOM_IN: zoom *= 1.2f; InvalidateRect(hwnd, NULL, TRUE); break;
                case ID_BTN_ZOOM_OUT: zoom /= 1.2f; InvalidateRect(hwnd, NULL, TRUE); break;
                case ID_BTN_EXPGCODE: generate_toolpath(fopen("layout.nc", "w")); break;
                case ID_BTN_SIM: {
                    sim_active = !sim_active;
                    if(sim_active) { generate_toolpath(NULL); sim_current_dist = 0; sim_playing = 0; KillTimer(hwnd, 2); }
                    RECT rc; GetClientRect(hwnd, &rc); SendMessage(hwnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom)); InvalidateRect(hwnd, NULL, TRUE); break;
                }
                case ID_BTN_SIM_PLAY: {
                    sim_playing = !sim_playing;
                    if(sim_playing) SetTimer(hwnd, 2, 30, NULL); else KillTimer(hwnd, 2);
                    break;
                }
                case ID_BTN_SIM_START: sim_current_dist = 0; InvalidateRect(hwnd, NULL, TRUE); break;
                case ID_BTN_SIM_END: sim_current_dist = sim_total_dist; InvalidateRect(hwnd, NULL, TRUE); break;
                case ID_BTN_SIM_PREV: sim_current_dist -= 20; if(sim_current_dist < 0) sim_current_dist = 0; InvalidateRect(hwnd, NULL, TRUE); break;
                case ID_BTN_SIM_NEXT: sim_current_dist += 20; if(sim_current_dist > sim_total_dist) sim_current_dist = sim_total_dist; InvalidateRect(hwnd, NULL, TRUE); break;
            }
            if (HIWORD(wParam) == BN_CLICKED) SetFocus(hwnd); 
            return 0;
        }
        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam; int count = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
            for (int i = 0; i < count; i++) {
                char path[MAX_PATH]; DragQueryFile(hDrop, i, path, MAX_PATH);
                add_shape_from_file(path);
            }
            DragFinish(hDrop); update_listbox(hwnd); pack_shapes(hwnd); return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam); ScreenToClient(hwnd, &pt);
            float log_x = (pt.x - pan_x) / zoom, log_y = (pt.y - pan_y) / zoom;
            if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) zoom *= 1.15f; else zoom /= 1.15f;
            if (zoom < 0.01f) zoom = 0.01f; pan_x = pt.x - log_x * zoom; pan_y = pt.y - log_y * zoom; InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        case WM_LBUTTONDBLCLK: {
            float mx = (GET_X_LPARAM(lParam) - pan_x) / zoom, my = (GET_Y_LPARAM(lParam) - pan_y) / zoom; resize_target = -2;
            for (int i = num_shapes - 1; i >= 0; i--) { if (mx >= shapes[i].x && mx <= shapes[i].x + shapes[i].w && my >= shapes[i].y && my <= shapes[i].y + shapes[i].h) { resize_target = i; selected_shape = i; break; } }
            if (resize_target == -2 && mx >= 0 && mx <= sheet_w && my >= 0 && my <= sheet_h) resize_target = -1;
            if (resize_target >= -1) {
                char buf[64]; edit_orig_w = resize_target == -1 ? sheet_w : shapes[resize_target].w; edit_orig_h = resize_target == -1 ? sheet_h : shapes[resize_target].h;
                sprintf(buf, "%.2f", edit_orig_w); SetWindowText(hBtn[ID_TXT_W], buf); sprintf(buf, "%.2f", edit_orig_h); SetWindowText(hBtn[ID_TXT_H], buf);
                SetFocus(hBtn[ID_TXT_W]); SendMessage(hBtn[ID_TXT_W], EM_SETSEL, 0, -1);
            }
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        case WM_LBUTTONDOWN: {
            float mx = (GET_X_LPARAM(lParam) - pan_x) / zoom, my = (GET_Y_LPARAM(lParam) - pan_y) / zoom;
            int hit = -1;
            for (int i = num_shapes - 1; i >= 0; i--) { if (mx >= shapes[i].x && mx <= shapes[i].x + shapes[i].w && my >= shapes[i].y && my <= shapes[i].y + shapes[i].h) { hit = i; break; } }
            
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                is_shift_zooming = 1; pan_start_y = GET_Y_LPARAM(lParam); zoom_start = zoom; SetCapture(hwnd);
            } else if (hit != -1) {
                selected_shape = hit;
                SendMessage(hBtn[ID_LIST_ORDER], LB_SETSEL, FALSE, -1);
                SendMessage(hBtn[ID_LIST_ORDER], LB_SETSEL, TRUE, hit);
                
                if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && num_shapes < MAX_SHAPES) { shapes[num_shapes] = shapes[hit]; selected_shape = num_shapes++; save_state(); update_listbox(hwnd); }
                is_dragging = 1; drag_offset_x = mx - shapes[selected_shape].x; drag_offset_y = my - shapes[selected_shape].y; SetCapture(hwnd);
            } else {
                selected_shape = -1; 
                SendMessage(hBtn[ID_LIST_ORDER], LB_SETSEL, FALSE, -1);
                is_panning = 1; pan_start_x = GET_X_LPARAM(lParam); pan_start_y = GET_Y_LPARAM(lParam); pan_start_px = pan_x; pan_start_py = pan_y; SetCapture(hwnd);
            }
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        case WM_MOUSEMOVE: {
            if (is_shift_zooming) {
                int dy = pan_start_y - GET_Y_LPARAM(lParam); zoom = zoom_start * pow(1.01f, dy);
                if(zoom < 0.01f) zoom = 0.01f; InvalidateRect(hwnd, NULL, FALSE);
            } else if (is_panning) {
                pan_x = pan_start_px + (GET_X_LPARAM(lParam) - pan_start_x); pan_y = pan_start_py + (GET_Y_LPARAM(lParam) - pan_start_y); InvalidateRect(hwnd, NULL, FALSE);
            } else if (is_dragging && selected_shape != -1) {
                shapes[selected_shape].x = (GET_X_LPARAM(lParam) - pan_x) / zoom - drag_offset_x; shapes[selected_shape].y = (GET_Y_LPARAM(lParam) - pan_y) / zoom - drag_offset_y; InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (is_dragging) { save_state(); dim_target = selected_shape; SetTimer(hwnd, 1, 2000, NULL); }
            is_dragging = is_panning = is_shift_zooming = 0; ReleaseCapture(); return 0;
        }
        case WM_TIMER: {
            if (wParam == 1) { KillTimer(hwnd, 1); dim_target = -2; InvalidateRect(hwnd, NULL, FALSE); }
            if (wParam == 2 && sim_playing) {
                char buf[32]; GetWindowText(hBtn[ID_TXT_SIM_SPD], buf, 32); sim_speed = atof(buf);
                if(sim_speed < 0.1f) sim_speed = 0.1f; sim_current_dist += sim_speed;
                if (sim_current_dist >= sim_total_dist) { sim_current_dist = sim_total_dist; sim_playing = 0; KillTimer(hwnd, 2); }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc);
            HDC hMemDC = CreateCompatibleDC(hdc); HBITMAP hBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom); HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);
            FillRect(hMemDC, &rc, (HBRUSH)(COLOR_WINDOW+1));
            
            HPEN redPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0)); SelectObject(hMemDC, redPen); SelectObject(hMemDC, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hMemDC, (int)pan_x, (int)pan_y, (int)(pan_x + sheet_w * zoom), (int)(pan_y + sheet_h * zoom)); DeleteObject(redPen);
            
            HPEN normPen = CreatePen(PS_SOLID, 1, RGB(0, 120, 255)), selPen = CreatePen(PS_DASH, 2, RGB(255, 120, 0)), linePen = CreatePen(PS_SOLID, 2, RGB(30, 30, 30));
            
            for (int i = 0; i < num_shapes; i++) {
                int is_sel = (i == selected_shape || i == resize_target) || (SendMessage(hBtn[ID_LIST_ORDER], LB_GETSEL, i, 0) > 0);
                SelectObject(hMemDC, is_sel ? selPen : normPen);
                
                int L = (int)(pan_x + shapes[i].x * zoom), T = (int)(pan_y + shapes[i].y * zoom), R = (int)(pan_x + (shapes[i].x + shapes[i].w) * zoom), B = (int)(pan_y + (shapes[i].y + shapes[i].h) * zoom);
                if (shapes[i].is_oval) Ellipse(hMemDC, L, T, R, B); else Rectangle(hMemDC, L, T, R, B);
                
                if (shapes[i].num_lines > 0) {
                    SelectObject(hMemDC, linePen);
                    float scale_x = shapes[i].w / shapes[i].orig_w;
                    float scale_y = shapes[i].h / shapes[i].orig_h;
                    for (int j = 0; j < shapes[i].num_lines; j++) {
                        int x1 = (int)(pan_x + (shapes[i].x + shapes[i].lines[j].x1 * scale_x) * zoom);
                        int y1 = (int)(pan_y + (shapes[i].y + shapes[i].lines[j].y1 * scale_y) * zoom);
                        int x2 = (int)(pan_x + (shapes[i].x + shapes[i].lines[j].x2 * scale_x) * zoom);
                        int y2 = (int)(pan_y + (shapes[i].y + shapes[i].lines[j].y2 * scale_y) * zoom);
                        MoveToEx(hMemDC, x1, y1, NULL); LineTo(hMemDC, x2, y2);
                    }
                }
                
                SetBkMode(hMemDC, TRANSPARENT);
                SetTextColor(hMemDC, RGB(0, 0, 0));
                char num_buf[16];
                sprintf(num_buf, "%d", i + 1);
                TextOut(hMemDC, L + 4, T + 4, num_buf, strlen(num_buf));
            }
            DeleteObject(normPen); DeleteObject(selPen); DeleteObject(linePen);

            if (sim_active && num_sim_segs > 0) {
                float tr = sim_tool_r * zoom; if (tr < 1.0f) tr = 1.0f;
                HPEN cutPen = CreatePen(PS_SOLID, (int)(tr*2), RGB(200, 200, 200)), rapidPen = CreatePen(PS_DOT, 1, RGB(255, 0, 0));
                float accum = 0, head_x = 0, head_y = 0;
                for(int i=0; i<num_sim_segs; i++) {
                    if (accum >= sim_current_dist) break;
                    float d = sim_segs[i].len, draw_d = d;
                    if (accum + d > sim_current_dist) draw_d = sim_current_dist - accum;
                    int p1x = (int)(pan_x + sim_segs[i].x1 * zoom), p1y = (int)(pan_y + sim_segs[i].y1 * zoom);
                    float rx = sim_segs[i].x1 + (sim_segs[i].x2 - sim_segs[i].x1) * (draw_d/d), ry = sim_segs[i].y1 + (sim_segs[i].y2 - sim_segs[i].y1) * (draw_d/d);
                    int p2x = (int)(pan_x + rx * zoom), p2y = (int)(pan_y + ry * zoom);
                    if (sim_segs[i].type == 2) { SelectObject(hMemDC, cutPen); MoveToEx(hMemDC, p1x, p1y, NULL); LineTo(hMemDC, p2x, p2y); }
                    else if (sim_segs[i].type == 0) { SelectObject(hMemDC, rapidPen); MoveToEx(hMemDC, p1x, p1y, NULL); LineTo(hMemDC, p2x, p2y); }
                    head_x = p2x; head_y = p2y; accum += d;
                }
                HPEN toolPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0)); SelectObject(hMemDC, toolPen); SelectObject(hMemDC, GetStockObject(NULL_BRUSH));
                Ellipse(hMemDC, head_x - tr, head_y - tr, head_x + tr, head_y + tr);
                DeleteObject(cutPen); DeleteObject(rapidPen); DeleteObject(toolPen);
            }

            if (dim_target >= -1) {
                float dx=0, dy=0, dw=sheet_w, dh=sheet_h; if (dim_target >= 0) { dx=shapes[dim_target].x; dy=shapes[dim_target].y; dw=shapes[dim_target].w; dh=shapes[dim_target].h; }
                int L=(int)(pan_x+dx*zoom), T=(int)(pan_y+dy*zoom), R=(int)(pan_x+(dx+dw)*zoom), B=(int)(pan_y+(dy+dh)*zoom);
                HPEN dimPen = CreatePen(PS_SOLID, 1, RGB(0, 150, 0)); SelectObject(hMemDC, dimPen);
                MoveToEx(hMemDC, L, B+5, NULL); LineTo(hMemDC, L, B+20); MoveToEx(hMemDC, R, B+5, NULL); LineTo(hMemDC, R, B+20); MoveToEx(hMemDC, L, B+12, NULL); LineTo(hMemDC, R, B+12);
                MoveToEx(hMemDC, R+5, T, NULL); LineTo(hMemDC, R+20, T); MoveToEx(hMemDC, R+5, B, NULL); LineTo(hMemDC, R+20, B); MoveToEx(hMemDC, R+12, T, NULL); LineTo(hMemDC, R+12, B);
                SetBkMode(hMemDC, TRANSPARENT); SetTextColor(hMemDC, RGB(0,100,0)); char buf[32]; 
                sprintf(buf, "%.1f", dw); TextOut(hMemDC, L + (R-L)/2 - 15, B+15, buf, strlen(buf));
                sprintf(buf, "%.1f", dh); TextOut(hMemDC, R+15, T + (B-T)/2 - 10, buf, strlen(buf)); DeleteObject(dimPen);
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, hMemDC, 0, 0, SRCCOPY);
            SelectObject(hMemDC, hOldBmp); DeleteObject(hBmp); DeleteDC(hMemDC); EndPaint(hwnd, &ps); return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASS wc = {0}; wc.style = CS_DBLCLKS; wc.lpfnWndProc = WindowProc; wc.hInstance = hInst; wc.lpszClassName = "NestClass"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, "NestClass", "Nester Pro - Simulator & CNC", WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 750, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show); UpdateWindow(hwnd);
    PostMessage(hwnd, WM_COMMAND, ID_BTN_ZOOM_EXT, 0);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
