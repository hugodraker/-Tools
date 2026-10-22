// https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/model.safetensors
// https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/main/vocab.txt
// gcc ai01.c -o ai01.exe -mwindows -lgdi32 -lm
// 2026 Hugo Released into the Public Domain

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_MSGS 500
#define ID_EDIT 101

// --- UI Data ---
typedef struct { char text[1024]; int is_user; } ChatMsg;
ChatMsg chat_log[MAX_MSGS];
int msg_count = 0; int scroll_y = 0;
HWND hMainWindow; HWND hEdit; WNDPROC OldEditProc;

void add_message(const char* text, int is_user) {
    if (msg_count < MAX_MSGS) {
        strncpy(chat_log[msg_count].text, text, 1023);
        chat_log[msg_count].is_user = is_user;
        msg_count++;
    }
    scroll_y = -999999; 
}

// --- Deep Learning: all-MiniLM-L6-v2 Architecture ---
#define HIDDEN_DIM 384
#define INTERMEDIATE_DIM 1536
#define NUM_HEADS 12
#define HEAD_DIM 32
#define NUM_LAYERS 6
#define MAX_SEQ_LEN 128
#define VOCAB_SIZE 30522

float *tok_emb, *pos_emb, *typ_emb, *emb_ln_w, *emb_ln_b;
char vocab[VOCAB_SIZE][100];

typedef struct {
    float *q_w, *q_b, *k_w, *k_b, *v_w, *v_b;
    float *o_w, *o_b, *ln1_w, *ln1_b;
    float *ff1_w, *ff1_b, *ff2_w, *ff2_b, *ln2_w, *ln2_b;
} TransformerLayer;

TransformerLayer layers[NUM_LAYERS];

// --- Native SafeTensors Parser ---
char* safe_tensors_data = NULL;
char* json_header = NULL;

float* get_tensor(const char* name) {
    char search_str[256];
    sprintf(search_str, "\"%s\"", name);
    
    char* pos = strstr(json_header, search_str);
    if (!pos) {
        // Fallback: Some models prefix with "bert." in the safetensors file
        sprintf(search_str, "\"bert.%s\"", name);
        pos = strstr(json_header, search_str);
        if (!pos) return NULL;
    }
    
    char* offset_pos = strstr(pos, "\"data_offsets\"");
    if (!offset_pos) return NULL;
    
    char* array_start = strchr(offset_pos, '[');
    if (!array_start) return NULL;
    
    long long start_offset;
    sscanf(array_start + 1, "%lld", &start_offset);
    
    // The offset is in bytes, so we add it to a char* pointer before casting to float*
    return (float*)(safe_tensors_data + start_offset);
}

