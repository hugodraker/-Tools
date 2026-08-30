/* ============================================================================
 * CSV SQL - Command Line Remote Client
 *
 * COMPILATION INSTRUCTIONS:
 *   gcc -Os -s -o csvsqlc.exe csvsqlc.c -lws2_32
 *
 * USAGE:
 *   csvsqlc.exe 127.0.0.1:23 -q:"SELECT * FROM table;" -n:1 -p:admin
 * ============================================================================ */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define QUERY_BUFFER_SIZE   65536
#define MAX_OUTPUT_SIZE     (1024 * 1024 * 5) /* 5 MB Text Output Buffer */

static const char telnet_magic[] = "\xA6";

/* ============================================================================
 * 6-BIT PACKING & RLE COMPRESSION ENGINE
 * ============================================================================ */
unsigned char CharTo6Bit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == ' ') return 62;
    return 63; /* Escape sequence */
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
 * MAIN CLIENT ROUTINE
 * ============================================================================ */
int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: csvsqlc.exe <ip:port> -q:\"<query>\" [-n:<userid>] [-p:<password>]\n");
        printf("Example: csvsqlc.exe 127.0.0.1:23 -q:\"SELECT * FROM table;\" -n:1\n");
        return 1;
    }

    char target[256] = "127.0.0.1:23";
    char query[QUERY_BUFFER_SIZE] = "";
    char password[128] = "admin";
    int userid = 1;

    // Parse Arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-q:", 3) == 0) {
            strncpy(query, argv[i] + 3, sizeof(query) - 1);
        } else if (strncmp(argv[i], "-n:", 3) == 0) {
            userid = atoi(argv[i] + 3);
        } else if (strncmp(argv[i], "-p:", 3) == 0) {
            strncpy(password, argv[i] + 3, sizeof(password) - 1);
        } else {
            strncpy(target, argv[i], sizeof(target) - 1);
        }
    }

    if (strlen(query) == 0) {
        printf("Error: No query provided. Use -q:\"query\"\n");
        return 1;
    }

    char ip[256];
    int port = 23;
    strncpy(ip, target, sizeof(ip));
    char* colon = strchr(ip, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Error: Failed to initialize Winsock.\n");
        return 1;
    }

    // Connect to Server
    SOCKET client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        struct hostent* he = gethostbyname(ip);
        if (he) addr.sin_addr.s_addr = *(unsigned long*)he->h_addr_list[0];
    }

    int timeout = 10000; // 10 second receive timeout
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    if (connect(client_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        printf("Error: Could not connect to %s:%d\n", ip, port);
        closesocket(client_sock);
        WSACleanup();
        return 1;
    }

    // Build Payload & Checksum
    char temp[QUERY_BUFFER_SIZE];
    snprintf(temp, sizeof(temp), "%d\xA6%s\xA6%s", userid, password, query);
    unsigned int sum = 0; 
    for (char* p = temp; *p; p++) sum += (unsigned char)*p;
    
    char* uncomp = malloc(QUERY_BUFFER_SIZE * 2);
    snprintf(uncomp, QUERY_BUFFER_SIZE * 2, "%s\xA6%u", temp, sum);
    
    // Pack & Compress
    char* packed_buf = malloc(QUERY_BUFFER_SIZE * 2);
    int packed_size = Pack6Bit(uncomp, strlen(uncomp) + 1, packed_buf, QUERY_BUFFER_SIZE * 2);
    
    char* comp_buf = malloc(QUERY_BUFFER_SIZE * 2);
    int comp_size = CompressRLE(packed_buf, packed_size, comp_buf, QUERY_BUFFER_SIZE * 2);
    
    if (comp_size <= 0) {
        printf("Error: Failed to compress payload.\n");
        closesocket(client_sock); WSACleanup(); return 1;
    }

    // Send Payload
    int magic_len = strlen(telnet_magic); 
    char* send_buf = malloc(QUERY_BUFFER_SIZE * 2 + 256);
    memcpy(send_buf, telnet_magic, magic_len); 
    memcpy(send_buf + magic_len, comp_buf, comp_size);
    
    if (send(client_sock, send_buf, magic_len + comp_size, 0) <= 0) {
        printf("Error: Connection lost while sending.\n");
        closesocket(client_sock); WSACleanup(); return 1;
    }
    
    // Receive Response
    char* recv_buf = malloc(MAX_OUTPUT_SIZE); 
    int recv_len = recv(client_sock, recv_buf, MAX_OUTPUT_SIZE - 1, 0);
    
    if (recv_len > 0) {
        if (recv_len >= magic_len && strncmp(recv_buf, telnet_magic, magic_len) == 0) {
            char* rle_payload = recv_buf + magic_len; 
            int rle_len = recv_len - magic_len;
            
            char* unpacked_rle = malloc(MAX_OUTPUT_SIZE);
            int unpacked_rle_size = DecompressRLE(rle_payload, rle_len, unpacked_rle, MAX_OUTPUT_SIZE);
            
            char* decomp_buf = malloc(MAX_OUTPUT_SIZE);
            int decomp_size = Unpack6Bit(unpacked_rle, unpacked_rle_size, decomp_buf, MAX_OUTPUT_SIZE);
            
            if (decomp_size > 0) {
                // Print directly to console, maintaining original formatting
                printf("%s\n", decomp_buf);
            } else {
                printf("Error: Failed to decompress response.\n");
            }
            free(unpacked_rle); 
            free(decomp_buf);
        } else {
            printf("Error: Invalid response magic string from server.\n");
        }
    } else {
        printf("Error: No response or connection dropped.\n");
    }
    
    // Cleanup
    free(uncomp); free(packed_buf); free(comp_buf); free(send_buf); free(recv_buf);
    closesocket(client_sock);
    WSACleanup();
    return 0;
}