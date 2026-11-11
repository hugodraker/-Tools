/* ============================================================================
 *
 * xlsx_edit - edit cells values in xlsx files
 *
 * COMPILATION:
 *
 * gcc -Os -s xlsx_edit.c miniz.c -o xlsx_edit.exe -luser32
 *
 * USAGE:
 *
 * xlsx_edit.exe report.xlsx "Sheet1|A1=5|b2=Hello\nWorld|b3=2026-11-06|c5|Summary|a2='00123|a3==SUM(Sheet1!A1:A5)"
 * (Note: "c5" without '=' pastes the current Windows clipboard starting at C5)
 *
 *  2026 HUGO
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */
/*
ALTERNATE USAGE
Method 1: Interactive Typing
xlsx_edit.exe data.xlsx
Interactive XLSX Editor. Target: data.xlsx
Type commands like 'Sheet1|A1=5', or type 'bye' to save and exit.
xlsx> Sheet1
xlsx> A1=Hello
xlsx> A2=World
xlsx> bye
Saving changes to data.xlsx...
Done.
Method 2: Scripting with a Text File
Create script.txt:

Plaintext
Summary
A1=Total Sales
B1==SUM(Data!B2:B100)
Data
A1=Date
B1=Amount
Run it in CMD:

xlsx_edit.exe data.xlsx < script.txt
(It will silently process the whole file, hit EOF, save, and exit).
*/
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <io.h>
#define MINIZ_IMPLEMENTATION
#include "miniz.h"
// ============================================================================
// Dynamic String Buffer
// ============================================================================
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb) {
    sb->cap = 4096;
    sb->len = 0;
    sb->data = (char *)malloc(sb->cap);
    sb->data[0] = '\0';
}

void sb_Ensure(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 > sb->cap) {
        while (sb->len + extra + 1 > sb->cap) sb->cap *= 2;
        sb->data = (char *)realloc(sb->data, sb->cap);
    }
}

void sb_append_n(StrBuf *sb, const char *s, size_t n) {
    sb_Ensure(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append(StrBuf *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

void sb_append_xml_escaped(StrBuf *sb, const char *s) {
    for (; *s; s++) {
        switch (*s) {
            case '&':  sb_append(sb, "&amp;"); break;
            case '<':  sb_append(sb, "&lt;"); break;
            case '>':  sb_append(sb, "&gt;"); break;
            case '\"': sb_append(sb, "&quot;"); break;
            case '\'': sb_append(sb, "&apos;"); break;
            default:   sb_append_n(sb, s, 1); break;
        }
    }
}

void sb_free(StrBuf *sb) { free(sb->data); }

// ============================================================================
// Cell & Sheet Data Structures
// ============================================================================
typedef struct {
    char ref[16];
    int col;
    int row;
    char *val;
    int force_string;
} CellEdit;

typedef struct {
    char name[128];
    CellEdit *edits;
    size_t count;
    size_t cap;
} SheetEdit;

typedef struct {
    SheetEdit *sheets;
    size_t count;
} WorkbookEdits;

// ============================================================================
// Coordinate, Number & Date Serial Helpers
// ============================================================================
int parse_cell_ref(const char *str, char *norm_ref, int *out_col, int *out_row) {
    int i = 0, col = 0;
    while (isalpha((unsigned char)str[i])) {
        if (i >= 3) return 0;
        char u = (char)toupper((unsigned char)str[i]);
        norm_ref[i] = u;
        col = col * 26 + (u - 'A' + 1);
        i++;
    }
    if (i == 0 || col > 16384) return 0;
    int j = 0, row = 0;
    while (isdigit((unsigned char)str[i + j])) {
        if (i + j >= 15) return 0;
        norm_ref[i + j] = str[i + j];
        row = row * 10 + (str[i + j] - '0');
        j++;
    }
    if (j == 0 || str[i + j] != '\0' || row <= 0 || row > 1048576) return 0;
    norm_ref[i + j] = '\0';
    *out_col = col;
    *out_row = row;
    return 1;
}

void make_cell_ref(int col, int row, char *out_ref) {
    char cols[8];
    int i = 0, c = col;
    while (c > 0 && i < 7) {
        int rem = (c - 1) % 26;
        cols[i++] = (char)('A' + rem);
        c = (c - 1) / 26;
    }
    int j = 0;
    while (i > 0) out_ref[j++] = cols[--i];
    sprintf(out_ref + j, "%d", row);
}

int is_number(const char *s) {
    if (!*s) return 0;
    char *endptr = NULL;
    strtod(s, &endptr);
    return (*endptr == '\0');
}

int parse_iso_date_to_excel_serial(const char *s, int *out_serial) {
    if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (!isdigit((unsigned char)s[i])) return 0;
    }

    int y = atoi(s);
    int m = atoi(s + 5);
    int d = atoi(s + 8);

    if (y < 1900 || y > 9999 || m < 1 || m > 12 || d < 1) return 0;

    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int is_leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int max_d = mdays[m - 1] + (m == 2 && is_leap ? 1 : 0);
    if (d > max_d) return 0;

    int yr = y - (m <= 2);
    int era = (yr >= 0 ? yr : yr - 399) / 400;
    unsigned yoe = (unsigned)(yr - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int days_since_1970 = era * 146097 + (int)doe - 719468;
    int serial = days_since_1970 + 25569;
    if (serial < 60) serial--;

    *out_serial = serial;
    return 1;
}

// ============================================================================
// Command Argument & Win32 Clipboard Parser
// ============================================================================
char *unescape_c_str(const char *src, size_t len) {
    char *out = (char *)malloc(len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\\' && i + 1 < len) {
            i++;
            switch (src[i]) {
                case 'n':  out[j++] = '\n'; break;
                case 'r':  out[j++] = '\r'; break;
                case 't':  out[j++] = '\t'; break;
                case '\\': out[j++] = '\\'; break;
                case '|':  out[j++] = '|';  break;
                case '\'': out[j++] = '\''; break;
                case '\"': out[j++] = '\"'; break;
                default:   out[j++] = src[i]; break;
            }
        } else {
            out[j++] = src[i];
        }
    }
    out[j] = '\0';
    return out;
}

SheetEdit *get_or_add_sheet(WorkbookEdits *wb, const char *name) {
    for (size_t i = 0; i < wb->count; i++) {
        if (_stricmp(wb->sheets[i].name, name) == 0) return &wb->sheets[i];
    }
    wb->sheets = (SheetEdit *)realloc(wb->sheets, (wb->count + 1) * sizeof(SheetEdit));
    SheetEdit *s = &wb->sheets[wb->count++];
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->edits = NULL;
    s->count = 0;
    s->cap = 0;
    return s;
}

void add_cell_edit(SheetEdit *s, const char *ref, int col, int row, const char *raw_val) {
    int force_str = (raw_val[0] == '\'') ? 1 : 0;
    const char *actual_val = force_str ? (raw_val + 1) : raw_val;

    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->edits[i].ref, ref) == 0) {
            free(s->edits[i].val);
            s->edits[i].val = _strdup(actual_val);
            s->edits[i].force_string = force_str;
            return;
        }
    }
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->edits = (CellEdit *)realloc(s->edits, s->cap * sizeof(CellEdit));
    }
    CellEdit *ce = &s->edits[s->count++];
    strcpy(ce->ref, ref);
    ce->col = col;
    ce->row = row;
    ce->val = _strdup(actual_val);
    ce->force_string = force_str;
}