int load_transformer_model() {
    // 1. Load the WordPiece Vocabulary
    FILE* f = fopen("vocab.txt", "r");
    if (f) {
        char line[128];
        for (int i = 0; i < VOCAB_SIZE; i++) {
            if (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                strcpy(vocab[i], line);
            }
        }
        fclose(f);
    } else {
        return 0; // Failed to load vocab
    }

    // 2. Memory-Map the SafeTensors File directly from Disk
    HANDLE hFile = CreateFile("model.safetensors", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;
    
    HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) return 0;
    
    char* file_map = (char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!file_map) return 0;
    
    // First 8 bytes of a safetensors file dictate the JSON header length
    uint64_t header_size = *(uint64_t*)file_map;
    
    json_header = (char*)malloc(header_size + 1);
    memcpy(json_header, file_map + 8, header_size);
    json_header[header_size] = '\0';
    
    safe_tensors_data = file_map + 8 + header_size;
    
    // 3. Map Pointers dynamically using the parsed JSON offsets
    tok_emb = get_tensor("embeddings.word_embeddings.weight");
    pos_emb = get_tensor("embeddings.position_embeddings.weight");
    typ_emb = get_tensor("embeddings.token_type_embeddings.weight");
    emb_ln_w = get_tensor("embeddings.LayerNorm.weight");
    emb_ln_b = get_tensor("embeddings.LayerNorm.bias");

    if (!tok_emb) return 0; // Validation check

    char key[256];
    for (int i = 0; i < NUM_LAYERS; i++) {
        sprintf(key, "encoder.layer.%d.attention.self.query.weight", i); layers[i].q_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.self.query.bias", i); layers[i].q_b = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.self.key.weight", i); layers[i].k_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.self.key.bias", i); layers[i].k_b = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.self.value.weight", i); layers[i].v_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.self.value.bias", i); layers[i].v_b = get_tensor(key);
        
        sprintf(key, "encoder.layer.%d.attention.output.dense.weight", i); layers[i].o_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.output.dense.bias", i); layers[i].o_b = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.output.LayerNorm.weight", i); layers[i].ln1_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.attention.output.LayerNorm.bias", i); layers[i].ln1_b = get_tensor(key);
        
        sprintf(key, "encoder.layer.%d.intermediate.dense.weight", i); layers[i].ff1_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.intermediate.dense.bias", i); layers[i].ff1_b = get_tensor(key);
        sprintf(key, "encoder.layer.%d.output.dense.weight", i); layers[i].ff2_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.output.dense.bias", i); layers[i].ff2_b = get_tensor(key);
        sprintf(key, "encoder.layer.%d.output.LayerNorm.weight", i); layers[i].ln2_w = get_tensor(key);
        sprintf(key, "encoder.layer.%d.output.LayerNorm.bias", i); layers[i].ln2_b = get_tensor(key);
    }
    
    return 1; // Success
}

// WordPiece Subword Tokenization (Greedy Longest Match)
int tokenize(const char* text, int* tokens) {
    char clean[1024]; strcpy(clean, text);
    for(char* p = clean; *p; p++) *p = tolower(*p);
    
    int count = 0;
    tokens[count++] = 101; // [CLS]
    
    char* word = strtok(clean, " ,.?!'\"");
    while (word != NULL && count < MAX_SEQ_LEN - 1) {
        int wlen = strlen(word);
        int start = 0;
        while (start < wlen) {
            int end = wlen, found = 0;
            while (end > start) {
                char sub[100];
                if (start == 0) {
                    strncpy(sub, word + start, end - start); sub[end - start] = '\0';
                } else {
                    strcpy(sub, "##"); strncat(sub, word + start, end - start);
                }
                for (int i = 0; i < VOCAB_SIZE; i++) {
                    if (strcmp(vocab[i], sub) == 0) {
                        tokens[count++] = i; start = end; found = 1; break;
                    }
                }
                if (found) break;
                end--;
            }
            if (!found) { tokens[count++] = 100; break; } // [UNK]
        }
        word = strtok(NULL, " ,.?!'\"");
    }
    tokens[count++] = 102; // [SEP]
    return count;
}

// --- Transformer Math Operations ---
float gelu(float x) {
    return 0.5f * x * (1.0f + tanhf(0.7978845608f * (x + 0.044715f * x * x * x)));
}

void layer_norm(float* out, float* in, float* w, float* b, int seq_len, int dim) {
    for (int i = 0; i < seq_len; i++) {
        float mean = 0, var = 0;
        for (int j = 0; j < dim; j++) mean += in[i*dim + j];
        mean /= dim;
        for (int j = 0; j < dim; j++) var += (in[i*dim + j] - mean) * (in[i*dim + j] - mean);
        float stddev = sqrtf((var / dim) + 1e-12f);
        for (int j = 0; j < dim; j++) out[i*dim + j] = ((in[i*dim + j] - mean) / stddev) * w[j] + b[j];
    }
}

void matmul(float* out, float* in, float* weight, float* bias, int seq_len, int in_dim, int out_dim) {
    for (int i = 0; i < seq_len; i++) {
        for (int j = 0; j < out_dim; j++) {
            float sum = bias[j];
            for (int k = 0; k < in_dim; k++) sum += in[i*in_dim + k] * weight[j*in_dim + k];
            out[i*out_dim + j] = sum;
        }
    }
}

