/* ============================================================================
 * CSV SQL - In-Memory Mutator (Text-Box Grid, Native RLE, Checksum Fix)
 *
 * COMPILATION INSTRUCTIONS:
 *   gcc -Os -s -Wl,--subsystem,windows -mwindows -o csvsql.exe csvsql.c -lcomdlg32 -lcomctl32 -lws2_32
 *
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ws2_32.lib")

/* ============================================================================
 * MACROS & CONSTANTS
 * ============================================================================ */
#define ID_EDIT_QUERY       101
#define ID_BTN_RUN          102
#define ID_BTN_OPEN         103
#define ID_LIST_TABLES      104
#define ID_STATUSBAR        106
#define ID_BTN_CLEAR        107
#define ID_COMBO_HISTORY    108
#define ID_COMBO_SERVER     109
#define ID_BTN_CONNECT      110
#define ID_BTN_COPY         111
#define ID_BTN_PASTE        112
#define ID_EDIT_OUTPUT      113

#define MAX_TABLES          32
#define MAX_COLUMNS         256
#define MAX_COLUMN_NAME     128
#define QUERY_BUFFER_SIZE   65536
#define MAX_CELL_SIZE       4096
#define MAX_CONCURRENT_CLIENTS 10
#define MAX_OUTPUT_SIZE     (1024 * 1024 * 5) /* 5 MB Text Output Buffer */

#define COLOR_BG_LIGHT      RGB(255, 255, 255)

/* ============================================================================
 * DATA STRUCTURES
 * ============================================================================ */
typedef struct {
    char name[MAX_COLUMN_NAME];
    char* filename;
    char delim;
    char** columns;
    int num_columns;
    char*** rows;
    int num_rows;
    int capacity_rows;
    int column_widths[MAX_COLUMNS];
} Table;

typedef enum {
    TK_EOF, TK_IDENT, TK_STR, TK_NUM, 
    TK_SELECT, TK_FROM, TK_WHERE, TK_INSERT, TK_INTO, TK_VALUES, 
    TK_UPDATE, TK_SET, TK_DELETE, TK_CREATE, TK_TABLE, TK_DROP,
    TK_JOIN, TK_ON, TK_AND, TK_OR, 
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_COMMA, TK_LPAREN, TK_RPAREN, TK_STAR, TK_SEMI
} TokenKind;

typedef struct { TokenKind kind; char value[512]; } Token;
typedef enum { OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_AND, OP_OR, VAL_STR, VAL_IDENT } ExprKind;
typedef struct ExprNode { ExprKind kind; char* value; struct ExprNode* left; struct ExprNode* right; } ExprNode;
typedef enum { STMT_SELECT, STMT_INSERT, STMT_UPDATE, STMT_DELETE, STMT_CREATE, STMT_DROP } StmtKind;

typedef struct {
    StmtKind kind;
    char table[MAX_COLUMN_NAME];
    char** cols; int num_cols;
    char** vals; int num_vals;
    ExprNode* where;
    char join_table[MAX_COLUMN_NAME];
    ExprNode* join_cond;
} SQLStmt;

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */
static Table tables[MAX_TABLES];
static int num_tables = 0;
static char* dropped_files[MAX_TABLES];
static int num_dropped_files = 0;
static int current_table_idx = -1;

static HWND hMainWnd, hEditQuery, hEditOutput, hComboHistory, hComboServer, hBtnConnect;
static HWND hBtnRun, hBtnClear, hBtnCopy, hBtnPaste, hBtnOpen, hListTables, hStatusBar;
static HFONT hFontFixed, hFontNormal;
static HBRUSH hBrushBg;

static char startup_tables[2048] = "";
static char startup_query[QUERY_BUFFER_SIZE] = "";

static char ui_status_msg[512] = "";

static const char* lex_ptr;
static Token curr_tok;
static CRITICAL_SECTION db_cs;

/* Telnet State */
static volatile LONG active_clients = 0;
static int telnet_enabled = 0;
static int telnet_plaintext = 1;
static int telnet_port = 23;
static int telnet_timeout = 20;
static char telnet_magic[128] = "\xA6";
static char telnet_password[128] = "";

static SOCKET client_sock = INVALID_SOCKET;
static int is_connected = 0;
static int client_userid = 1;
static char client_password[128] = "";

static WNDPROC OldEditProc = NULL;

/* ============================================================================
 * PROTOTYPES
 * ============================================================================ */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM);

void InitApplication(void);
ATOM RegisterAppClass(HINSTANCE);
BOOL CreateMainWindow(HINSTANCE, INT);
void ProcessStartup(void);

void SyncUI(const char* status, const char* result);
void AddToHistory(const char* query);
char* strdup_safe(const char* s);
char* trim(char* str);
char* StripQuotes(char* str);
char** ParseCommaLine(char* line, int* count);

void LoadHistory(void);
void SaveHistory(void);
void SaveTablesToINI(void);
void RefreshTablesList(void);
void ResetTableData(Table* tbl);
void ClearTable(Table* tbl);
int FindTable(const char* name);
int GetColIndex(Table* tbl, const char* colname);

void CopyToClipboard(const char* text);
void PasteToEdit(HWND hEdit);

unsigned char CharTo6Bit(char c);
char Bit6ToChar(unsigned char b);
int Pack6Bit(const char* in, int in_len, char* out, int out_max);
int Unpack6Bit(const char* in, int in_len, char* out, int out_max);
int CompressRLE(const char* in, int in_len, char* out, int out_max);
int DecompressRLE(const char* in, int in_len, char* out, int out_max);

void NextToken(void);
int Match(TokenKind kind);
int MatchIdent(char* out_name);
ExprNode* MakeNode(ExprKind k, ExprNode* l, ExprNode* r, const char* v);
void FreeAST(ExprNode* n);
ExprNode* ParseOr(void);
ExprNode* ParsePrimary(void);
ExprNode* ParseCmp(void);
ExprNode* ParseAnd(void);
SQLStmt* ParseStmt(void);
void FreeStmt(SQLStmt* s);

char* GetASTValJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2);
int EvalExprJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2);
void ExecuteAST(SQLStmt* s, char* out_buf, size_t out_max);
void ExecuteQueryEx(const char* query, char* out_buf, size_t out_max);

char DetectDelimiter(const char* data);
BOOL LoadCSV(const char* filename, const char* tablename);
static void emit_rfc4180(FILE* fp, const char* field, int is_last, char delim);
void SaveCSV(Table* tbl);

void ToggleConnection(void);
void SendToRemote(const char* query);
DWORD WINAPI TelnetClientThread(LPVOID lpParam);
DWORD WINAPI TelnetListenerThread(LPVOID lpParam);

void UpdateStatusBar(const char* text, ...);


/* ============================================================================
 * 6-BIT PACKING & RLE COMPRESSION ENGINE
 * ============================================================================ */
unsigned char CharTo6Bit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == ' ') return 62;
    return 63; 
}

char Bit6ToChar(unsigned char b) {
    if (b < 26) return 'A' + b;
    if (b < 52) return 'a' + (b - 26);
    if (b < 62) return '0' + (b - 52);
    if (b == 62) return ' ';
    return '\0'; 
}

int Pack6Bit(const char* in, int in_len, char* out, int out_max) {
    unsigned int bit_buf = 0; int bit_len = 0; int o = 0;
    for(int i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        unsigned char b6 = CharTo6Bit(c);
        
        bit_buf = (bit_buf << 6) | b6; bit_len += 6;
        if (b6 == 63) { bit_buf = (bit_buf << 8) | c; bit_len += 8; }
        while (bit_len >= 8) {
            if (o >= out_max) return -1;
            bit_len -= 8; out[o++] = (char)((bit_buf >> bit_len) & 0xFF);
        }
    }
    if (bit_len > 0) {
        if (o >= out_max) return -1;
        out[o++] = (char)((bit_buf << (8 - bit_len)) & 0xFF);
    }
    return o;
}

int Unpack6Bit(const char* in, int in_len, char* out, int out_max) {
    unsigned int bit_buf = 0; int bit_len = 0; int i = 0; int o = 0;
    while (i < in_len || bit_len >= 6) {
        while (bit_len < 14 && i < in_len) { bit_buf = (bit_buf << 8) | ((unsigned char)in[i++]); bit_len += 8; }
        if (bit_len < 6) break;
        
        unsigned char b6 = (bit_buf >> (bit_len - 6)) & 0x3F; bit_len -= 6;
        if (b6 == 63) {
            if (bit_len < 8) {
                if (i < in_len) { bit_buf = (bit_buf << 8) | ((unsigned char)in[i++]); bit_len += 8; } 
                else break;
            }
            unsigned char raw = (bit_buf >> (bit_len - 8)) & 0xFF; bit_len -= 8;
            if (o < out_max) out[o++] = (char)raw;
            if (raw == '\0') break; 
        } else {
            if (o < out_max) out[o++] = Bit6ToChar(b6);
        }
    }
    if (o < out_max) out[o] = '\0';
    return o;
}

int CompressRLE(const char* in, int in_len, char* out, int out_max) {
    int i = 0, o = 0;
    while (i < in_len && o < out_max - 4) {
        int run = 1;
        while (i + run < in_len && in[i + run] == in[i] && run < 255) run++;
        if (run >= 3 || in[i] == '\x1B') {
            out[o++] = '\x1B'; out[o++] = (char)run; out[o++] = in[i];
            i += run;
        } else {
            for (int j = 0; j < run; j++) out[o++] = in[i];
            i += run;
        }
    }
    return o;
}

int DecompressRLE(const char* in, int in_len, char* out, int out_max) {
    int i = 0, o = 0;
    while (i < in_len && o < out_max - 1) {
        if (in[i] == '\x1B') {
            if (i + 2 >= in_len) break;
            unsigned char run = (unsigned char)in[i+1];
            char val = in[i+2];
            for (int j = 0; j < run && o < out_max - 1; j++) out[o++] = val;
            i += 3;
        } else { out[o++] = in[i++]; }
    }
    out[o] = '\0';
    return o;
}