char *get_clipboard_utf8(void) {
    if (!OpenClipboard(NULL)) return NULL;
    char *result = NULL;

    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            WCHAR *wstr = (WCHAR *)GlobalLock(hData);
            if (wstr) {
                int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
                if (utf8_len > 0) {
                    result = (char *)malloc(utf8_len);
                    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result, utf8_len, NULL, NULL);
                }
                GlobalUnlock(hData);
            }
        }
    } else if (IsClipboardFormatAvailable(CF_TEXT)) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            char *astr = (char *)GlobalLock(hData);
            if (astr) {
                result = _strdup(astr);
                GlobalUnlock(hData);
            }
        }
    }
    CloseClipboard();
    return result;
}

void paste_clipboard_at(SheetEdit *s, int start_col, int start_row) {
    char *clip = get_clipboard_utf8();
    if (!clip || !*clip) {
        free(clip);
        return;
    }

    int cur_col = start_col;
    int cur_row = start_row;
    const char *p = clip;
    StrBuf cell_buf;
    sb_init(&cell_buf);

    while (*p) {
        cell_buf.len = 0;
        cell_buf.data[0] = '\0';

        if (*p == '\"') {
            p++;
            while (*p) {
                if (*p == '\"') {
                    if (*(p + 1) == '\"') {
                        sb_append_n(&cell_buf, "\"", 1);
                        p += 2;
                    } else {
                        p++;
                        break;
                    }
                } else if (*p == '\r' && *(p + 1) == '\n') {
                    sb_append_n(&cell_buf, "\n", 1);
                    p += 2;
                } else {
                    sb_append_n(&cell_buf, p, 1);
                    p++;
                }
            }
            while (*p && *p != '\t' && *p != '\r' && *p != '\n') {
                sb_append_n(&cell_buf, p, 1);
                p++;
            }
        } else {
            while (*p && *p != '\t' && *p != '\r' && *p != '\n') {
                sb_append_n(&cell_buf, p, 1);
                p++;
            }
        }

        if (cur_col <= 16384 && cur_row <= 1048576) {
            char ref[16];
            make_cell_ref(cur_col, cur_row, ref);
            add_cell_edit(s, ref, cur_col, cur_row, cell_buf.data);
        }

        if (*p == '\t') {
            cur_col++;
            p++;
            if (*p == '\0' && cur_col <= 16384 && cur_row <= 1048576) {
                char ref[16];
                make_cell_ref(cur_col, cur_row, ref);
                add_cell_edit(s, ref, cur_col, cur_row, "");
            }
        } else if (*p == '\r' || *p == '\n') {
            if (*p == '\r' && *(p + 1) == '\n') p += 2;
            else p++;
            cur_row++;
            cur_col = start_col;
            if (*p == '\0') break;
        }
    }

    sb_free(&cell_buf);
    free(clip);
}