void attention(float* out, float* q, float* k, float* v, int seq_len) {
    for (int h = 0; h < NUM_HEADS; h++) {
        for (int i = 0; i < seq_len; i++) {
            float scores[MAX_SEQ_LEN]; float max_score = -1e9f;
            for (int j = 0; j < seq_len; j++) {
                float sum = 0;
                for (int d = 0; d < HEAD_DIM; d++) sum += q[i*HIDDEN_DIM + h*HEAD_DIM + d] * k[j*HIDDEN_DIM + h*HEAD_DIM + d];
                scores[j] = sum / sqrtf((float)HEAD_DIM);
                if (scores[j] > max_score) max_score = scores[j];
            }
            float exp_sum = 0;
            for (int j = 0; j < seq_len; j++) {
                scores[j] = expf(scores[j] - max_score); exp_sum += scores[j];
            }
            for (int d = 0; d < HEAD_DIM; d++) {
                float sum = 0;
                for (int j = 0; j < seq_len; j++) sum += (scores[j] / exp_sum) * v[j*HIDDEN_DIM + h*HEAD_DIM + d];
                out[i*HIDDEN_DIM + h*HEAD_DIM + d] = sum;
            }
        }
    }
}

// Full Forward Pass
void get_sentence_embedding(const char* text, float* embedding) {
    int tokens[MAX_SEQ_LEN];
    int seq_len = tokenize(text, tokens);
    
    float *x = calloc(seq_len * HIDDEN_DIM, sizeof(float));
    float *temp = calloc(seq_len * HIDDEN_DIM, sizeof(float));
    float *q = calloc(seq_len * HIDDEN_DIM, sizeof(float));
    float *k = calloc(seq_len * HIDDEN_DIM, sizeof(float));
    float *v = calloc(seq_len * HIDDEN_DIM, sizeof(float));
    float *ff1 = calloc(seq_len * INTERMEDIATE_DIM, sizeof(float));
    
    // 1. Embeddings Lookups
    for (int i = 0; i < seq_len; i++) {
        for (int d = 0; d < HIDDEN_DIM; d++) {
            x[i*HIDDEN_DIM + d] = tok_emb[tokens[i]*HIDDEN_DIM + d] + pos_emb[i*HIDDEN_DIM + d] + typ_emb[d];
        }
    }
    layer_norm(x, x, emb_ln_w, emb_ln_b, seq_len, HIDDEN_DIM);
    
    // 2. Transformer Layers
    for (int l = 0; l < NUM_LAYERS; l++) {
        matmul(q, x, layers[l].q_w, layers[l].q_b, seq_len, HIDDEN_DIM, HIDDEN_DIM);
        matmul(k, x, layers[l].k_w, layers[l].k_b, seq_len, HIDDEN_DIM, HIDDEN_DIM);
        matmul(v, x, layers[l].v_w, layers[l].v_b, seq_len, HIDDEN_DIM, HIDDEN_DIM);
        
        attention(temp, q, k, v, seq_len);
        matmul(q, temp, layers[l].o_w, layers[l].o_b, seq_len, HIDDEN_DIM, HIDDEN_DIM);
        
        for (int i = 0; i < seq_len * HIDDEN_DIM; i++) x[i] += q[i];
        layer_norm(x, x, layers[l].ln1_w, layers[l].ln1_b, seq_len, HIDDEN_DIM);
        
        matmul(ff1, x, layers[l].ff1_w, layers[l].ff1_b, seq_len, HIDDEN_DIM, INTERMEDIATE_DIM);
        for (int i = 0; i < seq_len * INTERMEDIATE_DIM; i++) ff1[i] = gelu(ff1[i]);
        matmul(temp, ff1, layers[l].ff2_w, layers[l].ff2_b, seq_len, INTERMEDIATE_DIM, HIDDEN_DIM);
        
        for (int i = 0; i < seq_len * HIDDEN_DIM; i++) x[i] += temp[i];
        layer_norm(x, x, layers[l].ln2_w, layers[l].ln2_b, seq_len, HIDDEN_DIM);
    }
    
    // 3. Mean Pooling
    for (int d = 0; d < HIDDEN_DIM; d++) {
        float sum = 0;
        for (int i = 0; i < seq_len; i++) sum += x[i*HIDDEN_DIM + d];
        embedding[d] = sum / seq_len;
    }
    
    free(x); free(temp); free(q); free(k); free(v); free(ff1);
}