/* ============================================================================
 * UTILITY & CLIPBOARD FUNCTIONS
 * ============================================================================ */
char* trim(char* str) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str) { char* end = str + strlen(str) - 1; while (end > str && isspace((unsigned char)*end)) *end-- = '\0'; } 
    return str;
}
char* strdup_safe(const char* s) {
    if (!s) return NULL; size_t len = strlen(s) + 1; char* dup = malloc(len); return dup ? memcpy(dup, s, len) : NULL;
}
char* StripQuotes(char* str) {
    str = trim(str); size_t len = strlen(str); 
    if (len >= 2 && ((str[0] == '\'' && str[len-1] == '\'') || (str[0] == '"' && str[len-1] == '"'))) { str[len-1] = '\0'; str++; } 
    return str;
}

void SyncUI(const char* status, const char* result) {
    EnterCriticalSection(&db_cs);
    if (status) strncpy(ui_status_msg, status, sizeof(ui_status_msg)-1);
    LeaveCriticalSection(&db_cs);
    
    char* pass_res = NULL;
    if (result) pass_res = strdup_safe(result);
    PostMessage(hMainWnd, WM_USER + 2, 0, (LPARAM)pass_res); 
}

void CopyToClipboard(const char* text) {
    if (!OpenClipboard(hMainWnd)) return;
    EmptyClipboard(); size_t len = strlen(text) + 1; HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMem) { memcpy(GlobalLock(hMem), text, len); GlobalUnlock(hMem); SetClipboardData(CF_TEXT, hMem); }
    CloseClipboard();
}

void PasteToEdit(HWND hEdit) {
    if (!IsClipboardFormatAvailable(CF_TEXT)) return;
    if (!OpenClipboard(hMainWnd)) return;
    HGLOBAL hMem = GetClipboardData(CF_TEXT);
    if (hMem) { 
        const char* text = (const char*)GlobalLock(hMem); 
        if (text) { 
            SetWindowTextA(hEdit, ""); 
            SendMessageA(hEdit, EM_REPLACESEL, TRUE, (LPARAM)text); 
            GlobalUnlock(hMem); 
        } 
    }
    CloseClipboard();
}

int FindTable(const char* name) { for (int i = 0; i < num_tables; i++) if (_stricmp(tables[i].name, name) == 0) return i; return -1; }
int GetColIndex(Table* tbl, const char* colname) { for(int i=0; i<tbl->num_columns; i++) if(_stricmp(tbl->columns[i], colname) == 0) return i; return -1; }
void ResetTableData(Table* tbl) { 
    if (tbl->columns) { for (int i = 0; i < tbl->num_columns; i++) free(tbl->columns[i]); free(tbl->columns); tbl->columns = NULL; } 
    if (tbl->rows) { for (int r = 0; r < tbl->num_rows; r++) { for (int c = 0; c < tbl->num_columns; c++) free(tbl->rows[r][c]); free(tbl->rows[r]); } free(tbl->rows); tbl->rows = NULL; } 
    tbl->num_columns = 0; tbl->num_rows = 0; tbl->capacity_rows = 0; 
}
void ClearTable(Table* tbl) { ResetTableData(tbl); free(tbl->filename); memset(tbl, 0, sizeof(Table)); }

void SaveTablesToINI(void) {
    char ini_path[MAX_PATH]; GetModuleFileNameA(NULL, ini_path, MAX_PATH); 
    char* ext = strrchr(ini_path, '.'); if (ext) strcpy(ext, ".INI"); else strcat(ini_path, ".INI");
    
    char buf[2048] = {0};
    EnterCriticalSection(&db_cs);
    for (int i = 0; i < num_tables; i++) {
        if (strcmp(tables[i].name, "_results_") == 0) continue;
        if (strlen(buf) > 0) strcat(buf, ", ");
        strcat(buf, tables[i].name);
    }
    LeaveCriticalSection(&db_cs);
    WritePrivateProfileStringA("Client", "Tables", buf, ini_path);
}

void LoadHistory(void) {
    FILE* fp = fopen("queries.csv", "r"); if (!fp) return; char line[QUERY_BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        char* trimmed = trim(line); 
        if (strlen(trimmed) > 0) { wchar_t wbuf[QUERY_BUFFER_SIZE]; MultiByteToWideChar(CP_ACP, 0, trimmed, -1, wbuf, QUERY_BUFFER_SIZE); SendMessage(hComboHistory, CB_ADDSTRING, 0, (LPARAM)wbuf); }
    }
    fclose(fp);
}

void SaveHistory(void) {
    FILE* fp = fopen("queries.csv", "w"); if (!fp) return; int count = SendMessage(hComboHistory, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) { 
        wchar_t wbuf[QUERY_BUFFER_SIZE]; SendMessage(hComboHistory, CB_GETLBTEXT, i, (LPARAM)wbuf); 
        char buf[QUERY_BUFFER_SIZE]; WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, QUERY_BUFFER_SIZE, NULL, NULL); 
        fprintf(fp, "%s\n", buf); 
    }
    fclose(fp);
}

void AddToHistory(const char* query) {
    char buf[QUERY_BUFFER_SIZE]; 
    strncpy(buf, query, sizeof(buf) - 1); 
    buf[sizeof(buf) - 1] = '\0';
    
    /* Replace all linefeeds and carriage returns with spaces */
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') {
            buf[i] = ' ';
        }
    }
    
    char* trimmed = trim(buf); 
    if (strlen(trimmed) == 0) return;
    
    wchar_t wbuf[QUERY_BUFFER_SIZE]; 
    MultiByteToWideChar(CP_ACP, 0, trimmed, -1, wbuf, QUERY_BUFFER_SIZE);
    
    if (SendMessage(hComboHistory, CB_FINDSTRINGEXACT, -1, (LPARAM)wbuf) == CB_ERR) {
        SendMessage(hComboHistory, CB_ADDSTRING, 0, (LPARAM)wbuf);
    }
}
void RefreshTablesList(void) {
    EnterCriticalSection(&db_cs);
    int count = num_tables; int cur_idx = current_table_idx;
    char** buf = NULL; if(count > 0) buf = calloc(count, sizeof(char*));
    for (int i = 0; i < count; i++) { buf[i] = malloc(256); sprintf(buf[i], "[%s] %d cols / %d rows", tables[i].name, tables[i].num_columns, tables[i].num_rows); }
    LeaveCriticalSection(&db_cs);
    
    SendMessage(hListTables, LB_RESETCONTENT, 0, 0);
    for(int i = 0; i < count; i++) { wchar_t wbuf[256]; MultiByteToWideChar(CP_ACP, 0, buf[i], -1, wbuf, 256); SendMessage(hListTables, LB_ADDSTRING, 0, (LPARAM)wbuf); free(buf[i]); }
    if(buf) free(buf);
    if (cur_idx >= 0 && cur_idx < count) SendMessage(hListTables, LB_SETCURSEL, cur_idx, 0);
}
void UpdateStatusBar(const char* text, ...) { 
    va_list args; va_start(args, text); char f[512]; vsnprintf(f, 512, text, args); va_end(args); 
    wchar_t w[512]; MultiByteToWideChar(CP_ACP, 0, f, -1, w, 512); SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)w); 
}

/* ============================================================================
 * AST PARSER & EVALUATOR ENGINE
 * ============================================================================ */
void NextToken(void) {
    while (*lex_ptr && isspace((unsigned char)*lex_ptr)) lex_ptr++;
    curr_tok.value[0] = '\0'; curr_tok.kind = TK_EOF;

    if (!*lex_ptr) return;

    if (*lex_ptr == ';') { curr_tok.kind = TK_SEMI; curr_tok.value[0] = ';'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == ',') { curr_tok.kind = TK_COMMA; curr_tok.value[0] = ','; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '(') { curr_tok.kind = TK_LPAREN; curr_tok.value[0] = '('; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == ')') { curr_tok.kind = TK_RPAREN; curr_tok.value[0] = ')'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '*') { curr_tok.kind = TK_STAR; curr_tok.value[0] = '*'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '=') { curr_tok.kind = TK_EQ; curr_tok.value[0] = '='; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '!' && *(lex_ptr+1) == '=') { curr_tok.kind = TK_NEQ; strcpy(curr_tok.value, "!="); lex_ptr+=2; return; }
    if (*lex_ptr == '<') { curr_tok.kind = TK_LT; curr_tok.value[0] = '<'; curr_tok.value[1] = '\0'; lex_ptr++; return; }
    if (*lex_ptr == '>') { curr_tok.kind = TK_GT; curr_tok.value[0] = '>'; curr_tok.value[1] = '\0'; lex_ptr++; return; }

    if (*lex_ptr == '\'' || *lex_ptr == '"') {
        char quote = *lex_ptr++; int i = 0;
        while (*lex_ptr && *lex_ptr != quote && i < 510) curr_tok.value[i++] = *lex_ptr++;
        if (*lex_ptr == quote) lex_ptr++;
        curr_tok.value[i] = '\0'; curr_tok.kind = TK_STR;
        return;
    }

    /* Strict Alphanumeric Identifiers Only */
    if (isalpha((unsigned char)*lex_ptr) || *lex_ptr == '_') {
        int i = 0; while (isalnum((unsigned char)*lex_ptr) || *lex_ptr == '_') { if (i < 510) curr_tok.value[i++] = *lex_ptr; lex_ptr++; }
        curr_tok.value[i] = '\0'; char up[512]; for(int j=0; j<=i; j++) up[j] = toupper((unsigned char)curr_tok.value[j]);
        
        if (!strcmp(up, "SELECT")) curr_tok.kind = TK_SELECT; else if (!strcmp(up, "FROM")) curr_tok.kind = TK_FROM;
        else if (!strcmp(up, "WHERE")) curr_tok.kind = TK_WHERE; else if (!strcmp(up, "INSERT")) curr_tok.kind = TK_INSERT;
        else if (!strcmp(up, "INTO")) curr_tok.kind = TK_INTO; else if (!strcmp(up, "VALUES")) curr_tok.kind = TK_VALUES;
        else if (!strcmp(up, "UPDATE")) curr_tok.kind = TK_UPDATE; else if (!strcmp(up, "SET")) curr_tok.kind = TK_SET;
        else if (!strcmp(up, "DELETE")) curr_tok.kind = TK_DELETE; else if (!strcmp(up, "CREATE")) curr_tok.kind = TK_CREATE;
        else if (!strcmp(up, "TABLE")) curr_tok.kind = TK_TABLE; else if (!strcmp(up, "DROP")) curr_tok.kind = TK_DROP;
        else if (!strcmp(up, "JOIN") || !strcmp(up, "INNER") || !strcmp(up, "LEFT")) curr_tok.kind = TK_JOIN; 
        else if (!strcmp(up, "ON")) curr_tok.kind = TK_ON;
        else if (!strcmp(up, "AND")) curr_tok.kind = TK_AND; else if (!strcmp(up, "OR")) curr_tok.kind = TK_OR;
        else curr_tok.kind = TK_IDENT;
        return;
    }
    
    if (isdigit((unsigned char)*lex_ptr) || (*lex_ptr == '-' && isdigit((unsigned char)*(lex_ptr+1)))) {
        int i = 0; if (*lex_ptr == '-') { curr_tok.value[i++] = *lex_ptr++; }
        while (isdigit((unsigned char)*lex_ptr) || *lex_ptr == '.') { if (i < 510) curr_tok.value[i++] = *lex_ptr; lex_ptr++; }
        curr_tok.value[i] = '\0'; curr_tok.kind = TK_NUM; return;
    }
    
    curr_tok.kind = TK_IDENT; curr_tok.value[0] = *lex_ptr++; curr_tok.value[1] = '\0';
}