void parse_instructions(const char *cmd, WorkbookEdits *wb, SheetEdit **cur_sheet) {
    size_t len = strlen(cmd);
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        int is_delim = (cmd[i] == '\0');
        if (cmd[i] == '|') {
            size_t bs = 0, k = i;
            while (k > start && cmd[k - 1] == '\\') { bs++; k--; }
            if (bs % 2 == 0) is_delim = 1;
        }

        if (is_delim) {
            char *token = unescape_c_str(cmd + start, i - start);
            char *eq = strchr(token, '=');
            if (eq) {
                *eq = '\0';
                char *lhs = token;
                char *rhs = eq + 1;
                char norm_ref[16];
                int col, row;
                if (parse_cell_ref(lhs, norm_ref, &col, &row)) {
                    if (!*cur_sheet) *cur_sheet = get_or_add_sheet(wb, "Sheet1");
                    add_cell_edit(*cur_sheet, norm_ref, col, row, rhs);
                } else {
                    fprintf(stderr, "Warning: Invalid cell reference '%s' ignored.\n", lhs);
                }
            } else if (token[0] != '\0') {
                char norm_ref[16];
                int col, row;
                if (token[0] == '#' && token[1] != '\0') {
                    *cur_sheet = get_or_add_sheet(wb, token + 1);
                } else if (parse_cell_ref(token, norm_ref, &col, &row)) {
                    if (!*cur_sheet) *cur_sheet = get_or_add_sheet(wb, "Sheet1");
                    paste_clipboard_at(*cur_sheet, col, row);
                } else {
                    *cur_sheet = get_or_add_sheet(wb, token);
                }
            }
            free(token);
            start = i + 1;
        }
    }
}
// ============================================================================
// Styles XML Builder & Patcher (Ensures Date Format Style Exists)
// ============================================================================
const char *get_default_styles_xml(void) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
           "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
           "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
           "<fills count=\"2\">"
             "<fill><patternFill patternType=\"none\"/></fill>"
             "<fill><patternFill patternType=\"gray125\"/></fill>"
           "</fills>"
           "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
           "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
           "<cellXfs count=\"2\">"
             "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
             "<xf numFmtId=\"14\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
           "</cellXfs>"
           "</styleSheet>";
}

char *patch_styles_for_date(const char *orig_styles, int *out_date_style_idx, size_t *out_len) {
    if (!orig_styles) {
        *out_date_style_idx = 1;
        const char *def = get_default_styles_xml();
        *out_len = strlen(def);
        return _strdup(def);
    }

    const char *cx_open = strstr(orig_styles, "<cellXfs");
    const char *cx_close = cx_open ? strstr(cx_open, "</cellXfs>") : NULL;
    if (!cx_open || !cx_close) {
        *out_date_style_idx = 0;
        *out_len = strlen(orig_styles);
        return _strdup(orig_styles);
    }

    const char *cx_tag_end = strchr(cx_open, '>');
    const char *p = cx_tag_end + 1;
    int idx = 0;

    while (p < cx_close) {
        const char *xf = strstr(p, "<xf");
        if (!xf || xf >= cx_close || (xf[3] != ' ' && xf[3] != '>' && xf[3] != '/')) break;

        const char *xf_tag_end = strchr(xf, '>');
        if (!xf_tag_end || xf_tag_end > cx_close) break;

        int self_close = (*(xf_tag_end - 1) == '/');
        const char *xf_end = self_close ? (xf_tag_end + 1) : (strstr(xf_tag_end, "</xf>") + 5);

        const char *nf = strstr(xf, "numFmtId=\"14\"");
        if (nf && nf < xf_tag_end) {
            *out_date_style_idx = idx;
            *out_len = strlen(orig_styles);
            return _strdup(orig_styles);
        }
        idx++;
        p = xf_end;
    }

    *out_date_style_idx = idx;
    StrBuf sb;
    sb_init(&sb);

    sb_append_n(&sb, orig_styles, cx_open - orig_styles);
    char header_buf[64];
    snprintf(header_buf, sizeof(header_buf), "<cellXfs count=\"%d\">", idx + 1);
    sb_append(&sb, header_buf);

    sb_append_n(&sb, cx_tag_end + 1, cx_close - (cx_tag_end + 1));
    sb_append(&sb, "<xf numFmtId=\"14\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>");
    sb_append(&sb, cx_close);

    *out_len = sb.len;
    return sb.data;
}