// --- Semantic Intents Engine ---
typedef struct { const char *training_data; const char *response; float vector[HIDDEN_DIM]; } Intent;

Intent corpus[] = {
    {"Who are you, and what is your name?", "Raymond. Raymond Babbitt."},
    {"How are you feeling today?", "I am sitting in a chair. Processing data."},
    {"Where do you buy your clothes and underwear?", "I get my underwear at K-Mart. Cincinnati."},
    {"Are you a good driver?", "I'm an excellent driver. Yeah."},
    {"What television shows do you watch?", "Time for Wapner. 15 minutes to Wapner."},
    {"Is flying on an airplane safe?", "Qantas. Qantas never crashed."},
    {"What is the weather like outside?", "It's raining. Definitely raining."},
    {"Tell me a funny joke.", "I don't know any jokes."},
    {"What do you eat for breakfast?", "Maple syrup is supposed to be on the table before the pancakes."},
    {"When is it time to go to bed and sleep?", "Lights out at 11. Definitely 11."},
    {NULL, NULL, {0}}
};

void train_nlp_model() {
    for (int i = 0; corpus[i].training_data != NULL; i++) {
        get_sentence_embedding(corpus[i].training_data, corpus[i].vector);
    }
}

int handle_semantic_intent(const char *input, char *response) {
    float user_vector[HIDDEN_DIM];
    get_sentence_embedding(input, user_vector);
    
    float best_score = 0.0f;
    int best_match_idx = -1;

    for (int i = 0; corpus[i].training_data != NULL; i++) {
        float dot = 0, mag_user = 0, mag_corpus = 0;
        for (int v = 0; v < HIDDEN_DIM; v++) {
            dot += user_vector[v] * corpus[i].vector[v];
            mag_user += user_vector[v] * user_vector[v];
            mag_corpus += corpus[i].vector[v] * corpus[i].vector[v];
        }
        
        float similarity = dot / (sqrtf(mag_user) * sqrtf(mag_corpus));
        if (similarity > best_score) {
            best_score = similarity;
            best_match_idx = i;
        }
    }
    
    if (best_score > 0.45f && best_match_idx != -1) {
        strcpy(response, corpus[best_match_idx].response);
        return 1;
    }
    return 0;
}

// --- Main Chat Processor ---
void process_eliza(const char* input) {
    char response[1024];
    
    if (handle_semantic_intent(input, response)) {
        add_message(response, 0);
        return;
    }
    
    const char *fallbacks[] = { "Yeah. Definitely.", "Uh-oh.", "I get my underwear at K-Mart.", "Definitely. Yeah." };
    static int f_idx = 0;
    add_message(fallbacks[f_idx], 0);
    f_idx = (f_idx + 1) % 4;
}