int Match(TokenKind kind) { if (curr_tok.kind == kind) { NextToken(); return 1; } return 0; }

int MatchIdent(char* out_name) {
    if (curr_tok.kind == TK_IDENT || curr_tok.kind == TK_STR || (curr_tok.kind >= TK_SELECT && curr_tok.kind <= TK_OR)) {
        strncpy(out_name, curr_tok.value, MAX_COLUMN_NAME - 1); 
        out_name[MAX_COLUMN_NAME - 1] = '\0'; 
        NextToken(); 
        return 1;
    }
    return 0;
}

ExprNode* MakeNode(ExprKind k, ExprNode* l, ExprNode* r, const char* v) { ExprNode* n = malloc(sizeof(ExprNode)); n->kind = k; n->left = l; n->right = r; n->value = v ? strdup_safe(v) : NULL; return n; }
void FreeAST(ExprNode* n) { if(!n) return; FreeAST(n->left); FreeAST(n->right); free(n->value); free(n); }
ExprNode* ParseOr(void);
ExprNode* ParsePrimary(void) {
    ExprNode* n = NULL; char ident[512];
    if (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM) { n = MakeNode(VAL_STR, NULL, NULL, curr_tok.value); NextToken(); }
    else if (Match(TK_LPAREN)) { n = ParseOr(); Match(TK_RPAREN); }
    else if (MatchIdent(ident)) { n = MakeNode(VAL_IDENT, NULL, NULL, ident); }
    return n;
}
ExprNode* ParseCmp(void) {
    ExprNode* n = ParsePrimary();
    while (curr_tok.kind == TK_EQ || curr_tok.kind == TK_NEQ || curr_tok.kind == TK_LT || curr_tok.kind == TK_GT) {
        ExprKind op = OP_EQ; if(curr_tok.kind == TK_NEQ) op = OP_NEQ; if(curr_tok.kind == TK_LT) op = OP_LT; if(curr_tok.kind == TK_GT) op = OP_GT;
        NextToken(); n = MakeNode(op, n, ParsePrimary(), NULL);
    }
    return n;
}
ExprNode* ParseAnd(void) { ExprNode* n = ParseCmp(); while (Match(TK_AND)) n = MakeNode(OP_AND, n, ParseCmp(), NULL); return n; }
ExprNode* ParseOr(void) { ExprNode* n = ParseAnd(); while (Match(TK_OR)) n = MakeNode(OP_OR, n, ParseAnd(), NULL); return n; }

SQLStmt* ParseStmt(void) {
    while (curr_tok.kind == TK_SEMI) NextToken(); 
    if (curr_tok.kind == TK_EOF) return NULL;
    
    SQLStmt* stmt = calloc(1, sizeof(SQLStmt)); int matched = 0;
    
    if (Match(TK_SELECT)) {
        stmt->kind = STMT_SELECT; stmt->cols = calloc(MAX_COLUMNS, sizeof(char*));
        if (Match(TK_STAR)) { stmt->cols[stmt->num_cols++] = strdup_safe("*"); }
        else { char col[512]; while (MatchIdent(col)) { if(stmt->num_cols < MAX_COLUMNS) stmt->cols[stmt->num_cols++] = strdup_safe(col); if (!Match(TK_COMMA)) break; } }
        if (Match(TK_FROM) && MatchIdent(stmt->table)) { 
            if (Match(TK_JOIN) && MatchIdent(stmt->join_table)) { if (Match(TK_ON)) stmt->join_cond = ParseOr(); }
            if (Match(TK_WHERE)) stmt->where = ParseOr(); 
            matched = 1; 
        }
    } 
    else if (Match(TK_INSERT) && Match(TK_INTO) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_INSERT; 
        if (Match(TK_VALUES) && Match(TK_LPAREN)) {
            stmt->vals = calloc(MAX_COLUMNS, sizeof(char*));
            while (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM) { if(stmt->num_vals < MAX_COLUMNS) stmt->vals[stmt->num_vals++] = strdup_safe(curr_tok.value); NextToken(); if (!Match(TK_COMMA)) break; }
            if (Match(TK_RPAREN)) matched = 1;
        }
    }
    else if (Match(TK_UPDATE) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_UPDATE; 
        if (Match(TK_SET)) {
            stmt->cols = calloc(2, sizeof(char*)); stmt->vals = calloc(2, sizeof(char*)); char col[512];
            if (MatchIdent(col)) {
                stmt->cols[0] = strdup_safe(col);
                if (Match(TK_EQ) && (curr_tok.kind == TK_STR || curr_tok.kind == TK_NUM)) { stmt->vals[0] = strdup_safe(curr_tok.value); NextToken(); stmt->num_cols = 1; stmt->num_vals = 1; matched = 1; }
                if (Match(TK_WHERE)) stmt->where = ParseOr();
            }
        }
    }
    else if (Match(TK_DELETE) && Match(TK_FROM) && MatchIdent(stmt->table)) { stmt->kind = STMT_DELETE; matched = 1; if (Match(TK_WHERE)) stmt->where = ParseOr(); }
    else if (Match(TK_CREATE) && Match(TK_TABLE) && MatchIdent(stmt->table)) {
        stmt->kind = STMT_CREATE; 
        if (Match(TK_LPAREN)) {
            stmt->cols = calloc(MAX_COLUMNS, sizeof(char*)); char col[512];
            while (curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) {
                if (curr_tok.kind == TK_IDENT && (_stricmp(curr_tok.value, "PRIMARY") == 0 || _stricmp(curr_tok.value, "FOREIGN") == 0 || _stricmp(curr_tok.value, "UNIQUE") == 0 || _stricmp(curr_tok.value, "CONSTRAINT") == 0 || _stricmp(curr_tok.value, "CHECK") == 0)) {
                    while (curr_tok.kind != TK_COMMA && curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) NextToken();
                } else if (MatchIdent(col)) {
                    if(stmt->num_cols < MAX_COLUMNS) stmt->cols[stmt->num_cols++] = strdup_safe(col);
                    while (curr_tok.kind != TK_COMMA && curr_tok.kind != TK_RPAREN && curr_tok.kind != TK_EOF) NextToken();
                } else { NextToken(); }
                if (Match(TK_COMMA)) continue; else break;
            }
            if (Match(TK_RPAREN)) matched = 1;
        }
    }
    else if (Match(TK_DROP) && Match(TK_TABLE) && MatchIdent(stmt->table)) { stmt->kind = STMT_DROP; matched = 1; }
    
    if (!matched) { free(stmt); while(curr_tok.kind != TK_EOF && curr_tok.kind != TK_SEMI) NextToken(); if (curr_tok.kind == TK_SEMI) NextToken(); return NULL; }
    while(curr_tok.kind != TK_EOF && curr_tok.kind != TK_SEMI) NextToken(); if (curr_tok.kind == TK_SEMI) NextToken(); return stmt;
}

void FreeStmt(SQLStmt* s) {
    if(!s) return;
    if(s->cols) { for(int i=0; i<s->num_cols; i++) free(s->cols[i]); free(s->cols); }
    if(s->vals) { for(int i=0; i<s->num_vals; i++) free(s->vals[i]); free(s->vals); }
    FreeAST(s->where); FreeAST(s->join_cond); free(s);
}

char* GetASTValJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2) {
    if (!n) return NULL;
    if (n->kind == VAL_STR) return n->value;
    if (n->kind == VAL_IDENT) {
        char* dot = strchr(n->value, '.');
        if (dot) {
            char tname[256]; strncpy(tname, n->value, dot - n->value); tname[dot - n->value] = '\0'; char* cname = dot + 1;
            if (_stricmp(tname, t1->name) == 0) { int c = GetColIndex(t1, cname); if (c >= 0 && r1 < t1->num_rows) return t1->rows[r1][c]; }
            else if (t2 && _stricmp(tname, t2->name) == 0) { int c = GetColIndex(t2, cname); if (c >= 0 && r2 < t2->num_rows) return t2->rows[r2][c]; }
        } else {
            int c = GetColIndex(t1, n->value); if (c >= 0 && r1 < t1->num_rows) return t1->rows[r1][c];
            if (t2) { c = GetColIndex(t2, n->value); if (c >= 0 && r2 < t2->num_rows) return t2->rows[r2][c]; }
        }
    }
    return NULL;
}

int EvalExprJoin(ExprNode* n, Table* t1, int r1, Table* t2, int r2) {
    if (!n) return 1; 
    if (n->kind == OP_AND) return EvalExprJoin(n->left, t1, r1, t2, r2) && EvalExprJoin(n->right, t1, r1, t2, r2);
    if (n->kind == OP_OR) return EvalExprJoin(n->left, t1, r1, t2, r2) || EvalExprJoin(n->right, t1, r1, t2, r2);
    
    char* vL = GetASTValJoin(n->left, t1, r1, t2, r2); char* vR = GetASTValJoin(n->right, t1, r1, t2, r2);
    if (!vL) vL = ""; if (!vR) vR = "";
    
    double numL = atof(vL), numR = atof(vR);
    int isNum = (isdigit((unsigned char)vL[0]) || vL[0]=='-') && (isdigit((unsigned char)vR[0]) || vR[0]=='-');
    
    if (n->kind == OP_EQ) return _stricmp(vL, vR) == 0;
    if (n->kind == OP_NEQ) return _stricmp(vL, vR) != 0;
    if (n->kind == OP_LT) return isNum ? (numL < numR) : (_stricmp(vL, vR) < 0);
    if (n->kind == OP_GT) return isNum ? (numL > numR) : (_stricmp(vL, vR) > 0);
    return 0;
}