// ============================================================================
// XML Cell Builder & Worksheet Patcher
// ============================================================================
void append_cell_xml(StrBuf *sb, const CellEdit *ce, int date_style_idx, const char *old_s_attr) {
    int date_serial = 0;
    char s_buf[64] = {0};
    
    // Dates force the global date style index. Everything else preserves the existing style attribute.
    if (!ce->force_string && parse_iso_date_to_excel_serial(ce->val, &date_serial)) {
        snprintf(s_buf, sizeof(s_buf), " s=\"%d\"", date_style_idx);
    } else if (old_s_attr && old_s_attr[0] != '\0') {
        snprintf(s_buf, sizeof(s_buf), " s=\"%s\"", old_s_attr);
    }

    if (!ce->force_string && ce->val[0] == '=') {
        sb_append(sb, "<c r=\"");
        sb_append(sb, ce->ref);
        sb_append(sb, "\"");
        sb_append(sb, s_buf);
        sb_append(sb, "><f>");
        sb_append_xml_escaped(sb, ce->val + 1);
        sb_append(sb, "</f></c>");
    } else if (!ce->force_string && date_serial) {
        char buf[256];
        snprintf(buf, sizeof(buf), "<c r=\"%s\"%s><v>%d</v></c>",
                 ce->ref, s_buf, date_serial);
        sb_append(sb, buf);
    } else if (!ce->force_string && is_number(ce->val)) {
        sb_append(sb, "<c r=\"");
        sb_append(sb, ce->ref);
        sb_append(sb, "\"");
        sb_append(sb, s_buf);
        sb_append(sb, "><v>");
        sb_append_xml_escaped(sb, ce->val);
        sb_append(sb, "</v></c>");
    } else {
        sb_append(sb, "<c r=\"");
        sb_append(sb, ce->ref);
        sb_append(sb, "\"");
        sb_append(sb, s_buf);
        sb_append(sb, " t=\"inlineStr\"><is><t xml:space=\"preserve\">");
        sb_append_xml_escaped(sb, ce->val);
        sb_append(sb, "</t></is></c>");
    }
}

int cmp_edits(const void *a, const void *b) {
    const CellEdit *ca = (const CellEdit *)a;
    const CellEdit *cb = (const CellEdit *)b;
    if (ca->row != cb->row) return ca->row - cb->row;
    return ca->col - cb->col;
}