// --- Win32 GUI Subclass & Procedures ---
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        char buf[1024]; GetWindowText(hwnd, buf, 1024);
        if (strlen(buf) > 0) {
            add_message(buf, 1); SetWindowText(hwnd, "");
            process_eliza(buf); InvalidateRect(hMainWindow, NULL, TRUE); 
        }
        return 0; 
    }
    return CallWindowProc(OldEditProc, hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hEdit = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 0, 100, 30, hwnd, (HMENU)ID_EDIT, GetModuleHandle(NULL), NULL);
            HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN, "Consolas");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            OldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            
            if (load_transformer_model()) {
                train_nlp_model(); 
                add_message("Raymond is here. SafeTensors engine loaded natively.", 0);
            } else {
                add_message("Error: Could not load model.safetensors or vocab.txt. Check files.", 0);
            }
            return 0;
        }
        case WM_SIZE: { MoveWindow(hEdit, 10, HIWORD(lParam) - 40, LOWORD(lParam) - 20, 30, TRUE); return 0; }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect; GetClientRect(hwnd, &clientRect);
            int win_width = clientRect.right - clientRect.left;
            HDC hdcMem = CreateCompatibleDC(hdc); HBITMAP hbmMem = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom); HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
            HBRUSH bgBrush = CreateSolidBrush(RGB(240, 240, 240)); FillRect(hdcMem, &clientRect, bgBrush); DeleteObject(bgBrush);
            HBRUSH brushUser = CreateSolidBrush(RGB(200, 200, 200)); HBRUSH brushEliza = CreateSolidBrush(RGB(255, 255, 255));
            HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN, "Consolas");
            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont); SetBkMode(hdcMem, TRANSPARENT);
            
            int y_cursor = 20, bubble_padding = 10, max_bubble_w = (win_width * 7) / 10;
            int total_height = 20;
            for (int i = 0; i < msg_count; i++) {
                RECT calcRect = {0, 0, max_bubble_w, 0}; DrawText(hdcMem, chat_log[i].text, -1, &calcRect, DT_CALCRECT | DT_WORDBREAK);
                total_height += (calcRect.bottom - calcRect.top) + (bubble_padding * 2) + 10;
            }
            int visible_space = clientRect.bottom - 50; 
            if (scroll_y < -(total_height - visible_space) && total_height > visible_space) scroll_y = -(total_height - visible_space);
            if (total_height <= visible_space || scroll_y > 0) scroll_y = 0;
            y_cursor += scroll_y; 

            for (int i = 0; i < msg_count; i++) {
                RECT textRect = {0, 0, max_bubble_w, 0}; DrawText(hdcMem, chat_log[i].text, -1, &textRect, DT_CALCRECT | DT_WORDBREAK);
                int b_width = textRect.right + (bubble_padding * 2), b_height = textRect.bottom + (bubble_padding * 2);
                RECT bubbleRect = chat_log[i].is_user ? (RECT){win_width - b_width - 20, y_cursor, win_width - 20, y_cursor + b_height} : (RECT){20, y_cursor, 20 + b_width, y_cursor + b_height};
                
                SelectObject(hdcMem, GetStockObject(BLACK_PEN)); 
                SelectObject(hdcMem, chat_log[i].is_user ? brushUser : brushEliza);
                Rectangle(hdcMem, bubbleRect.left, bubbleRect.top, bubbleRect.right, bubbleRect.bottom); 
                
                SetTextColor(hdcMem, RGB(0, 0, 0));
                RECT drawRect = {bubbleRect.left + bubble_padding, bubbleRect.top + bubble_padding, bubbleRect.right - bubble_padding, bubbleRect.bottom - bubble_padding};
                DrawText(hdcMem, chat_log[i].text, -1, &drawRect, DT_WORDBREAK);
                y_cursor += b_height + 10;
            }
            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hbmOld); SelectObject(hdcMem, hOldFont); DeleteObject(hbmMem); DeleteDC(hdcMem); DeleteObject(brushUser); DeleteObject(brushEliza); DeleteObject(hFont);
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "ElizaChatClass"; WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc; wc.hInstance = hInstance; wc.lpszClassName = CLASS_NAME; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClass(&wc);
    hMainWindow = CreateWindowEx(0, CLASS_NAME, "Raymond (Zero-Dependency SafeTensors)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 450, 600, NULL, NULL, hInstance, NULL);
    if (hMainWindow == NULL) return 0; ShowWindow(hMainWindow, nCmdShow);
    MSG msg = {0}; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}