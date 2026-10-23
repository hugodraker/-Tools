/* ============================================================================
 * AI LLAMA test 01
 *
 * COMPILATION INSTRUCTIONS (GCC/MinGW):
 *   gcc "ai02.c" -Os -s -o "ai02.exe" -mwindows -lgdi32 -lshell32 -lm
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

#define MAX_MSGS 500
#define ID_EDIT 101
#define ID_COMBO 102

#define MSG_USER    0
#define MSG_AI      1
#define MSG_SYS_OK  2
#define MSG_SYS_ERR 3

#define WM_UPDATE_CHAT (WM_APP + 1)

// --- Dynamic LLaMA Architecture Variables ---
int dim = 0; int hidden_dim = 0; int n_layers = 0;
int n_heads = 0; int n_kv_heads = 0; int vocab_size = 0;
int head_dim = 0; int kv_dim = 0;
float rope_theta = 10000.0f;

int use_gpu = 1;
volatile int is_generating = 0;
char last_error_msg[1024] = ""; 

// --- Memory Tracker for Load-Time Dequantization ---
void* allocated_ptrs[2000];
int ptr_count = 0;

void* track_mem(void* ptr) {
    if (ptr && ptr_count < 2000) allocated_ptrs[ptr_count++] = ptr;
    return ptr;
}

void cleanup_memory() {
    for (int i = 0; i < ptr_count; i++) free(allocated_ptrs[i]);
    ptr_count = 0;
}

// --- Tokenizer Data ---
typedef struct { char* str; int id; } Token;
Token* vocab_map = NULL;
int current_vocab_size = 0;

// --- Neural Network Weights ---
float *tok_emb, *output_norm_w, *lm_head_w;

typedef struct {
    float *wq, *wk, *wv, *wo;
    float *ffn_gate, *ffn_down, *ffn_up;
    float *attn_norm, *ffn_norm;
} LlamaLayer;

LlamaLayer *layers = NULL;

// --- Global UI Data ---
typedef struct { char text[4096]; int msg_type; RECT hit_box; } ChatMsg;
ChatMsg chat_log[MAX_MSGS];
int msg_count = 0; int scroll_y = 0;
HWND hMainWindow; HWND hEdit; HWND hCombo; WNDPROC OldEditProc;
char ini_path[MAX_PATH]; char current_model_dir[MAX_PATH];
int total_height = 0; int visible_space = 0;

void add_message(const char* text, int msg_type) {
    if (msg_count < MAX_MSGS) {
        strncpy(chat_log[msg_count].text, text, 4095); 
        chat_log[msg_count].text[4095] = '\0';
        chat_log[msg_count].msg_type = msg_type;
        SetRect(&chat_log[msg_count].hit_box, 0, 0, 0, 0); msg_count++;
    }
    scroll_y = -999999; PostMessage(hMainWindow, WM_UPDATE_CHAT, 0, 0); 
}

void update_last_message(const char* append_text) {
    if (msg_count > 0) {
        strncat(chat_log[msg_count - 1].text, append_text, 4095 - strlen(chat_log[msg_count - 1].text));
        PostMessage(hMainWindow, WM_UPDATE_CHAT, 0, 0);
    }
}

// --- Native JSON & SafeTensors Parsers ---
int get_json_int(const char* json, const char* key) {
    char search[256]; sprintf(search, "\"%s\"", key);
    char* pos = strstr(json, search);
    if (!pos) return 0;
    pos += strlen(search);
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' || *pos == ':')) pos++;
    return atoi(pos);
}

int load_config_json(const char* dir_path) {
    char path[MAX_PATH]; sprintf(path, "%s\\config.json", dir_path);
    FILE* f = fopen(path, "rb");
    if (!f) { sprintf(last_error_msg, "File Missing: Could not find config.json in %s", dir_path); return 0; }
    
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    char* json = (char*)malloc(fsize + 1); fread(json, 1, fsize, f); json[fsize] = 0; fclose(f);
    
    dim = get_json_int(json, "hidden_size");
    hidden_dim = get_json_int(json, "intermediate_size");
    n_layers = get_json_int(json, "num_hidden_layers");
    n_heads = get_json_int(json, "num_attention_heads");
    n_kv_heads = get_json_int(json, "num_key_value_heads");
    if (n_kv_heads == 0) n_kv_heads = n_heads; 
    vocab_size = get_json_int(json, "vocab_size");
    
    int rt = get_json_int(json, "rope_theta");
    if (rt > 0) rope_theta = (float)rt;
    
    if (n_heads > 0) { head_dim = dim / n_heads; kv_dim = n_kv_heads * head_dim; }
    free(json);
    
    if (dim == 0 || n_layers == 0 || n_heads == 0) {
        sprintf(last_error_msg, "Config Error: Parsed invalid values (dim=%d, layers=%d, heads=%d). Ensure config.json is not corrupted.", dim, n_layers, n_heads);
        return 0;
    }
    return 1;
}

int load_bpe_tokenizer(const char* dir_path) {
    if (vocab_map) {
        for (int i = 0; i < current_vocab_size; i++) { if (vocab_map[i].str) free(vocab_map[i].str); }
        free(vocab_map); vocab_map = NULL;
    }
    
    char v_path[MAX_PATH]; sprintf(v_path, "%s\\vocab.json", dir_path);
    FILE* f_voc = fopen(v_path, "rb");
    if (!f_voc) { sprintf(last_error_msg, "File Missing: Could not find vocab.json in %s", dir_path); return 0; }
    
    current_vocab_size = vocab_size;
    vocab_map = (Token*)calloc(vocab_size, sizeof(Token));
    fseek(f_voc, 0, SEEK_END); long fsize = ftell(f_voc); fseek(f_voc, 0, SEEK_SET);
    char* json = (char*)malloc(fsize + 1); fread(json, 1, fsize, f_voc); json[fsize] = 0; fclose(f_voc);
    
    char* p = json;
    while ((p = strchr(p, '"')) != NULL) {
        p++; char* end_quote = strchr(p, '"');
        if (!end_quote) break;
        *end_quote = '\0';
        char* colon = strchr(end_quote + 1, ':');
        if (colon) {
            int id = atoi(colon + 1);
            if (id >= 0 && id < vocab_size) { 
                if (vocab_map[id].str) free(vocab_map[id].str);
                vocab_map[id].str = strdup(p); 
                vocab_map[id].id = id; 
            }
        }
        p = colon ? colon + 1 : end_quote + 1;
    }
    free(json); return 1;
}

HANDLE hFile_ptr = INVALID_HANDLE_VALUE; HANDLE hMap_ptr = NULL;
char* file_map_ptr = NULL; char* safe_tensors_data = NULL; char* json_header = NULL;

float fp16_to_fp32(uint16_t h) {
    uint32_t sign = (h >> 15) & 1;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    if (exp == 0 || exp == 31) return 0.0f; 
    exp = exp + (127 - 15);
    uint32_t f = (sign << 31) | (exp << 23) | (mant << 13);
    return *(float*)&f;
}

// --- Universal Type-Agnostic Tensor Fetcher ---
float* fetch_tensor(const char* name, int elements) {
    if (!json_header) return NULL;
    char search_str[256]; sprintf(search_str, "\"%s\"", name);
    char* pos = strstr(json_header, search_str);
    
    if (!pos) {
        char temp[256]; strcpy(temp, name);
        char* ext = strstr(temp, ".weight");
        if (ext) { *ext = '\0'; sprintf(search_str, "\"%s\"", temp); pos = strstr(json_header, search_str); }
    }
    if (!pos && strncmp(name, "model.", 6) == 0) {
        sprintf(search_str, "\"%s\"", name + 6); pos = strstr(json_header, search_str);
    }
    if (!pos) return NULL;
    
    char* off_pos = strstr(pos, "\"data_offsets\"");
    if (!off_pos) return NULL;
    long long start; sscanf(strchr(off_pos, '[') + 1, "%lld", &start);
    
    int is_f16 = strstr(pos, "\"F16\"") != NULL;
    int is_bf16 = strstr(pos, "\"BF16\"") != NULL;
    
    if (is_f16) {
        float* out = (float*)track_mem(malloc(elements * sizeof(float)));
        if (!out) return NULL;
        uint16_t* raw = (uint16_t*)(safe_tensors_data + start);
        for(int i = 0; i < elements; i++) out[i] = fp16_to_fp32(raw[i]);
        return out;
    } else if (is_bf16) {
        float* out = (float*)track_mem(malloc(elements * sizeof(float)));
        if (!out) return NULL;
        uint16_t* raw = (uint16_t*)(safe_tensors_data + start);
        for(int i = 0; i < elements; i++) {
            uint32_t val = (uint32_t)raw[i] << 16;
            out[i] = *(float*)&val;
        }
        return out;
    }
    return (float*)(safe_tensors_data + start);
}

#define FETCH_TENS(ptr, name, elems) do { \
    ptr = fetch_tensor(name, elems); \
    if (!ptr) { \
        sprintf(last_error_msg, "SafeTensors Error: Missing tensor key '%s' or system out of memory.", name); \
        return 0; \
    } \
} while(0)

int load_llama_model(const char* target_path) {
    if (file_map_ptr) { UnmapViewOfFile(file_map_ptr); CloseHandle(hMap_ptr); CloseHandle(hFile_ptr); free(json_header); file_map_ptr = NULL; json_header = NULL; }
    cleanup_memory();
    
    char dir_path[MAX_PATH]; char st_path[MAX_PATH] = "";
    DWORD attr = GetFileAttributesA(target_path);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        strcpy(dir_path, target_path);
        char* last_slash = strrchr(dir_path, '\\');
        if (last_slash) *last_slash = '\0';
        if (strstr(target_path, ".safetensors")) strcpy(st_path, target_path);
    } else {
        strcpy(dir_path, target_path);
    }

    if (!load_config_json(dir_path)) return 0; 
    if (!load_bpe_tokenizer(dir_path)) return 0; 
    layers = (LlamaLayer*)realloc(layers, n_layers * sizeof(LlamaLayer));

    if (strlen(st_path) == 0) {
        char search_pattern[MAX_PATH]; sprintf(search_pattern, "%s\\*.safetensors", dir_path);
        WIN32_FIND_DATAA findFileData; HANDLE hFind = FindFirstFileA(search_pattern, &findFileData);
        
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
    if (hFile_ptr == INVALID_HANDLE_VALUE) { strcpy(last_error_msg, "OS Error: Could not lock or open the .safetensors file."); return 0; }
    
    LARGE_INTEGER fsize; GetFileSizeEx(hFile_ptr, &fsize);
    if (fsize.QuadPart < 1024 * 1024) { 
        sprintf(last_error_msg, "Download Error: File is only %lld bytes! You downloaded a Git LFS text pointer instead of the real weights.", fsize.QuadPart);
        CloseHandle(hFile_ptr); hFile_ptr = INVALID_HANDLE_VALUE; return 0;
    }
    
    hMap_ptr = CreateFileMapping(hFile_ptr, NULL, PAGE_READONLY, 0, 0, NULL);
    file_map_ptr = (char*)MapViewOfFile(hMap_ptr, FILE_MAP_READ, 0, 0, 0);
    
    uint64_t header_size = *(uint64_t*)file_map_ptr;
    if (header_size > 100000000 || header_size < 2) {
        sprintf(last_error_msg, "Corrupt File: The SafeTensors header size (%llu) is invalid.", header_size);
        return 0;
    }
    
    json_header = (char*)malloc(header_size + 1);
    memcpy(json_header, file_map_ptr + 8, header_size); json_header[header_size] = '\0';
    safe_tensors_data = file_map_ptr + 8 + header_size;
    
    if (strstr(json_header, "\"q_invperm\"")) {
        strcpy(last_error_msg, "Format Error: Detected Apple MLX 'q_invperm'. MLX bit-packing cannot be natively descrambled on Windows.");
        return 0;
    }
    
    FETCH_TENS(tok_emb, "model.embed_tokens.weight", vocab_size * dim);
    FETCH_TENS(output_norm_w, "model.norm.weight", dim);
    lm_head_w = fetch_tensor("lm_head.weight", vocab_size * dim);
    if (!lm_head_w) lm_head_w = tok_emb; 
    
    char key[256];
    for (int i = 0; i < n_layers; i++) {
        sprintf(key, "model.layers.%d.self_attn.q_proj.weight", i); FETCH_TENS(layers[i].wq, key, dim * dim);
        sprintf(key, "model.layers.%d.self_attn.k_proj.weight", i); FETCH_TENS(layers[i].wk, key, kv_dim * dim);
        sprintf(key, "model.layers.%d.self_attn.v_proj.weight", i); FETCH_TENS(layers[i].wv, key, kv_dim * dim);
        sprintf(key, "model.layers.%d.self_attn.o_proj.weight", i); FETCH_TENS(layers[i].wo, key, dim * dim);
        sprintf(key, "model.layers.%d.mlp.gate_proj.weight", i); FETCH_TENS(layers[i].ffn_gate, key, hidden_dim * dim);
        sprintf(key, "model.layers.%d.mlp.down_proj.weight", i); FETCH_TENS(layers[i].ffn_down, key, dim * hidden_dim);
        sprintf(key, "model.layers.%d.mlp.up_proj.weight", i); FETCH_TENS(layers[i].ffn_up, key, hidden_dim * dim);
        sprintf(key, "model.layers.%d.input_layernorm.weight", i); FETCH_TENS(layers[i].attn_norm, key, dim);
        sprintf(key, "model.layers.%d.post_attention_layernorm.weight", i); FETCH_TENS(layers[i].ffn_norm, key, dim);
    }
    return 1;
}

// --- LLaMA Core Math ---
void rmsnorm(float* o, float* x, float* weight, int size) {
    float ss = 0.0f;
    for (int j = 0; j < size; j++) ss += x[j] * x[j];
    ss /= size; ss += 1e-5f; ss = 1.0f / sqrtf(ss);
    for (int j = 0; j < size; j++) o[j] = weight[j] * (ss * x[j]);
}

void matmul(float* xout, float* x, float* w, int n, int d) {
    for (int i = 0; i < d; i++) {
        float val = 0.0f;
        for (int j = 0; j < n; j++) val += w[i * n + j] * x[j];
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

// --- ChatML Tokenization ---
int tokenize_greedy(const char* text, int* tokens) {
    int count = 0; 
    tokens[count++] = 0; 
    char buf[2048]; snprintf(buf, 2047, "<|im_start|> user %s <|im_end|> <|im_start|> assistant", text);
    char* word = strtok(buf, " \n\r\t");
    while (word && count < 200) {
        int best_id = -1; 
        for(int i = 0; i < vocab_size; i++) {
            if (vocab_map[i].str && strcmp(vocab_map[i].str, word) == 0) { best_id = i; break; }
        }
        if (best_id == -1) {
            char prefixed_g[256] = "Ġ"; strcat(prefixed_g, word);
            for(int i = 0; i < vocab_size; i++) { if (vocab_map[i].str && strcmp(vocab_map[i].str, prefixed_g) == 0) { best_id = i; break; } }
        }
        if (best_id == -1) {
            if(strcmp(word, "<|im_start|>") == 0) continue;
            if(strcmp(word, "user") == 0) { strcpy(word, "User:"); }
            if(strcmp(word, "assistant") == 0) { strcpy(word, "Assistant:"); }
        }
        if (best_id != -1) tokens[count++] = best_id;
        word = strtok(NULL, " \n\r\t");
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

void probe_gpu_hardware() {
    if (!use_gpu) return;
    HMODULE hOpenCL = LoadLibraryA("OpenCL.dll");
    HMODULE hVulkan = LoadLibraryA("vulkan-1.dll");
    char msg[512] = "Hardware Configuration:\nGPU Acceleration Requested (settings.ini)\n";
    if (hOpenCL) strcat(msg, "Detected: OpenCL Runtime.\n");
    if (hVulkan) strcat(msg, "Detected: Vulkan Runtime.\n");
    strcat(msg, "\nNotice: Raw GPU compute kernels uncompiled. Falling back to multi-threaded CPU matrix multiplication.");
    add_message(msg, MSG_SYS_ERR);
    if (hOpenCL) FreeLibrary(hOpenCL);
    if (hVulkan) FreeLibrary(hVulkan);
}

DWORD WINAPI GenerationThread(LPVOID lpParam) {
    char* prompt = (char*)lpParam;
    if (!json_header) { add_message("Error: No LLaMA model loaded.", MSG_SYS_ERR); free(prompt); is_generating = 0; return 0; }
    add_message("", MSG_AI); 
    
    int tokens[512] = {0}; 
    int seq_len = tokenize_greedy(prompt, tokens);
    
    float *x = calloc(dim, sizeof(float)), *xb = calloc(dim, sizeof(float));
    float *q = calloc(dim, sizeof(float)), *k = calloc(kv_dim, sizeof(float)), *v = calloc(kv_dim, sizeof(float));
    float *att = calloc(n_heads * 512, sizeof(float)), *out = calloc(dim, sizeof(float));
    float *h_gate = calloc(hidden_dim, sizeof(float)), *h_up = calloc(hidden_dim, sizeof(float)), *h_swi = calloc(hidden_dim, sizeof(float));
    float *key_cache = calloc(n_layers * 512 * kv_dim, sizeof(float)); 
    float *val_cache = calloc(n_layers * 512 * kv_dim, sizeof(float));

    if (!x || !xb || !q || !k || !v || !att || !out || !h_gate || !h_up || !h_swi || !key_cache || !val_cache) {
        add_message("Error: Out of Memory. The process lacks sufficient free RAM to allocate the KV Cache.", MSG_SYS_ERR);
        if(x) free(x); if(xb) free(xb); if(q) free(q); if(k) free(k); if(v) free(v); 
        if(att) free(att); if(out) free(out); if(h_gate) free(h_gate); if(h_up) free(h_up); 
        if(h_swi) free(h_swi); if(key_cache) free(key_cache); if(val_cache) free(val_cache);
        free(prompt); is_generating = 0; return 0;
    }

    for (int pos = 0; pos < seq_len + 50; pos++) { 
        if (pos >= 510) break; 
        int current_token = tokens[pos];
        if (current_token < 0 || current_token >= vocab_size) current_token = 0; 
        memcpy(x, &tok_emb[current_token * dim], dim * sizeof(float));

        for (int l = 0; l < n_layers; l++) {
            rmsnorm(xb, x, layers[l].attn_norm, dim);
            matmul(q, xb, layers[l].wq, dim, dim);
            matmul(k, xb, layers[l].wk, dim, kv_dim); 
            matmul(v, xb, layers[l].wv, dim, kv_dim);
            
            apply_rope(q, k, pos);
            
            int cache_offset = l * 512 * kv_dim + pos * kv_dim;
            memcpy(&key_cache[cache_offset], k, kv_dim * sizeof(float));
            memcpy(&val_cache[cache_offset], v, kv_dim * sizeof(float));

            for (int h = 0; h < n_heads; h++) {
                int q_off = h * head_dim;
                int kv_h = h / (n_heads / n_kv_heads); 
                
                float max_val = -1e9f;
                for (int t = 0; t <= pos; t++) {
                    float score = 0.0f;
                    int k_off = l * 512 * kv_dim + t * kv_dim + kv_h * head_dim;
                    for (int i = 0; i < head_dim; i++) score += q[q_off + i] * key_cache[k_off + i];
                    score /= sqrtf((float)head_dim);
                    att[h * 512 + t] = score;
                    if (score > max_val) max_val = score;
                }
                float exp_sum = 0.0f;
                for (int t = 0; t <= pos; t++) {
                    att[h * 512 + t] = expf(att[h * 512 + t] - max_val);
                    exp_sum += att[h * 512 + t];
                }
                for (int t = 0; t <= pos; t++) att[h * 512 + t] /= exp_sum;
                
                for (int i = 0; i < head_dim; i++) {
                    float val_sum = 0.0f;
                    for (int t = 0; t <= pos; t++) {
                        int v_off = l * 512 * kv_dim + t * kv_dim + kv_h * head_dim;
                        val_sum += att[h * 512 + t] * val_cache[v_off + i];
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
            int best_token = 0; float max_logit = -1e9f;
            for (int v_idx = 0; v_idx < vocab_size; v_idx++) {
                float logit = 0.0f;
                for (int i = 0; i < dim; i++) logit += xb[i] * lm_head_w[v_idx * dim + i];
                if (logit > max_logit) { max_logit = logit; best_token = v_idx; }
            }
            
            tokens[pos + 1] = best_token; 
            if (best_token >= 0 && best_token < vocab_size && vocab_map[best_token].str) {
                char clean_str[256];
                detokenize_bpe(vocab_map[best_token].str, clean_str);
                if (strstr(clean_str, "<|im_end|>") != NULL) break;
                update_last_message(clean_str);
            }
            
            if (best_token == 2 || best_token == 0) break; 
        }
    }
    
    free(x); free(xb); free(q); free(k); free(v); free(att); free(out); free(h_gate); free(h_up); free(h_swi); free(key_cache); free(val_cache);
    free(prompt); is_generating = 0; 
    return 0;
}

// --- Initialization & Config Parsing ---
void init_ini_and_combo(HWND hwnd) {
    GetModuleFileNameA(NULL, ini_path, MAX_PATH);
    char *last_slash = strrchr(ini_path, '\\');
    if (last_slash) *(last_slash + 1) = '\0';
    strcat(ini_path, "settings.ini");

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA("Hardware", "UseGPU", "1", ini_path);
        WritePrivateProfileStringA("Models", "List", "C:\\", ini_path);
        WritePrivateProfileStringA("Settings", "LastUsed", "C:\\", ini_path);
    }
    
    use_gpu = GetPrivateProfileIntA("Hardware", "UseGPU", 1, ini_path);

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

// --- Windows GUI Loop ---
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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            init_ini_and_combo(hwnd);
            hEdit = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 10, 100, 30, hwnd, (HMENU)ID_EDIT, GetModuleHandle(NULL), NULL);
            HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            OldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            
            if (load_llama_model(current_model_dir)) {
                msg_count = 0; 
                char msg[512]; sprintf(msg, "Successfully Loaded LLaMA!\nPath: %s\nLayers: %d\nDimensions: %d\nTokens: %d", current_model_dir, n_layers, dim, vocab_size);
                add_message(msg, MSG_SYS_OK); probe_gpu_hardware();
            } else {
                add_message("Drag and drop ANY file or folder from your downloaded LLaMA repository to load the model.", MSG_SYS_OK);
            }
            return 0;
        }
        
        case WM_UPDATE_CHAT: { InvalidateRect(hwnd, NULL, FALSE); return 0; }
        
        case WM_DROPFILES: {
            if (is_generating) { DragFinish((HDROP)wParam); return 0; }
            HDROP hDrop = (HDROP)wParam; char filepath[MAX_PATH];
            DragQueryFileA(hDrop, 0, filepath, MAX_PATH); DragFinish(hDrop);
            
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
                char msg[512]; sprintf(msg, "Successfully Mapped LLaMA Architecture!\nPath: %s\nLayers: %d\nDimensions: %d\nTokens: %d", dir_path, n_layers, dim, vocab_size);
                add_message(msg, MSG_SYS_OK); probe_gpu_hardware();
            } else {
                add_message(last_error_msg, MSG_SYS_ERR);
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
                        char msg[512]; sprintf(msg, "Switched LLaMA Architecture:\n%s\nLayers: %d", current_model_dir, n_layers);
                        add_message(msg, MSG_SYS_OK); probe_gpu_hardware();
                    } else { add_message(last_error_msg, MSG_SYS_ERR); }
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
            for (int i = 0; i < msg_count; i++) {
                if (PtInRect(&chat_log[i].hit_box, pt)) {
                    if (OpenClipboard(hwnd)) {
                        EmptyClipboard();
                        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, strlen(chat_log[i].text) + 1);
                        if (hg) {
                            memcpy(GlobalLock(hg), chat_log[i].text, strlen(chat_log[i].text) + 1);
                            GlobalUnlock(hg); SetClipboardData(CF_TEXT, hg);
                        }
                        CloseClipboard();
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_SIZE: { 
            int width = LOWORD(lParam), height = HIWORD(lParam);
            MoveWindow(hCombo, 10, height - 40, 200, 300, TRUE);
            MoveWindow(hEdit, 220, height - 40, width - 230, 30, TRUE); 
            if (visible_space > 0) {
                SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS, 0, total_height, visible_space, -scroll_y, 0 };
                SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            }
            InvalidateRect(hwnd, NULL, TRUE); return 0; 
        }

        case WM_VSCROLL: {
            int action = LOWORD(wParam);
            if (action == SB_LINEUP) scroll_y += 40;
            else if (action == SB_LINEDOWN) scroll_y -= 40;
            else if (action == SB_PAGEUP) scroll_y += visible_space;
            else if (action == SB_PAGEDOWN) scroll_y -= visible_space;
            else if (action == SB_THUMBTRACK) {
                SCROLLINFO si = { sizeof(si), SIF_TRACKPOS }; GetScrollInfo(hwnd, SB_VERT, &si); scroll_y = -si.nTrackPos;
            }
            if (scroll_y > 0) scroll_y = 0;
            if (scroll_y < -(total_height - visible_space) && total_height > visible_space) scroll_y = -(total_height - visible_space);
            InvalidateRect(hwnd, NULL, TRUE); return 0;
        }

        case WM_MOUSEWHEEL: { 
            scroll_y += (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 40 : -40; 
            if (scroll_y > 0) scroll_y = 0; 
            if (scroll_y < -(total_height - visible_space) && total_height > visible_space) scroll_y = -(total_height - visible_space);
            InvalidateRect(hwnd, NULL, TRUE); return 0; 
        }

        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect; GetClientRect(hwnd, &clientRect);
            int win_width = clientRect.right - clientRect.left;
            
            HDC hdcMem = CreateCompatibleDC(hdc); 
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom); 
            HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
            
            HBRUSH bgBrush = CreateSolidBrush(RGB(242, 242, 247)); FillRect(hdcMem, &clientRect, bgBrush);
            HBRUSH brushUser = CreateSolidBrush(RGB(10, 132, 255));
            HBRUSH brushAI = CreateSolidBrush(RGB(229, 229, 234));
            HBRUSH brushSysOK = CreateSolidBrush(RGB(52, 199, 89));
            HBRUSH brushSysErr = CreateSolidBrush(RGB(255, 59, 48));
            
            HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont); SetBkMode(hdcMem, TRANSPARENT);
            
            int y_cursor = 20, bubble_padding = 12, max_bubble_w = (win_width * 7) / 10;
            total_height = 20;
            for (int i = 0; i < msg_count; i++) {
                RECT calcRect = {0, 0, max_bubble_w, 0}; DrawText(hdcMem, chat_log[i].text, -1, &calcRect, DT_CALCRECT | DT_WORDBREAK);
                total_height += (calcRect.bottom - calcRect.top) + (bubble_padding * 2) + 10;
            }
            visible_space = clientRect.bottom - 60; 
            if (scroll_y < -(total_height - visible_space) && total_height > visible_space) scroll_y = -(total_height - visible_space);
            if (total_height <= visible_space || scroll_y > 0) scroll_y = 0;
            y_cursor += scroll_y; 

            for (int i = 0; i < msg_count; i++) {
                RECT textRect = {0, 0, max_bubble_w, 0}; DrawText(hdcMem, chat_log[i].text, -1, &textRect, DT_CALCRECT | DT_WORDBREAK);
                int b_width = textRect.right + (bubble_padding * 2), b_height = textRect.bottom + (bubble_padding * 2);
                
                RECT bubbleRect = (chat_log[i].msg_type == MSG_USER)
                    ? (RECT){win_width - b_width - 30, y_cursor, win_width - 30, y_cursor + b_height} 
                    : (RECT){30, y_cursor, 30 + b_width, y_cursor + b_height};
                
                chat_log[i].hit_box = bubbleRect;
                SelectObject(hdcMem, GetStockObject(NULL_PEN)); 
                
                HBRUSH currentBrush;
                if (chat_log[i].msg_type == MSG_USER) currentBrush = brushUser;
                else if (chat_log[i].msg_type == MSG_SYS_OK) currentBrush = brushSysOK;
                else if (chat_log[i].msg_type == MSG_SYS_ERR) currentBrush = brushSysErr;
                else currentBrush = brushAI;
                
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
                
                SetTextColor(hdcMem, (chat_log[i].msg_type == MSG_AI) ? RGB(0, 0, 0) : RGB(255, 255, 255));
                RECT drawRect = {bubbleRect.left + bubble_padding, bubbleRect.top + bubble_padding, bubbleRect.right - bubble_padding, bubbleRect.bottom - bubble_padding};
                DrawText(hdcMem, chat_log[i].text, -1, &drawRect, DT_WORDBREAK);
                y_cursor += b_height + 15;
            }
            
            RECT maskRect = {0, visible_space, win_width, clientRect.bottom}; FillRect(hdcMem, &maskRect, bgBrush);
            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);
            
            SelectObject(hdcMem, hbmOld); SelectObject(hdcMem, hOldFont); DeleteObject(hbmMem); DeleteDC(hdcMem); 
            DeleteObject(bgBrush); DeleteObject(brushUser); DeleteObject(brushAI); DeleteObject(brushSysOK); DeleteObject(brushSysErr); DeleteObject(hFont);
            
            SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS, 0, total_height, visible_space, -scroll_y, 0 };
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "LlamaChatClass"; WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc; wc.hInstance = hInstance; wc.lpszClassName = CLASS_NAME; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClass(&wc);
    hMainWindow = CreateWindowEx(0, CLASS_NAME, "Generative LLaMA Core", WS_OVERLAPPEDWINDOW | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT, 450, 600, NULL, NULL, hInstance, NULL);
    if (hMainWindow == NULL) return 0; ShowWindow(hMainWindow, nCmdShow);
    MSG msg = {0}; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}