char *build_or_patch_sheet_xml(const char *orig_xml, SheetEdit *se, int date_style_idx, size_t *out_len) {
    qsort(se->edits, se->count, sizeof(CellEdit), cmp_edits);
    StrBuf sb;
    sb_init(&sb);

    const char *sd_open = orig_xml ? strstr(orig_xml, "<sheetData") : NULL;
    if (!orig_xml || !sd_open) {
        sb_append(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                       "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
                       "<sheetData>");
        size_t idx = 0;
        while (idx < se->count) {
            int r = se->edits[idx].row;
            char rbuf[64];
            sprintf(rbuf, "<row r=\"%d\">", r);
            sb_append(&sb, rbuf);
            while (idx < se->count && se->edits[idx].row == r) {
                append_cell_xml(&sb, &se->edits[idx], date_style_idx, NULL);
                idx++;
            }
            sb_append(&sb, "</row>");
        }
        sb_append(&sb, "</sheetData></worksheet>");
        *out_len = sb.len;
        return sb.data;
    }

    const char *sd_tag_end = strchr(sd_open, '>');
    int self_closing = (sd_tag_end && *(sd_tag_end - 1) == '/');
    const char *sd_close = self_closing ? sd_tag_end : strstr(sd_open, "</sheetData>");

    if (self_closing) {
        sb_append_n(&sb, orig_xml, sd_tag_end - orig_xml - 1);
        sb_append(&sb, ">");
    } else {
        sb_append_n(&sb, orig_xml, sd_tag_end - orig_xml + 1);
    }

    size_t edit_idx = 0;
    const char *cur = sd_tag_end + 1;
    const char *limit = self_closing ? sd_tag_end : sd_close;

    while (cur < limit) {
        const char *row_start = strstr(cur, "<row");
        if (!row_start || row_start >= limit) {
            sb_append_n(&sb, cur, limit - cur);
            break;
        }
        sb_append_n(&sb, cur, row_start - cur);

        const char *row_tag_end = strchr(row_start, '>');
        int existing_row = 0;
        const char *r_attr = strstr(row_start, "r=\"");
        if (r_attr && r_attr < row_tag_end) existing_row = atoi(r_attr + 3);

        while (edit_idx < se->count && se->edits[edit_idx].row < existing_row) {
            int new_r = se->edits[edit_idx].row;
            char rbuf[64];
            sprintf(rbuf, "<row r=\"%d\">", new_r);
            sb_append(&sb, rbuf);
            while (edit_idx < se->count && se->edits[edit_idx].row == new_r) {
                append_cell_xml(&sb, &se->edits[edit_idx], date_style_idx, NULL);
                edit_idx++;
            }
            sb_append(&sb, "</row>");
        }

        int row_self_close = (*(row_tag_end - 1) == '/');
        const char *row_end = row_self_close ? (row_tag_end + 1) : (strstr(row_tag_end, "</row>") + 6);

        if (edit_idx >= se->count || se->edits[edit_idx].row != existing_row) {
            sb_append_n(&sb, row_start, row_end - row_start);
            cur = row_end;
            continue;
        }

        if (row_self_close) {
            sb_append_n(&sb, row_start, row_tag_end - row_start - 1);
            sb_append(&sb, ">");
            while (edit_idx < se->count && se->edits[edit_idx].row == existing_row) {
                append_cell_xml(&sb, &se->edits[edit_idx], date_style_idx, NULL);
                edit_idx++;
            }
            sb_append(&sb, "</row>");
            cur = row_end;
            continue;
        }

        sb_append_n(&sb, row_start, row_tag_end - row_start + 1);
        const char *c_cur = row_tag_end + 1;
        const char *c_limit = row_end - 6;

        while (c_cur < c_limit) {
            const char *c_start = strstr(c_cur, "<c");
            if (!c_start || c_start >= c_limit || (c_start[2] != ' ' && c_start[2] != '>')) {
                sb_append_n(&sb, c_cur, c_limit - c_cur);
                break;
            }
            sb_append_n(&sb, c_cur, c_start - c_cur);

            const char *c_tag_end = strchr(c_start, '>');
            int c_self_close = (*(c_tag_end - 1) == '/');
            const char *c_end = c_self_close ? (c_tag_end + 1) : (strstr(c_tag_end, "</c>") + 4);

            int ex_col = 0, ex_row = 0;
            char norm[16];
            const char *cr_attr = strstr(c_start, "r=\"");
            if (cr_attr && cr_attr < c_tag_end) {
                char tmp[16] = {0};
                int k = 0;
                cr_attr += 3;
                while (*cr_attr && *cr_attr != '\"' && k < 15) tmp[k++] = *cr_attr++;
                parse_cell_ref(tmp, norm, &ex_col, &ex_row);
            }

            while (edit_idx < se->count && se->edits[edit_idx].row == existing_row &&
                   se->edits[edit_idx].col < ex_col) {
                append_cell_xml(&sb, &se->edits[edit_idx], date_style_idx, NULL);
                edit_idx++;
            }

            if (edit_idx < se->count && se->edits[edit_idx].row == existing_row &&
                se->edits[edit_idx].col == ex_col) {
                
                // Extract existing s="..." attribute to preserve cell styling
                char s_attr[32] = {0};
                const char *s_ptr = strstr(c_start, " s=\"");
                if (s_ptr && s_ptr < c_tag_end) {
                    s_ptr += 4;
                    int k = 0;
                    while (*s_ptr && *s_ptr != '\"' && k < 31) s_attr[k++] = *s_ptr++;
                }

                // Append overwritten cell using extracted style (if not overridden by date formatting)
                append_cell_xml(&sb, &se->edits[edit_idx], date_style_idx, s_attr[0] ? s_attr : NULL);
                edit_idx++;
            } else {
                sb_append_n(&sb, c_start, c_end - c_start);
            }
            c_cur = c_end;
        }

        while (edit_idx < se->count && se->edits[edit_idx].row == existing_row) {
            append_cell_xml(&sb, &se->edits[edit_idx], date_style_idx, NULL);
            edit_idx++;
        }
        sb_append(&sb, "</row>");
        cur = row_end;
    }

    while (edit_idx < se->count) {
        int new_r = se->edits[edit_idx].row;
        char rbuf[64];
        sprintf(rbuf, "<row r=\"%d\">", new_r);
        sb_append(&sb, rbuf);
        while (edit_idx < se->count && se->edits[edit_idx].row == new_r) {
            append_cell_xml(&sb, &se->edits[edit_idx], date_style_idx, NULL);
            edit_idx++;
        }
        sb_append(&sb, "</row>");
    }

    if (self_closing) {
        sb_append(&sb, "</sheetData>");
        sb_append(&sb, sd_tag_end + 1);
    } else {
        sb_append(&sb, sd_close);
    }

    *out_len = sb.len;
    return sb.data;
}