void ExecuteAST(SQLStmt* s, char* out_buf, size_t out_max) {
    if (!s) return;
    
    if (s->kind == STMT_CREATE) {
        if (num_tables < MAX_TABLES) {
            Table* tbl = &tables[num_tables++]; memset(tbl, 0, sizeof(Table)); strncpy(tbl->name, s->table, sizeof(tbl->name)-1);
            char fn[256]; snprintf(fn, sizeof(fn), "%s.csv", s->table); tbl->filename = strdup_safe(fn); tbl->delim = ',';
            tbl->num_columns = s->num_cols; tbl->columns = calloc(MAX_COLUMNS, sizeof(char*));
            for(int i=0; i<s->num_cols; i++) { tbl->columns[i] = strdup_safe(s->cols[i]); tbl->column_widths[i] = strlen(s->cols[i]); }
            tbl->capacity_rows = 1000; tbl->rows = calloc(tbl->capacity_rows, sizeof(char**));
            current_table_idx = num_tables - 1; SaveTablesToINI();
            size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Created table '%s'.\r\n", s->table);
        }
    }
    else if (s->kind == STMT_DROP) {
        int idx = FindTable(s->table);
        if (idx >= 0) {
            dropped_files[num_dropped_files++] = strdup_safe(tables[idx].filename); ResetTableData(&tables[idx]); free(tables[idx].filename);
            for (int i = idx; i < num_tables - 1; i++) tables[i] = tables[i+1]; num_tables--; current_table_idx = num_tables > 0 ? 0 : -1; 
            SaveTablesToINI(); size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Dropped table '%s'.\r\n", s->table);
        } else { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Table '%s' not found.\r\n", s->table); }
    }
    else if (s->kind == STMT_INSERT) {
        int idx = FindTable(s->table);
        if (idx < 0 && num_tables < MAX_TABLES) {
            Table* new_tbl = &tables[num_tables]; memset(new_tbl, 0, sizeof(Table)); strncpy(new_tbl->name, s->table, sizeof(new_tbl->name)-1);
            char fn[256]; snprintf(fn, sizeof(fn), "%s.csv", s->table); new_tbl->filename = strdup_safe(fn); new_tbl->delim = ',';
            new_tbl->num_columns = s->num_vals; new_tbl->columns = calloc(MAX_COLUMNS, sizeof(char*));
            for(int i=0; i<s->num_vals; i++) { char col[32]; sprintf(col, "Col%d", i+1); new_tbl->columns[i] = strdup_safe(col); new_tbl->column_widths[i] = strlen(col); }
            new_tbl->capacity_rows = 1000; new_tbl->rows = calloc(new_tbl->capacity_rows, sizeof(char**)); idx = num_tables++; SaveTablesToINI();
        }
        if (idx >= 0) {
            Table* tbl = &tables[idx];
            if (tbl->num_rows >= tbl->capacity_rows) { tbl->capacity_rows *= 2; tbl->rows = realloc(tbl->rows, tbl->capacity_rows * sizeof(char**)); }
            tbl->rows[tbl->num_rows] = calloc(tbl->num_columns, sizeof(char*));
            for (int i=0; i<tbl->num_columns && i<s->num_vals; i++) { tbl->rows[tbl->num_rows][i] = strdup_safe(s->vals[i]); int vlen = strlen(s->vals[i]); if(vlen > tbl->column_widths[i]) tbl->column_widths[i] = vlen; }
            tbl->num_rows++; current_table_idx = idx; 
            size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Inserted 1 row into '%s'.\r\n", s->table);
        } else { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Table limit reached.\r\n"); }
    }
    else if (s->kind == STMT_UPDATE) {
        int idx = FindTable(s->table);
        if (idx >= 0 && s->num_cols == 1) {
            Table* tbl = &tables[idx]; int set_col = GetColIndex(tbl, s->cols[0]);
            if (set_col >= 0) {
                int affected = 0; for (int r=0; r<tbl->num_rows; r++) { if (EvalExprJoin(s->where, tbl, r, NULL, -1)) { free(tbl->rows[r][set_col]); tbl->rows[r][set_col] = strdup_safe(s->vals[0]); affected++; } }
                current_table_idx = idx; size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Updated %d rows in '%s'.\r\n", affected, s->table);
            } else { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Column '%s' not found.\r\n", s->cols[0]); }
        } else { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Table not found or syntax invalid.\r\n"); }
    }
    else if (s->kind == STMT_DELETE) {
        int idx = FindTable(s->table);
        if (idx >= 0) {
            Table* tbl = &tables[idx]; 
            if(!s->where) { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Bare deletes prevented. Provide a WHERE clause.\r\n"); }
            else {
                int affected = 0;
                for (int r=0; r<tbl->num_rows; r++) { if (EvalExprJoin(s->where, tbl, r, NULL, -1)) { for (int c=0; c<tbl->num_columns; c++) free(tbl->rows[r][c]); free(tbl->rows[r]); for (int k=r; k<tbl->num_rows - 1; k++) tbl->rows[k] = tbl->rows[k+1]; tbl->num_rows--; r--; affected++; } }
                current_table_idx = idx; size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Deleted %d rows from '%s'.\r\n", affected, s->table);
            }
        } else { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Table '%s' not found.\r\n", s->table); }
    }
    else if (s->kind == STMT_SELECT) {
        int idx = FindTable(s->table);
        if (idx >= 0) {
            Table* src1 = &tables[idx]; Table* src2 = NULL;
            int join_ok = 1;
            if (s->join_table[0] != '\0') {
                int j_idx = FindTable(s->join_table);
                if (j_idx >= 0) src2 = &tables[j_idx]; 
                else { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Joined table '%s' not found.\r\n", s->join_table); join_ok = 0; }
            }
            
            if (join_ok) {
                int res_idx = FindTable("_results_");
                if (res_idx < 0 && num_tables < MAX_TABLES) { res_idx = num_tables++; strcpy(tables[res_idx].name, "_results_"); tables[res_idx].filename = NULL; }
                if (res_idx >= 0) {
                    Table* dst = &tables[res_idx]; ResetTableData(dst);
                    dst->num_columns = 0; dst->columns = calloc(MAX_COLUMNS, sizeof(char*));
                    if (s->cols[0] && strcmp(s->cols[0], "*") == 0) {
                        for(int c=0; c<src1->num_columns; c++){ dst->columns[dst->num_columns] = strdup_safe(src1->columns[c]); dst->column_widths[dst->num_columns++] = src1->column_widths[c]; }
                        if (src2) { for(int c=0; c<src2->num_columns && dst->num_columns < MAX_COLUMNS; c++){ char cname[256]; snprintf(cname, 256, "%s.%s", src2->name, src2->columns[c]); dst->columns[dst->num_columns] = strdup_safe(cname); dst->column_widths[dst->num_columns++] = strlen(cname); } }
                    } else { for(int i=0; i<s->num_cols; i++){ dst->columns[dst->num_columns] = strdup_safe(s->cols[i]); dst->column_widths[dst->num_columns++] = strlen(s->cols[i]); } }
                    
                    dst->capacity_rows = 1000; dst->rows = calloc(dst->capacity_rows, sizeof(char**)); dst->num_rows = 0;
                    
                    for (int r1=0; r1<src1->num_rows; r1++) {
                        if (src2) {
                            for (int r2=0; r2<src2->num_rows; r2++) {
                                if (!s->join_cond || EvalExprJoin(s->join_cond, src1, r1, src2, r2)) {
                                    if (!s->where || EvalExprJoin(s->where, src1, r1, src2, r2)) {
                                        if (dst->num_rows >= dst->capacity_rows) { dst->capacity_rows *= 2; dst->rows = realloc(dst->rows, dst->capacity_rows * sizeof(char**)); }
                                        dst->rows[dst->num_rows] = calloc(dst->num_columns, sizeof(char*));
                                        if (s->cols[0] && strcmp(s->cols[0], "*") == 0) {
                                            int dc = 0;
                                            for(int c=0; c<src1->num_columns; c++) dst->rows[dst->num_rows][dc++] = strdup_safe(src1->rows[r1][c]);
                                            for(int c=0; c<src2->num_columns && dc < MAX_COLUMNS; c++) dst->rows[dst->num_rows][dc++] = strdup_safe(src2->rows[r2][c]);
                                        } else {
                                            for (int i=0; i<s->num_cols; i++) { ExprNode fake_node = { VAL_IDENT, s->cols[i], NULL, NULL }; char* val = GetASTValJoin(&fake_node, src1, r1, src2, r2); dst->rows[dst->num_rows][i] = val ? strdup_safe(val) : strdup_safe(""); }
                                        }
                                        dst->num_rows++;
                                    }
                                }
                            }
                        } else {
                            if (!s->where || EvalExprJoin(s->where, src1, r1, NULL, -1)) {
                                if (dst->num_rows >= dst->capacity_rows) { dst->capacity_rows *= 2; dst->rows = realloc(dst->rows, dst->capacity_rows * sizeof(char**)); }
                                dst->rows[dst->num_rows] = calloc(dst->num_columns, sizeof(char*));
                                if (s->cols[0] && strcmp(s->cols[0], "*") == 0) {
                                    for(int c=0; c<src1->num_columns; c++) dst->rows[dst->num_rows][c] = strdup_safe(src1->rows[r1][c]);
                                } else {
                                    for (int i=0; i<s->num_cols; i++) { ExprNode fake_node = { VAL_IDENT, s->cols[i], NULL, NULL }; char* val = GetASTValJoin(&fake_node, src1, r1, NULL, -1); dst->rows[dst->num_rows][i] = val ? strdup_safe(val) : strdup_safe(""); }
                                }
                                dst->num_rows++;
                            }
                        }
                    }
                    
                    int* widths = calloc(dst->num_columns, sizeof(int));
                    for (int c = 0; c < dst->num_columns; c++) {
                        widths[c] = strlen(dst->columns[c]);
                        for (int r = 0; r < dst->num_rows; r++) {
                            if (dst->rows[r][c]) {
                                int len = strlen(dst->rows[r][c]);
                                if (len > widths[c]) widths[c] = len;
                            }
                        }
                    }
                    size_t pos = strlen(out_buf);
                    for (int c = 0; c < dst->num_columns; c++) pos += snprintf(out_buf + pos, out_max - pos, "%-*s  ", widths[c], dst->columns[c]);
                    pos += snprintf(out_buf + pos, out_max - pos, "\r\n");
                    for (int c = 0; c < dst->num_columns; c++) {
                        for (int i = 0; i < widths[c]; i++) pos += snprintf(out_buf + pos, out_max - pos, "-");
                        pos += snprintf(out_buf + pos, out_max - pos, "  ");
                    }
                    pos += snprintf(out_buf + pos, out_max - pos, "\r\n");
                    for (int r = 0; r < dst->num_rows; r++) {
                        for (int c = 0; c < dst->num_columns; c++) pos += snprintf(out_buf + pos, out_max - pos, "%-*s  ", widths[c], dst->rows[r][c] ? dst->rows[r][c] : "");
                        pos += snprintf(out_buf + pos, out_max - pos, "\r\n");
                    }
                    if (dst->num_rows == 0) pos += snprintf(out_buf + pos, out_max - pos, "(0 rows)\r\n");
                    pos += snprintf(out_buf + pos, out_max - pos, "\r\n");
                    free(widths);
                    ClearTable(dst); num_tables--;
                }
            }
        } else { size_t pos = strlen(out_buf); snprintf(out_buf + pos, out_max - pos, "Error: Table '%s' not found.\r\n", s->table); }
    }
}

void ExecuteQueryEx(const char* query, char* out_buf, size_t out_max) {
    if(!query || !*query) return;
    
    EnterCriticalSection(&db_cs); int conn = is_connected; LeaveCriticalSection(&db_cs);
    if (conn && !out_buf) {
        EnterCriticalSection(&db_cs); lex_ptr = query; NextToken(); int syntax_ok = 1;
        while (curr_tok.kind != TK_EOF) { SQLStmt* ast = ParseStmt(); if (ast) FreeStmt(ast); else { if (curr_tok.kind != TK_EOF) syntax_ok = 0; break; } }
        LeaveCriticalSection(&db_cs);
        if (!syntax_ok) { SyncUI("Syntax Error before sending.", "Syntax Error or Unsupported Command."); return; }
        SendToRemote(query); AddToHistory(query); return; 
    }
    
    int is_local = (out_buf == NULL);
    char* exec_buf = out_buf; size_t exec_max = out_max;
    if (is_local) { exec_max = MAX_OUTPUT_SIZE; exec_buf = calloc(exec_max, 1); }
    
    EnterCriticalSection(&db_cs); lex_ptr = query; NextToken();
    int has_success = 0; int has_error = 0;
    
    while (curr_tok.kind != TK_EOF) {
        SQLStmt* ast = ParseStmt();
        if (ast) { 
            ExecuteAST(ast, exec_buf, exec_max); 
            FreeStmt(ast); has_success = 1; 
        } 
        else if (curr_tok.kind != TK_EOF) {
            size_t pos = strlen(exec_buf); snprintf(exec_buf + pos, exec_max - pos, "Syntax Error or Unsupported Command.\r\n");
            has_error = 1; break;
        }
    }
    LeaveCriticalSection(&db_cs);
    
    if (is_local) {
        if (has_success && !has_error) AddToHistory(query);
        SyncUI(has_error ? "Error" : "Success", exec_buf);
        free(exec_buf); SetFocus(hEditQuery); 
    }
}

/* ============================================================================
 * TELNET SERVER & CLIENT ENGINE
 * ============================================================================ */
void ToggleConnection(void) {
    EnterCriticalSection(&db_cs);
    if (is_connected) { closesocket(client_sock); client_sock = INVALID_SOCKET; is_connected = 0; LeaveCriticalSection(&db_cs); SyncUI("Disconnected from remote server.", NULL); return; }
    LeaveCriticalSection(&db_cs);
    
    char server_str[256]; GetWindowTextA(hComboServer, server_str, sizeof(server_str)); if (strlen(server_str) == 0) return;
    char ip[256]; int port = 23; strcpy(ip, server_str); char* colon = strchr(ip, ':'); if (colon) { *colon = '\0'; port = atoi(colon + 1); }
    unsigned long ip_addr = inet_addr(ip); if (ip_addr == INADDR_NONE) { struct hostent* he = gethostbyname(ip); if (he) ip_addr = *(unsigned long*)he->h_addr_list[0]; }
    
    SOCKET tmp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); struct sockaddr_in addr = {0}; addr.sin_family = AF_INET; addr.sin_port = htons(port); addr.sin_addr.s_addr = ip_addr;
    int timeout = 5000; setsockopt(tmp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    
    int success = (connect(tmp_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    
    EnterCriticalSection(&db_cs);
    if (success) {
        client_sock = tmp_sock; is_connected = 1; char msg[256]; sprintf(msg, "Connected to %s:%d", ip, port);
        LeaveCriticalSection(&db_cs); SyncUI(msg, NULL);
        if (SendMessageA(hComboServer, CB_FINDSTRINGEXACT, -1, (LPARAM)server_str) == CB_ERR) {
            SendMessageA(hComboServer, CB_ADDSTRING, 0, (LPARAM)server_str);
            char all_servers[1024] = {0}; int count = SendMessage(hComboServer, CB_GETCOUNT, 0, 0);
            for (int i=0; i<count; i++) { char temp[256]; SendMessageA(hComboServer, CB_GETLBTEXT, i, (LPARAM)temp); strcat(all_servers, temp); if (i < count - 1) strcat(all_servers, ","); }
            char ini_path[MAX_PATH]; GetModuleFileNameA(NULL, ini_path, MAX_PATH); char* ext = strrchr(ini_path, '.'); if (ext) strcpy(ext, ".INI"); else strcat(ini_path, ".INI"); WritePrivateProfileStringA("Client", "Servers", all_servers, ini_path);
        }
    } else {
        closesocket(tmp_sock); LeaveCriticalSection(&db_cs); MessageBoxA(hMainWnd, "Failed to connect to server.", "Connection Error", MB_ICONERROR);
    }
}

void SendToRemote(const char* query) {
    char temp[QUERY_BUFFER_SIZE]; snprintf(temp, sizeof(temp), "%d\xA6%s\xA6%s", client_userid, client_password, query);
    unsigned int sum = 0; for (char* p = temp; *p; p++) sum += (unsigned char)*p;
    
    char* uncomp = malloc(QUERY_BUFFER_SIZE * 2);
    snprintf(uncomp, QUERY_BUFFER_SIZE * 2, "%s\xA6%u", temp, sum);
    
    char* packed_buf = malloc(QUERY_BUFFER_SIZE * 2);
    int packed_size = Pack6Bit(uncomp, strlen(uncomp) + 1, packed_buf, QUERY_BUFFER_SIZE * 2);
    
    char* comp_buf = malloc(QUERY_BUFFER_SIZE * 2);
    int comp_size = CompressRLE(packed_buf, packed_size, comp_buf, QUERY_BUFFER_SIZE * 2);
    
    if (comp_size <= 0) { SyncUI("Error", "Error compressing payload."); free(uncomp); free(packed_buf); free(comp_buf); return; }
    
    int magic_len = strlen(telnet_magic); 
    char* send_buf = malloc(QUERY_BUFFER_SIZE * 2 + 256);
    memcpy(send_buf, telnet_magic, magic_len); memcpy(send_buf + magic_len, comp_buf, comp_size);
    
    if (send(client_sock, send_buf, magic_len + comp_size, 0) <= 0) {
        EnterCriticalSection(&db_cs); closesocket(client_sock); client_sock = INVALID_SOCKET; is_connected = 0; LeaveCriticalSection(&db_cs);
        SyncUI("Error: Connection lost.", "Error: Connection lost."); 
        free(uncomp); free(packed_buf); free(comp_buf); free(send_buf); return;
    }
    
    char* recv_buf = malloc(MAX_OUTPUT_SIZE); 
    int recv_len = recv(client_sock, recv_buf, MAX_OUTPUT_SIZE - 1, 0);
    if (recv_len > 0) {
        if (recv_len >= magic_len && strncmp(recv_buf, telnet_magic, magic_len) == 0) {
            char* rle_payload = recv_buf + magic_len; int rle_len = recv_len - magic_len;
            
            char* unpacked_rle = malloc(MAX_OUTPUT_SIZE);
            int unpacked_rle_size = DecompressRLE(rle_payload, rle_len, unpacked_rle, MAX_OUTPUT_SIZE);
            
            char* decomp_buf = malloc(MAX_OUTPUT_SIZE);
            int decomp_size = Unpack6Bit(unpacked_rle, unpacked_rle_size, decomp_buf, MAX_OUTPUT_SIZE);
            
            if (decomp_size > 0) {
                SyncUI("Received remote response.", decomp_buf);
            } else SyncUI("Error", "Error decompressing response.");
            
            free(unpacked_rle); free(decomp_buf);
        } else SyncUI("Error", "Error: Invalid response magic string.");
    } else {
        EnterCriticalSection(&db_cs); closesocket(client_sock); client_sock = INVALID_SOCKET; is_connected = 0; LeaveCriticalSection(&db_cs);
        SyncUI("Error: Connection dropped.", "Error: No response or connection dropped.");
    }
    
    free(uncomp); free(packed_buf); free(comp_buf); free(send_buf); free(recv_buf);
}

DWORD WINAPI TelnetClientThread(LPVOID lpParam) {
    SOCKET sock = (SOCKET)lpParam; DWORD timeout = telnet_timeout * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    
    char* in_buf = malloc(QUERY_BUFFER_SIZE * 2);
    
    while (1) {
        int in_len = recv(sock, in_buf, (QUERY_BUFFER_SIZE * 2) - 1, 0); if (in_len <= 0) break;
        if (telnet_plaintext) {
            in_buf[in_len] = '\0'; if (_strnicmp(in_buf, "bye", 3) == 0) break; 
            char* out_buf = calloc(MAX_OUTPUT_SIZE, 1);
            ExecuteQueryEx(in_buf, out_buf, MAX_OUTPUT_SIZE); send(sock, out_buf, strlen(out_buf), 0);
            free(out_buf);
        } else {
            int magic_len = strlen(telnet_magic); if (in_len < magic_len || strncmp(in_buf, telnet_magic, magic_len) != 0) break; 
            char* rle_payload = in_buf + magic_len; int rle_len = in_len - magic_len; 
            
            char* unpacked_rle = malloc(QUERY_BUFFER_SIZE * 2);
            int unpacked_rle_size = DecompressRLE(rle_payload, rle_len, unpacked_rle, QUERY_BUFFER_SIZE * 2);
            
            char* decomp_buf = malloc(QUERY_BUFFER_SIZE * 2);
            int decomp_size = Unpack6Bit(unpacked_rle, unpacked_rle_size, decomp_buf, QUERY_BUFFER_SIZE * 2);
            
            if (decomp_size > 0) {
                char* payload_start = decomp_buf; char* last_pipe = strrchr(payload_start, '\xA6'); 
                char* out_buf = calloc(MAX_OUTPUT_SIZE, 1);
                
                if (last_pipe) {
                    *last_pipe = '\0'; unsigned int provided_chk = atoi(last_pipe + 1);
                    unsigned int sum = 0; for(char* p = decomp_buf; *p; p++) sum += (unsigned char)*p;
                    if (sum == provided_chk) {
                        char* pipe1 = strchr(decomp_buf, '\xA6');
                        if (pipe1) {
                            *pipe1 = '\0'; char* pipe2 = strchr(pipe1 + 1, '\xA6');
                            if (pipe2) {
                                *pipe2 = '\0'; char* rcv_pass = pipe1 + 1; char* rcv_query = pipe2 + 1;
                                int pass_len = strlen(telnet_password); int rcv_len = strlen(rcv_pass); int mismatch = (pass_len ^ rcv_len);
                                for (int i = 0; i < pass_len && i < rcv_len; i++) mismatch |= (telnet_password[i] ^ rcv_pass[i]);
                                if (mismatch == 0) {
                                    if (_strnicmp(rcv_query, "bye", 3) == 0) { free(out_buf); free(unpacked_rle); free(decomp_buf); break; }
                                    ExecuteQueryEx(rcv_query, out_buf, MAX_OUTPUT_SIZE); 
                                } else { strcpy(out_buf, "Error: Access Denied (Invalid Password)\r\n"); Sleep(2000); }
                            } else strcpy(out_buf, "Error: Invalid Format\r\n");
                        } else strcpy(out_buf, "Error: Invalid Format\r\n");
                    } else { strcpy(out_buf, "Error: Checksum mismatch\r\n"); }
                } else strcpy(out_buf, "Error: Invalid Format\r\n");
                
                char* packed_buf = malloc(MAX_OUTPUT_SIZE);
                int packed_size = Pack6Bit(out_buf, strlen(out_buf) + 1, packed_buf, MAX_OUTPUT_SIZE);
                
                char* comp_buf = malloc(MAX_OUTPUT_SIZE);
                int comp_size = CompressRLE(packed_buf, packed_size, comp_buf, MAX_OUTPUT_SIZE);
                
                char* send_buf = malloc(MAX_OUTPUT_SIZE + 256); 
                memcpy(send_buf, telnet_magic, magic_len); memcpy(send_buf + magic_len, comp_buf, comp_size);
                send(sock, send_buf, magic_len + comp_size, 0); 
                
                free(out_buf); free(packed_buf); free(comp_buf); free(send_buf);
            } else { free(unpacked_rle); free(decomp_buf); break; }
            free(unpacked_rle); free(decomp_buf);
        }
    }
    free(in_buf);
    closesocket(sock); InterlockedDecrement(&active_clients); return 0;
}

DWORD WINAPI TelnetListenerThread(LPVOID lpParam) {
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(telnet_port);
    bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)); listen(listen_sock, SOMAXCONN);
    while (1) { 
        SOCKET c_sock = accept(listen_sock, NULL, NULL); 
        if (c_sock != INVALID_SOCKET) { 
            if (InterlockedIncrement(&active_clients) <= MAX_CONCURRENT_CLIENTS) { CreateThread(NULL, 0, TelnetClientThread, (LPVOID)c_sock, 0, NULL); } 
            else { closesocket(c_sock); InterlockedDecrement(&active_clients); }
        } 
    }
    return 0;
}

/* ============================================================================
 * RFC 4180 CSV ENGINE
 * ============================================================================ */
char DetectDelimiter(const char* data) {
    int counts[256] = {0};
    for(int i=0; data[i] && data[i] != '\n'; i++) {
        if(data[i] == '"') { i++; while(data[i] && data[i] != '"') i++; if(!data[i]) break; }
        else counts[(unsigned char)data[i]]++;
    }
    int max = 0; char delim = ','; char candidates[] = {',', ';', '\t', '|', '\xA6'};
    for(int i=0; i<5; i++) { if(counts[(unsigned char)candidates[i]] > max) { max = counts[(unsigned char)candidates[i]]; delim = candidates[i]; } } return delim;
}

BOOL LoadCSV(const char* filename, const char* tablename) {
    FILE* fp = fopen(filename, "rb"); if (!fp) return FALSE; fseek(fp, 0, SEEK_END); long len = ftell(fp); fseek(fp, 0, SEEK_SET);
    char* buf = malloc(len + 1); fread(buf, 1, len, fp); buf[len] = '\0'; fclose(fp);
    
    EnterCriticalSection(&db_cs);
    if (num_tables >= MAX_TABLES) { free(buf); LeaveCriticalSection(&db_cs); return FALSE; }
    Table* tbl = &tables[num_tables]; memset(tbl, 0, sizeof(Table)); strncpy(tbl->name, tablename, sizeof(tbl->name)-1); tbl->filename = strdup_safe(filename); tbl->delim = DetectDelimiter(buf);
    tbl->capacity_rows = 1000; tbl->rows = calloc(tbl->capacity_rows, sizeof(char**)); tbl->columns = calloc(MAX_COLUMNS, sizeof(char*));
    
    char* p = buf; int is_header = 1;
    while (*p) {
        char** fields = calloc(MAX_COLUMNS, sizeof(char*)); int count = 0; int in_row = 1;
        while(in_row && *p) {
            char fbuf[MAX_CELL_SIZE]; int fi = 0; int in_quotes = 0;
            if(*p == '"') {
                in_quotes = 1; p++; while(*p) { if(*p == '"') { if(*(p+1) == '"') { fbuf[fi++] = '"'; p+=2; } else { in_quotes = 0; p++; break; } } else { if(fi < MAX_CELL_SIZE-1) fbuf[fi++] = *p; p++; } }
            } else { while(*p && *p != tbl->delim && *p != '\n' && *p != '\r') { if(fi < MAX_CELL_SIZE-1) fbuf[fi++] = *p; p++; } }
            fbuf[fi] = '\0'; if(count < MAX_COLUMNS) fields[count++] = strdup_safe(fbuf);
            if(*p == tbl->delim) p++; else if(*p == '\r' || *p == '\n') { in_row = 0; if(*p == '\r' && *(p+1) == '\n') p+=2; else p++; } else if(!*p) in_row = 0;
        }
        if (count > 0 || (count==0 && *p)) {
            if (is_header) { tbl->num_columns = count; for (int i = 0; i < count; i++) { tbl->columns[i] = fields[i]; tbl->column_widths[i] = strlen(fields[i]); } free(fields); is_header = 0; } 
            else {
                if (tbl->num_rows >= tbl->capacity_rows) { tbl->capacity_rows *= 2; tbl->rows = realloc(tbl->rows, tbl->capacity_rows * sizeof(char**)); }
                tbl->rows[tbl->num_rows] = calloc(tbl->num_columns, sizeof(char*));
                for (int i = 0; i < tbl->num_columns && i < count; i++) { tbl->rows[tbl->num_rows][i] = fields[i]; int field_len = strlen(fields[i]); if (field_len > tbl->column_widths[i]) tbl->column_widths[i] = field_len; }
                tbl->num_rows++; free(fields);
            }
        } else free(fields);
    }
    free(buf); current_table_idx = num_tables++; LeaveCriticalSection(&db_cs); 
    SaveTablesToINI();
    char msg[256]; sprintf(msg, "Loaded: %s (%d cols, %d rows, delim '%c')", tablename, tbl->num_columns, tbl->num_rows, tbl->delim); SyncUI(msg, ""); return TRUE;
}

static void emit_rfc4180(FILE* fp, const char* field, int is_last, char delim) {
    if (!field) { fprintf(fp, "%c", is_last ? '\n' : delim); return; } int needs_quotes = strchr(field, delim) || strchr(field, '\n') || strchr(field, '"');
    if (needs_quotes) { fputc('"', fp); for (const char* c = field; *c; c++) { if (*c == '"') fputs("\"\"", fp); else fputc(*c, fp); } fputc('"', fp); } else fputs(field, fp); fputc(is_last ? '\n' : delim, fp);
}
void SaveCSV(Table* tbl) {
    if (!tbl->filename) return; FILE* fp = fopen(tbl->filename, "wb"); if (!fp) return;
    for (int c = 0; c < tbl->num_columns; c++) emit_rfc4180(fp, tbl->columns[c], c == tbl->num_columns - 1, tbl->delim);
    for (int r = 0; r < tbl->num_rows; r++) { for (int c = 0; c < tbl->num_columns; c++) emit_rfc4180(fp, tbl->rows[r][c], c == tbl->num_columns - 1, tbl->delim); } fclose(fp);
}

/* ============================================================================
 * UI WNDPROCS & STARTUP
 * ============================================================================ */
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN) {
        if (wParam == 'A' && GetKeyState(VK_CONTROL) < 0) { SendMessage(hwnd, EM_SETSEL, 0, -1); return 0; }
        if (wParam == VK_F5 && hwnd == hEditQuery) { char query[QUERY_BUFFER_SIZE]; GetWindowTextA(hwnd, query, QUERY_BUFFER_SIZE); ExecuteQueryEx(query, NULL, 0); return 0; }
    }
    if (uMsg == WM_CHAR && wParam == 1) { return 0; } return CallWindowProc(OldEditProc, hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_USER + 1: ProcessStartup(); return 0;
        case WM_USER + 2: {
            char t_status[512]; 
            EnterCriticalSection(&db_cs); strcpy(t_status, ui_status_msg); LeaveCriticalSection(&db_cs);
            if (t_status[0]) { wchar_t ws[512]; MultiByteToWideChar(CP_ACP, 0, t_status, -1, ws, 512); SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)ws); }
            char* t_result = (char*)lParam;
            if (t_result) { SetWindowTextA(hEditOutput, t_result); free(t_result); }
            RefreshTablesList(); InvalidateRect(hBtnConnect, NULL, TRUE); return 0;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            if (pdis->CtlID == ID_BTN_CONNECT) {
                EnterCriticalSection(&db_cs); int conn = is_connected; LeaveCriticalSection(&db_cs);
                HBRUSH hbr; if (conn) hbr = CreateSolidBrush(RGB(50, 200, 50)); else hbr = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
                FillRect(pdis->hDC, &pdis->rcItem, hbr); DeleteObject(hbr); SetBkMode(pdis->hDC, TRANSPARENT); SetTextColor(pdis->hDC, conn ? RGB(255,255,255) : GetSysColor(COLOR_BTNTEXT)); HFONT hOldFont = (HFONT)SelectObject(pdis->hDC, hFontNormal); DrawTextA(pdis->hDC, conn ? "Connected" : "Connect", -1, &pdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(pdis->hDC, hOldFont); DrawEdge(pdis->hDC, &pdis->rcItem, (pdis->itemState & ODS_SELECTED) ? BDR_SUNKENOUTER : BDR_RAISEDINNER, BF_RECT); return TRUE;
            } break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_BTN_RUN: if (HIWORD(wParam) == BN_CLICKED) { char q[QUERY_BUFFER_SIZE]; GetWindowTextA(hEditQuery, q, QUERY_BUFFER_SIZE); ExecuteQueryEx(q, NULL, 0); } break;
                case ID_BTN_CLEAR: if (HIWORD(wParam) == BN_CLICKED) { SetWindowTextW(hEditQuery, L""); SetFocus(hEditQuery); } break;
                case ID_BTN_CONNECT: if (HIWORD(wParam) == BN_CLICKED) ToggleConnection(); break;
                case ID_BTN_PASTE: if (HIWORD(wParam) == BN_CLICKED) { PasteToEdit(hEditQuery); SetFocus(hEditQuery); } break;
                case ID_BTN_COPY: if (HIWORD(wParam) == BN_CLICKED) { SendMessage(hEditOutput, EM_SETSEL, 0, -1); SendMessage(hEditOutput, WM_COPY, 0, 0); SendMessage(hEditOutput, EM_SETSEL, -1, -1); } break;
                case ID_COMBO_HISTORY: if (HIWORD(wParam) == CBN_SELCHANGE) { int idx = SendMessage(hComboHistory, CB_GETCURSEL, 0, 0); if (idx != CB_ERR) { wchar_t wb[QUERY_BUFFER_SIZE]; SendMessage(hComboHistory, CB_GETLBTEXT, idx, (LPARAM)wb); SetWindowTextW(hEditQuery, wb); } } break;
                case ID_LIST_TABLES: if (HIWORD(wParam) == LBN_SELCHANGE) { EnterCriticalSection(&db_cs); current_table_idx = SendMessage(hListTables, LB_GETCURSEL, 0, 0); LeaveCriticalSection(&db_cs); SyncUI(NULL, ""); } break;
                case ID_BTN_OPEN: { OPENFILENAMEA ofn = {0}; char sz[MAX_PATH] = ""; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files\0*.*\0"; ofn.lpstrFile = sz; ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST; if (GetOpenFileNameA(&ofn)) { char* fn = strrchr(sz, '\\'); fn = fn ? fn + 1 : sz; char tb[128]; char* dot = strrchr(fn, '.'); if (dot) { strncpy(tb, fn, dot - fn); tb[dot - fn] = '\0'; } else strcpy(tb, fn); LoadCSV(sz, tb); } break; }
            } break;
        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam); if (w == 0 || h == 0) return 0;
            MoveWindow(hBtnOpen, 10, 10, 120, 30, TRUE); MoveWindow(hComboServer, 140, 13, 200, 200, TRUE); MoveWindow(hBtnConnect, 350, 10, 100, 30, TRUE); MoveWindow(hBtnCopy, 460, 10, 100, 30, TRUE);
            MoveWindow(hListTables, 10, 50, w - 20, 80, TRUE); MoveWindow(hComboHistory, 10, 140, w - 20, 200, TRUE); 
            MoveWindow(hBtnRun, 10, 175, 80, 60, TRUE); MoveWindow(hBtnPaste, 95, 175, 80, 60, TRUE); MoveWindow(hEditQuery, 180, 175, w - 270, 60, TRUE); MoveWindow(hBtnClear, w - 80, 175, 70, 60, TRUE); 
            MoveWindow(hEditOutput, 10, 245, w - 20, h - 245 - 25, TRUE); MoveWindow(hStatusBar, 0, h - 25, w, 25, TRUE); 
            break;
        }
        case WM_DESTROY:
            EnterCriticalSection(&db_cs); for (int i = 0; i < num_tables; i++) { SaveCSV(&tables[i]); ClearTable(&tables[i]); } for (int i = 0; i < num_dropped_files; i++) { remove(dropped_files[i]); free(dropped_files[i]); } LeaveCriticalSection(&db_cs); DeleteCriticalSection(&db_cs);
            SaveHistory(); DeleteObject(hFontFixed); DeleteObject(hFontNormal); DeleteObject(hBrushBg); WSACleanup(); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void InitApplication(void) { InitializeCriticalSection(&db_cs); INITCOMMONCONTROLSEX icex; icex.dwSize = sizeof(INITCOMMONCONTROLSEX); icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES; InitCommonControlsEx(&icex); hBrushBg = CreateSolidBrush(COLOR_BG_LIGHT); }
ATOM RegisterAppClass(HINSTANCE hInstance) { WNDCLASSEXW wc = {0}; wc.cbSize = sizeof(WNDCLASSEXW); wc.lpfnWndProc = WndProc; wc.hInstance = hInstance; wc.lpszClassName = L"CSV_SQL_Main"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = hBrushBg; return RegisterClassExW(&wc); }

BOOL CreateMainWindow(HINSTANCE hInstance, INT nCmdShow) {
    hMainWnd = CreateWindowExW(0, L"CSV_SQL_Main", L"CSV SQL - Query Your CSV Files", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, NULL, NULL, hInstance, NULL); if (!hMainWnd) return FALSE;
    hFontFixed = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    hFontNormal = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    
    hBtnOpen = CreateWindowW(L"BUTTON", L"📂 Open Table...", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_OPEN, hInstance, NULL); SendMessage(hBtnOpen, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    hComboServer = CreateWindowW(L"COMBOBOX", L"", CBS_DROPDOWN | WS_VISIBLE | WS_CHILD | WS_VSCROLL, 0, 0, 0, 0, hMainWnd, (HMENU)ID_COMBO_SERVER, hInstance, NULL); SendMessage(hComboServer, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    hBtnConnect = CreateWindowW(L"BUTTON", L"Connect", BS_OWNERDRAW | WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_CONNECT, hInstance, NULL);
    hBtnCopy = CreateWindowW(L"BUTTON", L"Copy Output", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_COPY, hInstance, NULL); SendMessage(hBtnCopy, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    hListTables = CreateWindowW(L"LISTBOX", L"", LBS_NOTIFY | WS_BORDER | WS_VISIBLE | WS_CHILD | WS_VSCROLL, 0, 0, 0, 0, hMainWnd, (HMENU)ID_LIST_TABLES, hInstance, NULL); SendMessage(hListTables, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    hComboHistory = CreateWindowW(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VISIBLE | WS_CHILD | WS_VSCROLL, 0, 0, 0, 0, hMainWnd, (HMENU)ID_COMBO_HISTORY, hInstance, NULL); SendMessage(hComboHistory, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    hBtnRun = CreateWindowW(L"BUTTON", L"Run", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_RUN, hInstance, NULL); SendMessage(hBtnRun, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    hBtnPaste = CreateWindowW(L"BUTTON", L"Paste", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_PASTE, hInstance, NULL); SendMessage(hBtnPaste, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    hEditQuery = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", ES_AUTOHSCROLL | ES_WANTRETURN | WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE, 0, 0, 0, 0, hMainWnd, (HMENU)ID_EDIT_QUERY, hInstance, NULL); SendMessage(hEditQuery, WM_SETFONT, (WPARAM)hFontFixed, TRUE); OldEditProc = (WNDPROC)SetWindowLongPtrW(hEditQuery, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    hBtnClear = CreateWindowW(L"BUTTON", L"Clear", BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hMainWnd, (HMENU)ID_BTN_CLEAR, hInstance, NULL); SendMessage(hBtnClear, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    
    hEditOutput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VISIBLE, 0, 0, 0, 0, hMainWnd, (HMENU)ID_EDIT_OUTPUT, hInstance, NULL); 
    SendMessage(hEditOutput, WM_SETFONT, (WPARAM)hFontFixed, TRUE); 
    SendMessage(hEditOutput, EM_LIMITTEXT, (WPARAM)(MAX_OUTPUT_SIZE), 0);
    SetWindowLongPtrW(hEditOutput, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    
    hStatusBar = CreateWindowW(L"STATUSCLASSNAME", L"", SBARS_SIZEGRIP | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hMainWnd, (HMENU)ID_STATUSBAR, hInstance, NULL); SendMessage(hStatusBar, WM_SETFONT, (WPARAM)hFontNormal, TRUE); int parts[] = {300, 600, -1}; SendMessage(hStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
    
    ShowWindow(hMainWnd, nCmdShow); PostMessage(hMainWnd, WM_USER + 1, 0, 0); return TRUE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (_wcsicmp(argv[i], L"-t") == 0) { i++; while (i < argc && argv[i][0] != L'-') { if (strlen(startup_tables) > 0) strcat(startup_tables, " "); char temp[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, argv[i], -1, temp, MAX_PATH, NULL, NULL); strcat(startup_tables, temp); i++; } i--; }
            else if (_wcsicmp(argv[i], L"-q") == 0 && i + 1 < argc) { WideCharToMultiByte(CP_ACP, 0, argv[i+1], -1, startup_query, sizeof(startup_query), NULL, NULL); i++; }
        } LocalFree(argv);
    }
    InitApplication(); if (!RegisterAppClass(hInstance)) return 1; if (!CreateMainWindow(hInstance, nCmdShow)) return 1; UpdateWindow(hMainWnd);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); } return (int)msg.wParam;
}

void ProcessStartup(void) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa); LoadHistory();
    char ini_path[MAX_PATH]; GetModuleFileNameA(NULL, ini_path, MAX_PATH); char* ext = strrchr(ini_path, '.'); if (ext) strcpy(ext, ".INI"); else strcat(ini_path, ".INI");
    
    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES) {
        FILE* fp = fopen(ini_path, "w");
        if (fp) { fprintf(fp, "[Telnet]\nEnabled=1\nPort=23\nTimeout=20\nPlaintext=0\nMagic=\xA6\nPassword=admin\n\n[Client]\nUserId=1\nPassword=admin\nServers=127.0.0.1:23\nTables=table\n"); fclose(fp); } else UpdateStatusBar("Error: Cannot create INI file in the current directory.");
    }
    
    telnet_enabled = GetPrivateProfileIntA("Telnet", "Enabled", 0, ini_path); 
    telnet_plaintext = GetPrivateProfileIntA("Telnet", "Plaintext", 0, ini_path); 
    telnet_port = GetPrivateProfileIntA("Telnet", "Port", 23, ini_path); 
    telnet_timeout = GetPrivateProfileIntA("Telnet", "Timeout", 20, ini_path); 
    GetPrivateProfileStringA("Telnet", "Magic", "\xA6", telnet_magic, sizeof(telnet_magic), ini_path); 
    GetPrivateProfileStringA("Telnet", "Password", "admin", telnet_password, sizeof(telnet_password), ini_path); 
    
    client_userid = GetPrivateProfileIntA("Client", "UserId", 1, ini_path);
    GetPrivateProfileStringA("Client", "Password", "admin", client_password, sizeof(client_password), ini_path); 
    
    char servers_buf[1024] = {0}; GetPrivateProfileStringA("Client", "Servers", "", servers_buf, sizeof(servers_buf), ini_path);
    if (strlen(servers_buf) > 0) { char* s = strtok(servers_buf, ","); while(s) { char* trimmed = trim(s); wchar_t ws[256]; MultiByteToWideChar(CP_ACP, 0, trimmed, -1, ws, 256); SendMessage(hComboServer, CB_ADDSTRING, 0, (LPARAM)ws); s = strtok(NULL, ","); } SendMessage(hComboServer, CB_SETCURSEL, 0, 0); }
    
    if (telnet_enabled) CreateThread(NULL, 0, TelnetListenerThread, NULL, 0, NULL);
    
    char ini_tables[2048] = {0};
    GetPrivateProfileStringA("Client", "Tables", "", ini_tables, sizeof(ini_tables), ini_path);
    if (strlen(startup_tables) == 0 && strlen(ini_tables) > 0) {
        strcpy(startup_tables, ini_tables);
    }
    
    if (strlen(startup_tables) > 0) { 
        char* tbl = strtok(startup_tables, ","); 
        while (tbl) { 
            char* trimmed = trim(tbl); 
            if (strlen(trimmed) > 0) { 
                char tablename[MAX_COLUMN_NAME];
                char filename[MAX_PATH];
                char* dot = strrchr(trimmed, '.'); 
                if (dot && _stricmp(dot, ".csv") == 0) {
                    strcpy(filename, trimmed);
                    strncpy(tablename, trimmed, dot - trimmed); tablename[dot - trimmed] = '\0';
                } else {
                    sprintf(filename, "%s.csv", trimmed);
                    strcpy(tablename, trimmed);
                }
                LoadCSV(filename, tablename); 
            } 
            tbl = strtok(NULL, ","); 
        } 
    } else {
        LoadCSV("table.csv", "table");
    }
    
    if (strlen(startup_query) > 0) { SetWindowTextA(hEditQuery, startup_query); ExecuteQueryEx(startup_query, NULL, 0); }
}

/*
Ignoring data types and constraints makes the parser highly resilient to complex CREATE TABLE scripts, but it turns your engine into a schema-less key-value store (similar to SQLite's loose typing, but even looser). Because the backend storage format is just a CSV file, strict typing doesn't naturally exist anyway.

However, treating every column as a plain text string introduces several specific quirks and risks:

1. The "10 Apples" == "10 Oranges" Math Bug
Because the engine doesn't know if a column is an INTEGER or TEXT, the EvalExprJoin function guesses based on the first character:
int isNum = (isdigit((unsigned char)vL[0]) || vL[0]=='-') ...
If you run WHERE role > '1st Class', the engine sees the 1, assumes it is doing math, and uses atof() to convert it. It will evaluate "1st Class" as 1.0 and "10th Class" as 10.0. This is usually helpful, but it means "10 Apples" == "10 Oranges" evaluates to TRUE because both convert to 10.0.

2. No Referential Integrity (Orphaned Data)
By skipping FOREIGN KEY constraints, the engine will happily let you DELETE FROM sites WHERE site_id = 'SITE01'. It will not warn you, nor will it cascade the deletion to the staff table. You will be left with ghost employees assigned to a site that no longer exists.

3. Primary Key & Unique Duplication
Skipping PRIMARY KEY("username") means the engine performs no duplicate-checking during an INSERT. You could accidentally insert five users all with the username ewright, and the engine will blindly append them to the CSV.

4. Missing Default Values
Your CREATE TABLE script specifies "active" INTEGER NOT NULL DEFAULT 1. Because the parser throws this away, if you execute an INSERT statement that omits the active column, the engine won't inject the 1. It will simply leave the cell as a blank \0 string, violating the NOT NULL intention.

5. Telephone Number Truncation Risks
If you ever execute a math operation or sorting function on the phone column (e.g., 055-1234), the engine may treat it as a number and strip the leading zero during the comparison, which can cause unexpected JOIN or WHERE matches.

For a lightweight CSV mutator, ignoring types is the most pragmatic design choice—saving you from writing thousands of lines of strict type-casting logic. You just have to trust that the client sending the queries is validating its own data first.
*/