// ============================================================================
// Workbook Sheet Name -> ZIP Path Resolver
// ============================================================================
int find_sheet_zip_path(mz_zip_archive *zip, const char *sheet_name, char *out_path) {
    size_t wb_size = 0, rels_size = 0;
    char *wb_xml = (char *)mz_zip_reader_extract_file_to_heap(zip, "xl/workbook.xml", &wb_size, 0);
    char *rels_xml = (char *)mz_zip_reader_extract_file_to_heap(zip, "xl/_rels/workbook.xml.rels", &rels_size, 0);
    if (!wb_xml || !rels_xml) {
        mz_free(wb_xml); mz_free(rels_xml);
        return 0;
    }

    wb_xml = (char *)realloc(wb_xml, wb_size + 1); wb_xml[wb_size] = '\0';
    rels_xml = (char *)realloc(rels_xml, rels_size + 1); rels_xml[rels_size] = '\0';

    char rid[64] = {0};
    const char *p = wb_xml;
    while ((p = strstr(p, "<sheet ")) != NULL) {
        const char *end = strchr(p, '>');
        if (!end) break;
        const char *n_attr = strstr(p, "name=\"");
        if (n_attr && n_attr < end) {
            n_attr += 6;
            char name_buf[128] = {0};
            int k = 0;
            while (*n_attr && *n_attr != '\"' && k < 127) name_buf[k++] = *n_attr++;
            if (_stricmp(name_buf, sheet_name) == 0) {
                const char *r_attr = strstr(p, "r:id=\"");
                if (r_attr && r_attr < end) {
                    r_attr += 6;
                    int rk = 0;
                    while (*r_attr && *r_attr != '\"' && rk < 63) rid[rk++] = *r_attr++;
                }
                break;
            }
        }
        p = end + 1;
    }

    int found = 0;
    if (rid[0]) {
        char search_id[80];
        sprintf(search_id, "Id=\"%s\"", rid);
        const char *rp = strstr(rels_xml, search_id);
        if (rp) {
            while (rp > rels_xml && *rp != '<') rp--;
            const char *rend = strchr(rp, '>');
            const char *t_attr = strstr(rp, "Target=\"");
            if (t_attr && rend && t_attr < rend) {
                t_attr += 8;
                char target[256] = {0};
                int tk = 0;
                while (*t_attr && *t_attr != '\"' && tk < 255) target[tk++] = *t_attr++;
                if (target[0] == '/')
                    snprintf(out_path, 256, "%s", target + 1);
                else if (strncmp(target, "xl/", 3) == 0)
                    snprintf(out_path, 256, "%s", target);
                else
                    snprintf(out_path, 256, "xl/%s", target);
                found = 1;
            }
        }
    }

    free(wb_xml);
    free(rels_xml);
    return found;
}

// ============================================================================
// File Locking Guard
// ============================================================================
int check_file_locked(const char *filename) {
    HANDLE hFile = CreateFileA(filename, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        // If Excel or another program has an exclusive lock, it throws a sharing violation
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) {
            return 1;
        }
    } else {
        CloseHandle(hFile);
    }
    return 0;
}

// ============================================================================
// Create a Brand-New Minimal XLSX Archive or Patch Existing Archive
// ============================================================================
int create_new_xlsx(const char *filename, WorkbookEdits *wb) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, filename, 0)) return 0;

    const char *root_rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
    mz_zip_writer_add_mem(&zip, "_rels/.rels", root_rels, strlen(root_rels), MZ_DEFAULT_COMPRESSION);

    const char *styles_xml = get_default_styles_xml();
    mz_zip_writer_add_mem(&zip, "xl/styles.xml", styles_xml, strlen(styles_xml), MZ_DEFAULT_COMPRESSION);
    int date_style_idx = 1;

    StrBuf ct, wb_xml, wb_rels;
    sb_init(&ct); sb_init(&wb_xml); sb_init(&wb_rels);

    sb_append(&ct, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                   "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                   "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                   "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                   "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>");

    sb_append(&wb_xml, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                   "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                   "<workbookPr fullCalcOnLoad=\"1\"/>"
                   "<sheets>");

    sb_append(&wb_rels, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                        "<Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");

    if (wb->count == 0) get_or_add_sheet(wb, "Sheet1");

    for (size_t i = 0; i < wb->count; i++) {
        char buf[512];
        int idx = (int)(i + 1);
        snprintf(buf, sizeof(buf),
                 "<Override PartName=\"/xl/worksheets/sheet%d.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>", idx);
        sb_append(&ct, buf);

        sb_append(&wb_xml, "<sheet name=\"");
        sb_append_xml_escaped(&wb_xml, wb->sheets[i].name);
        snprintf(buf, sizeof(buf), "\" sheetId=\"%d\" r:id=\"rId%d\"/>", idx, idx);
        sb_append(&wb_xml, buf);

        snprintf(buf, sizeof(buf),
                 "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%d.xml\"/>",
                 idx, idx);
        sb_append(&wb_rels, buf);

        size_t s_len = 0;
        char *s_xml = build_or_patch_sheet_xml(NULL, &wb->sheets[i], date_style_idx, &s_len);
        char s_path[64];
        snprintf(s_path, sizeof(s_path), "xl/worksheets/sheet%d.xml", idx);
        mz_zip_writer_add_mem(&zip, s_path, s_xml, s_len, MZ_DEFAULT_COMPRESSION);
        free(s_xml);
    }

    sb_append(&ct, "</Types>");
    sb_append(&wb_xml, "</sheets></workbook>");
    sb_append(&wb_rels, "</Relationships>");

    mz_zip_writer_add_mem(&zip, "[Content_Types].xml", ct.data, ct.len, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_add_mem(&zip, "xl/workbook.xml", wb_xml.data, wb_xml.len, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_add_mem(&zip, "xl/_rels/workbook.xml.rels", wb_rels.data, wb_rels.len, MZ_DEFAULT_COMPRESSION);

    sb_free(&ct); sb_free(&wb_xml); sb_free(&wb_rels);
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return 1;
}

int modify_existing_xlsx(const char *filename, WorkbookEdits *wb) {
    mz_zip_archive rzip, wzip;
    memset(&rzip, 0, sizeof(rzip));
    memset(&wzip, 0, sizeof(wzip));

    if (!mz_zip_reader_init_file(&rzip, filename, 0)) {
        fprintf(stderr, "Error: Could not open '%s' as a valid ZIP/XLSX archive.\n", filename);
        return 0;
    }

    char tmp_file[MAX_PATH];
    snprintf(tmp_file, sizeof(tmp_file), "%s.tmp", filename);
    if (!mz_zip_writer_init_file(&wzip, tmp_file, 0)) {
        mz_zip_reader_end(&rzip);
        return 0;
    }

    int date_style_idx = 0;
    size_t orig_styles_len = 0, new_styles_len = 0;
    char *orig_styles = (char *)mz_zip_reader_extract_file_to_heap(&rzip, "xl/styles.xml", &orig_styles_len, 0);
    if (orig_styles) {
        orig_styles = (char *)realloc(orig_styles, orig_styles_len + 1);
        orig_styles[orig_styles_len] = '\0';
    }
    char *patched_styles = patch_styles_for_date(orig_styles, &date_style_idx, &new_styles_len);
    free(orig_styles);

    char (*target_paths)[256] = calloc(wb->count, 256);
    for (size_t i = 0; i < wb->count; i++) {
        if (!find_sheet_zip_path(&rzip, wb->sheets[i].name, target_paths[i])) {
            fprintf(stderr, "Warning: Sheet '%s' not found in workbook.\n", wb->sheets[i].name);
        }
    }

    int wrote_styles = 0;
    mz_uint num_files = mz_zip_reader_get_num_files(&rzip);
    for (mz_uint i = 0; i < num_files; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&rzip, i, &st)) continue;

        if (_stricmp(st.m_filename, "xl/calcChain.xml") == 0) continue;

        if (_stricmp(st.m_filename, "xl/styles.xml") == 0) {
            mz_zip_writer_add_mem(&wzip, st.m_filename, patched_styles, new_styles_len, MZ_DEFAULT_COMPRESSION);
            wrote_styles = 1;
            continue;
        }

        if (_stricmp(st.m_filename, "xl/workbook.xml") == 0) {
            size_t wb_len = 0;
            char *wb_xml = (char *)mz_zip_reader_extract_to_heap(&rzip, i, &wb_len, 0);
            wb_xml = (char *)realloc(wb_xml, wb_len + 1);
            wb_xml[wb_len] = '\0';

            StrBuf sb_wb;
            sb_init(&sb_wb);
            char *calcPr = strstr(wb_xml, "<calcPr ");
            
            if (calcPr) {
                sb_append_n(&sb_wb, wb_xml, calcPr - wb_xml);
                sb_append(&sb_wb, "<calcPr fullCalcOnLoad=\"1\" ");
                sb_append(&sb_wb, calcPr + 8);
            } else {
                char *end_tag = strstr(wb_xml, "</workbook>");
                if (end_tag) {
                    sb_append_n(&sb_wb, wb_xml, end_tag - wb_xml);
                    sb_append(&sb_wb, "<calcPr fullCalcOnLoad=\"1\"/></workbook>");
                } else {
                    sb_append(&sb_wb, wb_xml);
                }
            }
            
            mz_zip_writer_add_mem(&wzip, st.m_filename, sb_wb.data, sb_wb.len, MZ_DEFAULT_COMPRESSION);
            sb_free(&sb_wb);
            free(wb_xml);
            continue;
        }

        int matched_sheet = -1;
        for (size_t s = 0; s < wb->count; s++) {
            if (target_paths[s][0] && _stricmp(st.m_filename, target_paths[s]) == 0) {
                matched_sheet = (int)s;
                break;
            }
        }

        if (matched_sheet >= 0) {
            size_t orig_len = 0;
            char *orig_xml = (char *)mz_zip_reader_extract_to_heap(&rzip, i, &orig_len, 0);
            orig_xml = (char *)realloc(orig_xml, orig_len + 1);
            orig_xml[orig_len] = '\0';

            size_t new_len = 0;
            char *new_xml = build_or_patch_sheet_xml(orig_xml, &wb->sheets[matched_sheet], date_style_idx, &new_len);
            mz_zip_writer_add_mem(&wzip, st.m_filename, new_xml, new_len, MZ_DEFAULT_COMPRESSION);

            free(orig_xml);
            free(new_xml);
        } else {
            mz_zip_writer_add_from_zip_reader(&wzip, &rzip, i);
        }
    }

    if (!wrote_styles && patched_styles) {
        mz_zip_writer_add_mem(&wzip, "xl/styles.xml", patched_styles, new_styles_len, MZ_DEFAULT_COMPRESSION);
    }

    free(patched_styles);
    free(target_paths);
    mz_zip_reader_end(&rzip);
    mz_zip_writer_finalize_archive(&wzip);
    mz_zip_writer_end(&wzip);

    if (!MoveFileExA(tmp_file, filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fprintf(stderr, "Error: Failed to replace '%s' (is it open in Excel?).\n", filename);
        DeleteFileA(tmp_file);
        return 0;
    }
    return 1;
}
// ============================================================================
// Main Entry Point
// ============================================================================
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage 1 (Command line): %s <file.xlsx> \"Sheet1|A1=5|B2=Hello\"\n", argv[0]);
        printf("Usage 2 (Interactive):  %s <file.xlsx>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    // Explicit check to prevent corrupting temporary files if Excel has locked the original
    if (GetFileAttributesA(filename) != INVALID_FILE_ATTRIBUTES) {
        if (check_file_locked(filename)) {
            fprintf(stderr, "Error: The file '%s' is locked by another process.\n", filename);
            return 1;
        }
    }

    WorkbookEdits wb;
    memset(&wb, 0, sizeof(wb));
    SheetEdit *cur_sheet = NULL;

    int is_tty = _isatty(_fileno(stdin));

    // MODE 1: Old-school single string argument
    if (argc >= 3) {
        parse_instructions(argv[2], &wb, &cur_sheet);
    } 
    // MODE 2: Interactive typing or Piped script input
    else {
        if (is_tty) {
            printf("Interactive XLSX Editor. Target: %s\n", filename);
            printf("Type commands like 'Sheet1|A1=5', or type 'bye' to save and exit.\n");
        }

        char line[4096];
        while (1) {
            if (is_tty) {
                printf("xlsx> ");
                fflush(stdout);
            }

            if (!fgets(line, sizeof(line), stdin)) {
                if (is_tty) printf("\n");
                break; 
            }

            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '\0') continue;
            
            if (_stricmp(line, "bye") == 0 || _stricmp(line, "exit") == 0 || _stricmp(line, "quit") == 0) {
                break;
            }

            parse_instructions(line, &wb, &cur_sheet);
        }
    }

    // Don't overwrite the file if they didn't make any changes
    if (wb.count == 0) {
        if (is_tty || argc >= 3) printf("No edits made. Exiting without saving.\n");
        return 0;
    }

    if (is_tty && argc < 3) printf("Saving changes to %s...\n", filename);

    DWORD attrs = GetFileAttributesA(filename);
    int ok = 0;
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        ok = create_new_xlsx(filename, &wb);
    } else {
        ok = modify_existing_xlsx(filename, &wb);
    }

    // Cleanup
    for (size_t i = 0; i < wb.count; i++) {
        for (size_t j = 0; j < wb.sheets[i].count; j++) {
            free(wb.sheets[i].edits[j].val);
        }
        free(wb.sheets[i].edits);
    }
    free(wb.sheets);

    if (is_tty && argc < 3) printf(ok ? "Done.\n" : "Failed to save.\n");
    return ok ? 0 : 1;
}
