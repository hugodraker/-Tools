/*
 * vhdmaster.c - VHD Master 0.2
 * Two-pane VHD image editor for Win32 (converted from ISO Master).
 * Implemented: VHD fixed-disk container (footer/checksum/open/save/resize/convert),
 *              MBR partition table (create/delete/properties/active/resize/mbr/vbr),
 *              local filesystem browser, FAT16/FAT32 read/write/format engine,
 *              NTFS read engine (mount/MFT/runlists/index listing/extract),
 *              NTFS write skeleton (add file/folder, delete - best effort),
 *              Drag & Drop recursive imports, QEMU boot integration, Physical Cloning,
 *              Intelligent Shrink, Secure Zeroing, Compacting, Defragmentation.
 *
 * Compile: gcc -Os -s -mwindows -o vhdmaster.exe vhdmaster.c -lcomctl32 -lcomdlg32
 *
 * PUBLIC DOMAIN. NO WARRANTY.
 * ============================================================================ */

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_IE
#define _WIN32_IE 0x0500
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <windowsx.h>

/* ============================================================ CONSTANTS */
#define APP_NAME        "VHD Master"
#define APP_VERSION     "0.2"
#define WINDOW_WIDTH    800
#define WINDOW_HEIGHT   600
#define SECTOR_SIZE     512
#define MAX_MBR_PARTS   4

#define IDC_LOCAL_LIST    1001
#define IDC_VHD_LIST      1002

#define ID_LOCAL_BACK     2001
#define ID_LOCAL_NEWDIR   2002
#define ID_VHD_BACK       2003
#define ID_VHD_ADD        2005
#define ID_VHD_EXTRACT    2006
#define ID_VHD_DELETE     2007

#define IDM_IMAGE_NEW      3001
#define IDM_IMAGE_OPEN     3002
#define IDM_IMAGE_SAVE     3003
#define IDM_IMAGE_CLOSE    3010
#define IDM_IMAGE_CLONE_PHYSICAL 3009
#define IDM_IMAGE_CONVERT  3007
#define IDM_IMAGE_QEMU_BOOT 3008
#define IDM_IMAGE_QUIT     3006

#define IDM_PART_LIST      3020
#define IDM_PART_DELETE    3022
#define IDM_PART_FORMAT    3023
#define IDM_DISK_RESIZE    3024
#define IDM_PART_ACTIVE    3025
#define IDM_PART_RESIZE    3026
#define IDM_PART_VBR_FILE  3027
#define IDM_PART_COMPACT   3028
#define IDM_PART_REPLACE_BOOT 3029
#define IDM_PART_DEFRAG    3034
#define IDM_PART_DIAGNOSE_RAW  3037

#define IDM_DISK_MBR_STD     3030
#define IDM_DISK_TRIM        3031
#define IDM_DISK_EXTRACT_MBR 3032
#define IDM_DISK_EXTRACT_VBR 3033

#define IDM_PART_CREATE_FAT12   3051
#define IDM_PART_CREATE_FAT16_S 3052
#define IDM_PART_CREATE_FAT16   3053
#define IDM_PART_CREATE_FAT32   3054
#define IDM_PART_CREATE_FAT32L  3055
#define IDM_PART_CREATE_FAT16L  3056
#define IDM_PART_CREATE_NTFS    3057
#define IDM_PART_FORMAT        3023
#define IDM_PART_FORMAT_NTFS   3035   /* <-- ADD THIS */
#define IDM_DISK_RESIZE        3024

#define IDM_HELP_ABOUT     3041

#define IDM_MRU_1          3101
#define IDM_MRU_SEP        3100

#define GET_MFT_SEQ(idx) ((idx >= 2 && idx <= 15) ? (u64)(idx) : 1ULL)
#define MAKE_MFT_REF(idx) ((u64)(idx) | (GET_MFT_SEQ(idx) << 48))

/* ============================================================ TYPEDEFS */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed long long   s64;

typedef struct {
    int used;
    u8  type;         /* MBR partition type byte        */
    u8  boot;         /* 0x80 bootable, 0x00 not        */
    u32 lba_begin;    /* first sector (relative to 0)   */
    u32 lba_count;    /* size in sectors                */
} MbrPart;

typedef struct {
    BOOL        isOpen;
    char        path[MAX_PATH];
    u8         *img;          /* entire VHD file in RAM */
    long long   img_bytes;    /* file size incl. footer */
    u64         cap;          /* virtual disk size      */
    long long   data_offset;  /* byte offset of LBA 0   */
    
    /* Dynamic (Sparse) VHD support */
    u32         disk_type;    /* 2 = Fixed, 3 = Dynamic */
    u32         block_size;   /* typically 2MB (2097152 bytes) */
    u32         sec_per_block;/* block_size / 512 */
    u32         max_bat_entries;
    u64         bat_offset;
    u32        *bat;          /* parsed BAT entries (sector offsets) */
    u32         bitmap_secs;  /* sectors reserved for block allocation bitmap */

    MbrPart     parts[MAX_MBR_PARTS];
    int         fs_mounted;   /* FAT engine attached?   */
    u32         fs_part_lba, fs_part_nsec;
} VhdState;


/* ============================================================ GLOBALS */
HWND g_hMainWnd = NULL, g_hLocalListView = NULL, g_hVhdListView = NULL;
HWND g_hStatusBar = NULL, g_hProgressBar = NULL, g_hCancelBtn = NULL;
HINSTANCE g_hInstance = NULL;

VhdState g_vhd = {0};
char g_current_local_path[MAX_PATH];
char g_mru[5][MAX_PATH] = {0};
BOOL g_show_hidden = FALSE;
BOOL g_dragging = FALSE;
volatile BOOL g_cancel_operation = FALSE;
static int g_last_percent = -1;

static int g_view_mode = 0; // 0 = MBR Partitions, 1 = FS Files

/* NTFS State */
#define NTFS_MFT_ROOT 5
#define NTFS_FL_IN_USE 0x0001
#define NTFS_FL_IS_DIR 0x0002
#define NTFS_AT_FILE_NAME 0x30
#define NTFS_AT_DATA 0x80
#define NTFS_AT_INDEX_ROOT 0x90
#define NTFS_AT_INDEX_ALLOC 0xA0

/* In CONSTANTS section */
#define IDM_PART_FORMAT_NTFS   3035
#define IDM_PART_NTFS_DIRTY    3036

/* NTFS Attribute Types */
#define NTFS_AT_VOLUME_NAME    0x60
#define NTFS_AT_VOLUME_INFO    0x70

/* $VOLUME_INFORMATION Flags */
#define NTFS_VOLUME_IS_DIRTY         0x0001
#define NTFS_VOLUME_RESIZE_LOG_FILE  0x0002
#define NTFS_VOLUME_UPGRADE_ON_MOUNT 0x0004
#define NTFS_VOLUME_MOUNTED_ON_NT4   0x0008
#define NTFS_VOLUME_DELETE_USN       0x0010
#define NTFS_VOLUME_REPAIR_OBJECT_ID 0x0020
#define NTFS_VOLUME_CHKDSK_RAN       0x0080
#define NTFS_VOLUME_MODIFIED_CHKDSK  0x4000

#define ADD_ATTR(func) do { \
    u8 *_old_p = p; \
    p = (func); \
    wr16le(_old_p + 0x0E, attr_id++); \
} while(0)

u64 g_ntfs_mft_mirr_lcn = 0;

int g_ntfs = 0;
u64 g_ntfs_cur_dir = NTFS_MFT_ROOT;
u64 g_ntfs_mft_lcn = 0;
u32 g_ntfs_clus_size = 0;
u32 g_ntfs_mft_rec = 1024;
u64 g_ntfs_part_lba = 0;
u32 g_ntfs_spc = 0;
u32 g_ntfs_idx_bytes = 4096;

static void populate_vhd_listview(void);
static void set_local_path(const char* path);
static int fs_list(u32 dir_cluster);
static void update_mbr_in_ram(void);
static int import_recursive(const char* host_path, u32 parent_cluster);
static int vhd_open(const char* path);
static void format_83_name(const u8 *src, char *dst);
static void make_83_name(const char *in, u8 *out);
static int ntfs_list_dir(u64 dir_ref);
static int ntfs_import_recursive(const char* host_path, u64 parent_ref);
static int ntfs_extract_file(u64 mft_ref, const char *dest_path);
static int ntfs_extract_recursive(u64 mft_ref, const char *host_dir);
static int ntfs_delete_by_ref(u64 mft_ref);
static u64 ntfs_mkdir(const char *name, u64 parent_ref);
static int read_sec(u32 lba, u8 *buf, u32 count);
static int write_sec(u32 lba, const u8 *buf, u32 count);

static void ntfs_format_init_record(u8 *rec, u32 mft_index, u16 flags);
static u8* ntfs_add_attr_std_info(u8 *p, u64 ntfs_time, u32 file_attr);
static u8* ntfs_add_attr_data_nonres(u8 *p, u64 total_clusters, u64 lcn, u64 total_bytes);
static u8* ntfs_add_attr_index_root(u8 *p);

static u8* ntfs_add_attr_file_name(u8 *p, u64 parent_ref, const char *name, u64 ntfs_time, u32 flags, u64 alloc_sz, u64 data_sz, u8 namespace);
static void insert_indx_entry(u8 *buf, u64 parent_ref, const char *name, u64 child_ref, int is_dir, u8 namespace);

/* Endian read/write forward declarations */
static u16 rd16le(const u8 *p);
static u32 rd32le(const u8 *p);
static u64 rd64le(const u8 *p);
static u32 rd32be(const u8 *p);
static u64 rd64be(const u8 *p);
static void wr16le(u8 *p, u16 v);
static void wr32le(u8 *p, u32 v);
static void wr64le(u8 *p, u64 v);

/* ============================================================ BYTE HELPERS */
static void append_indx_entry(u8 *blk, u64 parent_ref, const char *name, u64 child_ref, u32 file_attrs, u8 name_type, u64 alloc_sz, u64 real_sz) {
    u32 ents_off = rd32le(blk + 0x18) + 0x18;
    u32 end_off  = rd32le(blk + 0x1C) + 0x18;
    u8 *dummy = blk + end_off - 0x10;
    
    int name_len = 0;
    while (name[name_len]) name_len++;
    
    u32 fn_len = 66 + (name_len * 2);
    u32 entry_len = (16 + fn_len + 7) & ~7;
    
    memmove(dummy + entry_len, dummy, 0x10);
    
    u8 *e = dummy;
    memset(e, 0, entry_len);
    wr64le(e + 0x00, child_ref);
    wr16le(e + 0x08, entry_len);
    wr16le(e + 0x0A, fn_len);
    wr16le(e + 0x0C, 0); 
    
    u8 *fn = e + 16;
    wr64le(fn + 0x00, parent_ref);
    u64 ntfs_time = 116444736000000000ULL;
    for(int i=0; i<4; i++) wr64le(fn + 0x08 + (i*8), ntfs_time);
    
    wr64le(fn + 0x28, alloc_sz); 
    wr64le(fn + 0x30, real_sz); 
    wr32le(fn + 0x38, file_attrs);
    
    fn[0x40] = name_len;
    fn[0x41] = name_type;
    for (int i = 0; i < name_len; i++) wr16le(fn + 0x42 + (i * 2), (unsigned char)name[i]);
    
    wr32le(blk + 0x1C, end_off - 0x18 + entry_len);
}
static void apply_usa_fixup(u8 *blk) {
    u16 usa_off = rd16le(blk + 0x04);
    u16 usa_cnt = rd16le(blk + 0x06);
    u16 usn = 1; 
    wr16le(blk + usa_off, usn);
    for (u16 i = 1; i < usa_cnt; i++) {
        u8 *sec_tail = blk + (i * 512) - 2;
        wr16le(blk + usa_off + (i * 2), rd16le(sec_tail));
        wr16le(sec_tail, usn);
    }
}
static void copy_to_clipboard(HWND hwnd, const char* text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();

    size_t len = strlen(text) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text, len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}
static void insert_indx_entry(u8 *buf, u64 parent_ref, const char *name, u64 child_ref, int is_dir, u8 namespace) {
    int name_len = 0; while (name[name_len]) name_len++;
    u32 fn_len = 66 + (name_len * 2);
    u32 entry_len = (16 + fn_len + 7) & ~7;
    
    u32 entries_off = rd32le(buf + 0x18);
    u8 *curr = buf + 0x18 + entries_off;
    u8 *insert_pos = NULL;

    /* NTFS Collation Rule 1: Case-Insensitive Alphabetic Sort */
    while (!(rd16le(curr + 0x0C) & 0x02)) { 
        u8 *fn = curr + 16;
        int cur_len = fn[0x40];
        int cmp = 0;
        int min_len = name_len < cur_len ? name_len : cur_len;
        
        for (int i = 0; i < min_len; i++) {
            u16 c1 = (unsigned char)name[i];
            if (c1 >= 'a' && c1 <= 'z') c1 -= 32; 
            u16 c2 = rd16le(fn + 0x42 + (i * 2));
            if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
            if (c1 != c2) { cmp = c1 - c2; break; }
        }
        if (cmp == 0) cmp = name_len - cur_len;
        
        if (cmp < 0) { insert_pos = curr; break; }
        curr += rd16le(curr + 0x08); 
    }

    if (!insert_pos) insert_pos = curr;

    u32 used_size = rd32le(buf + 0x1C);
    u32 shift_size = (buf + 0x18 + used_size) - insert_pos;
    memmove(insert_pos + entry_len, insert_pos, shift_size);
    memset(insert_pos, 0, entry_len);
    
    wr64le(insert_pos + 0x00, child_ref);
    wr16le(insert_pos + 0x08, entry_len);
    wr16le(insert_pos + 0x0A, fn_len);
    wr16le(insert_pos + 0x0C, 0);
    
    u8 *fn = insert_pos + 16;
    wr64le(fn + 0x00, parent_ref);
    u64 ntfs_time = (u64)time(NULL) * 10000000ULL + 116444736000000000ULL;
    wr64le(fn + 0x08, ntfs_time); wr64le(fn + 0x10, ntfs_time);
    wr64le(fn + 0x18, ntfs_time); wr64le(fn + 0x20, ntfs_time);
    wr64le(fn + 0x28, 0); wr64le(fn + 0x30, 0);
    wr32le(fn + 0x38, is_dir ? (0x10000000 | 0x16) : 0x06);
    fn[0x40] = name_len;
    fn[0x41] = namespace; 
    
    for (int i = 0; i < name_len; i++) wr16le(fn + 0x42 + (i * 2), (unsigned char)name[i]);
    wr32le(buf + 0x1C, used_size + entry_len);
}
static void init_indx_block(u8 *blk, u64 vcn) {
    memset(blk, 0, 4096);
    memcpy(blk, "INDX", 4);
    wr16le(blk + 0x04, 0x28);               
    wr16le(blk + 0x06, 9);                  
    wr64le(blk + 0x08, 0);                  
    wr64le(blk + 0x10, vcn);                
    
    wr32le(blk + 0x18, 0x28);               
    wr32le(blk + 0x1C, 0x38);               
    wr32le(blk + 0x20, 4096 - 0x18); 
    blk[0x24] = 0x00;                       
    
    u8 *e = blk + 0x18 + 0x28;              
    wr64le(e + 0x00, 0);
    wr16le(e + 0x08, 0x10);                 
    wr16le(e + 0x0A, 0);                    
    wr16le(e + 0x0C, 0x02);                 
    wr16le(e + 0x0E, 0);
    
    wr16le(blk + 0x28, 1);                  
}
static u8* ntfs_add_attr_data_nonres_empty_named(u8 *p, const char *name) {
    int name_len = 0;
    while (name[name_len]) name_len++;
    
    u32 name_bytes = name_len * 2;
    u32 name_off = 0x40;
    u32 runlist_off = (name_off + name_bytes + 7) & ~7u;
    
    /* FIX: Allocate 8 bytes explicitly for the runlist 0x00 terminator */
    u32 total = runlist_off + 8; 
    
    wr32le(p + 0, NTFS_AT_DATA);
    wr32le(p + 4, total);
    p[8] = 1; /* NON-RESIDENT */
    p[9] = name_len;
    wr16le(p + 0x0A, name_off);
    
    /* FIX: ATTR_IS_SPARSE flag for non-resident headers is 0x8000 */
    wr16le(p + 0x0C, 0x8000); 
    wr16le(p + 0x0E, 0); 
    
    wr64le(p + 0x10, 0); /* VCN Start */
    wr64le(p + 0x18, 0); /* VCN End */
    
    /* FIX: Point the runlist offset to the internal buffer and terminate it */
    wr16le(p + 0x20, runlist_off); 
    wr16le(p + 0x22, 0);
    wr32le(p + 0x24, 0);
    wr64le(p + 0x28, 0); 
    wr64le(p + 0x30, 0); 
    wr64le(p + 0x38, 0); 
    
    for (int i = 0; i < name_len; i++) {
        wr16le(p + name_off + (i * 2), (unsigned char)name[i]);
    }
    
    memset(p + runlist_off, 0, 8); /* Guarantee 0x00 end-of-runlist marker */
    
    return p + total;
}
static u8* ntfs_add_attr_data_res_usn_max(u8 *p) {
    u32 total = 64; 
    wr32le(p + 0, NTFS_AT_DATA);
    wr32le(p + 4, total);
    p[8] = 0; p[9] = 4;
    wr16le(p + 0x0A, 24); wr16le(p + 0x0C, 0); wr16le(p + 0x0E, 0);
    wr32le(p + 0x10, 32); wr16le(p + 0x14, 32); p[0x16] = 0; p[0x17] = 0;
    
    /* Name: "$Max" */
    p[24] = '$'; p[25] = 0; p[26] = 'M'; p[27] = 0; 
    p[28] = 'a'; p[29] = 0; p[30] = 'x'; p[31] = 0;
    
    u8 *val = p + 32;
    memset(val, 0, 32);
    wr64le(val + 0, 0x2000000); /* MaximumSize: 32MB */
    wr64le(val + 8, 0x400000);  /* AllocationDelta: 4MB */
    
    return p + total;
}
static u8* ntfs_add_attr_security_descriptor(u8 *p) {
    /* Self-Relative Security Descriptor granting Everyone: Full Control */
    u8 sd[] = {
        0x01, 0x00, 0x04, 0x80, /* Revision=1, Sbz1=0, Control=0x8004 (SE_DACL_PRESENT | SE_SELF_RELATIVE) */
        0x14, 0x00, 0x00, 0x00, /* OffsetOwner = 20 */
        0x24, 0x00, 0x00, 0x00, /* OffsetGroup = 36 */
        0x00, 0x00, 0x00, 0x00, /* OffsetSacl  = 0 */
        0x34, 0x00, 0x00, 0x00, /* OffsetDacl  = 52 */
        /* Owner SID: Administrators (S-1-5-32-544) */
        0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
        /* Group SID: Administrators (S-1-5-32-544) */
        0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
        /* DACL Header: Rev 2, Size 28, 1 ACE */
        0x02, 0x00, 0x1C, 0x00, 0x01, 0x00, 0x00, 0x00,
        /* ACE 1: Access Allowed, Everyone (S-1-1-0), Full Control */
        0x00, 0x03, 0x14, 0x00, 0xFF, 0x01, 0x1F, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    u32 sd_len = sizeof(sd); /* Exactly 80 bytes */
    u32 total = (0x18 + sd_len + 7) & ~7u;

    wr32le(p + 0, 0x50);          /* Attribute: $SECURITY_DESCRIPTOR */
    wr32le(p + 4, total);         
    p[8] = 0; p[9] = 0;           
    wr16le(p + 0x0A, 0x18);       
    wr16le(p + 0x0C, 0);          
    wr16le(p + 0x0E, 0);          
    wr32le(p + 0x10, sd_len);     
    wr16le(p + 0x14, 0x18);       
    p[0x16] = 0; p[0x17] = 0;     

    memcpy(p + 0x18, sd, sd_len);
    memset(p + 0x18 + sd_len, 0, total - (0x18 + sd_len));
    return p + total;
}
static u8* ntfs_add_idx_root_named(u8 *p, const char *name, u32 collation_rule) {
    u32 name_len = (u32)strlen(name);
    u32 name_size = name_len * 2;
    u32 attr_hdr_len = (0x18 + name_size + 7) & ~7u; 
    u32 content_len = 32 + 16; 
    u32 total = attr_hdr_len + content_len;
    
    wr32le(p + 0, 0x90);
    wr32le(p + 4, total);
    p[8] = 0; p[9] = (u8)name_len;
    wr16le(p + 0x0A, 0x18);       
    wr16le(p + 0x0C, 0);
    wr16le(p + 0x0E, 0); 
    wr32le(p + 0x10, content_len);
    wr16le(p + 0x14, (u16)attr_hdr_len); 
    p[0x16] = 0; p[0x17] = 0;
    
    for(u32 i = 0; i < name_len; i++) wr16le(p + 0x18 + i * 2, name[i]);
    memset(p + 0x18 + name_size, 0, attr_hdr_len - (0x18 + name_size));
    
    u8 *b = p + attr_hdr_len;
    wr32le(b + 0x00, 0x00);           
    wr32le(b + 0x04, collation_rule); 
    wr32le(b + 0x08, 4096);           
    b[0x0C] = 1; b[0x0D] = 0; b[0x0E] = 0; b[0x0F] = 0; 
    
    wr32le(b + 0x10, 16);            
    wr32le(b + 0x14, 32);            
    wr32le(b + 0x18, 32);            
    wr32le(b + 0x1C, 0);             
    
    u8 *e = b + 0x20;
    wr64le(e + 0x00, 0);            
    wr16le(e + 0x08, 16);            
    wr16le(e + 0x0A, 0);            
    wr16le(e + 0x0C, 0x02);          
    wr16le(e + 0x0E, 0);            
    
    return p + total;
}
static u8* ntfs_add_attr_data_res_empty(u8 *p, const char *name) {
    u32 name_len = name ? (u32)strlen(name) : 0;
    u32 name_size = name_len * 2;
    u32 attr_hdr_len = (0x18 + name_size + 7) & ~7u; 
    u32 total = attr_hdr_len; 

    wr32le(p + 0, 0x80);          
    wr32le(p + 4, total);         
    p[8] = 0; p[9] = (u8)name_len;    
    wr16le(p + 0x0A, 0x18);       
    wr16le(p + 0x0C, 0);          
    wr16le(p + 0x0E, 0);          
    wr32le(p + 0x10, 0);          
    wr16le(p + 0x14, (u16)attr_hdr_len); 
    p[0x16] = 0; p[0x17] = 0;     

    if (name_len > 0) {
        for (u32 i = 0; i < name_len; i++) {
            wr16le(p + 0x18 + (i * 2), (unsigned char)name[i]);
        }
    }
    
    memset(p + 0x18 + name_size, 0, total - (0x18 + name_size));
    return p + total;
}
static void print_owner_sid(u8 *sd_data) {
    u32 owner_off = *(u32*)(sd_data + 0x04); /* Offset to Owner SID */
    if (owner_off == 0) return;
    
    u8 *sid = sd_data + owner_off;
    u8 revision = sid[0];
    u8 sub_auth_count = sid[1];
    
    /* Identifier Authority is a 48-bit Big-Endian value (usually 5 for NT Authority) */
    u64 authority = 0;
    for (int i = 0; i < 6; i++) {
        authority = (authority << 8) | sid[2 + i];
    }
    
    printf("Owner SID: S-%d-%llu", revision, authority);
    
    /* Sub-Authorities are 32-bit Little-Endian */
    for (int i = 0; i < sub_auth_count; i++) {
        u32 sub_auth = *(u32*)(sid + 8 + (i * 4));
        printf("-%u", sub_auth);
    }
    printf("\n");
}
static void vhd_update_checksum(u8 *footer) {
    u32 sum = 0;
    /* Zero out the old checksum at offset 0x40 (64) */
    footer[64] = 0; footer[65] = 0; footer[66] = 0; footer[67] = 0;
    
    /* Sum all bytes */
    for (int i = 0; i < 512; i++) {
        sum += footer[i];
    }
    
    /* Write 1's complement (Big Endian) */
    sum = ~sum;
    footer[64] = (sum >> 24) & 0xFF;
    footer[65] = (sum >> 16) & 0xFF;
    footer[66] = (sum >> 8)  & 0xFF;
    footer[67] = sum & 0xFF;
}
static void build_root_index_block(u8 *blk, u64 ntfs_time) {
    memset(blk, 0, 4096);
    memcpy(blk, "INDX", 4);
    wr16le(blk + 0x04, 0x28); /* USA offset */
    wr16le(blk + 0x06, 9);    /* USA count */
    wr64le(blk + 0x08, 0);    /* LSN */
    wr64le(blk + 0x10, 0);    /* VCN */
    wr32le(blk + 0x18, 0x28); /* Entries offset relative to Node Header (0x18) */
    wr32le(blk + 0x20, 4096 - 0x18); /* Allocated size */
    wr32le(blk + 0x24, 0);    /* Leaf node */

    u16 usn = 1;
    wr16le(blk + 0x28, usn);

    u32 offset = 0x40; /* 0x18 Node Header + 0x28 Entries Offset */
    
    /* Strict Alphabetical Unicode Sort Required by NTFS */
    struct { const char *name; u64 ref; u32 attrs; } sys_files[] = {
        {"$AttrDef", 4, 0x06},
        {"$BadClus", 8, 0x06},
        {"$Bitmap", 6, 0x06},
        {"$Boot", 7, 0x06},
        {"$Extend", 11, 0x10000000 | 0x06},
        {"$LogFile", 2, 0x06},
        {"$MFT", 0, 0x06},
        {"$MFTMirr", 1, 0x06},
        {"$Secure", 9, 0x06},
        {"$UpCase", 10, 0x06},
        {"$Volume", 3, 0x06}
    };

    for (int i = 0; i < 11; i++) {
        u8 *e = blk + offset;
        u32 nlen = (u32)strlen(sys_files[i].name);
        u32 fn_body = 0x42 + nlen * 2;
        u32 entry_len = (0x10 + fn_body + 7) & ~7u;

        wr64le(e, sys_files[i].ref | (1ULL << 48)); /* Include Sequence Number 1 */
        wr16le(e + 0x08, (u16)entry_len);
        wr16le(e + 0x0A, (u16)fn_body);
        wr16le(e + 0x0C, 0); 

        u8 *fn = e + 0x10;
        wr64le(fn, 5ULL | (1ULL << 48)); /* Parent = MFT 5 (Root Dir) */
        for (int t = 0; t < 4; t++) wr64le(fn + 8 + t * 8, ntfs_time);
        wr64le(fn + 0x28, 0); 
        wr64le(fn + 0x30, 0); 
        wr32le(fn + 0x38, sys_files[i].attrs);
        fn[0x40] = (u8)nlen;
        fn[0x41] = 1; 
        for (u32 j = 0; j < nlen; j++) wr16le(fn + 0x42 + j * 2, sys_files[i].name[j]);

        offset += entry_len;
    }

    /* Dummy END entry */
    u8 *e = blk + offset;
    wr64le(e, 0);
    wr16le(e + 0x08, 0x10);
    wr16le(e + 0x0A, 0);
    wr16le(e + 0x0C, 0x02); 
    offset += 0x10;
    wr32le(blk + 0x1C, offset - 0x18); 

    /* Apply Fixups */
    for (int i = 0; i < 8; i++) {
        u8 *sec = blk + i * 512;
        wr16le(blk + 0x2A + i * 2, rd16le(sec + 510));
        wr16le(sec + 510, usn);
    }
}
static void build_upcase(u8 *buf) {
    for (u32 i = 0; i < 65536; i++) {
        u16 v = (u16)i;
        if (v >= 'a' && v <= 'z') v -= 32; /* Basic ASCII upcase */
        wr16le(buf + i * 2, v);
    }
}
static void build_attrdef(u8 *buf) {
    struct { const char *name; u32 type; u32 flags; } defs[] = {
        {"$STANDARD_INFORMATION", 0x10, 0},
        {"$ATTRIBUTE_LIST", 0x20, 0},
        {"$FILE_NAME", 0x30, 2}, /* 2 = Indexed */
        {"$OBJECT_ID", 0x40, 0},
        {"$SECURITY_DESCRIPTOR", 0x50, 0},
        {"$VOLUME_NAME", 0x60, 0},
        {"$VOLUME_INFORMATION", 0x70, 0},
        {"$DATA", 0x80, 0},
        {"$INDEX_ROOT", 0x90, 0},
        {"$INDEX_ALLOCATION", 0xA0, 0},
        {"$BITMAP", 0xB0, 0},
        {"$REPARSE_POINT", 0xC0, 0},
        {"$LOGGED_UTILITY_STREAM", 0x100, 0}
    };
    memset(buf, 0, 2560);
    for (int i = 0; i < 13; i++) {
        u8 *p = buf + i * 160;
        for (int j = 0; defs[i].name[j]; j++) p[j * 2] = defs[i].name[j];
        wr32le(p + 128, defs[i].type);
        wr32le(p + 140, defs[i].flags);
        wr64le(p + 152, 0xFFFFFFFFFFFFFFFFULL); /* Max size: Unlimited */
    }
}
static u8* ntfs_add_attr_data_res(u8 *p, const u8 *data, u32 len) {
    u32 total = (0x18 + len + 7) & ~7u;
    wr32le(p + 0, NTFS_AT_DATA);
    wr32le(p + 4, total);
    p[8] = 0; p[9] = 0;
    wr16le(p + 0x10, (u16)len);
    wr16le(p + 0x14, 0x18);
    if (len > 0 && data) memcpy(p + 0x18, data, len);
    else if (len > 0) memset(p + 0x18, 0, len);
    return p + total;
}
static void vhd_parse_gpt(void) {
    u8 gpt_hdr[512];
    if (read_sec(1, gpt_hdr, 1) != 0) return;
    if (memcmp(gpt_hdr, "EFI PART", 8) != 0) return;

    u64 entry_lba = rd64le(gpt_hdr + 72);
    u32 num_entries = rd32le(gpt_hdr + 80);
    u32 entry_size = rd32le(gpt_hdr + 84);

    if (entry_size < 128 || num_entries == 0) return;

    /* Allocate buffer for GPT entries (typically 128 entries * 128 bytes = 16KB) */
    u32 bytes_to_read = num_entries * entry_size;
    u32 secs_to_read = (bytes_to_read + 511) / 512;
    u8 *entries = (u8*)malloc(secs_to_read * 512);
    if (!entries) return;

    if (read_sec((u32)entry_lba, entries, secs_to_read) != 0) {
        free(entries);
        return;
    }

    int part_idx = 0;
    
    /* Windows Basic Data Partition GUID: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 */
    const u8 basic_data_guid[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };

    for (u32 i = 0; i < num_entries && part_idx < MAX_MBR_PARTS; i++) {
        const u8 *ent = entries + i * entry_size;
        
        /* Check if it's a Basic Data Partition (NTFS/exFAT) */
        if (memcmp(ent, basic_data_guid, 16) == 0) {
            u64 first_lba = rd64le(ent + 32);
            u64 last_lba  = rd64le(ent + 40);

            g_vhd.parts[part_idx].used      = 1;
            g_vhd.parts[part_idx].type      = 0x07; /* Map to NTFS for UI compatibility */
            g_vhd.parts[part_idx].boot      = 0;
            g_vhd.parts[part_idx].lba_begin = (u32)first_lba;
            g_vhd.parts[part_idx].lba_count = (u32)(last_lba - first_lba + 1);
            part_idx++;
        }
    }
    free(entries);
}
static int ntfs_mount(u32 lba) {
    u8 vbr[512];
    if (read_sec(lba, vbr, 1) != 0) return -1;
    if (memcmp(vbr + 3, "NTFS    ", 8) != 0) return -2;

    g_ntfs_part_lba = lba;
    g_ntfs_spc = vbr[0x0D]; /* Sectors per cluster */
    u16 bps = *(u16*)(vbr + 0x0B); /* Bytes per sector */
    g_ntfs_clus_size = bps * g_ntfs_spc;

    g_ntfs_mft_lcn = *(u64*)(vbr + 0x30);
    g_ntfs_mft_mirr_lcn = *(u64*)(vbr + 0x38);

    /* CORRECT MFT Record Size check (Signed power of two) */
    signed char raw_mft_clusters = (signed char)vbr[0x40];
    if (raw_mft_clusters < 0) {
        g_ntfs_mft_rec = 1U << (-raw_mft_clusters); 
    } else {
        g_ntfs_mft_rec = (u32)raw_mft_clusters * g_ntfs_clus_size;
    }

    /* CORRECT Index Block Size check */
    signed char raw_idx_clusters = (signed char)vbr[0x44];
    if (raw_idx_clusters < 0) {
        g_ntfs_idx_bytes = 1U << (-raw_idx_clusters); 
    } else {
        g_ntfs_idx_bytes = (u32)raw_idx_clusters * g_ntfs_clus_size;
    }

    g_ntfs_cur_dir = NTFS_MFT_ROOT;
    g_vhd.fs_mounted = 1;
    g_ntfs = 1;

    return 0;
}
static int ntfs_read_runlist(const u8 *runlist, u8 *out_buf, u32 alloc_size) {
    s64 current_lcn = 0; /* Must be signed 64-bit to handle negative jumps */
    u32 out_offset = 0;
    const u8 *p = runlist;

    while (*p != 0x00) {
        u8 len_sz = *p & 0x0F;
        u8 off_sz = (*p >> 4) & 0x0F;
        p++;

        /* 1. Extract Length in Clusters */
        u64 run_len = 0;
        for (int i = 0; i < len_sz; i++) {
            run_len |= ((u64)p[i]) << (i * 8);
        }
        p += len_sz;

        /* 2. Extract LCN Offset */
        s64 run_off = 0;
        if (off_sz > 0) {
            for (int i = 0; i < off_sz; i++) {
                run_off |= ((u64)p[i]) << (i * 8);
            }
            /* Sign extension for negative cluster jumps */
            if (p[off_sz - 1] & 0x80) {
                for (int i = off_sz; i < 8; i++) {
                    run_off |= (0xFFULL << (i * 8));
                }
            }
            current_lcn += run_off;
        }
        p += off_sz;

        /* Calculate exact bytes to read without overflowing alloc_size */
        u64 run_bytes_total = run_len * g_ntfs_clus_size;
        u32 bytes_to_read = (run_bytes_total > (alloc_size - out_offset)) 
                            ? (alloc_size - out_offset) 
                            : (u32)run_bytes_total;

        /* 3. Read Clusters */
        if (off_sz == 0) {
            /* True sparse block */
            memset(out_buf + out_offset, 0, bytes_to_read);
        } else {
            u64 run_lba = g_ntfs_part_lba + (current_lcn * g_ntfs_spc);
            u32 secs = bytes_to_read / 512;
            u32 remainder = bytes_to_read % 512;

            /* Read full sectors directly */
            if (secs > 0) {
                read_sec((u32)run_lba, out_buf + out_offset, secs);
            }
            
            /* Safely read unaligned tail using a bounce buffer to prevent heap overflow */
            if (remainder > 0) {
                u8 bounce[512];
                read_sec((u32)run_lba + secs, bounce, 1);
                memcpy(out_buf + out_offset + (secs * 512), bounce, remainder);
            }
        }

        out_offset += bytes_to_read;
        if (out_offset >= alloc_size) break;
    }
    return out_offset;
}
static int ntfs_apply_fixups(u8 *rec, u32 rec_size) {
    /* Verify this is actually an MFT record or Directory Index block */
    if (memcmp(rec, "FILE", 4) != 0 && memcmp(rec, "INDX", 4) != 0) {
        return -1; 
    }

    u16 usa_offset = *(u16*)(rec + 0x04);
    u16 usa_count  = *(u16*)(rec + 0x06);
    
    if (usa_count == 0 || usa_offset + usa_count * 2 > rec_size) return 0;

    u16 seq_num = *(u16*)(rec + usa_offset);
    u16 *usa = (u16*)(rec + usa_offset + 2);

    /* Fixups in NTFS are ALWAYS every 512 bytes, regardless of disk sector size */
    for (u32 i = 1; i < usa_count; i++) {
        u32 sec_end = i * 512 - 2;
        if (sec_end + 2 > rec_size) break;
        
        /* Check for torn writes */
        if (*(u16*)(rec + sec_end) != seq_num) return -1; 
        
        /* Restore original hidden bytes */
        *(u16*)(rec + sec_end) = usa[i - 1];              
    }
    return 0;
}
static int vhd_translate_lba(u32 lba, u64 *out_file_offset) {
    if (!g_vhd.isOpen) return -1;

    if (g_vhd.disk_type == 2) { /* Fixed */
        u64 off = g_vhd.data_offset + (u64)lba * 512;
        if (off + 512 > (u64)g_vhd.img_bytes - 512) return -1;
        *out_file_offset = off;
        return 0;
    }

    if (g_vhd.disk_type == 3) { /* Dynamic */
        if (!g_vhd.bat || g_vhd.sec_per_block == 0) return -1;
        u32 block_idx = lba / g_vhd.sec_per_block;
        u32 sec_in_block = lba % g_vhd.sec_per_block;

        if (block_idx >= g_vhd.max_bat_entries) return -1;

        u32 bat_sec = g_vhd.bat[block_idx];
        if (bat_sec == 0xFFFFFFFF) {
            return 1; /* Unallocated block (reads as all zeroes) */
        }

        /* VHD dynamic blocks include a sector bitmap before data sectors */
        u64 block_data_offset = ((u64)bat_sec + g_vhd.bitmap_secs) * 512;
        u64 off = block_data_offset + (u64)sec_in_block * 512;
        if (off + 512 > (u64)g_vhd.img_bytes) return -1;

        *out_file_offset = off;
        return 0;
    }

    return -1;
}
static u16 rd16le(const u8 *p) { return (u16)p[0] | ((u16)p[1]<<8); }
static void write_attr_def(u8 *p, const char *name, u32 type, u32 flags, u64 min_sz, u64 max_sz) {
    for(int i = 0; name[i]; i++) {
        p[i * 2] = name[i];
        p[(i * 2) + 1] = 0;
    }
    wr32le(p + 128, type);
    wr32le(p + 132, 0); 
    wr32le(p + 136, 0); 
    wr32le(p + 140, flags);
    wr64le(p + 144, min_sz);
    wr64le(p + 152, max_sz);
}
static void wr16le(u8 *p, u16 v) { p[0]=(u8)v; p[1]=(u8)(v>>8); }
static u32 rd32le(const u8 *p) { return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static void wr32le(u8 *p, u32 v) { p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static u32 rd32be(const u8 *p) { return ((u32)p[0]<<24) | ((u32)p[1]<<16) | ((u32)p[2]<<8) | p[3]; }
static void wr32be(u8 *p, u32 v) { p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
static u64 rd64be(const u8 *p) { u64 v=0; int i; for(i=0;i<8;i++) v=(v<<8)|p[i]; return v; }
static void wr64be(u8 *p, u64 v) { int i; for(i=0;i<8;i++) p[i]=(u8)(v>>(56-8*i)); }
static void wr16be(u8 *p, u16 v) { p[0]=(u8)(v>>8); p[1]=(u8)v; }
static u64 rd64le(const u8 *p) { u64 v=0; int i; for(i=7;i>=0;i--) v=(v<<8)|p[i]; return v; }
static void wr64le(u8 *p, u64 v) { int i; for(i=0;i<8;i++) p[i]=(u8)(v>>(8*i)); }

static const char* get_basename(const char* path) {
    const char* slash = strrchr(path, '/'); if (!slash) slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

static void format_size(u64 bytes, char* buffer, int buf_size) {
    if (bytes >= 1073741824ULL)      snprintf(buffer, buf_size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576ULL)    snprintf(buffer, buf_size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024ULL)       snprintf(buffer, buf_size, "%.2f KB", bytes / 1024.0);
    else                             snprintf(buffer, buf_size, "%I64u B", bytes);
}

void UpdateWindowTitle(void) {
    char title[MAX_PATH + 64];
    if (g_vhd.isOpen && strlen(g_vhd.path) > 0)
        snprintf(title, sizeof(title), "%s %s - [%s]", APP_NAME, APP_VERSION, get_basename(g_vhd.path));
    else if (g_vhd.isOpen)
        snprintf(title, sizeof(title), "%s %s - [New VHD]", APP_NAME, APP_VERSION);
    else
        snprintf(title, sizeof(title), "%s %s", APP_NAME, APP_VERSION);
    SetWindowTextA(g_hMainWnd, title);
}

/* ============================================================ PROGRESS HELPERS */
void PumpMessages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
}

void ShowProgress(BOOL show) {
    ShowWindow(g_hProgressBar, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hCancelBtn, show ? SW_SHOW : SW_HIDE);
    g_cancel_operation = FALSE;
    g_last_percent = -1;
    if (show) SendMessageA(g_hProgressBar, PBM_SETPOS, 0, 0);
}

void UpdateProgress(int percent) {
    if (percent != g_last_percent) {
        SendMessageA(g_hProgressBar, PBM_SETPOS, percent, 0);
        g_last_percent = percent;
    }
    PumpMessages();
}

/* ============================================================ DIALOGS */
char g_input_result[MAX_PATH];
HWND g_hInputEdit;

LRESULT CALLBACK InputWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == 1)      { GetWindowTextA(g_hInputEdit, g_input_result, MAX_PATH); DestroyWindow(hwnd); }
            else if (LOWORD(wp) == 2) { g_input_result[0] = '\0'; DestroyWindow(hwnd); }
            break;
        case WM_CLOSE: g_input_result[0] = '\0'; DestroyWindow(hwnd); break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

BOOL ShowInputBox(HWND parent, const char* title, const char* prompt, char* out_buf) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = InputWndProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "VhdInputBoxClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "VhdInputBoxClass", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 300, 140,
        parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", prompt, WS_CHILD | WS_VISIBLE, 10, 10, 260, 20, hDlg, NULL, g_hInstance, NULL);
    g_hInputEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", out_buf, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        10, 35, 260, 22, hDlg, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 110, 70, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 195, 70, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);

    SetFocus(g_hInputEdit); EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    if (g_input_result[0] != '\0') { strcpy(out_buf, g_input_result); return TRUE; }
    return FALSE;
}

HWND g_hCombo;
int g_combo_sel_data = -1;
LRESULT CALLBACK ComboDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == 1) { 
                int sel = SendMessageA(g_hCombo, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_combo_sel_data = SendMessageA(g_hCombo, CB_GETITEMDATA, sel, 0);
                } else g_combo_sel_data = -1;
                DestroyWindow(hwnd); 
            }
            else if (LOWORD(wp) == 2) { g_combo_sel_data = -1; DestroyWindow(hwnd); }
            break;
        case WM_CLOSE: g_combo_sel_data = -1; DestroyWindow(hwnd); break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

BOOL ShowDriveSelectBox(HWND parent, char* out_drive) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ComboDlgProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "VhdComboDlgClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "VhdComboDlgClass", "Select Physical Drive",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 350, 140,
        parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", "Select source drive (Requires Admin):", WS_CHILD | WS_VISIBLE, 10, 10, 310, 20, hDlg, NULL, g_hInstance, NULL);
    
    g_hCombo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, 35, 310, 200, hDlg, NULL, g_hInstance, NULL);
    
    CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 160, 70, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 245, 70, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);

    int count = 0;
    for (int i = 0; i < 32; i++) {
        char path[64]; snprintf(path, 64, "\\\\.\\PhysicalDrive%d", i);
        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            GET_LENGTH_INFORMATION gli; DWORD ret;
            if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &gli, sizeof(gli), &ret, NULL)) {
                char display[128]; char sz[64];
                format_size(gli.Length.QuadPart, sz, sizeof(sz));
                snprintf(display, sizeof(display), "PhysicalDrive%d (%s)", i, sz);
                int idx = SendMessageA(g_hCombo, CB_ADDSTRING, 0, (LPARAM)display);
                SendMessageA(g_hCombo, CB_SETITEMDATA, idx, i);
                count++;
            }
            CloseHandle(h);
        }
    }
    if (count == 0) {
        int idx = SendMessageA(g_hCombo, CB_ADDSTRING, 0, (LPARAM)"No drives found (Run as Admin?)");
        SendMessageA(g_hCombo, CB_SETITEMDATA, idx, -1);
    }
    SendMessageA(g_hCombo, CB_SETCURSEL, 0, 0);

    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    
    if (g_combo_sel_data != -1) {
        snprintf(out_drive, 64, "\\\\.\\PhysicalDrive%d", g_combo_sel_data);
        return TRUE;
    }
    return FALSE;
}

char g_lost_log[65536];
int g_lost_action = 0;
LRESULT CALLBACK LostFilesProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wp) == 1)      { g_lost_action = 1; DestroyWindow(hwnd); }
            else if (LOWORD(wp) == 2) { g_lost_action = 0; DestroyWindow(hwnd); }
            else if (LOWORD(wp) == 3) {
                OPENFILENAMEA sfn = {0};
                char path[MAX_PATH] = "lost_files.txt";
                sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
                sfn.lpstrFile = path; sfn.nMaxFile = MAX_PATH;
                sfn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files\0*.*\0";
                sfn.lpstrDefExt = "txt";
                if (GetSaveFileNameA(&sfn)) {
                    FILE* f = fopen(path, "w");
                    if (f) { fputs(g_lost_log, f); fclose(f); MessageBoxA(hwnd, "Exported successfully.", "Success", MB_OK); }
                }
            }
            break;
        case WM_CLOSE: g_lost_action = 0; DestroyWindow(hwnd); break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

BOOL ShowLostFilesDialog(HWND parent) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = LostFilesProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "VhdLostFilesClass"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "VhdLostFilesClass", "Data Loss Warning",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        parent, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "STATIC", "The following items exceed the new partition bounds and will be deleted:", WS_CHILD | WS_VISIBLE, 10, 10, 360, 20, hDlg, NULL, g_hInstance, NULL);
    CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_lost_log, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        10, 30, 360, 180, hDlg, NULL, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Proceed", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 210, 220, 75, 23, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 295, 220, 75, 23, hDlg, (HMENU)2, g_hInstance, NULL);
    CreateWindowExA(0, "BUTTON", "Export...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 220, 75, 23, hDlg, (HMENU)3, g_hInstance, NULL);

    EnableWindow(parent, FALSE);
    MSG msg;
    while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageA(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    return g_lost_action;
}

/* ============================================================ SETTINGS & MRU */
void SaveSettings(void) {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* p = strrchr(iniPath, '\\');
    if (p) strcpy(p + 1, "vhdmaster.ini"); else strcpy(iniPath, "vhdmaster.ini");

    WINDOWPLACEMENT wp = {0}; wp.length = sizeof(WINDOWPLACEMENT);
    if (GetWindowPlacement(g_hMainWnd, &wp)) {
        char buf[32];
        snprintf(buf, 32, "%d", (int)wp.rcNormalPosition.left);   WritePrivateProfileStringA("Window", "X", buf, iniPath);
        snprintf(buf, 32, "%d", (int)wp.rcNormalPosition.top);    WritePrivateProfileStringA("Window", "Y", buf, iniPath);
        snprintf(buf, 32, "%d", (int)(wp.rcNormalPosition.right - wp.rcNormalPosition.left));  WritePrivateProfileStringA("Window", "Width", buf, iniPath);
        snprintf(buf, 32, "%d", (int)(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top));  WritePrivateProfileStringA("Window", "Height", buf, iniPath);
    }
    for (int i = 0; i < 5; i++) {
        char key[16]; snprintf(key, 16, "MRU%d", i + 1);
        WritePrivateProfileStringA("MRU", key, g_mru[i], iniPath);
    }
}

void UpdateMRUMenu(void) {
    HMENU hMenu = GetMenu(g_hMainWnd);
    if (!hMenu) return;
    HMENU hFile = GetSubMenu(hMenu, 0);
    DeleteMenu(hFile, IDM_MRU_SEP, MF_BYCOMMAND);
    for (int i = 0; i < 5; i++) DeleteMenu(hFile, IDM_MRU_1 + i, MF_BYCOMMAND);

    int count = 0;
    for (int i = 0; i < 5; i++) if (strlen(g_mru[i]) > 0) count++;

    if (count > 0) {

        for (int i = 0; i < 5; i++) {
            if (strlen(g_mru[i]) > 0) {
                char text[MAX_PATH + 10];
                snprintf(text, sizeof(text), "&%d %s", i + 1, g_mru[i]);
                InsertMenuA(hFile, IDM_IMAGE_QUIT, MF_BYCOMMAND | MF_STRING, IDM_MRU_1 + i, text);
            }
        }
        InsertMenuA(hFile, IDM_IMAGE_QUIT, MF_BYCOMMAND | MF_SEPARATOR, IDM_MRU_SEP, NULL);
    }
    DrawMenuBar(g_hMainWnd);
}

void LoadSettings(void) {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* p = strrchr(iniPath, '\\');
    if (p) strcpy(p + 1, "vhdmaster.ini"); else strcpy(iniPath, "vhdmaster.ini");

    char buf[32];
    int x = -9999, y = -9999, w = WINDOW_WIDTH, h = WINDOW_HEIGHT;
    if (GetPrivateProfileStringA("Window", "X", "", buf, 32, iniPath) && strlen(buf) > 0) x = atoi(buf);
    if (GetPrivateProfileStringA("Window", "Y", "", buf, 32, iniPath) && strlen(buf) > 0) y = atoi(buf);
    if (GetPrivateProfileStringA("Window", "Width", "", buf, 32, iniPath) && strlen(buf) > 0) w = atoi(buf);
    if (GetPrivateProfileStringA("Window", "Height", "", buf, 32, iniPath) && strlen(buf) > 0) h = atoi(buf);
    if (x != -9999 && y != -9999) SetWindowPos(g_hMainWnd, NULL, x, y, w, h, SWP_NOZORDER);

    for (int i = 0; i < 5; i++) {
        char key[16]; snprintf(key, 16, "MRU%d", i + 1);
        GetPrivateProfileStringA("MRU", key, "", g_mru[i], MAX_PATH, iniPath);
    }
    UpdateMRUMenu();
}

void UpdateMRU(const char* path) {
    int existing = -1;
    for (int i = 0; i < 5; i++) if (strcmp(g_mru[i], path) == 0) existing = i;
    if (existing != -1) {
        char temp[MAX_PATH]; strcpy(temp, g_mru[existing]);
        for (int i = existing; i > 0; i--) strcpy(g_mru[i], g_mru[i-1]);
        strcpy(g_mru[0], temp);
    } else {
        for (int i = 4; i > 0; i--) strcpy(g_mru[i], g_mru[i-1]);
        strcpy(g_mru[0], path);
    }
    UpdateMRUMenu(); SaveSettings();
}

/* ============================================================ VHD CONTAINER */
static void vhd_build_footer(u8 *foot, u64 cap) {
    u32 sum; int i;
    memset(foot, 0, 512);
    memcpy(foot + 0,  "conectix", 8);
    wr32be(foot + 8,  0x00000002);
    wr32be(foot + 12, 0x00010000);
    wr64be(foot + 16, 0xFFFFFFFFFFFFFFFFULL); // Required format for Fixed Disks
    wr32be(foot + 24, (u32)(time(NULL) - 946684800)); // VHD timestamp is Jan 1, 2000 epoch
    memcpy(foot + 28, "vhdm", 4);
    wr32be(foot + 32, 0x00010000);
    memcpy(foot + 36, "Wi2k", 4);
    wr64be(foot + 40, cap);
    wr64be(foot + 48, cap);
    
    u32 ts = (u32)(cap / 512);
    u32 c, h, s;
    if (ts > 65535 * 16 * 255) ts = 65535 * 16 * 255;
    if (ts >= 65535 * 16 * 63) {
        s = 255; h = 16; c = ts / (s * h);
    } else {
        s = 17;
        u32 cy_hx = ts / s;
        h = (cy_hx + 1023) / 1024;
        if (h < 4) h = 4;
        if (cy_hx >= (h * 1024) || h > 16) {
            s = 31; h = 16; cy_hx = ts / s;
        }
        if (cy_hx >= (h * 1024)) {
            s = 63; h = 16; cy_hx = ts / s;
        }
        c = cy_hx / h;
    }
    wr16be(foot + 56, (u16)c);
    foot[58] = (u8)h;
    foot[59] = (u8)s;

    wr32be(foot + 60, 2); // Disk Type Fixed
    for (i = 0; i < 16; i++) foot[68 + i] = (u8)(rand() & 0xFF);
    foot[84] = 0;
    
    memset(foot + 64, 0, 4);
    sum = 0;
    for (i = 0; i < 512; i++) sum += foot[i];
    wr32be(foot + 64, ~sum);
}

static int vhd_validate(const u8 *img, long long bytes, long long *data_offset, u64 *cap, u32 *disk_type) {
    if (bytes < 1024) return -1;
    const u8 *foot = img + bytes - 512;

    if (memcmp(foot, "conectix", 8) == 0) *data_offset = 0;
    else if (memcmp(img, "conectix", 8) == 0) *data_offset = 512;
    else return -2;

    *disk_type = rd32be(foot + 60);
    if (*disk_type != 2 && *disk_type != 3) return -3; /* 2 = Fixed, 3 = Dynamic */

    *cap = rd64be(foot + 48);
    if (*cap == 0) return -4;

    return 0;
}

static void vhd_parse_mbr(void) {
    int i;
    memset(g_vhd.parts, 0, sizeof(g_vhd.parts));
    if (!g_vhd.isOpen) return;
    
    u8 mbr[512];
    if (read_sec(0, mbr, 1) != 0) return;

    /* Detect GPT Protective MBR */
    if (mbr[0x1BE + 4] == 0xEE) {
        vhd_parse_gpt();
        return;
    }

    /* Standard MBR parsing */
    for (i = 0; i < MAX_MBR_PARTS; i++) {
        const u8 *e = mbr + 0x1BE + i * 16;
        if (e[4] == 0) continue;
        g_vhd.parts[i].used      = 1;
        g_vhd.parts[i].type      = e[4];
        g_vhd.parts[i].boot      = e[0];
        g_vhd.parts[i].lba_begin = rd32le(e + 8);
        g_vhd.parts[i].lba_count = rd32le(e + 12);
    }
}

static void update_mbr_in_ram(void) {
    if (!g_vhd.isOpen) return;
    u8 mbr[512];
    if (read_sec(0, mbr, 1) != 0) return;

    /* CRITICAL: Do not overwrite a GPT Protective MBR. Doing so will 
       orphan the GPT headers and corrupt the disk. */
    if (mbr[0x1BE + 4] == 0xEE) return;

    for (int i = 0; i < MAX_MBR_PARTS; i++) {
        u8 *e = mbr + 0x1BE + i * 16;
        memset(e, 0, 16);
        if (g_vhd.parts[i].used) {
            e[0] = g_vhd.parts[i].boot;
            e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;
            e[4] = g_vhd.parts[i].type;
            e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
            wr32le(e + 8, g_vhd.parts[i].lba_begin);
            wr32le(e + 12, g_vhd.parts[i].lba_count);
        }
    }
    write_sec(0, mbr, 1);
}

static void vhd_close(void) {
    if (g_vhd.bat) { free(g_vhd.bat); g_vhd.bat = NULL; }
    if (g_vhd.img) { free(g_vhd.img); g_vhd.img = NULL; }
    g_vhd.isOpen = FALSE; g_vhd.img_bytes = 0; g_vhd.data_offset = 0; g_vhd.cap = 0;
    g_vhd.fs_mounted = 0; g_view_mode = 0;
    g_ntfs = 0;
    UpdateWindowTitle();
}

static int vhd_open(const char* path) {
    HANDLE h; LARGE_INTEGER sz; u8 *buf; int rc;
    
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return -1; }
    
    /* Allocate the buffer. 
       Note: Loading multi-gigabyte files directly into RAM is architecture-limited. 
       This works for smaller VHDs, but consider CreateFileMapping for massive disks. */
    buf = (u8*)malloc((size_t)sz.QuadPart);
    if (!buf) { CloseHandle(h); return -5; }
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    
    /* Loop read to prevent ReadFile 32-bit truncation on disks > 4GB */
    u64 remaining = sz.QuadPart;
    u8 *ptr = buf;
    while (remaining > 0) {
        DWORD to_read = (remaining > 0x40000000) ? 0x40000000 : (DWORD)remaining;
        DWORD got = 0;
        if (!ReadFile(h, ptr, to_read, &got, NULL) || got != to_read) {
            free(buf); CloseHandle(h); return -2;
        }
        ptr += got;
        remaining -= got;
    }
    CloseHandle(h);

    long long doff; u64 cap; u32 dtype;
    rc = vhd_validate(buf, (long long)sz.QuadPart, &doff, &cap, &dtype);
    if (rc != 0) { free(buf); return rc; }

    vhd_close();
    g_vhd.img         = buf;
    g_vhd.img_bytes   = (long long)sz.QuadPart;
    g_vhd.data_offset = doff;
    g_vhd.cap         = cap;
    g_vhd.disk_type   = dtype;
    g_vhd.isOpen      = TRUE;
    strncpy(g_vhd.path, path, MAX_PATH - 1);

    /* Parse dynamic sparse header */
    if (dtype == 3) {
        const u8 *foot = (doff == 512) ? buf : (buf + sz.QuadPart - 512);
        u64 dyn_hdr_off = rd64be(foot + 16);
        if (dyn_hdr_off + 1024 > (u64)sz.QuadPart) { vhd_close(); return -6; }

        const u8 *dyn = buf + dyn_hdr_off;
        if (memcmp(dyn, "cxsparse", 8) != 0) { vhd_close(); return -7; }

        g_vhd.bat_offset      = rd64be(dyn + 16);
        g_vhd.max_bat_entries = rd32be(dyn + 28);
        g_vhd.block_size      = rd32be(dyn + 32);
        if (g_vhd.block_size == 0) g_vhd.block_size = 2097152; /* Default 2MB */
        g_vhd.sec_per_block   = g_vhd.block_size / 512;

        u32 bitmap_bytes = (g_vhd.sec_per_block + 7) / 8;
        g_vhd.bitmap_secs = (bitmap_bytes + 511) / 512;

        g_vhd.bat = (u32*)malloc(g_vhd.max_bat_entries * sizeof(u32));
        if (!g_vhd.bat) { vhd_close(); return -5; }

        const u8 *bat_raw = buf + g_vhd.bat_offset;
        for (u32 i = 0; i < g_vhd.max_bat_entries; i++) {
            g_vhd.bat[i] = rd32be(bat_raw + i * 4);
        }
    }

    vhd_parse_mbr();
    UpdateWindowTitle();
    return 0;
}

static int vhd_create(const char* path, u32 size_mb) {
    u64 cap = (u64)size_mb * 1024 * 1024;
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -2;

    ShowProgress(TRUE);
    u8 buf[65536];
    memset(buf, 0, sizeof(buf));
    buf[510] = 0x55; buf[511] = 0xAA; 
    
    u64 rem = cap;
    DWORD w;
    while (rem > 0) {
        u32 chunk = (rem > sizeof(buf)) ? sizeof(buf) : (u32)rem;
        WriteFile(h, buf, chunk, &w, NULL);
        if (buf[510]) { buf[510] = 0; buf[511] = 0; } 
        rem -= chunk;
        if (rem % (1024 * 1024 * 10) == 0) UpdateProgress((int)(((cap - rem) * 100) / cap));
    }

    u8 footer[512];
    vhd_build_footer(footer, cap);
    WriteFile(h, footer, 512, &w, NULL);
    CloseHandle(h);
    ShowProgress(FALSE);
    return 0;
}

static int vhd_save(void) {
    HANDLE h; DWORD w;
    if (!g_vhd.isOpen || !g_vhd.img) return -1;
    vhd_build_footer(g_vhd.img + g_vhd.img_bytes - 512, g_vhd.cap);
    h = CreateFileA(g_vhd.path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -2;
    WriteFile(h, g_vhd.img, (DWORD)g_vhd.img_bytes, &w, NULL);
    CloseHandle(h);
    return (w == (DWORD)g_vhd.img_bytes) ? 0 : -3;
}

static int vhd_resize(u32 new_mb) {
    u64 newcap = (u64)new_mb * 1024 * 1024;
    long long new_total = (long long)newcap + 512;
    int i;
    if (!g_vhd.isOpen) return -1;

    for (i = 0; i < MAX_MBR_PARTS; i++)
        if (g_vhd.parts[i].used &&
            (u64)(g_vhd.parts[i].lba_begin + g_vhd.parts[i].lba_count) * 512 > newcap)
            return -2;

    u8 *nbuf = (u8*)calloc(1, (size_t)new_total);
    if (!nbuf) return -3;
    
    long long copy_len = g_vhd.img_bytes - 512;
    if (copy_len > (long long)newcap) copy_len = newcap;
    
    memcpy(nbuf, g_vhd.img + g_vhd.data_offset, (size_t)copy_len);
    free(g_vhd.img);
    g_vhd.img = nbuf;
    g_vhd.img_bytes = new_total;
    g_vhd.cap = newcap;
    g_vhd.data_offset = 0; 
    return 0;
}

/* ============================================================ PARTITIONS */
static const char* part_type_name(u8 t) {
    switch (t) {
        case 0x01: return "FAT12";
        case 0x04: case 0x06: case 0x0E: return "FAT16";
        case 0x05: case 0x0F: return "Extended";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: case 0x0C: return "FAT32";
        case 0x83: return "Linux";
        default:   return "Other";
    }
}

static void part_show_properties(HWND hwnd) {
    char msg[1024], sz1[32], sz2[32], line[128];
    int i; u64 total_used = 0;
    strcpy(msg, "");
    for (i = 0; i < MAX_MBR_PARTS; i++) {
        if (!g_vhd.parts[i].used) { snprintf(line, sizeof(line), "Slot %d: (empty)\n", i + 1); }
        else {
            format_size((u64)g_vhd.parts[i].lba_count * 512, sz1, sizeof(sz1));
            total_used += (u64)g_vhd.parts[i].lba_count * 512;
            snprintf(line, sizeof(line), "Slot %d: %s, %s, start LBA %lu%s\n",
                     i + 1, part_type_name(g_vhd.parts[i].type), sz1,
                     (unsigned long)g_vhd.parts[i].lba_begin,
                     g_vhd.parts[i].boot == 0x80 ? ", bootable" : "");
        }
        strcat(msg, line);
    }
    format_size(total_used, sz1, sizeof(sz1));
    format_size(g_vhd.cap, sz2, sizeof(sz2));
    snprintf(line, sizeof(line), "\nCapacity: %s\nUsed by partitions: %s\nFree: %.1f%%",
             sz2, sz1, g_vhd.cap ? 100.0 * (1.0 - (double)total_used / (double)g_vhd.cap) : 0.0);
    strcat(msg, line);
    MessageBoxA(hwnd, msg, "Disk Properties", MB_ICONINFORMATION);
}

static int part_create_fat(HWND hwnd, u8 force_type) {
    u64 cap_secs = g_vhd.cap / 512;
    u8 used_end[4] = {0,0,0,0};
    int i, best = -1, slot = -1;
    u64 best_sz = 0, gap_begin = 0;

    { u8 flags[4] = {0,0,0,0};
      for (i = 0; i < MAX_MBR_PARTS; i++) if (g_vhd.parts[i].used) {
          u64 s = g_vhd.parts[i].lba_begin, e = s + g_vhd.parts[i].lba_count;
          if (s < 4) used_end[0] = 1; (void)e;
      } }

    {
        u64 ranges[5][2]; int nr = 0, j;
        u32 starts[4], counts[4]; int n = 0;
        for (i = 0; i < MAX_MBR_PARTS; i++) if (g_vhd.parts[i].used) { starts[n] = g_vhd.parts[i].lba_begin; counts[n] = g_vhd.parts[i].lba_count; n++; }
        for (i = 0; i < n - 1; i++) for (j = i + 1; j < n; j++)
            if (starts[j] < starts[i]) { u32 t = starts[i]; starts[i] = starts[j]; starts[j] = t;
                                         t = counts[i]; counts[i] = counts[j]; counts[j] = t; }
        u64 cursor = 63;
        for (i = 0; i < n && nr < 5; i++) {
            if (starts[i] > cursor) { ranges[nr][0] = cursor; ranges[nr][1] = starts[i]; nr++; }
            if ((u64)starts[i] + counts[i] > cursor) cursor = (u64)starts[i] + counts[i];
        }
        if (cursor < cap_secs) { ranges[nr][0] = cursor; ranges[nr][1] = cap_secs; nr++; }

        for (i = 0; i < nr; i++) {
            u64 len = ranges[i][1] - ranges[i][0];
            if (len > best_sz) { best_sz = len; best = i; gap_begin = ranges[i][0]; }
        }
    }
    if (best < 0 || best_sz < 4200) {
        MessageBoxA(hwnd, "No free space large enough for a partition.", "Create Partition", MB_ICONWARNING);
        return -1;
    }
    for (i = 0; i < MAX_MBR_PARTS; i++) if (!g_vhd.parts[i].used) { slot = i; break; }
    if (slot < 0) { MessageBoxA(hwnd, "MBR partition table is full (4/4 used).", "Create Partition", MB_ICONWARNING); return -1; }

    {
        u64 len = best_sz;
        if (len > 0xFFFFFFFFu) len = 0xFFFFFFFFu;
        
        u8 type = force_type != 0 ? force_type : ((len >= 65528 * 63) ? 0x0B : 0x06);

        g_vhd.parts[slot].used = 1;
        g_vhd.parts[slot].type = type;
        g_vhd.parts[slot].boot = 0;
        g_vhd.parts[slot].lba_begin = (u32)gap_begin;
        g_vhd.parts[slot].lba_count = (u32)len;
        
        update_mbr_in_ram();

        char szs[32]; format_size(len * 512, szs, sizeof(szs));
        char msg[128];
        snprintf(msg, sizeof(msg), "Created %s partition (%s) in MBR slot %d.", part_type_name(type), szs, slot + 1);
        SetWindowTextA(g_hStatusBar, msg);
        
        return slot;
    }
}

static void part_delete(HWND hwnd, int slot) {
    if (slot < 0 || slot >= MAX_MBR_PARTS || !g_vhd.parts[slot].used) return;
    g_vhd.parts[slot].used = 0;
    if (g_vhd.fs_mounted && g_vhd.fs_part_lba == g_vhd.parts[slot].lba_begin) {
        g_vhd.fs_mounted = 0;
        g_view_mode = 0;
        g_ntfs = 0;
    }
    update_mbr_in_ram();
    populate_vhd_listview();
    SetWindowTextA(g_hStatusBar, "Partition deleted from MBR.");
}

/* ============================================================ FAT ENGINE */
int g_fat_type = 0;
u32 g_fat_lba = 0, g_root_lba = 0, g_data_lba = 0;
u32 g_sec_per_clus = 0, g_fat_size = 0, g_root_secs = 0;
u32 g_root_cluster = 0, g_total_clusters = 0;
u32 g_current_dir_cluster = 0;

#define FS_MAX_ENTRIES 4096
typedef struct {
    char name[256];
    int  is_directory;
    u64  size;
    u64  first_cluster;
} FsEntry;
static FsEntry g_fs_entries[FS_MAX_ENTRIES];
static int     g_fs_entry_count = 0;

static int read_sec(u32 lba, u8 *buf, u32 count) {
    if (!g_vhd.isOpen || !g_vhd.img) return -1;

    if (g_vhd.disk_type == 3) { /* Dynamic Sparse VHD */
        for (u32 i = 0; i < count; i++) {
            u32 cur_lba = lba + i;
            u32 blk = cur_lba / g_vhd.sec_per_block;
            u32 sec = cur_lba % g_vhd.sec_per_block;

            /* Intercept unallocated sparse blocks to prevent out-of-bounds reads */
            if (blk >= g_vhd.max_bat_entries || g_vhd.bat[blk] == 0xFFFFFFFF) {
                memset(buf + (i * 512), 0, 512);
            } else {
                u64 file_sec = (u64)g_vhd.bat[blk] + g_vhd.bitmap_secs + sec;
                u64 byte_off = file_sec * 512;
                if (byte_off + 512 <= (u64)g_vhd.img_bytes) {
                    memcpy(buf + (i * 512), g_vhd.img + byte_off, 512);
                } else {
                    memset(buf + (i * 512), 0, 512);
                }
            }
        }
    } else { /* Fixed VHD */
        u64 byte_off = g_vhd.data_offset + ((u64)lba * 512);
        if (byte_off + (count * 512) <= (u64)g_vhd.img_bytes) {
            memcpy(buf, g_vhd.img + byte_off, count * 512);
        } else {
            return -1;
        }
    }
    return 0;
}
static int write_sec(u32 lba, const u8 *buf, u32 count) {
    for (u32 i = 0; i < count; i++) {
        u64 file_off = 0;
        int res = vhd_translate_lba(lba + i, &file_off);
        if (res == 0) {
            memcpy(g_vhd.img + file_off, buf + i * 512, 512);
        } else {
            /* Writing to an unallocated sparse block without image expansion */
            return -1;
        }
    }
    return 0;
}

static u32 cluster_to_lba(u32 cluster) {
    if (cluster >= 2) return g_data_lba + (cluster - 2) * g_sec_per_clus;
    return g_root_lba;
}

static u32 read_fat(u32 cluster) {
    if (cluster < 2 || cluster > g_total_clusters + 1) return 0x0FFFFFFF;
    u8 sec[512];
    if (g_fat_type == 32) {
        u32 sec_off = (cluster * 4) / 512;
        u32 ent_off = (cluster * 4) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        return rd32le(sec + ent_off) & 0x0FFFFFFF;
    } else if (g_fat_type == 16) {
        u32 sec_off = (cluster * 2) / 512;
        u32 ent_off = (cluster * 2) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        u32 val = rd16le(sec + ent_off);
        if (val >= 0xFFF8) val = 0x0FFFFFFF;
        return val;
    }
    return 0x0FFFFFFF;
}

static void write_fat(u32 cluster, u32 val) {
    if (cluster < 2 || cluster > g_total_clusters + 1) return;
    u8 sec[512];
    if (g_fat_type == 32) {
        u32 sec_off = (cluster * 4) / 512;
        u32 ent_off = (cluster * 4) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        u32 cur = rd32le(sec + ent_off);
        cur = (cur & 0xF0000000) | (val & 0x0FFFFFFF);
        wr32le(sec + ent_off, cur);
        write_sec(g_fat_lba + sec_off, sec, 1);
    } else if (g_fat_type == 16) {
        u32 sec_off = (cluster * 2) / 512;
        u32 ent_off = (cluster * 2) % 512;
        read_sec(g_fat_lba + sec_off, sec, 1);
        wr16le(sec + ent_off, (u16)val);
        write_sec(g_fat_lba + sec_off, sec, 1);
    }
}

static u32 alloc_cluster(void) {
    for (u32 i = 2; i <= g_total_clusters + 1; i++) {
        if (read_fat(i) == 0) {
            write_fat(i, 0x0FFFFFFF);
            u8 z[512] = {0};
            for(u32 k=0; k<g_sec_per_clus; k++) write_sec(cluster_to_lba(i)+k, z, 1);
            return i;
        }
    }
    return 0;
}

static u32 get_chain_length(u32 clus) {
    u32 count = 0;
    while (clus >= 2 && clus < 0x0FFFFFF0) {
        count++;
        clus = read_fat(clus);
    }
    return count;
}

static int is_chain_fragmented(u32 clus) {
    if (clus < 2 || clus >= 0x0FFFFFF0) return 0;
    u32 prev = clus;
    clus = read_fat(clus);
    while (clus >= 2 && clus < 0x0FFFFFF0) {
        if (clus != prev + 1) return 1;
        prev = clus;
        clus = read_fat(clus);
    }
    return 0;
}

static u32 find_contiguous_free(u32 count) {
    u32 start = 0;
    u32 streak = 0;
    for (u32 i = 2; i <= g_total_clusters + 1; i++) {
        if (read_fat(i) == 0) {
            if (streak == 0) start = i;
            streak++;
            if (streak == count) return start;
        } else {
            streak = 0;
        }
    }
    return 0;
}

static int fs_mount_any(int part_slot) {
    if (!g_vhd.isOpen || !g_vhd.parts[part_slot].used) return -1;
    g_vhd.fs_part_lba = g_vhd.parts[part_slot].lba_begin;
    g_vhd.fs_part_nsec = g_vhd.parts[part_slot].lba_count;

    u8 bpb[512];
    if (read_sec(g_vhd.fs_part_lba, bpb, 1) != 0) return -2;
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) return -3;

    /* NTFS Detection */
    if (memcmp(bpb + 3, "NTFS    ", 8) == 0) {
        g_ntfs = 1;
        g_fat_type = 7;
        g_ntfs_part_lba = g_vhd.fs_part_lba;
        u16 bps = rd16le(bpb + 0x0B);
        g_ntfs_spc = bpb[0x0D];
        g_ntfs_clus_size = bps * g_ntfs_spc;
        g_ntfs_mft_lcn = rd64le(bpb + 0x30);
        g_ntfs_mft_mirr_lcn = rd64le(bpb + 0x38);

        int mft_sz = (char)bpb[0x40];
        if (mft_sz < 0) g_ntfs_mft_rec = 1 << (-mft_sz);
        else g_ntfs_mft_rec = mft_sz * g_ntfs_clus_size;
        
        int idx_sz = (char)bpb[0x44];
        if (idx_sz < 0) g_ntfs_idx_bytes = 1 << (-idx_sz);
        else g_ntfs_idx_bytes = idx_sz * g_ntfs_clus_size;
        
        g_ntfs_cur_dir = NTFS_MFT_ROOT;
        g_vhd.fs_mounted = 1;
        return 0;
    }

    g_ntfs = 0;
    u16 bytsPerSec = rd16le(bpb + 11);
    if (bytsPerSec != 512) return -4;
    g_sec_per_clus = bpb[13];
    u16 rsvdSecCnt = rd16le(bpb + 14);
    u8 numFATs = bpb[16];
    u16 rootEntCnt = rd16le(bpb + 17);
    u16 totSec16 = rd16le(bpb + 19);
    u32 totSec32 = rd32le(bpb + 32);
    u16 fatSz16 = rd16le(bpb + 22);

    u32 fatSz = fatSz16 ? fatSz16 : rd32le(bpb + 36);
    u32 totSec = totSec16 ? totSec16 : totSec32;
    g_fat_size = fatSz;

    g_root_secs = ((rootEntCnt * 32) + 511) / 512;
    g_fat_lba = g_vhd.fs_part_lba + rsvdSecCnt;
    g_root_lba = g_fat_lba + (numFATs * fatSz);
    g_data_lba = g_root_lba + g_root_secs;

    u32 dataSecs = totSec - (rsvdSecCnt + (numFATs * fatSz) + g_root_secs);
    g_total_clusters = dataSecs / g_sec_per_clus;

    if (g_total_clusters < 4085) return -5; 
    else if (g_total_clusters < 65525) g_fat_type = 16;
    else g_fat_type = 32;

    if (g_fat_type == 32) g_root_cluster = rd32le(bpb + 44);
    else g_root_cluster = 0;

    g_vhd.fs_mounted = 1;
    g_current_dir_cluster = g_root_cluster;
    return 0;
}

static void format_83_name(const u8 *src, char *dst) {
    int i, j = 0;
    for (i = 0; i < 8 && src[i] != ' '; i++) dst[j++] = src[i];
    if (src[8] != ' ') {
        dst[j++] = '.';
        for (i = 8; i < 11 && src[i] != ' '; i++) dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static int fs_list(u32 dir_cluster) {
    g_fs_entry_count = 0;
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;

        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0x00) goto done; 
            if (ent[0] == 0xE5) continue; 
            if (ent[11] == 0x0F) continue; 
            if (ent[11] & 0x08) continue; 

            FsEntry *fse = &g_fs_entries[g_fs_entry_count];
            format_83_name(ent, fse->name);
            fse->is_directory = (ent[11] & 0x10) ? 1 : 0;
            fse->size = rd32le(ent + 28);
            fse->first_cluster = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            g_fs_entry_count++;
            if (g_fs_entry_count >= FS_MAX_ENTRIES) goto done;
        }

        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
done:
    return 0;
}

static int find_free_dir_entry(u32 dir_cluster, u32 *out_lba, u32 *out_offset) {
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        read_sec(lba, sec, 1);
        for (int i = 0; i < 512; i += 32) {
            if (sec[i] == 0x00 || sec[i] == 0xE5) {
                *out_lba = lba;
                *out_offset = i;
                return 0;
            }
        }
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) return -1;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            u32 next = read_fat(cur);
            if (next >= 0x0FFFFFF8) {
                u32 nclus = alloc_cluster();
                if (!nclus) return -1;
                write_fat(cur, nclus);
                cur = nclus;
            } else {
                cur = next;
            }
        }
    }
    return -1;
}

static void make_83_name(const char *in, u8 *out) {
    memset(out, ' ', 11);
    int i=0, j=0;
    while(in[i] && in[i] != '.' && j < 8) { out[j++] = toupper(in[i++]); }
    while(in[i] && in[i] != '.') i++;
    if (in[i] == '.') {
        i++; j=8;
        while(in[i] && j < 11) { out[j++] = toupper(in[i++]); }
    }
}

static int fs_add_file(const char* host_path, const char* name, u32 parent_cluster) {
    FILE *f = fopen(host_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    u32 first_clus = 0;
    if (sz > 0) {
        first_clus = alloc_cluster();
        if (!first_clus) { fclose(f); return -2; }
        u32 cur_clus = first_clus;
        u32 rem = (u32)sz;
        u8 buf[512];
        while (rem > 0) {
            u32 lba = cluster_to_lba(cur_clus);
            for (u32 i = 0; i < g_sec_per_clus && rem > 0; i++) {
                u32 chunk = rem > 512 ? 512 : rem;
                memset(buf, 0, 512);
                fread(buf, 1, chunk, f);
                write_sec(lba + i, buf, 1);
                rem -= chunk;
            }
            if (rem > 0) {
                u32 nclus = alloc_cluster();
                if (!nclus) { fclose(f); return -3; }
                write_fat(cur_clus, nclus);
                cur_clus = nclus;
            }
        }
    }
    fclose(f);

    u32 lba, off;
    if (find_free_dir_entry(parent_cluster, &lba, &off) != 0) return -4;

    u8 sec[512];
    read_sec(lba, sec, 1);
    u8 *ent = sec + off;
    memset(ent, 0, 32);
    make_83_name(name, ent);
    ent[11] = 0x20;
    wr16le(ent + 20, first_clus >> 16);
    wr16le(ent + 26, first_clus & 0xFFFF);
    wr32le(ent + 28, (u32)sz);
    write_sec(lba, sec, 1);
    return 0;
}

static u32 fs_mkdir(const char* name, u32 parent_cluster) {
    u32 dclus = alloc_cluster();
    if (!dclus) return 0;
    u32 lba, off;
    if (find_free_dir_entry(parent_cluster, &lba, &off) != 0) return 0;

    u8 sec[512];
    read_sec(lba, sec, 1);
    u8 *ent = sec + off;
    memset(ent, 0, 32);
    make_83_name(name, ent);
    ent[11] = 0x10;
    wr16le(ent + 20, dclus >> 16);
    wr16le(ent + 26, dclus & 0xFFFF);
    write_sec(lba, sec, 1);

    u8 z[512] = {0};
    memset(z, ' ', 11); z[0] = '.'; z[11] = 0x10;
    wr16le(z + 20, dclus >> 16); wr16le(z + 26, dclus & 0xFFFF);
    
    memset(z+32, ' ', 11); z[32] = '.'; z[33] = '.'; z[43] = 0x10;
    u32 pclus = parent_cluster;
    if (g_fat_type == 32 && pclus == g_root_cluster) pclus = 0;
    wr16le(z + 32 + 20, pclus >> 16); wr16le(z + 32 + 26, pclus & 0xFFFF);

    write_sec(cluster_to_lba(dclus), z, 1);
    return dclus;
}

static int import_recursive(const char* host_path, u32 parent_cluster) {
    DWORD attr = GetFileAttributesA(host_path);
    if (attr == INVALID_FILE_ATTRIBUTES) return -1;
    char basename[MAX_PATH];
    const char* slash = strrchr(host_path, '\\');
    if (!slash) slash = strrchr(host_path, '/');
    strcpy(basename, slash ? slash + 1 : host_path);

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        u32 new_clus = fs_mkdir(basename, parent_cluster);
        if (!new_clus) return -1;
        char search[MAX_PATH];
        snprintf(search, sizeof(search), "%s\\*", host_path);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                char child[MAX_PATH];
                snprintf(child, sizeof(child), "%s\\%s", host_path, fd.cFileName);
                import_recursive(child, new_clus);
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    } else {
        fs_add_file(host_path, basename, parent_cluster);
    }
    return 0;
}

static int fs_extract(u32 entry_idx, const char* dest_path) {
    if (entry_idx >= g_fs_entry_count) return -1;
    FsEntry *fse = &g_fs_entries[entry_idx];
    if (fse->is_directory) return -2;

    FILE *f = fopen(dest_path, "wb");
    if (!f) return -3;

    u32 clus = (u32)fse->first_cluster;
    u32 rem = (u32)fse->size;
    u8 buf[512];

    while (rem > 0 && clus >= 2 && clus < 0x0FFFFFF0) {
        u32 lba = cluster_to_lba(clus);
        for (u32 i = 0; i < g_sec_per_clus && rem > 0; i++) {
            read_sec(lba + i, buf, 1);
            u32 chunk = rem > 512 ? 512 : rem;
            fwrite(buf, 1, chunk, f);
            rem -= chunk;
        }
        clus = read_fat(clus);
    }
    fclose(f);
    return 0;
}

static int fs_delete(u32 entry_idx) {
    if (entry_idx >= g_fs_entry_count) return -1;
    FsEntry *fse = &g_fs_entries[entry_idx];
    u8 sec[512];
    u32 cur = g_current_dir_cluster;
    int is_root16 = (g_fat_type == 16 && cur == 0);
    u32 sec_idx = 0, found = 0;

    while (!found) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        read_sec(lba, sec, 1);
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0) goto end_search;
            if (ent[0] == 0xE5) continue;
            u32 eclus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            if (eclus == fse->first_cluster) {
                ent[0] = 0xE5;
                memset(ent + 1, 0, 31); /* Securely wipe entire directory entry, excluding marker */
                write_sec(lba, sec, 1);
                found = 1; break;
            }
        }
        if (found) break;
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
end_search:
    if (found && fse->first_cluster) {
        u32 c = (u32)fse->first_cluster;
        u8 z[512] = {0};
        while(c >= 2 && c < 0x0FFFFFF0) {
            u32 n = read_fat(c);
            u32 lba = cluster_to_lba(c);
            for(u32 i=0; i<g_sec_per_clus; i++) write_sec(lba+i, z, 1);
            write_fat(c, 0);
            c = n;
        }
    }
    return 0;
}

static void fs_defrag_dir(u32 dir_cluster, int *moved_count) {
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;

        int modified = 0;
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0x00) {
                if (modified) write_sec(lba, sec, 1);
                return; 
            }
            if (ent[0] == 0xE5 || ent[11] == 0x0F) continue;

            char fname[13]; format_83_name(ent, fname);
            if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) continue;

            int is_dir = (ent[11] & 0x10);
            u32 fclus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);

            if (!is_dir && fclus >= 2) {
                u32 len = get_chain_length(fclus);
                if (len > 1 && is_chain_fragmented(fclus)) {
                    u32 new_start = find_contiguous_free(len);
                    if (new_start >= 2) {
                        for (u32 c = 0; c < len; c++) {
                            write_fat(new_start + c, (c == len - 1) ? 0x0FFFFFFF : (new_start + c + 1));
                        }
                        
                        u32 cur_old = fclus;
                        u32 c_idx = 0;
                        u8 cbuf[512];
                        while (cur_old >= 2 && cur_old < 0x0FFFFFF0) {
                            u32 lba_old = cluster_to_lba(cur_old);
                            u32 lba_new = cluster_to_lba(new_start + c_idx);
                            for (u32 s = 0; s < g_sec_per_clus; s++) {
                                read_sec(lba_old + s, cbuf, 1);
                                write_sec(lba_new + s, cbuf, 1);
                            }
                            cur_old = read_fat(cur_old);
                            c_idx++;
                        }
                        
                        wr16le(ent + 20, new_start >> 16);
                        wr16le(ent + 26, new_start & 0xFFFF);
                        write_sec(lba, sec, 1); 
                        modified = 0; 
                        
                        cur_old = fclus;
                        u8 z[512] = {0};
                        while (cur_old >= 2 && cur_old < 0x0FFFFFF0) {
                            u32 lba_old = cluster_to_lba(cur_old);
                            for (u32 s = 0; s < g_sec_per_clus; s++) write_sec(lba_old + s, z, 1);
                            u32 nxt = read_fat(cur_old);
                            write_fat(cur_old, 0);
                            cur_old = nxt;
                        }
                        
                        (*moved_count)++;
                        PumpMessages();
                    }
                }
            } else if (is_dir && fclus >= 2) {
                if (modified) { write_sec(lba, sec, 1); modified = 0; }
                fs_defrag_dir(fclus, moved_count);
            }
        }
        if (modified) write_sec(lba, sec, 1);

        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
}

static int fs_format_partition(int part_idx, int fat32) {
    if (!g_vhd.isOpen || !g_vhd.parts[part_idx].used) return -1;
    u64 secs = (u64)g_vhd.parts[part_idx].lba_count;
    if (secs < 2048) return -2;

    u32 spc = 8;
    if (secs > 65536) spc = 16;
    if (secs > 524288) spc = 32;
    if (secs > 1048576) spc = 64; 

    fat32 = fat32 || (secs > 65525 * spc);
    u8 bpb[512] = {0};
    
    bpb[0] = 0xEB; bpb[1] = 0x58; bpb[2] = 0x90;
    memcpy(bpb + 3, "MSWIN4.1", 8);
    wr16le(bpb + 11, 512); 
    bpb[13] = (u8)spc;
    wr16le(bpb + 14, fat32 ? 32 : 1);
    bpb[16] = 2;
    wr16le(bpb + 17, fat32 ? 0 : 512);
    wr16le(bpb + 19, (secs < 65536) ? (u16)secs : 0);
    bpb[21] = 0xF8;
    
    u32 root_secs = fat32 ? 0 : ((512 * 32) / 512);
    u32 tmp_data = secs - (fat32 ? 32 : 1) - root_secs;
    u32 fat_sz = (tmp_data / spc * (fat32 ? 4 : 2) / 512) + 1;
    
    wr16le(bpb + 22, fat32 ? 0 : (u16)fat_sz);
    wr16le(bpb + 24, 63); 
    wr16le(bpb + 26, 255); 
    wr32le(bpb + 28, g_vhd.parts[part_idx].lba_begin);
    wr32le(bpb + 32, (secs >= 65536) ? (u32)secs : 0);

    if (fat32) {
        if (g_vhd.parts[part_idx].type != 0x0C) g_vhd.parts[part_idx].type = 0x0B;
        wr32le(bpb + 36, fat_sz);
        wr16le(bpb + 40, 0); 
        wr16le(bpb + 42, 0); 
        wr32le(bpb + 44, 2); 
        wr16le(bpb + 48, 1); 
        wr16le(bpb + 50, 6); 
        bpb[64] = 0x80; 
        bpb[66] = 0x29; 
        wr32le(bpb + 67, 0x12345678); 
        memcpy(bpb + 71, "NO NAME    ", 11);
        memcpy(bpb + 82, "FAT32   ", 8);
    } else {
        if (g_vhd.parts[part_idx].type != 0x0E && g_vhd.parts[part_idx].type != 0x01) g_vhd.parts[part_idx].type = 0x06;
        bpb[36] = 0x80; 
        bpb[38] = 0x29; 
        wr32le(bpb + 39, 0x12345678); 
        memcpy(bpb + 43, "NO NAME    ", 11);
        memcpy(bpb + 54, "FAT16   ", 8);
    }
    bpb[510] = 0x55; bpb[511] = 0xAA;
    
    write_sec(g_vhd.parts[part_idx].lba_begin, bpb, 1);
    
    u8 z[512] = {0};
    u32 rsvd = fat32 ? 32 : 1;
    u32 fat_start = g_vhd.parts[part_idx].lba_begin + rsvd;
    
    z[0] = 0xF8; z[1] = 0xFF; z[2] = 0xFF;
    if (fat32) {
        z[3] = 0x0F; z[4] = 0xFF; z[5] = 0xFF; z[6] = 0xFF; z[7] = 0x0F; 
    } else {
        z[3] = 0xFF;
    }
    write_sec(fat_start, z, 1);
    write_sec(fat_start + fat_sz, z, 1);
    
    memset(z, 0, 512);
    for(u32 i=1; i<fat_sz; i++) {
        write_sec(fat_start + i, z, 1);
        write_sec(fat_start + fat_sz + i, z, 1);
    }
    
    if (!fat32) {
        u32 rd_start = fat_start + fat_sz * 2;
        for(u32 i=0; i<root_secs; i++) write_sec(rd_start + i, z, 1);
    } else {
        u32 rd_start = fat_start + fat_sz * 2; 
        for(u32 i=0; i<spc; i++) write_sec(rd_start + i, z, 1);
    }

    update_mbr_in_ram();
    return 0;
}

/* ============================================================ DATA LOSS EVALUATION */
static void scan_dir_for_lost(u32 dir_cluster, u32 max_cluster, const char* path, char* log, int* count) {
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;
        
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0x00) return;
            if (ent[0] == 0xE5) continue;
            if (ent[11] == 0x0F) continue;
            
            char fname[13]; format_83_name(ent, fname);
            if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) continue;
            
            u32 fclus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            int is_dir = (ent[11] & 0x10);
            
            int is_lost = 0;
            u32 c = fclus;
            while (c >= 2 && c < 0x0FFFFFF0) {
                if (c >= max_cluster) { is_lost = 1; break; }
                c = read_fat(c);
            }
            
            char full_path[MAX_PATH];
            snprintf(full_path, sizeof(full_path), "%s%s%s", path, strcmp(path, "\\") == 0 ? "" : "\\", fname);
            
            if (is_lost) {
                if (strlen(log) < 65000) {
                    strcat(log, full_path); strcat(log, "\r\n");
                }
                (*count)++;
            } else if (is_dir && fclus >= 2) {
                scan_dir_for_lost(fclus, max_cluster, full_path, log, count);
            }
        }
        
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
}

static void delete_lost_items(u32 dir_cluster, u32 max_cluster) {
    u8 sec[512];
    u32 cur = dir_cluster;
    int is_root16 = (g_fat_type == 16 && dir_cluster == 0);
    u32 sec_idx = 0;

    while (1) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;
        
        int modified = 0;
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0x00) goto write_back;
            if (ent[0] == 0xE5 || ent[11] == 0x0F) continue;
            
            char fname[13]; format_83_name(ent, fname);
            if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) continue;
            
            u32 fclus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
            int is_dir = (ent[11] & 0x10);
            
            int is_lost = 0;
            u32 c = fclus;
            while (c >= 2 && c < 0x0FFFFFF0) {
                if (c >= max_cluster) { is_lost = 1; break; }
                c = read_fat(c);
            }
            
            if (is_lost) {
                c = fclus;
                u8 z[512] = {0};
                while (c >= 2 && c < 0x0FFFFFF0) {
                    u32 n = read_fat(c);
                    u32 clba = cluster_to_lba(c);
                    for(u32 j=0; j<g_sec_per_clus; j++) write_sec(clba+j, z, 1);
                    write_fat(c, 0);
                    c = n;
                }
                ent[0] = 0xE5;
                memset(ent + 1, 0, 31);
                modified = 1;
            } else if (is_dir && fclus >= 2) {
                if (modified) { write_sec(lba, sec, 1); modified = 0; }
                delete_lost_items(fclus, max_cluster);
            }
        }
    write_back:
        if (modified) write_sec(lba, sec, 1);
        
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }
}

/* ============================================================ NATIVE NTFS ENGINE */
static const u8* ntfs_find_attr_named(const u8 *rec, u32 type, const char *name) {
    u16 attr_off = *(u16*)(rec + 0x14);
    const u8 *a = rec + attr_off;
    
    while (*(u32*)a != 0xFFFFFFFF) {
        if (*(u32*)a == type) {
            u8 name_len = a[0x09];
            u16 name_off = *(u16*)(a + 0x0A);
            
            /* Compare UTF-16LE name against standard ASCII string */
            if (name_len == strlen(name)) {
                int match = 1;
                for (int i = 0; i < name_len; i++) {
                    if (a[name_off + (i * 2)] != name[i] || a[name_off + (i * 2) + 1] != 0) {
                        match = 0; break;
                    }
                }
                if (match) return a;
            }
        }
        u32 len = *(u32*)(a + 4);
        if (len == 0) break;
        a += len;
    }
    return NULL;
}
static int ntfs_read_mft_record(u64 rec, u8 *buf) {
    if (!g_ntfs) return -1;
    
    /* Strip the 16-bit sequence number to prevent catastrophic integer overflow */
    u64 safe_rec = rec & 0x0000FFFFFFFFFFFFULL;
    
    u64 byte_off = g_ntfs_mft_lcn * g_ntfs_clus_size + safe_rec * g_ntfs_mft_rec;
    u32 sec = (u32)(g_ntfs_part_lba + byte_off / 512);
    u32 nsecs = (u32)((g_ntfs_mft_rec + 511) / 512);
    if (nsecs > 16) return -2;

    u8 tmp[8192];
    if (read_sec(sec, tmp, nsecs) != 0) return -3;
    memcpy(buf, tmp + (byte_off % 512), (u32)g_ntfs_mft_rec);

    if (ntfs_apply_fixups(buf, (u32)g_ntfs_mft_rec) != 0) return -5;
    return 0;
}

static int ntfs_write_mft_record(u64 rec, const u8 *buf_in) {
    u8 buf[8192];
    u32 rsz = (u32)g_ntfs_mft_rec;
    if (rsz > sizeof(buf)) return -1;
    memcpy(buf, buf_in, rsz);
    
    u16 usa_off = rd16le(buf + 4);
    u16 usa_cnt = rd16le(buf + 6);
    u32 secs_in_rec = rsz / 512;
    
    if (usa_cnt == secs_in_rec + 1) {
        u16 usn = (u16)(rd16le(buf + usa_off) + 1);
        if (usn == 0) usn = 1; 
        wr16le(buf + usa_off, usn);
        for (u32 i = 0; i < secs_in_rec; i++) {
            u8 *tail = buf + i * 512 + 510;
            wr16le(buf + usa_off + 2 + i * 2, rd16le(tail));
            wr16le(tail, usn);
        }
    }
    
    /* Strip the 16-bit sequence number */
    u64 safe_rec = rec & 0x0000FFFFFFFFFFFFULL;
    u64 byte_off = g_ntfs_mft_lcn * g_ntfs_clus_size + safe_rec * g_ntfs_mft_rec;
    u32 sec = (u32)(g_ntfs_part_lba + byte_off / 512);
    u8 tmp[8192];
    u32 nsecs = rsz / 512;
    
    if (read_sec(sec, tmp, nsecs) != 0) return -2;
    memcpy(tmp, buf, rsz);
    int rc = write_sec(sec, tmp, nsecs);

    if (rc == 0 && safe_rec < 4 && g_ntfs_mft_mirr_lcn > 0) {
        u64 mbyte_off = g_ntfs_mft_mirr_lcn * g_ntfs_clus_size + safe_rec * g_ntfs_mft_rec;
        u32 msec = (u32)(g_ntfs_part_lba + mbyte_off / 512);
        if (read_sec(msec, tmp, nsecs) == 0) {
            memcpy(tmp, buf, rsz);
            write_sec(msec, tmp, nsecs);
        }
    }
    return rc;
}

static const u8* ntfs_first_attr(const u8 *rec) {
    return rec + rd16le(rec + 0x14);
}

static const u8* ntfs_next_attr(const u8 *rec, const u8 *attr) {
    (void)rec;
    u32 len = rd32le(attr + 4);
    if (len == 0) return NULL;
    const u8 *next = attr + len;
    if (rd32le(next) == 0xFFFFFFFF) return NULL;
    return next;
}

static const u8* ntfs_find_attr(const u8 *rec, u32 type, int instance) {
    const u8 *a = ntfs_first_attr(rec);
    int i = 0;
    while (a) {
        if (rd32le(a) == type) {
            if (instance < 0 || i == instance) return a;
            i++;
        }
        a = ntfs_next_attr(rec, a);
    }
    return NULL;
}

static int ntfs_run_next(const u8 *runs, int *pos, s64 *lcn_acc, u64 *len_out, s64 *lcn_out) {
    u8 h = runs[*pos];
    if (h == 0) return 0;
    int lb = h & 0x0F;
    int ob = (h >> 4) & 0x0F;
    if (lb == 0 || lb > 8 || ob > 8) return -1;

    u64 rlen = 0;
    for (int i = 0; i < lb; i++) rlen |= (u64)runs[*pos + 1 + i] << (8 * i);
    *pos += 1 + lb;

    if (ob == 0) {
        *lcn_out = -1;
    } else {
        u64 delta = 0;
        for (int i = 0; i < ob; i++) delta |= (u64)runs[*pos + i] << (8 * i);
        *pos += ob;
        if (delta >> (8 * ob - 1))
            delta |= (~0ULL) << (8 * ob);
        *lcn_acc += (s64)delta;
        *lcn_out = *lcn_acc;
    }
    *len_out = rlen;
    return 1;
}

static int ntfs_read_attr_range(const u8 *rec, const u8 *attr, u64 off, u64 want, u8 *out) {
    if (!(attr[8] & 1)) {
        u16 vlen = rd16le(attr + 0x10);
        u16 voff = rd16le(attr + 0x14);
        if (off >= vlen) return 0;
        u64 n = want < (u64)(vlen - off) ? want : (vlen - off);
        memcpy(out, attr + voff + off, (size_t)n);
        return (int)n;
    }

    u16 run_off = rd16le(attr + 0x20);
    u64 real_size = rd64le(attr + 0x30);
    if (off >= real_size) return 0;
    if (off + want > real_size) want = real_size - off;

    const u8 *runs = attr + run_off;
    int pos = 0;
    s64 lcn_acc = 0;
    u64 vcn = off / g_ntfs_clus_size;
    u64 done = 0, walk_vcn = 0;

    for (;;) {
        u64 rlen; s64 lcn;
        int r = ntfs_run_next(runs, &pos, &lcn_acc, &rlen, &lcn);
        if (r <= 0) break;
        u64 run_end = walk_vcn + rlen;
        
        if (run_end > vcn && done < want) {
            u64 skip = (vcn > walk_vcn) ? (vcn - walk_vcn) : 0;
            for (u64 c = skip; c < rlen && done < want; c++) {
                u64 in_off = ((walk_vcn + c) == vcn) ? (off % g_ntfs_clus_size) : 0;
                u64 n = want - done;
                if (n > g_ntfs_clus_size - in_off) n = g_ntfs_clus_size - in_off;
                
                if (lcn < 0) {
                    memset(out + done, 0, (size_t)n);
                } else {
                    u64 lba = g_ntfs_part_lba + (u64)(lcn + c) * g_ntfs_spc;
                    u32 sec = (u32)(lba + in_off / 512);
                    
                    if (in_off % 512 == 0 && n % 512 == 0) {
                        /* Fast path: sector aligned */
                        if (read_sec(sec, out + done, (u32)(n / 512)) != 0) break;
                    } else {
                        /* Safe path: Unaligned tail/head using 512b bounce */
                        u8 bounce[512];
                        u32 chunk = (u32)n;
                        u32 b_off = (u32)(in_off % 512);
                        u32 out_p = 0;
                        while (chunk > 0) {
                            if (read_sec(sec++, bounce, 1) != 0) break;
                            u32 to_copy = 512 - b_off;
                            if (to_copy > chunk) to_copy = chunk;
                            memcpy(out + done + out_p, bounce + b_off, to_copy);
                            chunk -= to_copy;
                            out_p += to_copy;
                            b_off = 0;
                        }
                    }
                }
                done += n;
            }
        }
        if (done >= want) break;
        walk_vcn = run_end;
    }
    return (int)done;
}

static int ntfs_stat_record(u64 mft_ref, int *is_dir, u64 *size) {
    u8 rec[4096];
    if (mft_ref > 0xFFFFFFFFULL) return -1;
    if (ntfs_read_mft_record(mft_ref, rec) != 0) return -2;
    if (!(rd16le(rec + 0x16) & NTFS_FL_IN_USE)) return -3;
    *is_dir = (rd16le(rec + 0x16) & NTFS_FL_IS_DIR) ? 1 : 0;
    *size = 0;
    const u8 *data = ntfs_find_attr(rec, NTFS_AT_DATA, 0);
    if (data) {
        if (data[8] & 1) *size = rd64le(data + 0x30);
        else             *size = rd16le(data + 0x10);
    }
    return 0;
}

static void ntfs_utf16_to_ascii(const u8 *src, u8 len_chars, char *dst) {
    int j = 0;
    for (int i = 0; i < len_chars && j < 250; i++) {
        u16 w = rd16le(src + i * 2);
        dst[j++] = (w < 128) ? (char)w : '_';
    }
    dst[j] = '\0';
}

static int ntfs_index_entry_read(const u8 *e, FsEntry *out) {
    /*
       NTFS Index Entry Layout:
       0x00: MFT Reference (8 bytes)
       0x08: Length of this index entry (2 bytes)
       0x0A: Length of the key / $FILE_NAME payload (2 bytes)
       0x0C: Flags (2 bytes) -> 0x01 = sub-node pointer, 0x02 = last entry in node
       0x10: Key payload ($FILE_NAME body directly, NO attribute header)
    */
    u16 step    = rd16le(e + 0x08);
    u16 key_len = rd16le(e + 0x0A);
    u16 flags   = rd16le(e + 0x0C);
    
    if (step < 0x10) return 0; /* Prevent infinite loops on corrupt data */
    
    /* The last entry in a node is a dummy entry (flag 0x02). It has no filename. */
    if (flags & 0x02) return 0;
    if (key_len == 0) return 0;

    /* Extract lower 48 bits of the MFT reference (stripping the sequence number) */
    u64 mft_ref = rd64le(e) & 0x0000FFFFFFFFFFFFULL; 

    /* fv points directly to the $FILE_NAME payload */
    const u8 *fv = e + 0x10; 
    
    u8 nl = fv[0x40]; /* Name length in characters */
    u8 ns = fv[0x41]; /* Namespace */
    
    /* Skip DOS-only names (namespace 2) to avoid duplicates in the UI */
    if (nl == 0 || ns == 2) return 0; 

    memset(out, 0, sizeof(*out));
    ntfs_utf16_to_ascii(fv + 0x42, nl, out->name);
    out->first_cluster = mft_ref;
    
    /* Offset 0x38 in $FILE_NAME payload contains file attributes */
    out->is_directory = (rd32le(fv + 0x38) & 0x10000000) ? 1 : 0;
    out->size = rd64le(fv + 0x30); /* Real file size */
    
    return (nl > 0);
}

static int ntfs_indx_fixup_and_parse(u8 *blk, u32 blk_size) {
    (void)blk_size;
    if (ntfs_apply_fixups(blk, g_ntfs_idx_bytes) != 0) return -1;

    u32 entries_off = rd32le(blk + 0x18);
    u32 end_used    = rd32le(blk + 0x1C);

    const u8 *base = blk + 0x18;
    const u8 *e = base + entries_off;
    const u8 *end = base + end_used;
    
    if (end > blk + g_ntfs_idx_bytes) end = blk + g_ntfs_idx_bytes;

    while (e + 0x10 <= end) {
        u16 step = rd16le(e + 0x08);
        u16 flags = rd16le(e + 0x0C);
        
        if (step < 0x10 || e + step > end) break;
        
        if (!(flags & 0x02)) {
            FsEntry ne;
            if (ntfs_index_entry_read(e, &ne)) {
                if (strcmp(ne.name, ".") != 0 && strcmp(ne.name, "..") != 0) {
                    if (g_fs_entry_count < FS_MAX_ENTRIES) {
                        g_fs_entries[g_fs_entry_count++] = ne;
                    }
                }
            }
        }
        if (flags & 0x02) break; /* End of this index block */
        e += step;
    }
    return 0;
}

static int ntfs_list_dir(u64 dir_ref) {
    g_fs_entry_count = 0;
    u8 rec[4096];
    if (ntfs_read_mft_record(dir_ref, rec) != 0) return -1;

    /* 1. Parse Resident $INDEX_ROOT (Small directories) */
    const u8 *ir = ntfs_find_attr(rec, NTFS_AT_INDEX_ROOT, 0);
    if (ir && !(ir[8] & 1)) {
        u32 attr_len = rd32le(ir + 4);
        u16 voff     = rd16le(ir + 0x14);
        
        /* Strict bounds checking to prevent crashes on corrupted MFTs */
        if (voff + 0x20 <= attr_len) {
            const u8 *rv   = ir + voff;
            const u8 *base = rv + 0x10; /* Start of Index Node Header */
            
            u32 ents_off = rd32le(base + 0x00); 
            u32 end_off  = rd32le(base + 0x04); 
            
            const u8 *e   = base + ents_off;
            const u8 *end = base + end_off;
            
            if (end > ir + attr_len) end = ir + attr_len;

            while (e + 0x10 <= end) {
                u16 step  = rd16le(e + 0x08);
                u16 flags = rd16le(e + 0x0C);
                
                if (step < 0x10 || e + step > end) break;
                
                /* flag 0x02 indicates the last dummy entry in the node */
                if (!(flags & 0x02)) {
                    FsEntry ne;
                    if (ntfs_index_entry_read(e, &ne)) {
                        if (strcmp(ne.name, ".") != 0 && strcmp(ne.name, "..") != 0) {
                            if (g_fs_entry_count < FS_MAX_ENTRIES) {
                                g_fs_entries[g_fs_entry_count++] = ne;
                            }
                        }
                    }
                }
                
                if (flags & 0x02) break; /* Reached end of node list */
                e += step;
            }
        }
    }

    /* 2. Parse Non-Resident $INDEX_ALLOCATION (Large directories / INDX blocks) */
    const u8 *ia = ntfs_find_attr(rec, NTFS_AT_INDEX_ALLOC, 0);
    if (ia && (ia[8] & 1)) {
        u64 total = rd64le(ia + 0x30); 
        
        /* Allocate EXACTLY one INDX block at a time instead of a 256KB static array. 
           This supports unlimited directory sizes and avoids stack/BSS bloat. */
        u8 *idx_block = (u8*)malloc(g_ntfs_idx_bytes);
        if (idx_block) {
            for (u64 offset = 0; offset + g_ntfs_idx_bytes <= total; offset += g_ntfs_idx_bytes) {
                
                int got = ntfs_read_attr_range(rec, ia, offset, g_ntfs_idx_bytes, idx_block);
                
                if (got == (int)g_ntfs_idx_bytes) {
                    /* Process block. ntfs_indx_fixup_and_parse must apply the 512-byte fixups 
                       internally and verify the "INDX" signature before trusting the block. */
                    ntfs_indx_fixup_and_parse(idx_block, 0);
                } else {
                    break; /* Truncated read or I/O error */
                }
            }
            free(idx_block);
        }
    }
    
    return g_fs_entry_count;
}

static int ntfs_extract_file(u64 mft_ref, const char *dest_path) {
    u8 rec[4096];
    if (ntfs_read_mft_record(mft_ref, rec) != 0) return -1;
    const u8 *data = ntfs_find_attr(rec, NTFS_AT_DATA, 0);
    if (!data) return -2;

    FILE *f = fopen(dest_path, "wb");
    if (!f) return -3;

    u8 buf[65536];
    if (!(data[8] & 1)) {
        u16 voff = rd16le(data + 0x14);
        u16 vlen = rd16le(data + 0x10);
        fwrite(data + voff, 1, vlen, f);
    } else {
        u64 total = rd64le(data + 0x30);
        u64 done = 0;
        while (done < total) {
            u64 want = total - done;
            if (want > sizeof(buf)) want = sizeof(buf);
            int got = ntfs_read_attr_range(rec, data, done, want, buf);
            if (got <= 0) break;
            fwrite(buf, 1, (size_t)got, f);
            done += (u64)got;
        }
    }
    fclose(f);
    return 0;
}

static int ntfs_extract_recursive(u64 mft_ref, const char *host_dir) {
    int is_dir; u64 size;
    if (ntfs_stat_record(mft_ref, &is_dir, &size) != 0) return -1;

    if (is_dir) {
        CreateDirectoryA(host_dir, NULL);
        ntfs_list_dir(mft_ref);
        for (int i = 0; i < g_fs_entry_count; i++) {
            char child[MAX_PATH];
            snprintf(child, sizeof(child), "%s\\%s", host_dir, g_fs_entries[i].name);
            if (g_fs_entries[i].is_directory)
                ntfs_extract_recursive(g_fs_entries[i].first_cluster, child);
            else {
                char fp[MAX_PATH];
                snprintf(fp, sizeof(fp), "%s\\%s", host_dir, g_fs_entries[i].name);
                ntfs_extract_file(g_fs_entries[i].first_cluster, fp);
            }
        }
    } else {
        char fp[MAX_PATH];
        snprintf(fp, sizeof(fp), "%s\\unnamed_%I64u", host_dir, mft_ref);
        ntfs_extract_file(mft_ref, fp);
    }
    return 0;
}

static u64 ntfs_alloc_mft_record(void) {
    u8 mrec[4096];
    if (ntfs_read_mft_record(0, mrec) != 0) return 0; /* Read $MFT */
    
    const u8 *bmp = ntfs_find_attr(mrec, 0xB0, 0); 
    if (!bmp || (bmp[8] & 1)) return 0;
    
    u16 voff = rd16le(bmp + 0x14);
    u32 vlen = rd32le(bmp + 0x10);
    u8 *b = (u8*)bmp + voff;
    
    u32 total = vlen * 8;
    for (u32 r = 24; r < total; r++) {
        if (!(b[r / 8] & (1 << (r % 8)))) {
            /* Claim the record in the bitmap */
            b[r / 8] |= (1 << (r % 8));
            ntfs_write_mft_record(0, mrec);
            return r;
        }
    }
    return 0;
}

static u64 ntfs_alloc_clusters(u64 num_clusters, u64 *out_lcn) {
    u8 b_rec[4096];
    if (ntfs_read_mft_record(6, b_rec) != 0) return 0; /* MFT 6 = $Bitmap */
    
    const u8 *b_data = ntfs_find_attr(b_rec, NTFS_AT_DATA, 0);
    if (!b_data) return 0;

    u64 b_size = (b_data[8] & 1) ? rd64le(b_data + 0x30) : rd32le(b_data + 0x10);
    u8 *bitmap = (u8*)malloc((size_t)b_size);
    if (!bitmap) return 0;

    u64 done = 0;
    while (done < b_size) {
        u64 want = b_size - done;
        if (want > 65536) want = 65536;
        int got = ntfs_read_attr_range(b_rec, b_data, done, want, bitmap + done);
        if (got <= 0) break;
        done += got;
    }

    u64 total_clusters = b_size * 8;
    u64 found_start = 0;
    u64 consecutive = 0;
    int found = 0;

    /* Scan past system reserved clusters (0..23) */
    for (u64 c = 24; c < total_clusters; c++) {
        if (!(bitmap[c / 8] & (1 << (c % 8)))) {
            if (consecutive == 0) found_start = c;
            consecutive++;
            if (consecutive == num_clusters) { found = 1; break; }
        } else {
            consecutive = 0;
        }
    }

    if (!found) { free(bitmap); return 0; }

    /* Mark clusters as allocated in memory bitmap */
    for (u64 c = found_start; c < found_start + num_clusters; c++) {
        bitmap[c / 8] |= (1 << (c % 8));
    }

    /* Write updated $Bitmap back to disk */
    if (b_data[8] & 1) {
        u16 b_run_off = rd16le(b_data + 0x20);
        const u8 *b_runs = b_data + b_run_off;
        int pos = 0; s64 lcn_acc = 0; u64 t_len; s64 t_lcn; u64 b_done = 0;
        while (ntfs_run_next(b_runs, &pos, &lcn_acc, &t_len, &t_lcn) > 0) {
            if (t_lcn >= 0 && b_done < b_size) {
                u64 lba = g_ntfs_part_lba + t_lcn * g_ntfs_spc;
                u64 bytes = t_len * g_ntfs_clus_size;
                if (b_done + bytes > b_size) bytes = b_size - b_done;
                write_sec((u32)lba, bitmap + b_done, (u32)((bytes + 511) / 512));
                b_done += bytes;
            }
        }
    }
    free(bitmap);
    *out_lcn = found_start;
    return num_clusters;
}
static int ntfs_index_insert(u8 *mft_rec, u64 parent_ref, const char *name, u64 child_ref, int is_dir) {
    u8 *p = mft_rec + rd16le(mft_rec + 0x14);
    u8 *idx_root = NULL;
    
    /* 1. Find the $INDEX_ROOT attribute */
    while (rd32le(p) != 0xFFFFFFFF) {
        if (rd32le(p) == 0x90) { idx_root = p; break; }
        p += rd32le(p + 4);
    }
    if (!idx_root) return -1;

    /* 2. Dynamically calculate the offset to the Node Header */
    u16 content_offset = rd16le(idx_root + 0x14);
    u8 *node_hdr = idx_root + content_offset + 0x10;
    u32 first_entry_off = rd32le(node_hdr + 0x00);
    u32 total_size = rd32le(node_hdr + 0x04);
    
    /* 3. Walk the index to find the End of Index (Dummy) entry */
    u8 *dummy = node_hdr + first_entry_off;
    while (!(rd16le(dummy + 0x0C) & 0x02)) {
        dummy += rd16le(dummy + 0x08);
    }
    
    /* 4. Calculate size of the new entry (8-byte aligned) */
    int name_len = 0;
    int has_dot = 0;
    while (name[name_len]) {
        if (name[name_len] == '.') has_dot = 1;
        name_len++;
    }
    
    u32 fn_len = 66 + (name_len * 2);
    u32 entry_len = (16 + fn_len + 7) & ~7; 
    
    /* 5. CRITICAL FIX: Shift everything from dummy to the end of the used MFT record */
    u32 bytes_in_use = rd32le(mft_rec + 0x18);
    u32 remaining = bytes_in_use - (u32)(dummy - mft_rec);
    memmove(dummy + entry_len, dummy, remaining);
    
    /* 6. Populate the new Index Entry */
    u8 *e = dummy;
    memset(e, 0, entry_len);
    wr64le(e + 0x00, child_ref);
    wr16le(e + 0x08, entry_len);
    wr16le(e + 0x0A, fn_len);
    wr16le(e + 0x0C, 0); 
    
    /* 7. Populate the inner File Name attribute */
    u8 *fn = e + 16;
    wr64le(fn + 0x00, parent_ref);
    u64 ntfs_time = (u64)time(NULL) * 10000000ULL + 116444736000000000ULL;
    wr64le(fn + 0x08, ntfs_time);
    wr64le(fn + 0x10, ntfs_time);
    wr64le(fn + 0x18, ntfs_time);
    wr64le(fn + 0x20, ntfs_time);
    wr64le(fn + 0x28, 0); 
    wr64le(fn + 0x30, 0); 
    wr32le(fn + 0x38, is_dir ? (0x10000000 | 0x16) : 0x06);
    fn[0x40] = name_len;
    fn[0x41] = (name_len > 8 && !has_dot) ? 0x01 : 0x03; 
    
    for (int i = 0; i < name_len; i++) {
        wr16le(fn + 0x42 + (i * 2), (unsigned char)name[i]);
    }
    
    /* 8. Safely update tree sizes and MFT usage */
    wr32le(node_hdr + 0x04, total_size + entry_len);
    
    u32 attr_len = rd32le(idx_root + 4);
    u32 attr_content_len = rd32le(idx_root + 0x10);
    wr32le(idx_root + 4, attr_len + entry_len);
    wr32le(idx_root + 0x10, attr_content_len + entry_len);
    
    /* Update overall MFT bytes in use, ensuring 8-byte alignment */
    bytes_in_use += entry_len;
    wr32le(mft_rec + 0x18, (bytes_in_use + 7) & ~7);
    
    return 0;
}

static int ntfs_add_file(const char *host_path, const char *name, u64 parent_ref) {
    FILE *fp = fopen(host_path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz < 0) { fclose(fp); return -1; }

    u64 rec_no = ntfs_alloc_mft_record();
    if (!rec_no) { fclose(fp); return -3; }

    u8 rec[8192];
    /* CRITICAL FIX: Cast and pass the newly allocated MFT record number */
    ntfs_format_init_record(rec, (u32)rec_no, NTFS_FL_IN_USE);
    
    u8 *p = rec + 0x38;
    u64 ntfs_time = (u64)time(NULL) * 10000000ULL + 116444736000000000ULL;

    p = ntfs_add_attr_std_info(p, ntfs_time, 0x20); /* Archive Flag */
    p = ntfs_add_attr_file_name(p, parent_ref, name, ntfs_time, 0x20, 0, 0, 0x03);

    if (sz <= 500) {
        /* Branch A: Resident File (Fits entirely inside MFT Record) */
        u32 data_len = (0x18 + (u32)sz + 7) & ~7u;
        wr32le(p + 0, NTFS_AT_DATA);
        wr32le(p + 4, data_len);
        p[8] = 0; p[9] = 0;
        wr16le(p + 0x10, (u16)sz);
        wr16le(p + 0x14, 0x18);
        if (sz > 0) fread(p + 0x18, 1, (size_t)sz, fp);
        p += data_len;
    } else {
        /* Branch B: Non-Resident File (Allocates clusters via $Bitmap) */
        u64 clusters_needed = ((u64)sz + g_ntfs_clus_size - 1) / g_ntfs_clus_size;
        u64 start_lcn = 0;
        
        if (ntfs_alloc_clusters(clusters_needed, &start_lcn) == 0) {
            fclose(fp);
            return -4; /* Disk full */
        }

        u8 *clus_buf = (u8*)calloc((size_t)clusters_needed, g_ntfs_clus_size);
        if (clus_buf) {
            fread(clus_buf, 1, (size_t)sz, fp);
            u32 sec = (u32)(g_ntfs_part_lba + start_lcn * g_ntfs_spc);
            u32 total_secs = (u32)((clusters_needed * g_ntfs_clus_size + 511) / 512);
            write_sec(sec, clus_buf, total_secs);
            free(clus_buf);
        }

        p = ntfs_add_attr_data_nonres(p, clusters_needed, start_lcn, (u64)sz);
    }
    fclose(fp);

    wr32le(p, 0xFFFFFFFF); /* Attribute list terminator */
    u32 bytes_in_use = (u32)(p + 4 - rec);
bytes_in_use = (bytes_in_use + 7) & ~7; /* Force 8-byte alignment */
wr32le(rec + 0x18, bytes_in_use);

    if (ntfs_write_mft_record(rec_no, rec) != 0) return -5;

    /* Insert filename into Parent Directory B-Tree index */
    u8 prec[8192];
    if (ntfs_read_mft_record(parent_ref, prec) == 0) {
        ntfs_index_insert(prec, parent_ref, name, rec_no, 0);
        ntfs_write_mft_record(parent_ref, prec);
    }
    return 0;
}

static u64 ntfs_mkdir(const char *name, u64 parent_ref) {
    u64 rec_no = ntfs_alloc_mft_record();
    if (!rec_no) return 0;

    u8 rec[8192];
    /* CRITICAL FIX: Cast and pass the newly allocated MFT record number */
    ntfs_format_init_record(rec, (u32)rec_no, NTFS_FL_IN_USE | NTFS_FL_IS_DIR);
    
    u8 *p = rec + 0x38;
    u64 ntfs_time = (u64)time(NULL) * 10000000ULL + 116444736000000000ULL;

    p = ntfs_add_attr_std_info(p, ntfs_time, 0x10); /* Directory flag */
    p = ntfs_add_attr_file_name(p, parent_ref, name, ntfs_time, 0x10000000 | 0x10, 0, 0, 0x03);
    p = ntfs_add_attr_index_root(p);

    wr32le(p, 0xFFFFFFFF);
    u32 bytes_in_use = (u32)(p + 4 - rec);
bytes_in_use = (bytes_in_use + 7) & ~7; /* Force 8-byte alignment */
wr32le(rec + 0x18, bytes_in_use);

    if (ntfs_write_mft_record(rec_no, rec) != 0) return 0;

    u8 prec[8192];
    if (ntfs_read_mft_record(parent_ref, prec) == 0) {
        if (ntfs_index_insert(prec, parent_ref, name, rec_no, 1) == 0)
            ntfs_write_mft_record(parent_ref, prec);
    }
    return rec_no;
}

static int ntfs_import_recursive(const char* host_path, u64 parent_ref) {
    DWORD attr = GetFileAttributesA(host_path);
    if (attr == INVALID_FILE_ATTRIBUTES) return -1;
    char basename[MAX_PATH];
    const char* slash = strrchr(host_path, '\\');
    if (!slash) slash = strrchr(host_path, '/');
    strcpy(basename, slash ? slash + 1 : host_path);

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        u64 new_dir = ntfs_mkdir(basename, parent_ref);
        if (!new_dir) return -1;
        char search[MAX_PATH];
        snprintf(search, sizeof(search), "%s\\*", host_path);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                char child[MAX_PATH];
                snprintf(child, sizeof(child), "%s\\%s", host_path, fd.cFileName);
                ntfs_import_recursive(child, new_dir);
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        return 0;
    }
    return ntfs_add_file(host_path, basename, parent_ref);
}

static void ntfs_format_init_record(u8 *rec, u32 mft_index, u16 flags) {
    memset(rec, 0, 1024);
    memcpy(rec, "FILE", 4);
    
    *(u16*)(rec + 0x04) = 0x30; /* USA Offset */
    *(u16*)(rec + 0x06) = 3;    /* USA Count (1024 / 512 + 1) */
    *(u64*)(rec + 0x08) = 0;    /* LSN */
    
    u16 seq_num = (mft_index >= 2 && mft_index <= 15) ? (u16)mft_index : 1;
    *(u16*)(rec + 0x10) = seq_num;
    
    *(u16*)(rec + 0x12) = 1;    /* Hard link count */
    *(u16*)(rec + 0x14) = 0x38; /* First attribute offset */
    *(u16*)(rec + 0x16) = flags;/* Flags (0x01 In-Use, 0x02 Dir) */
    
    /* CORRECTED: Force 8-byte alignment immediately */
    *(u32*)(rec + 0x18) = 0x40; /* Bytes In Use (0x38 + 8 bytes padding) */
    *(u32*)(rec + 0x1C) = 1024; /* Allocated Size */
    *(u64*)(rec + 0x20) = 0;    /* Base File Record */
    *(u16*)(rec + 0x28) = 1;    /* Next Attribute ID */
    *(u32*)(rec + 0x2C) = mft_index;/* MFT Record Number */
    
    /* REMOVED: Premature USN injection at 0x1FE/0x3FE */
    
    *(u32*)(rec + 0x38) = 0xFFFFFFFF; /* End of attributes marker */
}

static int ntfs_encode_run(u8 *out, u64 len, s64 lcn) {
    int lb = 0;
    u64 tlen = len;
    while (tlen > 0) { lb++; tlen >>= 8; }
    if (lb == 0) lb = 1;

    int ob = 1;
    s64 tlcn = lcn;
    if (tlcn >= 0) {
        while ((tlcn >> (ob * 8 - 1)) > 0 && ob < 8) ob++;
    } else {
        while ((tlcn >> (ob * 8 - 1)) < -1 && ob < 8) ob++;
    }

    out[0] = (u8)((ob << 4) | lb);
    int pos = 1;
    for (int i = 0; i < lb; i++) out[pos++] = (u8)(len >> (i * 8));
    for (int i = 0; i < ob; i++) out[pos++] = (u8)(lcn >> (i * 8));
    out[pos++] = 0x00;
    return pos;
}

static int ntfs_get_security_descriptor(u32 target_sec_id, u8 *out_sd, u32 max_sd_len) {
    if (target_sec_id == 0) return 0; /* ID 0 means no explicit security applied */

    u8 rec[4096];
    if (ntfs_read_mft_record(9, rec) != 0) return -1; /* MFT 9 is always $Secure */

    /* Find the $DATA attribute named "$SDS" */
    const u8 *sds = ntfs_find_attr_named(rec, NTFS_AT_DATA, "$SDS"); 
    if (!sds) return -2;

    u64 total_size = (sds[8] & 1) ? *(u64*)(sds + 0x30) : *(u32*)(sds + 0x10);
    
    u32 chunk_size = 65536; 
    u8 *chunk = (u8*)malloc(chunk_size);
    if (!chunk) return -4;

    u64 offset = 0;
    while (offset < total_size) {
        u32 want = (total_size - offset > chunk_size) ? chunk_size : (u32)(total_size - offset);
        
        /* Assumes your ntfs_read_attr_range uses the runlist decoder */
        int got = ntfs_read_attr_range(rec, sds, offset, want, chunk);
        if (got < 0x14) break;

        u32 i = 0;
        while (i + 0x14 <= (u32)got) {
            u32 hash   = *(u32*)(chunk + i + 0x00);
            u32 sec_id = *(u32*)(chunk + i + 0x04);
            u32 length = *(u32*)(chunk + i + 0x10);

            if (length == 0 || length > 0x100000) { 
                i += 16; 
                continue; 
            }

            if (sec_id == target_sec_id) {
                /* Target Found: Calculate SD size and read it directly via absolute offset */
                u32 sd_len = length - 0x14;
                if (sd_len > max_sd_len) sd_len = max_sd_len;
                
                int res = ntfs_read_attr_range(rec, sds, offset + i + 0x14, sd_len, out_sd);
                free(chunk);
                return res; /* Returns bytes written to out_sd */
            }

            /* SDS entries are padded to 16-byte alignments */
            i += (length + 15) & ~15;
        }
        
        /* If an entry is larger than the remaining chunk, 'i' will correctly 
           advance 'offset' to the beginning of the next valid entry on the next disk read. */
        if (i == 0) break; 
        offset += i; 
    }
    
    free(chunk);
    return -3; /* Security ID not found */
}
static u8* ntfs_add_attr_index_bitmap(u8 *p) {
    u32 total = 0x20;
    wr32le(p + 0, 0xB0); 
    wr32le(p + 4, total);
    p[8] = 0; p[9] = 0;
    wr16le(p + 0x10, 8); /* Must be at least 8 bytes */
    wr16le(p + 0x14, 0x18);
    p[0x18] = 0x01; /* VCN 0 is allocated */
    memset(p + 0x19, 0, total - 0x19);
    return p + total;
}
static void build_upcase_fixed(u8 *buf) {
    for (u32 i = 0; i < 65536; i++) {
        u16 c = (u16)i;
        if (c >= 'a' && c <= 'z') c -= 32; 
        wr16le(buf + (i * 2), c);
    }
}
static void build_attrdef_fixed(u8 *buf) {
    memset(buf, 0, 2560);
    write_attr_def(buf + 0*160, "$STANDARD_INFORMATION", 0x10, 0x40, 48, 72);
    write_attr_def(buf + 1*160, "$ATTRIBUTE_LIST", 0x20, 0x80, 0, 0xFFFFFFFFFFFFFFFFULL);
    write_attr_def(buf + 2*160, "$FILE_NAME", 0x30, 0x42, 68, 578);
    write_attr_def(buf + 3*160, "$OBJECT_ID", 0x40, 0x40, 0, 256);
    write_attr_def(buf + 4*160, "$SECURITY_DESCRIPTOR", 0x50, 0x80, 0, 0xFFFFFFFFFFFFFFFFULL);
    write_attr_def(buf + 5*160, "$VOLUME_NAME", 0x60, 0x40, 2, 256);
    write_attr_def(buf + 6*160, "$VOLUME_INFORMATION", 0x70, 0x40, 12, 12);
    write_attr_def(buf + 7*160, "$DATA", 0x80, 0x00, 0, 0xFFFFFFFFFFFFFFFFULL); /* 0x00 = Resident or Non-resident */
    write_attr_def(buf + 8*160, "$INDEX_ROOT", 0x90, 0x40, 0, 0xFFFFFFFFFFFFFFFFULL);
    write_attr_def(buf + 9*160, "$INDEX_ALLOCATION", 0xA0, 0x80, 0, 0xFFFFFFFFFFFFFFFFULL);
    write_attr_def(buf + 10*160, "$BITMAP", 0xB0, 0x80, 0, 0xFFFFFFFFFFFFFFFFULL);
    write_attr_def(buf + 11*160, "$REPARSE_POINT", 0xC0, 0x80, 0, 16384);
    write_attr_def(buf + 12*160, "$EA_INFORMATION", 0xD0, 0x40, 8, 8);
    write_attr_def(buf + 13*160, "$EA", 0xE0, 0x00, 0, 65536); /* 0x00 = Resident or Non-resident */
    write_attr_def(buf + 14*160, "$PROPERTY_SET", 0xF0, 0x80, 0, 0xFFFFFFFFFFFFFFFFULL);
    write_attr_def(buf + 15*160, "$LOGGED_UTILITY_STREAM", 0x100, 0x80, 0, 65536);
}
static u8* ntfs_add_attr_index_alloc(u8 *p, u32 clusters, u64 lcn, u64 bytes) {
    u8 run[16];
    int rlen = ntfs_encode_run(run, clusters, lcn);
    u32 total = (0x40 + rlen + 7) & ~7u;

    wr32le(p + 0, NTFS_AT_INDEX_ALLOC);
    wr32le(p + 4, total);
    p[8] = 1; p[9] = 0;
    wr16le(p + 0x20, 0x40);
    wr64le(p + 0x10, 0);
    wr64le(p + 0x18, clusters - 1);
    wr64le(p + 0x28, clusters * g_ntfs_clus_size);
    wr64le(p + 0x30, bytes);
    wr64le(p + 0x38, bytes);
    memcpy(p + 0x40, run, rlen);
    return p + total;
}
static u8* ntfs_add_attr_std_info(u8 *p, u64 ntfs_time, u32 file_attr) {
    u32 total = 0x18 + 0x48; /* 96 bytes total payload */
    wr32le(p + 0, 0x10);
    wr32le(p + 4, total);
    p[8] = 0; p[9] = 0;
    
    /* FIX: Name offset must point to the end of the header (0x18), even if Name Length is 0 */
    wr16le(p + 0x0A, 0x18); 
    
    wr16le(p + 0x0C, 0);
    wr16le(p + 0x10, 0x48); 
    wr16le(p + 0x14, 0x18);
    for (int i = 0; i < 4; i++) wr64le(p + 0x18 + i * 8, ntfs_time);
    wr32le(p + 0x38, file_attr);
    memset(p + 0x3C, 0, 0x48 - 0x24); 
    return p + total;
}

static u8* ntfs_add_attr_index_root_large(u8 *p) {
    u32 entry_len = 0x18; 
    u32 total = (0x18 + 0x10 + entry_len + 7) & ~7u; 

    wr32le(p + 0, NTFS_AT_INDEX_ROOT);
    wr32le(p + 4, total);
    p[8] = 0;
    wr16le(p + 0x10, 0x10 + entry_len); 
    wr16le(p + 0x14, 0x18); 

    u8 *b = p + 0x18;
    wr32le(b + 0x00, NTFS_AT_FILE_NAME);
    wr32le(b + 0x04, 1);
    wr32le(b + 0x08, 4096);
    b[0x0C] = 1; 

    wr32le(b + 0x10, 0x10);
    wr32le(b + 0x14, 0x10 + entry_len); 
    wr32le(b + 0x18, 0x10 + entry_len); 
    b[0x1C] = 1; /* Large Directory Flag */

    u8 *e = b + 0x20;
    wr64le(e + 0, 0);       
    wr16le(e + 8, (u16)entry_len);    
    wr16le(e + 10, 0);      
    wr16le(e + 12, 0x03); /* Has Sub-Node + END Dummy */
    wr16le(e + 14, 0);
    wr64le(e + 16, 0);    /* VCN 0 */

    return p + total;
}
static u8* ntfs_add_attr_data_nonres(u8 *p, u64 total_clusters, u64 lcn, u64 total_bytes) {
    u8 run[16];
    int rlen = ntfs_encode_run(run, total_clusters, lcn);
    u32 total = (0x40 + rlen + 7) & ~7u;

    wr32le(p + 0, NTFS_AT_DATA);
    wr32le(p + 4, total);
    p[8] = 1; p[9] = 0;
    wr16le(p + 0x20, 0x40);
    wr64le(p + 0x10, 0);
    wr64le(p + 0x18, total_clusters - 1);
    wr64le(p + 0x28, total_clusters * g_ntfs_clus_size);
    wr64le(p + 0x30, total_bytes);
    wr64le(p + 0x38, total_bytes);
    memcpy(p + 0x40, run, rlen);

    return p + total;
}

static u8* ntfs_add_attr_index_root(u8 *p) {
    u32 total = 0x18 + 0x20 + 0x10;
    wr32le(p + 0, NTFS_AT_INDEX_ROOT);
    wr32le(p + 4, total);
    p[8] = 0;
    wr16le(p + 0x10, 0x20 + 0x10);
    wr16le(p + 0x14, 0x18);

    u8 *b = p + 0x18;
    wr32le(b + 0x00, NTFS_AT_FILE_NAME);
    wr32le(b + 0x04, 1);
    wr32le(b + 0x08, g_ntfs_idx_bytes);
    b[0x0C] = 1;

    wr32le(b + 0x10, 0x10);
    wr32le(b + 0x14, 0x20); // total size = 0x10 header + 0x10 dummy entry
    wr32le(b + 0x18, 0x20); // alloc size
    b[0x1C] = 0;

    u8 *e = b + 0x20;
    /* CORRECTED STRUCT OFFSETS FOR NTFS INDEX ENTRIES */
    wr64le(e + 0, 0);       /* 0x00: MFT Reference */
    wr16le(e + 8, 0x10);    /* 0x08: Size of this index entry (step) */
    wr16le(e + 10, 0);      /* 0x0A: Size of key payload */
    wr16le(e + 12, 0x02);   /* 0x0C: Flags (0x02 = Dummy END of node) */
    wr16le(e + 14, 0);      /* 0x0E: Padding */

    return p + total;
}

static u8* ntfs_add_attr_volume_info(u8 *p, u8 maj, u8 min, u16 flags) {
    u32 total = 0x28; /* (0x18 + 12 + 7) & ~7u -> 8-byte aligned */
    wr32le(p + 0, NTFS_AT_VOLUME_INFO);
    wr32le(p + 4, total);
    p[8] = 0; p[9] = 0;
    wr16le(p + 0x10, 12);  /* Value length */
    wr16le(p + 0x14, 0x18);/* Value offset */
    memset(p + 0x18, 0, 12);
    p[0x18 + 8] = maj;
    p[0x18 + 9] = min;
    wr16le(p + 0x18 + 10, flags);
    memset(p + 0x18 + 12, 0, total - (0x18 + 12));
    return p + total;
}

static int ntfs_get_volume_flags(u16 *out_flags) {
    u8 rec[4096];
    if (ntfs_read_mft_record(3, rec) != 0) return -1;
    const u8 *vi = ntfs_find_attr(rec, NTFS_AT_VOLUME_INFO, 0);
    if (!vi || (vi[8] & 1)) return -2;
    u16 voff = rd16le(vi + 0x14);
    u16 vlen = rd16le(vi + 0x10);
    if (vlen < 12) return -3;
    if (out_flags) *out_flags = rd16le(vi + voff + 10);
    return 0;
}

static int ntfs_set_volume_flags(u16 flags, int mode) {
    u8 rec[4096];
    if (ntfs_read_mft_record(3, rec) != 0) return -1;
    const u8 *vi = ntfs_find_attr(rec, NTFS_AT_VOLUME_INFO, 0);
    if (!vi || (vi[8] & 1)) return -2;
    u16 voff = rd16le(vi + 0x14);
    u16 vlen = rd16le(vi + 0x10);
    if (vlen < 12) return -3;
    u8 *val = (u8*)vi + voff;
    u16 cur = rd16le(val + 10);
    if (mode == 1)      cur |= flags;   /* Set bits */
    else if (mode == 0) cur &= ~flags;  /* Clear bits */
    else                cur = flags;    /* Overwrite */
    wr16le(val + 10, cur);
    return ntfs_write_mft_record(3, rec);
}

static int ntfs_compact_partition(void) {
    u8 rec[4096];
    /* MFT Record 6 is always the $Bitmap system file */
    if (ntfs_read_mft_record(6, rec) != 0) return -1;
    
    const u8 *data = ntfs_find_attr(rec, NTFS_AT_DATA, 0);
    if (!data) return -2;
    
    /* Extract the exact byte size of the bitmap */
    u64 bitmap_size = (data[8] & 1) ? rd64le(data + 0x30) : rd32le(data + 0x10);
    
    /* Allocate heap buffers to prevent stack overflow */
    u8 *buf = (u8*)malloc(8192); 
    u8 *z_clus = (u8*)calloc(g_ntfs_spc, 512); /* Zero buffer for one full cluster */
    
    if (!buf || !z_clus) { 
        free(buf); free(z_clus); 
        return -3; 
    }
    
    u64 done = 0;
    u64 cluster_idx = 0;
    
    while (done < bitmap_size) {
        if (g_cancel_operation) break;
        
        u64 want = bitmap_size - done;
        if (want > 8192) want = 8192;
        
        int got = ntfs_read_attr_range(rec, data, done, want, buf);
        if (got <= 0) break;
        
        for (int i = 0; i < got; i++) {
            u8 b = buf[i];
            
            /* Fast-forward: 0xFF means all 8 clusters are currently in use */
            if (b == 0xFF) { 
                cluster_idx += 8;
                continue;
            }
            
            /* Check each bit. 0 = free, 1 = allocated */
            for (int bit = 0; bit < 8; bit++) {
                if (!(b & (1 << bit))) {
                    u32 lba = (u32)(g_ntfs_part_lba + cluster_idx * g_ntfs_spc);
                    write_sec(lba, z_clus, g_ntfs_spc);
                }
                cluster_idx++;
            }
        }
        done += got;
        
        /* Update progress bar */
        UpdateProgress((int)((done * 100) / bitmap_size));
    }
    
    free(buf);
    free(z_clus);
    return 0;
}
static int ntfs_remove_from_index_block(u8 *base, u32 *ents_off, u32 *end_off, u64 target_ref) {
    u8 *e = base + *ents_off;
    u8 *end = base + *end_off;
    
    while (e + 0x10 <= end) {
        u16 step = rd16le(e + 0x08);
        u16 flags = rd16le(e + 0x0C);
        if (step < 0x10 || e + step > end) break;

        if (!(flags & 0x02)) {
            u64 ref = rd64le(e) & 0x0000FFFFFFFFFFFFULL;
            if (ref == (target_ref & 0x0000FFFFFFFFFFFFULL)) {
                /* Target found: Collapse the array by shifting everything left */
                u32 tail_len = (u32)(end - (e + step));
                memmove(e, e + step, tail_len);
                *end_off -= step;
                return 1;
            }
        }
        if (flags & 0x02) break; /* End of node */
        e += step;
    }
    return 0;
}
static int ntfs_index_remove(u64 parent_ref, u64 child_ref) {
    u8 rec[8192];
    if (ntfs_read_mft_record(parent_ref, rec) != 0) return -1;

    /* 1. Try Resident INDEX_ROOT (Small directories) */
    const u8 *ir = ntfs_find_attr(rec, NTFS_AT_INDEX_ROOT, 0);
    if (ir && !(ir[8] & 1)) {
        u16 voff = rd16le(ir + 0x14);
        u8 *rv = (u8*)ir + voff;
        u32 ents_off = rd32le(rv + 0x10);
        u32 end_off  = rd32le(rv + 0x14);
        
        if (ntfs_remove_from_index_block(rv + 0x10, &ents_off, &end_off, child_ref)) {
            wr32le(rv + 0x14, end_off);
            ntfs_write_mft_record(parent_ref, rec);
            return 0;
        }
    }

    /* 2. Try Non-Resident INDEX_ALLOCATION (Large directories) */
    const u8 *ia = ntfs_find_attr(rec, NTFS_AT_INDEX_ALLOC, 0);
    if (ia && (ia[8] & 1)) {
        u16 run_off = rd16le(ia + 0x20);
        const u8 *runs = ia + run_off;
        int pos = 0; s64 lcn_acc = 0; u64 t_len; s64 t_lcn;
        
        while (ntfs_run_next(runs, &pos, &lcn_acc, &t_len, &t_lcn) > 0) {
            if (t_lcn < 0) continue;
            
            for (u64 c = 0; c < t_len; c += (g_ntfs_idx_bytes / g_ntfs_clus_size ? g_ntfs_idx_bytes / g_ntfs_clus_size : 1)) {
                u64 lba = g_ntfs_part_lba + ((u64)t_lcn + c) * g_ntfs_spc;
                u8 *blk = (u8*)malloc(g_ntfs_idx_bytes);
                u32 nsecs = g_ntfs_idx_bytes / 512;
                
                if (read_sec((u32)lba, blk, nsecs) == 0 && ntfs_apply_fixups(blk, g_ntfs_idx_bytes) == 0) {
                    u32 ents_off = rd32le(blk + 0x18);
                    u32 end_off  = rd32le(blk + 0x1C);
                    
                    if (ntfs_remove_from_index_block(blk + 0x18, &ents_off, &end_off, child_ref)) {
                        wr32le(blk + 0x1C, end_off);
                        
                        /* Generate new Update Sequence Numbers (USN) before writing back to disk */
                        u16 usa_off = rd16le(blk + 0x04);
                        u16 usa_cnt = rd16le(blk + 0x06);
                        if (usa_cnt == nsecs + 1) {
                            u16 usn = (u16)(rd16le(blk + usa_off) + 1);
                            if (usn == 0) usn = 1;
                            wr16le(blk + usa_off, usn);
                            for (u32 i = 0; i < nsecs; i++) {
                                u8 *tail = blk + i * 512 + 510;
                                wr16le(blk + usa_off + 2 + i * 2, rd16le(tail));
                                wr16le(tail, usn);
                            }
                        }
                        write_sec((u32)lba, blk, nsecs);
                        free(blk);
                        return 0;
                    }
                }
                free(blk);
            }
        }
    }
    return -1;
}
static int ntfs_free_clusters(const u8 *runs) {
    u8 b_rec[8192];
    if (ntfs_read_mft_record(6, b_rec) != 0) return -1; /* MFT 6 = $Bitmap */
    
    const u8 *b_data = ntfs_find_attr(b_rec, NTFS_AT_DATA, 0);
    if (!b_data) return -2;

    u64 b_size = (b_data[8] & 1) ? rd64le(b_data + 0x30) : rd32le(b_data + 0x10);
    u8 *bitmap = (u8*)malloc((size_t)b_size);
    if (!bitmap) return -3;

    /* 1. Load entire $Bitmap into memory */
    u64 done = 0;
    while (done < b_size) {
        u64 want = b_size - done;
        if (want > 65536) want = 65536;
        int got = ntfs_read_attr_range(b_rec, b_data, done, want, bitmap + done);
        if (got <= 0) break;
        done += got;
    }

    /* 2. Traverse the deleted file's runlist and un-toggle bits */
    int pos = 0, modified = 0;
    s64 lcn_acc = 0;
    u64 t_len; s64 t_lcn;

    while (ntfs_run_next(runs, &pos, &lcn_acc, &t_len, &t_lcn) > 0) {
        if (t_lcn >= 0) { /* Ignore sparse runs */
            for (u64 c = 0; c < t_len; c++) {
                u64 clu = (u64)t_lcn + c;
                if (clu < b_size * 8) {
                    bitmap[clu / 8] &= ~(1 << (clu % 8));
                }
            }
            modified = 1;
        }
    }

    /* 3. Write modified $Bitmap back to disk */
    if (modified && (b_data[8] & 1)) {
        u16 b_run_off = rd16le(b_data + 0x20);
        const u8 *b_runs = b_data + b_run_off;
        pos = 0; lcn_acc = 0; u64 b_done = 0;

        while (ntfs_run_next(b_runs, &pos, &lcn_acc, &t_len, &t_lcn) > 0) {
            if (t_lcn >= 0 && b_done < b_size) {
                u64 lba = g_ntfs_part_lba + t_lcn * g_ntfs_spc;
                u64 bytes = t_len * g_ntfs_clus_size;
                if (b_done + bytes > b_size) bytes = b_size - b_done;
                write_sec((u32)lba, bitmap + b_done, (u32)((bytes + 511) / 512));
                b_done += bytes;
            }
        }
    } else if (modified && !(b_data[8] & 1)) {
        u16 voff = rd16le(b_data + 0x14);
        memcpy((u8*)b_data + voff, bitmap, (size_t)b_size);
        ntfs_write_mft_record(6, b_rec);
    }

    free(bitmap);
    return 0;
}
static u8* ntfs_add_idx_bitmap_i30(u8 *p, u64 bytes) {
    u32 attr_len = (32 + bytes + 7) & ~7; 
    wr32le(p + 0, 0xB0);          
    wr32le(p + 4, attr_len);      
    p[8] = 0; p[9] = 4;                                     
    wr16le(p + 0x0A, 24);         
    wr16le(p + 0x0C, 0);          
    wr16le(p + 0x0E, 0);          
    wr32le(p + 0x10, (u32)bytes);       
    wr16le(p + 0x14, 32);         
    p[0x16] = 0; p[0x17] = 0;     
    
    p[24] = '$'; p[25] = 0; p[26] = 'I'; p[27] = 0; 
    p[28] = '3'; p[29] = 0; p[30] = '0'; p[31] = 0;
    
    memset(p + 32, 0, attr_len - 32);
    p[32] = 0x01; 
    return p + attr_len;
}
static u8* ntfs_add_idx_alloc_i30(u8 *p, u32 clusters, u64 lcn, u64 bytes) {
    u8 run[16];
    int rlen = ntfs_encode_run(run, clusters, lcn);
    u32 attr_len = (72 + rlen + 7) & ~7; 
    
    wr32le(p + 0, 0xA0);          
    wr32le(p + 4, attr_len);      
    p[8] = 1; p[9] = 4;                                     
    wr16le(p + 0x0A, 64);         
    wr16le(p + 0x0C, 0);          
    wr16le(p + 0x0E, 0);          
    
    wr64le(p + 16, 0);            
    wr64le(p + 24, clusters - 1); 
    wr16le(p + 32, 72);           
    wr16le(p + 34, 0);            
    wr32le(p + 36, 0);            
    
    /* CORRECTED: Allocated size must align to physical clusters */
    wr64le(p + 40, (u64)clusters * g_ntfs_clus_size);        
    wr64le(p + 48, bytes);        
    wr64le(p + 56, bytes);        
    
    p[64] = '$'; p[65] = 0; p[66] = 'I'; p[67] = 0; 
    p[68] = '3'; p[69] = 0; p[70] = '0'; p[71] = 0;
    
    /* CORRECTED: Dynamically generate runlist to support large VHD LCNs */
    memcpy(p + 72, run, rlen);
    memset(p + 72 + rlen, 0, attr_len - (72 + rlen));
    
    return p + attr_len;
}
static u8* ntfs_add_idx_root_i30(u8 *p, int is_large) {
    u32 entry_len = is_large ? 24 : 16;
    u32 content_len = 32 + entry_len; 
    u32 attr_len = (32 + content_len + 7) & ~7; 
    
    wr32le(p + 0, 0x90);          
    wr32le(p + 4, attr_len);            
    p[8] = 0; p[9] = 4;                                     
    wr16le(p + 0x0A, 24);         
    wr16le(p + 0x0C, 0);          
    wr16le(p + 0x0E, 0); 
    wr32le(p + 0x10, content_len);         
    wr16le(p + 0x14, 32);         
    p[0x16] = 0; p[0x17] = 0;     
    
    p[24] = '$'; p[25] = 0; p[26] = 'I'; p[27] = 0; 
    p[28] = '3'; p[29] = 0; p[30] = '0'; p[31] = 0;
    
    u8 *b = p + 32;
    wr32le(b + 0x00, 0x30);         
    wr32le(b + 0x04, 0x01);         
    wr32le(b + 0x08, 4096);         
    b[0x0C] = 1; b[0x0D] = 0; b[0x0E] = 0; b[0x0F] = 0; 
    
    wr32le(b + 0x10, 16);            
    wr32le(b + 0x14, 16 + entry_len);            
    wr32le(b + 0x18, 16 + entry_len);            
    wr32le(b + 0x1C, is_large ? 1 : 0);            
    
    u8 *e = b + 0x20;
    wr64le(e + 0x00, 0);            
    wr16le(e + 0x08, entry_len);            
    wr16le(e + 0x0A, 0);            
    wr16le(e + 0x0C, is_large ? 0x03 : 0x02);          
    wr16le(e + 0x0E, 0);            
    if (is_large) wr64le(e + 0x10, 0);
    
    memset(p + 32 + content_len, 0, attr_len - (32 + content_len));
    return p + attr_len;
}
static int ntfs_delete_by_ref(u64 mft_ref) {
    u8 rec[8192];
    if (ntfs_read_mft_record(mft_ref, rec) != 0) return -1;
    
    /* 1. Extract Parent Directory MFT Reference */
    const u8 *fn = ntfs_find_attr(rec, NTFS_AT_FILE_NAME, 0);
    u64 parent_ref = NTFS_MFT_ROOT;
    if (fn && !(fn[8] & 1)) {
        u16 voff = rd16le(fn + 0x14);
        parent_ref = rd64le(fn + voff) & 0x0000FFFFFFFFFFFFULL;
    }

    /* 2. Free Data Clusters in $Bitmap */
    const u8 *data = ntfs_find_attr(rec, NTFS_AT_DATA, 0);
    if (data && (data[8] & 1)) { /* Only non-resident data occupies clusters */
        u16 run_off = rd16le(data + 0x20);
        ntfs_free_clusters(data + run_off);
    }

    /* 3. Strip file from Parent Folder UI */
    ntfs_index_remove(parent_ref, mft_ref);

    /* 4. Kill the MFT Record */
    u16 fl = rd16le(rec + 0x16);
    wr16le(rec + 0x16, (u16)(fl & ~NTFS_FL_IN_USE));
    
    if (ntfs_write_mft_record(mft_ref, rec) != 0) return -2;

    return 0;
}

/* ============================================================ NTFS FORMATTER */
static int fs_format_ntfs_ex(int part_idx, int mark_dirty) {
    if (!g_vhd.isOpen || !g_vhd.parts[part_idx].used) return -1;
    u64 nsec = (u64)g_vhd.parts[part_idx].lba_count;
    if (nsec < 20480) return -2; 

    u32 part_lba = g_vhd.parts[part_idx].lba_begin;
    u32 spc = 8;
    u32 clus_sz = spc * 512;
    u64 tot_clusters = nsec / spc;
    if (tot_clusters < 100) return -2;

    u32 mft_clusters = 64; 
    u64 mft_lcn = 4;
    u64 mft_mirr_lcn = mft_lcn + mft_clusters;
    u32 mft_mirr_clusters = (4096 + clus_sz - 1) / clus_sz; 

    u64 logfile_lcn = mft_mirr_lcn + mft_mirr_clusters;
    u32 logfile_clusters = (2048 * 1024) / clus_sz;
    if (logfile_clusters > tot_clusters / 8) logfile_clusters = (u32)(tot_clusters / 8);
    if (logfile_clusters < 32) logfile_clusters = 32;
    u64 logfile_bytes = (u64)logfile_clusters * clus_sz;

    u64 bitmap_lcn = logfile_lcn + logfile_clusters;
    u32 bitmap_clusters = (u32)(((tot_clusters + 7) / 8 + clus_sz - 1) / clus_sz);
    if (bitmap_clusters == 0) bitmap_clusters = 1;

    u64 attrdef_lcn = bitmap_lcn + bitmap_clusters;
    u32 attrdef_clusters = (2560 + clus_sz - 1) / clus_sz;

    u64 upcase_lcn = attrdef_lcn + attrdef_clusters;
    u32 upcase_clusters = (131072 + clus_sz - 1) / clus_sz; 

    u64 root_idx_lcn = upcase_lcn + upcase_clusters;
    u32 root_idx_clusters = (4096 + clus_sz - 1) / clus_sz;

    u64 extend_idx_lcn = root_idx_lcn + root_idx_clusters;
    u32 extend_idx_clusters = (4096 + clus_sz - 1) / clus_sz;

    u32 boot_clusters = (8192 + clus_sz - 1) / clus_sz;
    u64 total_sys_clusters = extend_idx_lcn + extend_idx_clusters;
    if (total_sys_clusters >= tot_clusters) return -3;

    ShowProgress(TRUE);

    u8 vbr[512] = {0};
    vbr[0] = 0xEB; vbr[1] = 0x52; vbr[2] = 0x90;
    memcpy(vbr + 3, "NTFS    ", 8);
    wr16le(vbr + 0x0B, 512);
    vbr[0x0D] = (u8)spc;
    vbr[0x15] = 0xF8;
    wr16le(vbr + 0x18, 63);
    wr16le(vbr + 0x1A, 255);
    wr32le(vbr + 0x1C, part_lba);
    wr64le(vbr + 0x28, nsec - 1);
    wr64le(vbr + 0x30, mft_lcn);
    wr64le(vbr + 0x38, mft_mirr_lcn);
    vbr[0x40] = 0xF6; vbr[0x44] = 0xF4; 
    u64 serial = 0xA24C9E710F823B5DULL ^ (u64)time(NULL);
    wr64le(vbr + 0x48, serial);
    vbr[510] = 0x55; vbr[511] = 0xAA;

    write_sec(part_lba, vbr, 1);
    if (nsec > 1) write_sec(part_lba + (u32)nsec - 1, vbr, 1);

    u8 z[512] = {0};
    for (u32 s = 1; s < 16; s++) write_sec(part_lba + s, z, 1);
    
    u32 wipe_mft_c = (tot_clusters > 512) ? 512 : (u32)tot_clusters;
    for (u32 c = 0; c < wipe_mft_c; c++) {
        u32 sec = part_lba + (u32)(mft_lcn + c) * spc;
        for (u32 s = 0; s < spc; s++) write_sec(sec + s, z, 1);
    }

    u8 ff[512];
    memset(ff, 0xFF, sizeof(ff));
    for (u32 c = 0; c < logfile_clusters; c++) {
        u32 sec = part_lba + (u32)(logfile_lcn + c) * spc;
        for (u32 s = 0; s < spc; s++) write_sec(sec + s, ff, 1);
    }
    for (u32 c = 0; c < bitmap_clusters; c++) {
        u32 sec = part_lba + (u32)(bitmap_lcn + c) * spc;
        for (u32 s = 0; s < spc; s++) write_sec(sec + s, z, 1);
    }

    u8 *sys_buf = (u8*)calloc(upcase_clusters, clus_sz);
    if (sys_buf) {
        build_attrdef_fixed(sys_buf);
        write_sec(part_lba + (u32)attrdef_lcn * spc, sys_buf, attrdef_clusters * spc);
        memset(sys_buf, 0, upcase_clusters * clus_sz);
        build_upcase_fixed(sys_buf);
        write_sec(part_lba + (u32)upcase_lcn * spc, sys_buf, upcase_clusters * spc);
        free(sys_buf);
    }

    g_ntfs = 1; g_fat_type = 7; g_ntfs_part_lba = part_lba; g_ntfs_spc = spc;
    g_ntfs_clus_size = clus_sz; g_ntfs_mft_lcn = mft_lcn; g_ntfs_mft_mirr_lcn = mft_mirr_lcn;
    g_ntfs_mft_rec = 1024; g_ntfs_idx_bytes = 4096; g_ntfs_cur_dir = NTFS_MFT_ROOT;
    u64 ntfs_time = 116444736000000000ULL;

    for (u32 r = 0; r <= 27; r++) {
        if (r >= 16 && r <= 23) continue;
        
        u8 rec[1024];
        memset(rec, 0, 1024);
        
        u16 flags = NTFS_FL_IN_USE; 
        if (r == 5 || r == 11) flags |= NTFS_FL_IS_DIR; 
        
        /* FIX: Ensure the 0x04 flag is fully stripped out. Only 0x08 (View Index) belongs here. */
        if (r == 9 || r == 24 || r == 25 || r == 26) flags |= 0x08; 
        
        ntfs_format_init_record(rec, r, flags); 
        ntfs_write_mft_record(r, rec);
    }

    u8 *root_indx_buf = (u8*)calloc(1, 4096);
    init_indx_block(root_indx_buf, 0);

    u64 parent_ref = MAKE_MFT_REF(5);
    u32 bytes_in_use;
    
    const char *base_names[12] = {
        "$MFT", "$MFTMirr", "$LogFile", "$Volume", "$AttrDef", ".", 
        "$Bitmap", "$Boot", "$BadClus", "$Secure", "$UpCase", "$Extend"
    };

    u64 f_alloc[12] = {0}; u64 f_size[12] = {0};
    f_alloc[0] = (u64)mft_clusters * clus_sz;     f_size[0] = f_alloc[0];
    f_alloc[1] = 4096;                            f_size[1] = 4096;
    f_alloc[2] = (u64)logfile_clusters * clus_sz; f_size[2] = logfile_bytes;
    f_alloc[4] = attrdef_clusters * clus_sz;      f_size[4] = 2560;
    f_alloc[6] = bitmap_clusters * clus_sz;       f_size[6] = (tot_clusters + 7) / 8;
    f_alloc[7] = boot_clusters * clus_sz;         f_size[7] = 8192;
    f_alloc[10]= upcase_clusters * clus_sz;       f_size[10]= 131072;

    #define APPEND_ATTR(func) do { u8 *_old = p; p = (func); wr16le(_old + 0x0E, attr_id++); } while(0)

    for(int i=0; i<3; i++) {
        u8 rec[1024];
        ntfs_read_mft_record(i, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[i], ntfs_time, 0x06, f_alloc[i], f_size[i], 0x03));
        u32 clu = (u32)(f_alloc[i] / clus_sz);
        u64 lcn = (i==0) ? mft_lcn : (i==1 ? mft_mirr_lcn : logfile_lcn);
        APPEND_ATTR(ntfs_add_attr_data_nonres(p, clu, lcn, f_size[i]));
        
        if (i == 0) {
            u8 *start_p = p;
            u32 total_records = mft_clusters * (clus_sz / 1024);
            u32 bmp_bytes = (total_records + 7) / 8;
            u32 attr_len = (24 + bmp_bytes + 7) & ~7; 
            wr32le(p, 0xB0); wr32le(p + 4, attr_len);
            p[8] = 0; p[9] = 0; 
            wr16le(p + 0x0A, 0); wr16le(p + 0x0C, 0); wr16le(p + 0x0E, attr_id++);
            wr32le(p + 0x10, bmp_bytes); wr16le(p + 0x14, 24);
            p[0x16] = 0; p[0x17] = 0;
            memset(p + 24, 0, attr_len - 24);
            
            p[24] = 0xFF; p[25] = 0xFF; 
            p[26] = 0x00; p[27] = 0x0F; 
            p += attr_len;
        }
        
        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id); 
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(i, rec);
    }

    /* 3: $Volume */
    {
        u8 rec[1024];
        ntfs_read_mft_record(3, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[3], ntfs_time, 0x06, 0, 0, 0x03));
        
        u8 *start_p = p;
        wr32le(p, NTFS_AT_VOLUME_NAME); wr32le(p + 4, 40); 
        p[8] = 0; p[9] = 0; wr16le(p + 0x0A, 0); wr16le(p + 0x0C, 0); wr16le(p + 0x0E, attr_id++); 
        wr32le(p + 0x10, 16); wr16le(p + 0x14, 0x18); p[0x16] = 0; p[0x17] = 0;
        memcpy(p + 0x18, "N\0T\0F\0S\0_\0V\0H\0D\0", 16); p += 40;
        
        start_p = p;
        wr32le(p, 0x70); wr32le(p + 4, 40); 
        p[8] = 0; p[9] = 0; wr16le(p + 0x0A, 0); wr16le(p + 0x0C, 0); wr16le(p + 0x0E, attr_id++); 
        wr32le(p + 0x10, 12); wr16le(p + 0x14, 0x18); p[0x16] = 0; p[0x17] = 0;
        memset(p + 0x18, 0, 16); 
        p[0x18 + 8] = 3; p[0x18 + 9] = 1; p[0x18 + 10] = mark_dirty ? NTFS_VOLUME_IS_DIRTY : 0; 
        p += 40;
        
        APPEND_ATTR(ntfs_add_attr_data_res_empty(p, NULL));
        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(3, rec);
    }

    /* 4: $AttrDef */
    {
        u8 rec[1024];
        ntfs_read_mft_record(4, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[4], ntfs_time, 0x06, f_alloc[4], f_size[4], 0x03));
        APPEND_ATTR(ntfs_add_attr_data_nonres(p, attrdef_clusters, attrdef_lcn, f_size[4]));
        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(4, rec);
    }

    /* 5: Root Directory (.) */
    {
        u8 rec[1024];
        ntfs_read_mft_record(5, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x16));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[5], ntfs_time, 0x10000016, 0, 0, 0x03));
        
        APPEND_ATTR(ntfs_add_idx_root_i30(p, 1));
        APPEND_ATTR(ntfs_add_idx_alloc_i30(p, root_idx_clusters, root_idx_lcn, 4096));
        APPEND_ATTR(ntfs_add_idx_bitmap_i30(p, 8));
        
        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(5, rec);
    }

    /* 6: $Bitmap */
    {
        u8 rec[1024];
        ntfs_read_mft_record(6, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[6], ntfs_time, 0x06, f_alloc[6], f_size[6], 0x03));
        APPEND_ATTR(ntfs_add_attr_data_nonres(p, bitmap_clusters, bitmap_lcn, f_size[6]));
        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(6, rec);
    }

    /* 7: $Boot */
    {
        u8 rec[1024];
        ntfs_read_mft_record(7, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[7], ntfs_time, 0x06, f_alloc[7], f_size[7], 0x03));
        APPEND_ATTR(ntfs_add_attr_data_nonres(p, boot_clusters, 0, f_size[7])); 
        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(7, rec);
    }

    /* 8: $BadClus */
    {
        u8 rec[1024];
        ntfs_read_mft_record(8, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[8], ntfs_time, 0x06, 0, 0, 0x03));
        APPEND_ATTR(ntfs_add_attr_data_res_empty(p, NULL));

        u8 *start_p = p;
        wr32le(p, 0x80); wr32le(p + 4, 32); p[8] = 0; p[9] = 4;
        wr16le(p + 0x0A, 24); wr16le(p + 0x0C, 0); wr16le(p + 0x0E, attr_id++);
        wr32le(p + 0x10, 0); wr16le(p + 0x14, 32); p[0x16] = 0; p[0x17] = 0;
        const char *bad_name = "$Bad";
        for (int i=0; i<4; i++) { p[24 + i*2] = bad_name[i]; p[24 + i*2 + 1] = 0; }
        p += 32;

        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(8, rec);
    }

    /* 9: $Secure */
    {
        u8 rec[1024];
        ntfs_read_mft_record(9, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        /* FIX: $Secure is universally treated as a View Index file in Windows */
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[9], ntfs_time, 0x20000006, 0, 0, 0x03));
        
        APPEND_ATTR(ntfs_add_attr_data_res_empty(p, NULL));   
        APPEND_ATTR(ntfs_add_attr_data_res_empty(p, "$SDS")); 
        
        APPEND_ATTR(ntfs_add_idx_root_named(p, "$SDH", 0x12)); 
        APPEND_ATTR(ntfs_add_idx_root_named(p, "$SII", 0x10)); 

        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(9, rec);
    }

    /* 10: $UpCase */
    {
        u8 rec[1024];
        ntfs_read_mft_record(10, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[10], ntfs_time, 0x06, f_alloc[10], f_size[10], 0x03));
        APPEND_ATTR(ntfs_add_attr_data_nonres(p, upcase_clusters, upcase_lcn, f_size[10]));
        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(10, rec);
    }

    /* 11: $Extend */
    {
        u8 rec[1024];
        ntfs_read_mft_record(11, rec);
        u8 *p = rec + 0x38; u16 attr_id = 0;
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x16));
        APPEND_ATTR(ntfs_add_attr_file_name(p, parent_ref, base_names[11], ntfs_time, 0x10000016, 0, 0, 0x03));
        
        APPEND_ATTR(ntfs_add_idx_root_i30(p, 1)); 
        APPEND_ATTR(ntfs_add_idx_alloc_i30(p, extend_idx_clusters, extend_idx_lcn, 4096));
        APPEND_ATTR(ntfs_add_idx_bitmap_i30(p, 8));

        wr32le(p, 0xFFFFFFFF); wr16le(rec + 0x28, attr_id);
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(11, rec);
    }

    /* 12-15: System Reservoirs */
    for (u32 r = 12; r <= 15; r++) {
        u8 rec[1024];
        ntfs_read_mft_record(r, rec);
        u8 *p = rec + 0x38; 
        u16 attr_id = 0;
        
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, 0x06));
        APPEND_ATTR(ntfs_add_attr_data_res_empty(p, NULL)); 
        
        wr32le(p, 0xFFFFFFFF); 
        wr16le(rec + 0x28, attr_id); 
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(r, rec);
    }

    /* FIX: MFT 24-27 ($ObjId, $Quota, $Reparse, $UsnJrnl) */
    const char *ext_names[4] = { "$ObjId", "$Quota", "$Reparse", "$UsnJrnl" };
    u64 ext_parent = MAKE_MFT_REF(11);
    
    for (u32 i = 0; i < 4; i++) {
        u32 r = 24 + i;
        u8 rec[1024];
        ntfs_read_mft_record(r, rec);
        u8 *p = rec + 0x38; 
        u16 attr_id = 0;
        
        u32 fn_attrs = (i == 3) ? 0x0206 : 0x20000006; 
        
        /* FIX: Ensure standard_information absolutely DOES NOT contain the 0x20000000 view flag. */
        u32 std_attrs = (i == 3) ? 0x0206 : 0x06; 
        
        APPEND_ATTR(ntfs_add_attr_std_info(p, ntfs_time, std_attrs));
        APPEND_ATTR(ntfs_add_attr_file_name(p, ext_parent, ext_names[i], ntfs_time, fn_attrs, 0, 0, 0x03));
        
        /* 24, 25, 26 have empty unnamed data streams */
        if (i < 3) {
            APPEND_ATTR(ntfs_add_attr_data_res_empty(p, NULL));
        }
        
        if (i == 0) {
            APPEND_ATTR(ntfs_add_idx_root_named(p, "$O", 0x13)); 
        } else if (i == 1) {
            APPEND_ATTR(ntfs_add_idx_root_named(p, "$O", 0x11)); 
            APPEND_ATTR(ntfs_add_idx_root_named(p, "$Q", 0x10));
        } else if (i == 2) {
            APPEND_ATTR(ntfs_add_idx_root_named(p, "$R", 0x13));
        } else if (i == 3) {
            /* FIX: Pass only $Max for $UsnJrnl. Let CHKDSK generate the complex sparse $J data stream automatically to avoid structural rejections */
            APPEND_ATTR(ntfs_add_attr_data_res_usn_max(p));
        }
        
        wr32le(p, 0xFFFFFFFF); 
        wr16le(rec + 0x28, attr_id); 
        bytes_in_use = (u32)(p + 4 - rec);
        wr32le(rec + 0x18, (bytes_in_use + 7) & ~7);
        ntfs_write_mft_record(r, rec);
    }
    
    #undef APPEND_ATTR

    int root_order[] = {4, 8, 6, 7, 11, 2, 0, 1, 9, 10, 3, 5}; 
    for(int i = 0; i < 12; i++) {
        int idx = root_order[i];
        
        u32 attrs = 0x06; 
        if (idx == 11) attrs = 0x10000016; 
        else if (idx == 9) attrs = 0x20000006; /* FIX: Mirrored the 0x20 View Index flag for $Secure to avoid potential issues */
        else if (idx == 5) attrs = 0x10000016; 

        append_indx_entry(root_indx_buf, parent_ref, base_names[idx], MAKE_MFT_REF(idx), attrs, 0x03, f_alloc[idx], f_size[idx]);
    }

    apply_usa_fixup(root_indx_buf);
    write_sec(part_lba + (u32)root_idx_lcn * spc, root_indx_buf, 4096 / 512);
    free(root_indx_buf);

    u8 *ext_indx_buf = (u8*)calloc(1, 4096);
    init_indx_block(ext_indx_buf, 0);

    u64 ext_mfts[4] = { 24, 25, 26, 27 };
    for(int i = 0; i < 4; i++) {
        u32 fn_attrs = (i == 3) ? 0x0206 : 0x20000006;
        append_indx_entry(ext_indx_buf, ext_parent, ext_names[i], MAKE_MFT_REF(ext_mfts[i]), fn_attrs, 0x03, 0, 0);
    }

    apply_usa_fixup(ext_indx_buf);
    write_sec(part_lba + (u32)extend_idx_lcn * spc, ext_indx_buf, 4096 / 512);
    free(ext_indx_buf);

    u32 bsize = bitmap_clusters * clus_sz;
    u8 *bmp = (u8*)calloc(1, bsize);
    if (bmp) {
        #define MARK_RUN(lcn, count) for(u32 _i=0; _i<(count); _i++) bmp[((lcn) + _i) / 8] |= (1 << (((lcn) + _i) % 8))
        MARK_RUN(0, boot_clusters);
        MARK_RUN(mft_lcn, mft_clusters);
        MARK_RUN(mft_mirr_lcn, mft_mirr_clusters);
        MARK_RUN(logfile_lcn, logfile_clusters);
        MARK_RUN(bitmap_lcn, bitmap_clusters);
        MARK_RUN(attrdef_lcn, attrdef_clusters);
        MARK_RUN(upcase_lcn, upcase_clusters);
        MARK_RUN(root_idx_lcn, root_idx_clusters);
        MARK_RUN(extend_idx_lcn, extend_idx_clusters); 
        
        for (u64 c = tot_clusters; c < (u64)bsize * 8; c++) {
            bmp[c / 8] |= (1 << (c % 8));
        }
        
        for (u32 s = 0; s < bitmap_clusters * spc; s++) {
            write_sec(part_lba + (u32)bitmap_lcn * spc + s, bmp + s * 512, 1);
        }
        free(bmp);
    }

    g_vhd.parts[part_idx].type = 0x07; update_mbr_in_ram();
    ShowProgress(FALSE); return 0;
}

static int ntfs_defrag_file(u64 mft_ref, u8 *bitmap, u32 tot_clusters, int *moved) {
    u8 rec[8192];
    if (ntfs_read_mft_record(mft_ref, rec) != 0) return -1;
    
    /* Skip wildly complex files (Attribute Lists) to prevent MFT corruption */
    if (ntfs_find_attr(rec, 0x20, 0)) return 0; 
    
    u8 *data = (u8*)ntfs_find_attr(rec, NTFS_AT_DATA, 0);
    if (!data || !(data[8] & 1)) return 0; /* Skip resident files */
    
    u16 run_off = rd16le(data + 0x20);
    u8 *runs = data + run_off;
    
    int run_count = 0;
    int pos = 0;
    s64 lcn_acc = 0;
    u64 t_len; s64 t_lcn;
    u64 total_len = 0;
    
    /* 1. Audit the runlist */
    while (1) {
        int r = ntfs_run_next(runs, &pos, &lcn_acc, &t_len, &t_lcn);
        if (r <= 0) break;
        if (t_lcn < 0) return 0; /* Abort: Never defragment sparse files (prevents inflation) */
        total_len += t_len;
        run_count++;
    }
    
    if (run_count <= 1 || total_len == 0) return 0; /* Already defragmented */
    
    /* 2. Find contiguous free space in the bitmap */
    u64 best_lcn = 0, current_run = 0, start_lcn = 0;
    int found = 0;
    for (u64 i = 0; i < tot_clusters; ) {
        /* Fast-forward fully allocated bytes */
        if (current_run == 0 && (i % 8 == 0) && bitmap[i / 8] == 0xFF) { i += 8; continue; }
        
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            if (current_run == 0) start_lcn = i;
            current_run++;
            if (current_run == total_len) { best_lcn = start_lcn; found = 1; break; }
        } else {
            current_run = 0;
        }
        i++;
    }
    
    if (!found) return 0; /* Not enough contiguous space */
    
    /* 3. Copy cluster data to contiguous space */
    u32 buf_size = 65536; /* 64KB chunks */
    u8 *clus_buf = (u8*)malloc(buf_size);
    if (!clus_buf) return -1;
    
    u64 write_lcn = best_lcn;
    u64 done = 0;
    while (done < total_len) {
        u64 want_clus = total_len - done;
        if (want_clus > buf_size / g_ntfs_clus_size) want_clus = buf_size / g_ntfs_clus_size;
        
        int got = ntfs_read_attr_range(rec, data, done * g_ntfs_clus_size, want_clus * g_ntfs_clus_size, clus_buf);
        if (got <= 0) break;
        
        u32 sec = (u32)(g_ntfs_part_lba + write_lcn * g_ntfs_spc);
        u32 nsecs = (u32)((got + 511) / 512);
        write_sec(sec, clus_buf, nsecs);
        
        write_lcn += want_clus;
        done += want_clus;
        if (g_cancel_operation) { free(clus_buf); return 0; } /* Safe abort */
    }
    free(clus_buf);
    
    /* 4. Release old clusters and claim new ones in the memory bitmap */
    pos = 0; lcn_acc = 0;
    while (1) {
        int r = ntfs_run_next(runs, &pos, &lcn_acc, &t_len, &t_lcn);
        if (r <= 0) break;
        for (u64 c = 0; c < t_len; c++) {
            u64 clu = (u64)t_lcn + c;
            if (clu < tot_clusters) bitmap[clu / 8] &= ~(1 << (clu % 8));
        }
    }
    for (u64 c = 0; c < total_len; c++) {
        u64 clu = best_lcn + c;
        bitmap[clu / 8] |= (1 << (clu % 8));
    }
    
    /* 5. Rewrite $DATA attribute runlist */
    u8 new_runs[32];
    int new_run_len = ntfs_encode_run(new_runs, total_len, best_lcn);
    
    u32 old_attr_len = rd32le(data + 4);
    u32 new_attr_len = (run_off + new_run_len + 1 + 7) & ~7u;
    if (new_attr_len > old_attr_len) return -1; /* Failsafe */
    
    memcpy(data + run_off, new_runs, new_run_len);
    data[run_off + new_run_len] = 0x00; /* Terminator */
    for (u32 i = run_off + new_run_len + 1; i < new_attr_len; i++) data[i] = 0x00; /* Pad */
    
    if (new_attr_len < old_attr_len) {
        u32 diff = old_attr_len - new_attr_len;
        u8 *next_attr = data + old_attr_len;
        u32 rec_used = rd32le(rec + 0x18);
        memmove(data + new_attr_len, next_attr, rec_used - (u32)(next_attr - rec));
        wr32le(data + 4, new_attr_len);
        wr32le(rec + 0x18, rec_used - diff);
    }
    
    ntfs_write_mft_record(mft_ref, rec);
    (*moved)++;
    return 0;
}
static void ntfs_defrag_recursive(u64 dir_ref, u8 *bitmap, u32 tot_clusters, int *moved) {
    ntfs_list_dir(dir_ref);
    int count = g_fs_entry_count;
    if (count == 0) return;
    
    FsEntry *entries = (FsEntry*)malloc(count * sizeof(FsEntry));
    if (!entries) return;
    memcpy(entries, g_fs_entries, count * sizeof(FsEntry));
    
    for (int i = 0; i < count; i++) {
        if (g_cancel_operation) break;
        if (entries[i].first_cluster < 16) continue; /* Skip core system files */
        
        if (entries[i].is_directory) {
            ntfs_defrag_recursive(entries[i].first_cluster, bitmap, tot_clusters, moved);
        } else {
            ntfs_defrag_file(entries[i].first_cluster, bitmap, tot_clusters, moved);
        }
    }
    free(entries);
}
static void ntfs_diagnose_part(u32 part_lba) {
    u8 vbr[512];
    int errors = 0;
    char status_msg[128];

    if (read_sec(part_lba, vbr, 1) != 0) {
        SetWindowTextA(g_hStatusBar, "NTFS Diag: [FAIL] Could not read VBR sector.");
        return;
    }

    /* 1. Verify VBR Magic Numbers */
    if (vbr[0] != 0xEB || vbr[1] != 0x52 || vbr[2] != 0x90) errors++;
    if (memcmp(vbr + 3, "NTFS    ", 8) != 0) errors++;
    
    u16 bps = (u16)vbr[0x0B] | ((u16)vbr[0x0C] << 8);
    if (bps != 512 && bps != 4096) errors++;
    
    if (vbr[0x15] != 0xF8) errors++;
    if (vbr[0x1FE] != 0x55 || vbr[0x1FF] != 0xAA) errors++;

    /* 2. Verify the "Hidden Sectors" Trap */
    u32 hidden_sectors = (u32)vbr[0x1C] | ((u32)vbr[0x1D] << 8) | ((u32)vbr[0x1E] << 16) | ((u32)vbr[0x1F] << 24);
    if (hidden_sectors != part_lba) errors++;

    /* 3. Get Geometry Metrics */
    u32 spc = vbr[0x0D]; 
    if (spc == 0) {
        SetWindowTextA(g_hStatusBar, "NTFS Diag: [FAIL] Sectors Per Cluster is 0.");
        return;
    }

    u64 total_sectors = (u64)vbr[0x28] | ((u64)vbr[0x29] << 8) | ((u64)vbr[0x2A] << 16) | ((u64)vbr[0x2B] << 24) |
                        ((u64)vbr[0x2C] << 32) | ((u64)vbr[0x2D] << 40) | ((u64)vbr[0x2E] << 48) | ((u64)vbr[0x2F] << 56);
    
    u64 mft_lcn      = (u64)vbr[0x30] | ((u64)vbr[0x31] << 8) | ((u64)vbr[0x32] << 16) | ((u64)vbr[0x33] << 24) |
                       ((u64)vbr[0x34] << 32) | ((u64)vbr[0x35] << 40) | ((u64)vbr[0x36] << 48) | ((u64)vbr[0x37] << 56);
    
    u64 mft_mirr_lcn = (u64)vbr[0x38] | ((u64)vbr[0x39] << 8) | ((u64)vbr[0x3A] << 16) | ((u64)vbr[0x3B] << 24) |
                       ((u64)vbr[0x3C] << 32) | ((u64)vbr[0x3D] << 40) | ((u64)vbr[0x3E] << 48) | ((u64)vbr[0x3F] << 56);

    /* 4. Total Sectors Sanity & MBR Alignment */
    if (total_sectors == 0) errors++;
    
    for (int i = 0; i < MAX_MBR_PARTS; i++) {
        if (g_vhd.parts[i].used && g_vhd.parts[i].lba_begin == part_lba) {
            if (total_sectors > g_vhd.parts[i].lba_count) {
                errors++; 
            }
            break;
        }
    }

    /* 5. $MFT & $MFTMirr Out-of-Bounds Check */
    u64 total_clusters = total_sectors / spc;
    if (mft_lcn >= total_clusters) errors++;
    if (mft_mirr_lcn >= total_clusters) errors++;

    /* 6. MFT and Index Record Sizes Check */
    signed char mft_sz = (signed char)vbr[0x40];
    if (mft_sz > 0 && mft_sz > 4) errors++;       
    else if (mft_sz < 0 && mft_sz != -10) errors++; 

    signed char idx_sz = (signed char)vbr[0x44];
    if (idx_sz > 0 && idx_sz > 16) errors++;
    else if (idx_sz < 0 && idx_sz < -16) errors++;  

    /* 7. Check the $MFT Target */
    u64 mft_lba = part_lba + (mft_lcn * spc);
    u8 mft_rec[512];
    if (read_sec((u32)mft_lba, mft_rec, 1) != 0) {
        errors++;
    } else if (memcmp(mft_rec, "FILE", 4) != 0) {
        errors++;
    }

    /* 8. Validate Core System Files (MFT Records 0 through 3) */
    u32 mft_rec_size = (mft_sz < 0) ? (1U << (-mft_sz)) : ((u32)mft_sz * spc * bps);
    u32 mft_secs = mft_rec_size / 512;
    if (mft_secs == 0) mft_secs = 2; /* Failsafe to 1024 bytes */
    
    u8 rec_buf[4096]; 
    for (int i = 0; i <= 3; i++) {
        u64 rec_lba = mft_lba + (i * mft_secs);
        
        if (read_sec((u32)rec_lba, rec_buf, mft_secs) != 0) {
            errors++;
            continue;
        }

        /* Check FRS (File Record Segment) Magic Number */
        if (memcmp(rec_buf, "FILE", 4) != 0) {
            errors++;
            continue;
        }

        /* Check In-Use Flag (Offset 0x16) */
        u16 flags = (u16)rec_buf[0x16] | ((u16)rec_buf[0x17] << 8);
        if (!(flags & 0x01)) {
            errors++;
        }

        /* Special Check for Record 3 ($Volume) */
        if (i == 3) {
            int found_vol_info = 0;
            u16 attr_off = (u16)rec_buf[0x14] | ((u16)rec_buf[0x15] << 8);
            
            if (attr_off < mft_rec_size) {
                u8 *a = rec_buf + attr_off;
                
                while (a + 8 <= rec_buf + mft_rec_size) {
                    u32 atype = (u32)a[0] | ((u32)a[1] << 8) | ((u32)a[2] << 16) | ((u32)a[3] << 24);
                    if (atype == 0xFFFFFFFF) break; 
                    
                    if (atype == 0x70) { /* $VOLUME_INFORMATION */
                        found_vol_info = 1;
                        u16 data_off = (u16)a[0x14] | ((u16)a[0x15] << 8);
                        
                        /* Check NTFS Version (Expected: Major 3, Minor 1) */
                        if (data_off + 10 <= mft_rec_size) {
                            u8 major_ver = a[data_off + 8];
                            u8 minor_ver = a[data_off + 9];
                            if (major_ver != 3 || minor_ver != 1) {
                                errors++; 
                            }
                        }
                        break;
                    }
                    
                    u32 alen = (u32)a[4] | ((u32)a[5] << 8) | ((u32)a[6] << 16) | ((u32)a[7] << 24);
                    if (alen == 0 || a + alen > rec_buf + mft_rec_size) break; 
                    a += alen;
                }
            }
            if (!found_vol_info) {
                errors++;
            }
        }
    }

    /* Output Final Results to Statusbar */
    if (errors == 0) {
        SetWindowTextA(g_hStatusBar, "NTFS Diag: [PASS] Geometry, Bounds, and VBR valid.");
    } else {
        snprintf(status_msg, sizeof(status_msg), "NTFS Diag: [FAIL] Found %d structural error(s).", errors);
        SetWindowTextA(g_hStatusBar, status_msg);
    }
}
static int ntfs_defrag_partition(int *moved) {
    *moved = 0;
    u8 b_rec[4096];
    if (ntfs_read_mft_record(6, b_rec) != 0) return -1;
    
    const u8 *data = ntfs_find_attr(b_rec, NTFS_AT_DATA, 0);
    if (!data) return -2;
    
    u64 bitmap_size = (data[8] & 1) ? rd64le(data + 0x30) : rd32le(data + 0x10);
    u8 *bitmap = (u8*)malloc((size_t)bitmap_size);
    if (!bitmap) return -3;
    
    /* Load $Bitmap into memory */
    u64 done = 0;
    while (done < bitmap_size) {
        u64 want = bitmap_size - done;
        if (want > 65536) want = 65536;
        int got = ntfs_read_attr_range(b_rec, data, done, want, bitmap + done);
        if (got <= 0) break;
        done += got;
    }
    
    u32 tot_clusters = (u32)(bitmap_size * 8);
    ntfs_defrag_recursive(NTFS_MFT_ROOT, bitmap, tot_clusters, moved);
    
    /* Write modified $Bitmap back to disk */
    if (data[8] & 1) {
        u16 run_off = rd16le(data + 0x20);
        const u8 *runs = data + run_off;
        int pos = 0; s64 lcn_acc = 0;
        u64 t_len; s64 t_lcn;
        u64 b_done = 0;
        
        while (1) {
            int r = ntfs_run_next(runs, &pos, &lcn_acc, &t_len, &t_lcn);
            if (r <= 0 || b_done >= bitmap_size) break;
            if (t_lcn >= 0) {
                u64 lba = g_ntfs_part_lba + t_lcn * g_ntfs_spc;
                u64 bytes = t_len * g_ntfs_clus_size;
                if (b_done + bytes > bitmap_size) bytes = bitmap_size - b_done;
                write_sec((u32)lba, bitmap + b_done, (u32)((bytes + 511) / 512));
                b_done += bytes;
            }
        }
    }
    free(bitmap);
    return 0;
}
static int ntfs_set_dirty(int dirty) {
    return ntfs_set_volume_flags(NTFS_VOLUME_IS_DIRTY, dirty ? 1 : 0);
}

static int ntfs_is_dirty(void) {
    u16 flags = 0;
    if (ntfs_get_volume_flags(&flags) == 0) {
        return (flags & NTFS_VOLUME_IS_DIRTY) ? 1 : 0;
    }
    return 0;
}

static u8* ntfs_add_attr_file_name(u8 *p, u64 parent_ref, const char *name, u64 ntfs_time, u32 flags, u64 alloc_sz, u64 data_sz, u8 namespace) {
    int name_len = 0; while (name[name_len]) name_len++;
    u32 content_len = 66 + (name_len * 2);
    u32 attr_len = (24 + content_len + 7) & ~7; 

    wr32le(p + 0, 0x30); wr32le(p + 4, attr_len); p[8] = 0; p[9] = 0;                             
    wr16le(p + 0x0A, 24); wr16le(p + 0x0C, 0); wr16le(p + 0x0E, 0);                  
    wr32le(p + 0x10, content_len); wr16le(p + 0x14, 24); p[0x16] = 1; p[0x17] = 0;                          

    wr64le(p + 24, parent_ref); wr64le(p + 32, ntfs_time); wr64le(p + 40, ntfs_time);            
    wr64le(p + 48, ntfs_time); wr64le(p + 56, ntfs_time);            
    wr64le(p + 64, alloc_sz); wr64le(p + 72, data_sz);              
    wr32le(p + 80, flags); wr32le(p + 84, 0);                    
    p[88] = (u8)name_len; p[89] = namespace; 

    for (int i = 0; i < name_len; i++) wr16le(p + 90 + (i * 2), (unsigned char)name[i]);
    memset(p + 90 + (name_len * 2), 0, attr_len - (90 + (name_len * 2)));
    return p + attr_len;
}

static void verify_my_generated_record_3_gui(u32 part_lba) {
    u8 vbr[512];
    char msg[1024] = {0};
    char temp[128];

    if (read_sec(part_lba, vbr, 1) != 0) {
        MessageBoxA(g_hMainWnd, "Failed to read VBR sector.", "MFT Record 3 Check", MB_ICONERROR);
        return;
    }

    u32 spc = vbr[0x0D]; 
    u64 mft_lcn = (u64)vbr[0x30] | ((u64)vbr[0x31] << 8) | ((u64)vbr[0x32] << 16) | ((u64)vbr[0x33] << 24);
    
    u64 rec3_lba = part_lba + (mft_lcn * spc) + 6; 
    
    u8 rec3[1024];
    if (read_sec((u32)rec3_lba, rec3, 2) != 0) {
        MessageBoxA(g_hMainWnd, "Failed to read MFT Record 3 sectors.", "MFT Record 3 Check", MB_ICONERROR);
        return;
    }

    sprintf(msg, "--- Checking Generated MFT Record 3 ---\n\n");
    
    sprintf(temp, "Signature: %.4s\n", rec3);
    strcat(msg, temp);
    
    u16 usa_off = *(u16*)(rec3 + 0x04);
    sprintf(temp, "USA Offset: %u (Should be 48 / 0x30)\n", usa_off);
    strcat(msg, temp);
    
    sprintf(temp, "USA Count: %u (Should be 3 for 1024b)\n", *(u16*)(rec3 + 0x06));
    strcat(msg, temp);
    
    u16 seq_num = *(u16*)(rec3 + 0x10);
    sprintf(temp, "Sequence Number: %u (MUST BE 3! If 0 or 1, ntfs.sys crashes)\n", seq_num);
    strcat(msg, temp);
    
    u16 fixup_usn = *(u16*)(rec3 + usa_off); 
    sprintf(temp, "Fixup USN: %04X\n", fixup_usn);
    strcat(msg, temp);
    
    u16 sec1_end = *(u16*)(rec3 + 0x1FE);
    sprintf(temp, "Sector 1 End (0x1FE): %04X (Must match Fixup USN)\n", sec1_end);
    strcat(msg, temp);
    
    u16 sec2_end = *(u16*)(rec3 + 0x3FE);
    sprintf(temp, "Sector 2 End (0x3FE): %04X (Must match Fixup USN)\n", sec2_end);
    strcat(msg, temp);

    /* ==================================================
       AUTOMATICALLY COPY THE GATHERED DATA TO CLIPBOARD 
       ================================================== */
    copy_to_clipboard(g_hMainWnd, msg);

    /* Automated Failure Detection */
    if (memcmp(rec3, "FILE", 4) != 0) {
        strcat(msg, "\n\n[CRITICAL FAILURE]: Missing 'FILE' signature!");
        MessageBoxA(g_hMainWnd, msg, "MFT Record 3 Check - FAILED", MB_ICONERROR);
    } else if (seq_num != 3) {
        strcat(msg, "\n\n[CRITICAL FAILURE]: Sequence Number is NOT 3! This specifically causes the Bad FRS event.");
        MessageBoxA(g_hMainWnd, msg, "MFT Record 3 Check - FAILED", MB_ICONERROR);
    } else if (sec1_end != fixup_usn || sec2_end != fixup_usn) {
        strcat(msg, "\n\n[CRITICAL FAILURE]: Fixup array (USA) not applied to sector ends! ntfs.sys will reject this.");
        MessageBoxA(g_hMainWnd, msg, "MFT Record 3 Check - FAILED", MB_ICONERROR);
    } else {
        strcat(msg, "\n\n[PASS]: Structure looks correct for Record 3.");
        MessageBoxA(g_hMainWnd, msg, "MFT Record 3 Check - PASSED", MB_ICONINFORMATION);
    }
}
static void set_local_path(const char* path) {
    strcpy(g_current_local_path, path);
    ListView_DeleteAllItems(g_hLocalListView);
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", g_current_local_path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        int i = 0;
        if (strlen(g_current_local_path) > 3) {
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i++; lvi.pszText = "..";
            SendMessageA(g_hLocalListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
            LVITEMA s = {0}; s.iSubItem = 1; s.pszText = "<DIR>";
            SendMessageA(g_hLocalListView, LVM_SETITEMTEXTA, 0, (LPARAM)&s);
        }
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (!g_show_hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.pszText = fd.cFileName;
            SendMessageA(g_hLocalListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
            char sz[64];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) strcpy(sz, "<DIR>");
            else format_size(((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow, sz, sizeof(sz));
            LVITEMA s = {0}; s.iSubItem = 1; s.pszText = sz;
            SendMessageA(g_hLocalListView, LVM_SETITEMTEXTA, i, (LPARAM)&s);
            i++;
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

static int fs_format_ntfs(int part_idx) {
    return fs_format_ntfs_ex(part_idx, 0);
}

/* ============================================================ UI LISTVIEW */
static void populate_vhd_listview(void) {
    ListView_DeleteAllItems(g_hVhdListView);
    if (!g_vhd.isOpen) return;

    if (g_view_mode == 0) {
        int i;
        for (i = 0; i < MAX_MBR_PARTS; i++) {
            char name[128], sz[64];
            if (g_vhd.parts[i].used) {
                format_size((u64)g_vhd.parts[i].lba_count * 512, sz, sizeof(sz));
                snprintf(name, sizeof(name), "Partition %d (%s)%s", i + 1, part_type_name(g_vhd.parts[i].type),
                         g_vhd.parts[i].boot == 0x80 ? " [Active]" : "");
            } else {
                strcpy(sz, "-");
                snprintf(name, sizeof(name), "Partition %d (empty)", i + 1);
            }
            LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.pszText = name;
            SendMessageA(g_hVhdListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
            LVITEMA s = {0}; s.iSubItem = 1; s.pszText = sz;
            SendMessageA(g_hVhdListView, LVM_SETITEMTEXTA, i, (LPARAM)&s);
        }
    } else {
        int i = 0;
        int is_root = 0;
        if (g_ntfs) is_root = (g_ntfs_cur_dir == NTFS_MFT_ROOT);
        else is_root = (g_current_dir_cluster == g_root_cluster || (g_fat_type == 16 && g_current_dir_cluster == 0));
        
        LVITEMA lvi = {0}; lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.pszText = "..";
        SendMessageA(g_hVhdListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi);
        LVITEMA s = {0}; s.iSubItem = 1; s.pszText = is_root ? "<UNMOUNT>" : "<DIR>";
        SendMessageA(g_hVhdListView, LVM_SETITEMTEXTA, i, (LPARAM)&s);
        i++;

        for (int k = 0; k < g_fs_entry_count; k++) {
            if (strcmp(g_fs_entries[k].name, ".") == 0 || strcmp(g_fs_entries[k].name, "..") == 0) continue;
            LVITEMA lvi2 = {0}; lvi2.mask = LVIF_TEXT; lvi2.iItem = i; lvi2.pszText = g_fs_entries[k].name;
            SendMessageA(g_hVhdListView, LVM_INSERTITEMA, 0, (LPARAM)&lvi2);
            char sz[64];
            if (g_fs_entries[k].is_directory) strcpy(sz, "<DIR>");
            else format_size(g_fs_entries[k].size, sz, sizeof(sz));
            LVITEMA s2 = {0}; s2.iSubItem = 1; s2.pszText = sz;
            SendMessageA(g_hVhdListView, LVM_SETITEMTEXTA, i, (LPARAM)&s2);
            i++;
        }
    }
}

static void navigate_local(HWND hwnd, int item) {
    char name[256], type[64];
    ListView_GetItemText(g_hLocalListView, item, 0, name, sizeof(name));
    ListView_GetItemText(g_hLocalListView, item, 1, type, sizeof(type));
    if (strcmp(type, "<DIR>") == 0) {
        if (strcmp(name, "..") == 0) {
            char *p = strrchr(g_current_local_path, '\\');
            if (p && p != g_current_local_path) {
                *p = '\0';
                if (g_current_local_path[0] != '\0' && g_current_local_path[1] == ':' && g_current_local_path[2] == '\0')
                    strcat(g_current_local_path, "\\");
            }
        } else {
            if (g_current_local_path[strlen(g_current_local_path)-1] != '\\') strcat(g_current_local_path, "\\");
            strcat(g_current_local_path, name);
        }
        set_local_path(g_current_local_path);
    }
}

static void navigate_vhd(HWND hwnd, int item) {
    if (g_view_mode == 0) {
        if (g_vhd.parts[item].used) {
            if (fs_mount_any(item) == 0) {
                g_view_mode = 1;
                if (g_ntfs) ntfs_list_dir(g_ntfs_cur_dir);
                else fs_list(g_current_dir_cluster);
                populate_vhd_listview();
            } else {
                MessageBoxA(hwnd, "Failed to mount partition. (Not FAT/NTFS or unformatted)", "Error", MB_ICONERROR);
            }
        }
    } else {
        char name[256];
        ListView_GetItemText(g_hVhdListView, item, 0, name, sizeof(name));
        if (strcmp(name, "..") == 0) {
            int is_root = 0;
            if (g_ntfs) is_root = (g_ntfs_cur_dir == NTFS_MFT_ROOT);
            else is_root = (g_current_dir_cluster == g_root_cluster || (g_fat_type == 16 && g_current_dir_cluster == 0));
            
            if (is_root) {
                g_view_mode = 0; g_vhd.fs_mounted = 0; g_ntfs = 0;
                populate_vhd_listview();
            } else {
                if (g_ntfs) {
                    g_ntfs_cur_dir = NTFS_MFT_ROOT;
                    ntfs_list_dir(g_ntfs_cur_dir);
                } else {
                    u32 pclus = 0;
                    for(int k=0; k<g_fs_entry_count; k++) {
                        if (strcmp(g_fs_entries[k].name, "..") == 0) {
                            pclus = (u32)g_fs_entries[k].first_cluster;
                            if (pclus == 0 && g_fat_type == 32) pclus = g_root_cluster;
                            break;
                        }
                    }
                    g_current_dir_cluster = pclus;
                    fs_list(g_current_dir_cluster);
                }
                populate_vhd_listview();
            }
        } else {
            for (int k = 0; k < g_fs_entry_count; k++) {
                if (strcmp(g_fs_entries[k].name, name) == 0 && g_fs_entries[k].is_directory) {
                    if (g_ntfs) {
                        g_ntfs_cur_dir = g_fs_entries[k].first_cluster;
                        ntfs_list_dir(g_ntfs_cur_dir);
                    } else {
                        g_current_dir_cluster = (u32)g_fs_entries[k].first_cluster;
                        fs_list(g_current_dir_cluster);
                    }
                    populate_vhd_listview();
                    break;
                }
            }
        }
    }
}

/* ============================================================ UI LISTVIEW & COMMANDS */
static void cmd_clone_physical(HWND hwnd) {
    char drive_path[64];
    if (!ShowDriveSelectBox(hwnd, drive_path)) return;

    OPENFILENAMEA sfn = {0};
    char szVhd[MAX_PATH] = "";
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szVhd; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "VHD Files (*.vhd)\0*.vhd\0";
    sfn.lpstrDefExt = "vhd";
    sfn.Flags = OFN_OVERWRITEPROMPT;
    sfn.lpstrTitle = "Save Cloned VHD as...";
    if (!GetSaveFileNameA(&sfn)) return;

    HANDLE hIn = CreateFileA(drive_path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hIn == INVALID_HANDLE_VALUE) { MessageBoxA(hwnd, "Cannot open physical drive.", "Error", MB_ICONERROR); return; }

    GET_LENGTH_INFORMATION gli; DWORD ret;
    if (!DeviceIoControl(hIn, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &gli, sizeof(gli), &ret, NULL)) {
        CloseHandle(hIn); MessageBoxA(hwnd, "Cannot determine drive size.", "Error", MB_ICONERROR); return;
    }
    u64 fileSize = gli.Length.QuadPart;
    u64 cap = (fileSize + 511) & ~511ULL;

    HANDLE hOut = CreateFileA(szVhd, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hOut == INVALID_HANDLE_VALUE) { CloseHandle(hIn); MessageBoxA(hwnd, "Cannot create VHD.", "Error", MB_ICONERROR); return; }

    u8 *buf = (u8*)malloc(1048576);
    if (!buf) { CloseHandle(hIn); CloseHandle(hOut); return; }
    
    DWORD bytesRead, bytesWritten;
    u64 copied = 0;
    ShowProgress(TRUE);

    while (ReadFile(hIn, buf, 1048576, &bytesRead, NULL) && bytesRead > 0) {
        if (g_cancel_operation) break;
        WriteFile(hOut, buf, bytesRead, &bytesWritten, NULL);
        copied += bytesRead;
        if (copied % (1024 * 1024 * 10) == 0 || copied == fileSize) {
            UpdateProgress((int)((copied * 100) / fileSize));
        }
    }
    
    free(buf);
    CloseHandle(hIn);

    if (g_cancel_operation) {
        CloseHandle(hOut);
        DeleteFileA(szVhd);
        ShowProgress(FALSE);
        SetWindowTextA(g_hStatusBar, "Physical Clone cancelled.");
        return;
    }

    if (cap > copied) {
        u8 pad[512] = {0};
        WriteFile(hOut, pad, (DWORD)(cap - copied), &bytesWritten, NULL);
    }

    u8 footer[512];
    vhd_build_footer(footer, cap);
    WriteFile(hOut, footer, 512, &bytesWritten, NULL);
    CloseHandle(hOut);
    ShowProgress(FALSE);

    if (vhd_open(szVhd) == 0) {
        UpdateMRU(szVhd);
        populate_vhd_listview();
        SetWindowTextA(g_hStatusBar, "Clone complete. VHD Opened.");
    }
}

static void cmd_convert_img(HWND hwnd) {
    OPENFILENAMEA ofn = {0};
    char szImg[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szImg; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Raw Images (*.img;*.bin;*.iso)\0*.img;*.bin;*.iso\0All Files\0*.*\0";
    ofn.lpstrTitle = "Select source .img to convert";
    if (!GetOpenFileNameA(&ofn)) return;

    char szVhd[MAX_PATH] = "";
    OPENFILENAMEA sfn = {0};
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szVhd; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "VHD Files (*.vhd)\0*.vhd\0";
    sfn.lpstrDefExt = "vhd";
    sfn.Flags = OFN_OVERWRITEPROMPT;
    sfn.lpstrTitle = "Save converted VHD as...";
    if (!GetSaveFileNameA(&sfn)) return;

    FILE *fin = fopen(szImg, "rb");
    if (!fin) { MessageBoxA(hwnd, "Cannot open source file.", "Error", MB_ICONERROR); return; }
    
    FILE *fout = fopen(szVhd, "wb");
    if (!fout) { fclose(fin); MessageBoxA(hwnd, "Cannot create VHD file.", "Error", MB_ICONERROR); return; }

    fseek(fin, 0, SEEK_END);
    u64 fileSize = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    u64 cap = (fileSize + 511) & ~511ULL;

    u8 buf[1048576];
    size_t bytes;
    ShowProgress(TRUE);
    u64 copied = 0;

    while ((bytes = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (g_cancel_operation) break;
        fwrite(buf, 1, bytes, fout);
        copied += bytes;
        if (copied % (1024 * 1024 * 10) == 0 || copied == fileSize) {
            UpdateProgress((int)((copied * 100) / fileSize));
        }
    }
    
    if (g_cancel_operation) {
        fclose(fin); fclose(fout);
        DeleteFileA(szVhd);
        ShowProgress(FALSE);
        SetWindowTextA(g_hStatusBar, "Conversion cancelled.");
        return;
    }

    if (cap > copied) {
        u8 pad[512] = {0};
        fwrite(pad, 1, cap - copied, fout);
    }
    
    u8 footer[512];
    vhd_build_footer(footer, cap);
    fwrite(footer, 1, 512, fout);
    
    fclose(fin);
    fclose(fout);
    ShowProgress(FALSE);
    
    if (vhd_open(szVhd) == 0) {
        UpdateMRU(szVhd);
        populate_vhd_listview();
        SetWindowTextA(g_hStatusBar, "Conversion complete. Fixed VHD Opened.");
    }
}

static void cmd_qemu_boot(HWND hwnd) {
    if (!g_vhd.isOpen) return;
    
    char bootFile[MAX_PATH] = "";
    if (MessageBoxA(hwnd, "Do you want to attach a bootable CD/Floppy image as well?", "QEMU Boot", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        OPENFILENAMEA ofn = {0};
        ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
        ofn.lpstrFile = bootFile; ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "Bootable Images (*.iso;*.img)\0*.iso;*.img\0All Files\0*.*\0";
        GetOpenFileNameA(&ofn);
    }

    char args[1024];
    if (strlen(bootFile) > 0) {
        snprintf(args, sizeof(args), "-hda \"%s\" -cdrom \"%s\" -boot d -m 512", g_vhd.path, bootFile);
    } else {
        snprintf(args, sizeof(args), "-hda \"%s\" -m 512", g_vhd.path);
    }

    if ((INT_PTR)ShellExecuteA(hwnd, "open", "qemu-system-i386", args, NULL, SW_SHOW) <= 32) {
        MessageBoxA(hwnd, "Failed to launch QEMU. Ensure 'qemu-system-i386' is in your system PATH.", "QEMU Error", MB_ICONERROR);
    }
}

static void cmd_extract_mbr(HWND hwnd) {
    if (!g_vhd.isOpen || !g_vhd.img) return;
    OPENFILENAMEA sfn = {0};
    char szFile[MAX_PATH] = "mbr.bin";
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szFile; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "Bin Files (*.bin)\0*.bin\0All Files\0*.*\0";
    sfn.lpstrDefExt = "bin";
    if (GetSaveFileNameA(&sfn)) {
        FILE *f = fopen(szFile, "wb");
        if (f) {
            fwrite(g_vhd.img + g_vhd.data_offset, 1, 512, f);
            fclose(f);
            SetWindowTextA(g_hStatusBar, "MBR extracted successfully.");
        }
    }
}

static void cmd_extract_vbr(HWND hwnd) {
    if (!g_vhd.isOpen || !g_vhd.img) return;
    int active_slot = -1;
    for (int i = 0; i < MAX_MBR_PARTS; i++) {
        if (g_vhd.parts[i].used && g_vhd.parts[i].boot == 0x80) { active_slot = i; break; }
    }
    if (active_slot == -1) {
        MessageBoxA(hwnd, "No active (bootable) partition found to extract VBR from.", "Error", MB_ICONWARNING);
        return;
    }
    OPENFILENAMEA sfn = {0};
    char szFile[MAX_PATH] = "vbr.bin";
    sfn.lStructSize = sizeof(sfn); sfn.hwndOwner = hwnd;
    sfn.lpstrFile = szFile; sfn.nMaxFile = MAX_PATH;
    sfn.lpstrFilter = "Bin Files (*.bin)\0*.bin\0All Files\0*.*\0";
    sfn.lpstrDefExt = "bin";
    if (GetSaveFileNameA(&sfn)) {
        FILE *f = fopen(szFile, "wb");
        if (f) {
            fwrite(g_vhd.img + g_vhd.data_offset + (g_vhd.parts[active_slot].lba_begin * 512), 1, 512, f);
            fclose(f);
            SetWindowTextA(g_hStatusBar, "Active VBR extracted successfully.");
        }
    }
}

static void cmd_replace_os_boot(HWND hwnd) {
    if (g_view_mode != 1 || g_ntfs) {
        MessageBoxA(hwnd, "Please mount a FAT partition first (NTFS not supported for boot-file replacement).", "Error", MB_ICONWARNING);
        return;
    }
    char target_name[32] = "IO.SYS";
    if (!ShowInputBox(hwnd, "Replace OS Boot File", "Target filename in current directory:", target_name)) return;

    u8 target_83[11];
    make_83_name(target_name, target_83);

    u8 sec[512];
    u32 cur = g_current_dir_cluster;
    int is_root16 = (g_fat_type == 16 && cur == 0);
    u32 sec_idx = 0, found_lba = 0, found_off = 0, old_clus = 0;

    while (!found_lba) {
        u32 lba = is_root16 ? (g_root_lba + sec_idx) : (cluster_to_lba(cur) + sec_idx);
        if (read_sec(lba, sec, 1) != 0) break;
        for (int i = 0; i < 512; i += 32) {
            u8 *ent = sec + i;
            if (ent[0] == 0) break;
            if (ent[0] == 0xE5 || (ent[11] & 0x0F) == 0x0F) continue;
            if (memcmp(ent, target_83, 11) == 0) {
                found_lba = lba;
                found_off = i;
                old_clus = (rd16le(ent + 20) << 16) | rd16le(ent + 26);
                break;
            }
        }
        if (found_lba) break;
        sec_idx++;
        if (is_root16 && sec_idx >= g_root_secs) break;
        if (!is_root16 && sec_idx >= g_sec_per_clus) {
            sec_idx = 0;
            cur = read_fat(cur);
            if (cur >= 0x0FFFFFF8) break;
        }
    }

    if (!found_lba) {
        MessageBoxA(hwnd, "Target file not found in current directory.", "Error", MB_ICONERROR);
        return;
    }

    OPENFILENAMEA ofn = {0};
    char szHost[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szHost; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.lpstrTitle = "Select replacement file";
    if (!GetOpenFileNameA(&ofn)) return;

    FILE *f = fopen(szHost, "rb");
    if (!f) { MessageBoxA(hwnd, "Cannot open host file.", "Error", MB_ICONERROR); return; }
    fseek(f, 0, SEEK_END);
    u32 sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Free old clusters securely */
    if (old_clus >= 2) {
        u32 c = old_clus;
        u8 z[512] = {0};
        while (c >= 2 && c < 0x0FFFFFF0) {
            u32 n = read_fat(c);
            u32 clba = cluster_to_lba(c);
            for(u32 j=0; j<g_sec_per_clus; j++) write_sec(clba+j, z, 1);
            write_fat(c, 0);
            c = n;
        }
    }

    /* Write new file data */
    u32 first_clus = 0;
    if (sz > 0) {
        first_clus = alloc_cluster();
        if (!first_clus) { fclose(f); return; }
        u32 cur_clus = first_clus;
        u32 rem = sz;
        u8 buf[512];
        while (rem > 0) {
            u32 lba = cluster_to_lba(cur_clus);
            for (u32 i = 0; i < g_sec_per_clus && rem > 0; i++) {
                u32 chunk = rem > 512 ? 512 : rem;
                memset(buf, 0, 512);
                fread(buf, 1, chunk, f);
                write_sec(lba + i, buf, 1);
                rem -= chunk;
            }
            if (rem > 0) {
                u32 nclus = alloc_cluster();
                if (!nclus) break;
                write_fat(cur_clus, nclus);
                cur_clus = nclus;
            }
        }
    }
    fclose(f);

    /* Update existing directory entry precisely in place */
    read_sec(found_lba, sec, 1);
    u8 *ent = sec + found_off;
    wr16le(ent + 20, first_clus >> 16);
    wr16le(ent + 26, first_clus & 0xFFFF);
    wr32le(ent + 28, sz);
    write_sec(found_lba, sec, 1);

    fs_list(g_current_dir_cluster);
    populate_vhd_listview();
    SetWindowTextA(g_hStatusBar, "OS Boot file replaced successfully.");
}

static void cmd_write_mbr(HWND hwnd) {
    if (!g_vhd.isOpen || !g_vhd.img) return;
    if (MessageBoxA(hwnd, "Write standard Windows/DOS MBR? This will overwrite existing bootloader code, but preserve partitions.", "Write MBR", MB_YESNO | MB_ICONWARNING) != IDYES) return;

    static const u8 std_mbr[424] = {
        0xFA, 0x33, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8B, 0xF4, 0x50, 0x07, 0x50, 0x1F, 0xFB, 0xFC,
        0xBF, 0x00, 0x06, 0xB9, 0x00, 0x01, 0xF2, 0xA5, 0xEA, 0x1D, 0x06, 0x00, 0x00, 0xBE, 0xBE, 0x07,
        0xB3, 0x04, 0x80, 0x3C, 0x80, 0x74, 0x0E, 0x83, 0xC6, 0x10, 0xFE, 0xCB, 0x75, 0xF4, 0xCD, 0x18,
        0x8B, 0x14, 0x8B, 0x4C, 0x02, 0x8B, 0xEE, 0x83, 0xC6, 0x10, 0xFE, 0xCB, 0x74, 0x1A, 0x80, 0x3C,
        0x00, 0x74, 0xF4, 0xBE, 0x8B, 0x06, 0xAC, 0x3C, 0x00, 0x74, 0x0B, 0x56, 0xBB, 0x07, 0x00, 0xB4,
        0x0E, 0xCD, 0x10, 0x5E, 0xEB, 0xF0, 0xEB, 0xFE, 0xBF, 0x05, 0x00, 0xBB, 0x00, 0x7C, 0xB8, 0x01,
        0x02, 0xCD, 0x13, 0x73, 0x0C, 0x33, 0xC0, 0xCD, 0x13, 0x4F, 0x75, 0xED, 0xBE, 0xA3, 0x06, 0xEB,
        0xD3, 0xBE, 0xC2, 0x06, 0xBF, 0xFE, 0x7D, 0x81, 0x3D, 0x55, 0xAA, 0x75, 0xC7, 0x8B, 0xF5, 0xEA,
        0x00, 0x7C, 0x00, 0x00, 0x49, 0x6E, 0x76, 0x61, 0x6C, 0x69, 0x64, 0x20, 0x70, 0x61, 0x72, 0x74,
        0x69, 0x74, 0x69, 0x6F, 0x6E, 0x20, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x00, 0x45, 0x72, 0x72, 0x6F,
        0x72, 0x20, 0x6C, 0x6F, 0x61, 0x64, 0x69, 0x6E, 0x67, 0x20, 0x6F, 0x70, 0x65, 0x72, 0x61, 0x74,
        0x69, 0x6E, 0x67, 0x20, 0x73, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x00, 0x4D, 0x69, 0x73, 0x73, 0x69,
        0x6E, 0x67, 0x20, 0x6F, 0x70, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6E, 0x67, 0x20, 0x73, 0x79, 0x73,
        0x74, 0x65, 0x6D, 0x00
    };

    u8 *mbr = g_vhd.img + g_vhd.data_offset;
    memcpy(mbr, std_mbr, sizeof(std_mbr));
    SetWindowTextA(g_hStatusBar, "Standard MBR written. Save VHD to commit.");
}

static void cmd_write_vbr(HWND hwnd) {
    if (!g_vhd.isOpen || g_view_mode != 0) return;
    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= MAX_MBR_PARTS || !g_vhd.parts[sel].used) {
        MessageBoxA(hwnd, "Select a valid partition to inject the VBR into.", "Error", MB_ICONWARNING);
        return;
    }

    OPENFILENAMEA ofn = {0};
    char szBin[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szBin; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Bootsectors (*.bin)\0*.bin\0All Files\0*.*\0";
    if (!GetOpenFileNameA(&ofn)) return;

    FILE *f = fopen(szBin, "rb");
    if (!f) return;
    u8 vbr[512] = {0};
    fread(vbr, 1, 512, f);
    fclose(f);

    u8 *part_boot = g_vhd.img + g_vhd.data_offset + (g_vhd.parts[sel].lba_begin * 512);
    
    memcpy(part_boot, vbr, 11);
    
    if (g_vhd.parts[sel].type == 0x0B || g_vhd.parts[sel].type == 0x0C) { 
        memcpy(part_boot + 90, vbr + 90, 512 - 90 - 2); 
    } else { 
        memcpy(part_boot + 62, vbr + 62, 512 - 62 - 2); 
    }
    
    part_boot[510] = 0x55; part_boot[511] = 0xAA;
    SetWindowTextA(g_hStatusBar, "VBR injected successfully. Save VHD to commit.");
}

/* ============================================================ STANDARD COMMANDS */
static void cmd_vhd_open_dialog(HWND hwnd) {
    OPENFILENAMEA ofn = {0};
    char szFile[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "VHD Files (*.vhd)\0*.vhd\0All Files\0*.*\0";
    if (!GetOpenFileNameA(&ofn)) return;
    int res = vhd_open(szFile);
    if (res == 0) {
        UpdateMRU(szFile);
        populate_vhd_listview();
        char s[512]; snprintf(s, sizeof(s), "Opened: %s (%.1f MB)", szFile, g_vhd.cap / 1048576.0);
        SetWindowTextA(g_hStatusBar, s);
    } else if (res == -3) {
        MessageBoxA(hwnd, "Only fixed-size VHD images are supported.", "Unsupported VHD", MB_ICONERROR);
    } else {
        MessageBoxA(hwnd, "Failed to open a valid fixed VHD image.", "Error", MB_ICONERROR);
    }
}

static void cmd_new_vhd(HWND hwnd) {
    char buf[32] = "100";
    if (!ShowInputBox(hwnd, "New VHD", "Size in megabytes:", buf)) return;
    int mb = atoi(buf);
    if (mb < 1 || mb > 2040) { MessageBoxA(hwnd, "Size must be 1-2040 MB.", "New VHD", MB_ICONWARNING); return; }

    OPENFILENAMEA ofn = {0};
    char path[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "VHD Files (*.vhd)\0*.vhd\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "vhd";
    
    if (!GetSaveFileNameA(&ofn)) return;

    if (vhd_create(path, (u32)mb) != 0) { MessageBoxA(hwnd, "Failed to create VHD file.", "Error", MB_ICONERROR); return; }
    if (vhd_open(path) != 0) { MessageBoxA(hwnd, "Created but failed to reopen VHD.", "Error", MB_ICONERROR); return; }
    UpdateMRU(path);
    populate_vhd_listview();
    SetWindowTextA(g_hStatusBar, "New empty VHD created. Use Partition > Create to add one.");
}

static void cmd_save(HWND hwnd) {
    if (!g_vhd.isOpen) return;
    if (vhd_save() == 0) SetWindowTextA(g_hStatusBar, "VHD saved (footer checksum rebuilt).");
    else MessageBoxA(hwnd, "Failed to save VHD.", "Error", MB_ICONERROR);
}

static void cmd_resize(HWND hwnd) {
    char buf[32];
    if (!g_vhd.isOpen) return;
    snprintf(buf, sizeof(buf), "%u", (u32)(g_vhd.cap / 1048576));
    if (!ShowInputBox(hwnd, "Resize VHD Container", "New size in megabytes:", buf)) return;
    int mb = atoi(buf);
    int rc = vhd_resize((u32)mb);
    if (rc == 0) { populate_vhd_listview(); SetWindowTextA(g_hStatusBar, "VHD container resized. Data preserved."); }
    else if (rc == -2) MessageBoxA(hwnd, "Shrink refused: a partition extends beyond the new bounds.", "Resize", MB_ICONWARNING);
    else MessageBoxA(hwnd, "Resize failed (out of memory?).", "Resize", MB_ICONERROR);
}

static void cmd_extract_selected(HWND hwnd) {
    if (g_view_mode != 1) return;
    int sel = -1;
    int extracted = 0, failed = 0;
    while ((sel = ListView_GetNextItem(g_hVhdListView, sel, LVNI_SELECTED)) != -1) {
        char name[256];
        ListView_GetItemText(g_hVhdListView, sel, 0, name, sizeof(name));
        if (strcmp(name, "..") == 0) continue;
        
        char dest[MAX_PATH];
        snprintf(dest, sizeof(dest), "%s\\%s", g_current_local_path, name);
        
        if (g_ntfs) {
            for (int k = 0; k < g_fs_entry_count; k++) {
                if (strcmp(g_fs_entries[k].name, name) == 0) {
                    if (g_fs_entries[k].is_directory) {
                        if (ntfs_extract_recursive(g_fs_entries[k].first_cluster, dest) == 0) extracted++; else failed++;
                    } else {
                        if (ntfs_extract_file(g_fs_entries[k].first_cluster, dest) == 0) extracted++; else failed++;
                    }
                    break;
                }
            }
        } else {
            int eidx = -1;
            for(int k=0; k<g_fs_entry_count; k++) {
                if (strcmp(g_fs_entries[k].name, name) == 0) { eidx = k; break; }
            }
            if (eidx != -1 && !g_fs_entries[eidx].is_directory) {
                if (fs_extract(eidx, dest) == 0) extracted++;
                else failed++;
            }
        }
    }
    if (extracted > 0 || failed > 0) {
        set_local_path(g_current_local_path);
        char msg[128];
        snprintf(msg, sizeof(msg), "Extracted: %d, Failed: %d.", extracted, failed);
        SetWindowTextA(g_hStatusBar, msg);
    }
}

static void cmd_add_selected(HWND hwnd) {
    if (g_view_mode != 1) {
        MessageBoxA(hwnd, "Navigate into a FAT/NTFS partition first.", "Error", MB_ICONWARNING);
        return;
    }
    int sel = -1;
    int added = 0, failed = 0;
    while ((sel = ListView_GetNextItem(g_hLocalListView, sel, LVNI_SELECTED)) != -1) {
        char name[256];
        ListView_GetItemText(g_hLocalListView, sel, 0, name, sizeof(name));
        if (strcmp(name, "..") == 0) continue;
        char src[MAX_PATH];
        snprintf(src, sizeof(src), "%s\\%s", g_current_local_path, name);
        
        if (g_ntfs) {
            if (ntfs_import_recursive(src, g_ntfs_cur_dir) == 0) added++; else failed++;
        } else {
            if (import_recursive(src, g_current_dir_cluster) == 0) added++; else failed++;
        }
    }
    
    if (added > 0 || failed > 0) {
        if (g_ntfs) ntfs_list_dir(g_ntfs_cur_dir);
        else        fs_list(g_current_dir_cluster);
        populate_vhd_listview();
        char msg[128];
        snprintf(msg, sizeof(msg), "Imported: %d, Failed: %d.", added, failed);
        SetWindowTextA(g_hStatusBar, msg);
    }
}

static void cmd_delete_selected(HWND hwnd) {
    if (g_view_mode != 1) return;
    int sel = -1;
    int deleted = 0, failed = 0;
    while ((sel = ListView_GetNextItem(g_hVhdListView, sel, LVNI_SELECTED)) != -1) {
        char name[256];
        ListView_GetItemText(g_hVhdListView, sel, 0, name, sizeof(name));
        if (strcmp(name, "..") == 0) continue;
        
        if (g_ntfs) {
            for (int k = 0; k < g_fs_entry_count; k++) {
                if (strcmp(g_fs_entries[k].name, name) == 0) {
                    if (ntfs_delete_by_ref(g_fs_entries[k].first_cluster) == 0) deleted++;
                    else failed++;
                    break;
                }
            }
        } else {
            int eidx = -1;
            for(int k=0; k<g_fs_entry_count; k++) {
                if (strcmp(g_fs_entries[k].name, name) == 0) { eidx = k; break; }
            }
            if (eidx != -1) {
                if (fs_delete(eidx) == 0) deleted++;
                else failed++;
            }
        }
    }
    if (deleted > 0) {
        if (g_ntfs) ntfs_list_dir(g_ntfs_cur_dir);
        else        fs_list(g_current_dir_cluster);
        populate_vhd_listview();
        char msg[128];
        snprintf(msg, sizeof(msg), "Deleted: %d, Failed: %d.", deleted, failed);
        SetWindowTextA(g_hStatusBar, msg);
    }
}

/* ============================================================ WINDOW PROC */
static void init_listview_columns(HWND hLv) {
    LVCOLUMNA lvc = {0}; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
    lvc.cx = 350; lvc.pszText = (LPSTR)"Name";
    SendMessageA(hLv, LVM_INSERTCOLUMNA, 0, (LPARAM)&lvc);
    lvc.cx = 150; lvc.pszText = (LPSTR)"Size";
    SendMessageA(hLv, LVM_INSERTCOLUMNA, 1, (LPARAM)&lvc);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            HMENU hMenu = CreateMenu(), hFile = CreatePopupMenu();
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_NEW,   "&New...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_OPEN,  "&Open...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_SAVE,  "&Save\tCtrl+S");
AppendMenuA(hFile, MF_STRING, IDM_IMAGE_CLOSE, "&Close");
            AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_CLONE_PHYSICAL, "Create VHD from &Physical Disk...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_CONVERT, "&Convert .img to .vhd...");
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_QEMU_BOOT, "Boot in &QEMU...");
            AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFile, MF_STRING, IDM_IMAGE_QUIT,  "&Quit");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&File");

            HMENU hCreatePart = CreatePopupMenu();
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT12,  "FAT12 (0x01)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16_S,"FAT16 <32MB (0x04)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16,  "FAT16 >32MB (0x06)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32,  "FAT32 CHS (0x0B)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32L, "FAT32 LBA (0x0C)");
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16L, "FAT16 LBA (0x0E)");
            AppendMenuA(hCreatePart, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_NTFS,   "NTFS/exFAT (0x07)");

            HMENU hPart = CreatePopupMenu();
            AppendMenuA(hPart, MF_STRING, IDM_PART_LIST,   "&Properties");
            AppendMenuA(hPart, MF_POPUP, (UINT_PTR)hCreatePart, "&Create Partition");
            AppendMenuA(hPart, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hPart, MF_STRING, IDM_PART_COMPACT, "&Compact (Zero Free Space)");
            AppendMenuA(hPart, MF_STRING, IDM_PART_DEFRAG, "Defrag&ment Files");
            AppendMenuA(hPart, MF_STRING, IDM_PART_ACTIVE, "Set &Active (Bootable)");
            AppendMenuA(hPart, MF_STRING, IDM_PART_RESIZE, "&Resize Partition...");
            AppendMenuA(hPart, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hPart, MF_STRING, IDM_PART_VBR_FILE, "Write &VBR from File...");
            AppendMenuA(hPart, MF_STRING, IDM_PART_REPLACE_BOOT, "Replace OS &Boot File...");
            AppendMenuA(hPart, MF_STRING, IDM_PART_FORMAT, "&Format (FAT)...");
AppendMenuA(hPart, MF_STRING, IDM_PART_FORMAT_NTFS, "Format (&NTFS)...");
AppendMenuA(hPart, MF_STRING, IDM_PART_DIAGNOSE_RAW, "Diagnose RAW Issue");
AppendMenuA(hPart, MF_STRING, IDM_PART_NTFS_DIRTY,  "Toggle NTFS &Dirty Flag");
            AppendMenuA(hPart, MF_STRING, IDM_PART_DELETE, "&Delete Partition...");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hPart, "&Partition");

            HMENU hDisk = CreatePopupMenu();
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_EXTRACT_MBR, "Extract &MBR...");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_EXTRACT_VBR, "Extract Active &VBR...");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_MBR_STD, "Write &Standard MBR");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_RESIZE, "&Resize Disk Container...");
            AppendMenuA(hDisk, MF_STRING, IDM_DISK_TRIM,   "&Trim VHD to Last Partition");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hDisk, "&Disk");

            HMENU hHelp = CreatePopupMenu();
            AppendMenuA(hHelp, MF_STRING, IDM_HELP_ABOUT, "&About");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");
            SetMenu(hwnd, hMenu);

            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS };
            InitCommonControlsEx(&icc);

            g_hLocalListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd, (HMENU)IDC_LOCAL_LIST, g_hInstance, NULL);
            g_hVhdListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd, (HMENU)IDC_VHD_LIST, g_hInstance, NULL);
            init_listview_columns(g_hLocalListView);
            init_listview_columns(g_hVhdListView);
            g_hStatusBar = CreateWindowExA(0, STATUSCLASSNAMEA, "Ready. File > New to create a VHD.",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hProgressBar = CreateWindowExA(0, PROGRESS_CLASSA, NULL, WS_CHILD | PBS_SMOOTH,
                0, 0, 0, 0, hwnd, NULL, g_hInstance, NULL);
            g_hCancelBtn = CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | BS_PUSHBUTTON,
                0, 0, 0, 0, hwnd, (HMENU)2010, g_hInstance, NULL);

            DragAcceptFiles(hwnd, TRUE);

            char root[MAX_PATH];
            if (!GetEnvironmentVariableA("USERPROFILE", root, MAX_PATH)) strcpy(root, "C:\\");
            set_local_path(root);
            return 0;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right, half = rc.bottom / 2;
            SetWindowPos(g_hLocalListView, NULL, 0, 0, w, half, SWP_NOZORDER);
            SetWindowPos(g_hVhdListView,   NULL, 0, half, w, rc.bottom - half - 24, SWP_NOZORDER);
            SetWindowPos(g_hStatusBar,     NULL, 0, rc.bottom - 24, w, 24, SWP_NOZORDER);
            
            SetWindowPos(g_hProgressBar, NULL, w - 210, rc.bottom - 20, 150, 16, SWP_NOZORDER);
            SetWindowPos(g_hCancelBtn, NULL, w - 55, rc.bottom - 21, 50, 18, SWP_NOZORDER);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_dragging) {
                SetCursor(LoadCursor(NULL, IDC_CROSS));
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (g_dragging) {
                g_dragging = FALSE;
                ReleaseCapture();
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ClientToScreen(hwnd, &pt);
                RECT rcVhd;
                GetWindowRect(g_hVhdListView, &rcVhd);
                if (PtInRect(&rcVhd, pt)) {
                    cmd_add_selected(hwnd);
                }
            }
            break;
        }
        case WM_DROPFILES: {
            if (g_view_mode != 1) {
                MessageBoxA(hwnd, "Please navigate into a FAT/NTFS partition to drop files.", "Warning", MB_ICONWARNING);
                DragFinish((HDROP)wParam);
                break;
            }
            HDROP hDrop = (HDROP)wParam;
            UINT numFiles = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < numFiles; i++) {
                char path[MAX_PATH];
                DragQueryFileA(hDrop, i, path, MAX_PATH);
                if (g_ntfs) ntfs_import_recursive(path, g_ntfs_cur_dir);
                else import_recursive(path, g_current_dir_cluster);
            }
            DragFinish(hDrop);
            if (g_ntfs) ntfs_list_dir(g_ntfs_cur_dir);
            else fs_list(g_current_dir_cluster);
            populate_vhd_listview();
            SetWindowTextA(g_hStatusBar, "Dropped files imported successfully.");
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR nmh = (LPNMHDR)lParam;
            if (nmh->code == LVN_BEGINDRAG) {
                if (nmh->idFrom == IDC_LOCAL_LIST) {
                    g_dragging = TRUE;
                    SetCapture(hwnd);
                    SetCursor(LoadCursor(NULL, IDC_CROSS));
                }
            }
            else if (nmh->code == LVN_KEYDOWN) {
                LPNMLVKEYDOWN pnkd = (LPNMLVKEYDOWN)lParam;
                if (pnkd->wVKey == VK_RETURN) {
                    if (nmh->idFrom == IDC_VHD_LIST) {
                        int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                        if (sel >= 0) navigate_vhd(hwnd, sel);
                    } else if (nmh->idFrom == IDC_LOCAL_LIST) {
                        int sel = ListView_GetNextItem(g_hLocalListView, -1, LVNI_SELECTED);
                        if (sel >= 0) navigate_local(hwnd, sel);
                    }
                }
            }
            else if (nmh->code == NM_DBLCLK) {
                LPNMITEMACTIVATE lpnm = (LPNMITEMACTIVATE)lParam;
                if (lpnm->iItem >= 0) {
                    if (nmh->idFrom == IDC_VHD_LIST) navigate_vhd(hwnd, lpnm->iItem);
                    else if (nmh->idFrom == IDC_LOCAL_LIST) navigate_local(hwnd, lpnm->iItem);
                }
            }
            break;
        }
        case WM_CONTEXTMENU: {
            HWND hwndCtl = (HWND)wParam;
            POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
            if (hwndCtl == g_hVhdListView) {
                HMENU hMenu = CreatePopupMenu();
                if (g_view_mode == 1) {
                    AppendMenuA(hMenu, MF_STRING, ID_VHD_EXTRACT, "Extract File");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_REPLACE_BOOT, "Replace OS Boot File");
                    AppendMenuA(hMenu, MF_STRING, ID_VHD_DELETE, "Delete File");
                } else {
                    HMENU hCreatePart = CreatePopupMenu();
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT12,  "FAT12 (0x01)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16_S,"FAT16 <32MB (0x04)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16,  "FAT16 >32MB (0x06)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32,  "FAT32 CHS (0x0B)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT32L, "FAT32 LBA (0x0C)");
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_FAT16L, "FAT16 LBA (0x0E)");
                    AppendMenuA(hCreatePart, MF_SEPARATOR, 0, NULL);
                    AppendMenuA(hCreatePart, MF_STRING, IDM_PART_CREATE_NTFS,   "NTFS/exFAT (0x07)");
                    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hCreatePart, "Create Partition");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_FORMAT, "Format (FAT)");
AppendMenuA(hMenu, MF_STRING, IDM_PART_FORMAT_NTFS, "Format (NTFS)");
AppendMenuA(hMenu, MF_STRING, IDM_PART_DIAGNOSE_RAW, "Diagnose RAW Issue");
AppendMenuA(hMenu, MF_STRING, IDM_PART_NTFS_DIRTY,  "Toggle NTFS &Dirty Flag");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_COMPACT, "Compact (Zero Free Space)");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_DEFRAG, "Defragment Files");

                    AppendMenuA(hMenu, MF_STRING, IDM_PART_ACTIVE, "Set Active");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_RESIZE, "Resize Partition");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_VBR_FILE, "Write VBR from File");
                    AppendMenuA(hMenu, MF_STRING, IDM_PART_DELETE, "Delete");
                }
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            } else if (hwndCtl == g_hLocalListView) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_VHD_ADD, "Add to VHD");
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) >= IDM_MRU_1 && LOWORD(wParam) < IDM_MRU_1 + 5) {
                int idx = LOWORD(wParam) - IDM_MRU_1;
                if (strlen(g_mru[idx]) > 0 && vhd_open(g_mru[idx]) == 0) {
                    populate_vhd_listview();
                    SetWindowTextA(g_hStatusBar, g_mru[idx]);
                }
                return 0;
            }
            switch (LOWORD(wParam)) {
                case 2010: g_cancel_operation = TRUE; break;
                case IDM_IMAGE_NEW:   cmd_new_vhd(hwnd); break;
                case IDM_IMAGE_OPEN:  cmd_vhd_open_dialog(hwnd); break;
                case IDM_IMAGE_SAVE:  cmd_save(hwnd); break;
case IDM_IMAGE_CLOSE: 
                    if (g_vhd.isOpen) {
                        vhd_close();
                        populate_vhd_listview();
                        SetWindowTextA(g_hStatusBar, "VHD closed.");
                    }
                    break;
                case IDM_IMAGE_QUIT:  PostQuitMessage(0); break;
                
                case IDM_IMAGE_CLONE_PHYSICAL: cmd_clone_physical(hwnd); break;
                case IDM_IMAGE_CONVERT:   cmd_convert_img(hwnd); break;
                case IDM_IMAGE_QEMU_BOOT: cmd_qemu_boot(hwnd); break;

                case IDM_PART_LIST:   if (g_vhd.isOpen) part_show_properties(hwnd); break;
                case IDM_PART_CREATE_FAT12:  if (g_vhd.isOpen) { int s = part_create_fat(hwnd, 0x01); if(s>=0) fs_format_partition(s, 0); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT16_S:if (g_vhd.isOpen) { int s = part_create_fat(hwnd, 0x04); if(s>=0) fs_format_partition(s, 0); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT16:  if (g_vhd.isOpen) { int s = part_create_fat(hwnd, 0x06); if(s>=0) fs_format_partition(s, 0); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT32:  if (g_vhd.isOpen) { int s = part_create_fat(hwnd, 0x0B); if(s>=0) fs_format_partition(s, 1); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT32L: if (g_vhd.isOpen) { int s = part_create_fat(hwnd, 0x0C); if(s>=0) fs_format_partition(s, 1); populate_vhd_listview(); } break;
                case IDM_PART_CREATE_FAT16L: if (g_vhd.isOpen) { int s = part_create_fat(hwnd, 0x0E); if(s>=0) fs_format_partition(s, 0); populate_vhd_listview(); } break;
case IDM_PART_CREATE_NTFS:
    if (g_vhd.isOpen) {
        int s = part_create_fat(hwnd, 0x07);
        if (s >= 0) {
            fs_format_ntfs(s);
            populate_vhd_listview();
            SetWindowTextA(g_hStatusBar, "Created and formatted NTFS partition.");
        }
    }
    break;
case IDM_PART_DIAGNOSE_RAW: {
    if (!g_vhd.isOpen || g_view_mode != 0) break;
    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
        ntfs_diagnose_part(g_vhd.parts[sel].lba_begin);
u32 target_lba = g_vhd.parts[sel].lba_begin;
verify_my_generated_record_3_gui(target_lba);
    } else {
        MessageBoxA(hwnd, "Please select a valid partition.", "VHD Master", MB_ICONWARNING);
    }
    break;
}
case IDM_PART_NTFS_DIRTY: {
    if (!g_vhd.isOpen) break;
    int sel = (g_view_mode == 0) ? ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED) : -1;
    if (g_view_mode == 1 && g_ntfs) {
        int dirty = ntfs_is_dirty();
        ntfs_set_dirty(!dirty);
        char msg[128];
        snprintf(msg, sizeof(msg), "NTFS volume marked %s.", !dirty ? "DIRTY" : "CLEAN");
        SetWindowTextA(g_hStatusBar, msg);
        MessageBoxA(hwnd, msg, "NTFS Dirty Flag", MB_ICONINFORMATION);
    } else if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used && g_vhd.parts[sel].type == 0x07) {
        if (fs_mount_any(sel) == 0 && g_ntfs) {
            int dirty = ntfs_is_dirty();
            ntfs_set_dirty(!dirty);
            char msg[128];
            snprintf(msg, sizeof(msg), "Partition %d NTFS volume marked %s.", sel + 1, !dirty ? "DIRTY" : "CLEAN");
            SetWindowTextA(g_hStatusBar, msg);
            MessageBoxA(hwnd, msg, "NTFS Dirty Flag", MB_ICONINFORMATION);
            g_vhd.fs_mounted = 0; g_ntfs = 0;
        }
    } else {
        MessageBoxA(hwnd, "Please select an NTFS partition.", "VHD Master", MB_ICONWARNING);
    }
    break;
}
case IDM_PART_FORMAT_NTFS: {
    if (!g_vhd.isOpen || g_view_mode != 0) break;
    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
        if (MessageBoxA(hwnd, "Format this partition as NTFS? All existing partition data will be lost.",
                        "Confirm Format", MB_YESNO | MB_ICONWARNING) == IDYES) {
            if (fs_format_ntfs(sel) == 0) {
                populate_vhd_listview();
                SetWindowTextA(g_hStatusBar, "Formatted partition as NTFS successfully.");

            } else {
                MessageBoxA(hwnd, "Failed to format NTFS (partition must be >= 10 MB).", "Error", MB_ICONERROR);
            }
        }
    } else {
        MessageBoxA(hwnd, "Please select a valid partition to format.", "VHD Master", MB_ICONWARNING);
    }
    break;
}                
                case IDM_PART_DELETE: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        if (MessageBoxA(hwnd, "Are you sure you want to delete this partition?", "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            part_delete(hwnd, sel);
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition to delete.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_FORMAT: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        if (MessageBoxA(hwnd, "Format this partition as FAT? All data will be lost.", "Confirm Format", MB_YESNO | MB_ICONWARNING) == IDYES) {
                            if (fs_format_partition(sel, 0) == 0) {
                                populate_vhd_listview();
                                SetWindowTextA(g_hStatusBar, "Formatted partition successfully.");
                            } else {
                                MessageBoxA(hwnd, "Failed to format partition.", "Error", MB_ICONERROR);
                            }
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition to format.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_ACTIVE: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        for (int i = 0; i < MAX_MBR_PARTS; i++) g_vhd.parts[i].boot = (i == sel) ? 0x80 : 0x00;
                        update_mbr_in_ram();
                        populate_vhd_listview();
                        SetWindowTextA(g_hStatusBar, "Active (Bootable) partition updated.");
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
case IDM_PART_COMPACT: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        if (fs_mount_any(sel) == 0) {
                            ShowProgress(TRUE);
                            
                            if (g_ntfs) {
                                /* NTFS zero-fill compaction */
                                ntfs_compact_partition();
                            } else {
                                /* FAT zero-fill compaction */
                                u8 z[512] = {0};
                                for (u32 i = 2; i <= g_total_clusters + 1; i++) {
                                    if (g_cancel_operation) break;
                                    if (read_fat(i) == 0) {
                                        u32 lba = cluster_to_lba(i);
                                        for(u32 j=0; j<g_sec_per_clus; j++) write_sec(lba+j, z, 1);
                                    }
                                    if (i % 100 == 0) UpdateProgress((int)((i * 100) / g_total_clusters));
                                }
                            }
                            
                            ShowProgress(FALSE);
                            g_vhd.fs_mounted = 0;
                            SetWindowTextA(g_hStatusBar, g_cancel_operation ? "Compacting cancelled." : "Partition free space zeroed (Compacted).");
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition to compact.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }

case IDM_PART_DEFRAG: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        if (fs_mount_any(sel) == 0) {
                            ShowProgress(TRUE);
                            int moved = 0;
                            
                            if (g_ntfs) {
                                ntfs_defrag_partition(&moved);
                            } else {
                                fs_defrag_dir(g_root_cluster, &moved);
                            }
                            
                            ShowProgress(FALSE);
                            g_vhd.fs_mounted = 0;
                            char msg[128];
                            snprintf(msg, sizeof(msg), g_cancel_operation ? 
                                "Defragmentation cancelled. %d file(s) relocated." : 
                                "Defragmentation complete. %d file(s) relocated.", moved);
                            SetWindowTextA(g_hStatusBar, msg);
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a valid partition.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_RESIZE: {
                    if (!g_vhd.isOpen || g_view_mode != 0) break;
                    int sel = ListView_GetNextItem(g_hVhdListView, -1, LVNI_SELECTED);
                    if (sel >= 0 && sel < 4 && g_vhd.parts[sel].used) {
                        u32 max_secs = (u32)(g_vhd.cap / 512) - g_vhd.parts[sel].lba_begin;
                        for (int i = 0; i < MAX_MBR_PARTS; i++) {
                            if (i != sel && g_vhd.parts[i].used && g_vhd.parts[i].lba_begin >= g_vhd.parts[sel].lba_begin) {
                                u32 gap = g_vhd.parts[i].lba_begin - g_vhd.parts[sel].lba_begin;
                                if (gap < max_secs) max_secs = gap;
                            }
                        }
                        
                        char buf[32];
                        u32 max_mb = max_secs / 2048; 
                        u32 cur_mb = g_vhd.parts[sel].lba_count / 2048;
                        snprintf(buf, sizeof(buf), "%u", cur_mb);
                        
                        char prompt[256];
                        snprintf(prompt, sizeof(prompt), "New size in MB (Max %u MB):", max_mb);
                        if (ShowInputBox(hwnd, "Resize Partition", prompt, buf)) {
                            u32 new_mb = atoi(buf);
                            if (new_mb >= 1 && new_mb <= max_mb) {
                                u32 new_lba = new_mb * 2048;
                                
                                if (new_lba < g_vhd.parts[sel].lba_count) {
                                    if (fs_mount_any(sel) == 0 && !g_ntfs) {
                                        u32 data_sectors_new = 0;
                                        if (new_lba > (g_data_lba - g_vhd.fs_part_lba)) {
                                            data_sectors_new = new_lba - (g_data_lba - g_vhd.fs_part_lba);
                                        }
                                        u32 max_cluster = data_sectors_new / g_sec_per_clus + 2;
                                        if (new_lba <= (g_data_lba - g_vhd.fs_part_lba)) max_cluster = 2;
                                        
                                        g_lost_log[0] = '\0';
                                        int lost_count = 0;
                                        scan_dir_for_lost(g_root_cluster, max_cluster, "\\", g_lost_log, &lost_count);
                                        
                                        if (lost_count > 0) {
                                            if (!ShowLostFilesDialog(hwnd)) {
                                                g_vhd.fs_mounted = 0;
                                                break; 
                                            }
                                            delete_lost_items(g_root_cluster, max_cluster);
                                        }
                                        
                                        u8 bpb[512];
                                        read_sec(g_vhd.parts[sel].lba_begin, bpb, 1);
                                        if (new_lba < 65536) {
                                            wr16le(bpb + 19, (u16)new_lba);
                                            wr32le(bpb + 32, 0);
                                        } else {
                                            wr16le(bpb + 19, 0);
                                            wr32le(bpb + 32, new_lba);
                                        }
                                        write_sec(g_vhd.parts[sel].lba_begin, bpb, 1);
                                    }
                                    g_vhd.fs_mounted = 0; 
                                    g_ntfs = 0;
                                }
                                
                                g_vhd.parts[sel].lba_count = new_lba;
                                update_mbr_in_ram();
                                populate_vhd_listview();
                                SetWindowTextA(g_hStatusBar, "Partition resized. Save VHD to commit.");
                            } else {
                                MessageBoxA(hwnd, "Invalid size or exceeds available space.", "Error", MB_ICONERROR);
                            }
                        }
                    } else {
                        MessageBoxA(hwnd, "Please select a partition to resize.", "VHD Master", MB_ICONWARNING);
                    }
                    break;
                }
                case IDM_PART_VBR_FILE:     cmd_write_vbr(hwnd); break;
                case IDM_PART_REPLACE_BOOT: cmd_replace_os_boot(hwnd); break;
                
                case IDM_DISK_MBR_STD:      cmd_write_mbr(hwnd); break;
                case IDM_DISK_RESIZE:       cmd_resize(hwnd); break;
                case IDM_DISK_EXTRACT_MBR:  cmd_extract_mbr(hwnd); break;
                case IDM_DISK_EXTRACT_VBR:  cmd_extract_vbr(hwnd); break;

case IDM_DISK_TRIM: {
                    if (!g_vhd.isOpen) break;
                    
                    /* 1. Find the exact sector where the last partition ends */
                    u32 highest = 0;
                    for(int i = 0; i < MAX_MBR_PARTS; i++) {
                        if(g_vhd.parts[i].used) {
                            u32 end = g_vhd.parts[i].lba_begin + g_vhd.parts[i].lba_count;
                            if(end > highest) highest = end;
                        }
                    }
                    if (highest == 0) highest = 2048;
                    
                    /* 2. Protect Windows Metadata & GPT Backups
                       We add a strict 2 MB padding (4096 sectors) to the end. 
                       This prevents Windows from flagging the disk as corrupt due 
                       to missing LDM metadata or a severed Backup GPT Header. */
                    u32 safe_highest = highest + 4096; 
                    u32 new_mb = (safe_highest / 2048) + 1; 

                    if (vhd_resize(new_mb) == 0) {
                        /* 3. CRITICAL: The physical file has changed, meaning the 
                           Dynamic BAT, file size, and geometry in RAM are now stale.
                           We MUST reload the VHD to prevent the next I/O from 
                           corrupting the filesystem. */
                        char current_path[MAX_PATH];
                        strncpy(current_path, g_vhd.path, MAX_PATH);
                        
                        vhd_close();
                        vhd_open(current_path); /* Reload fresh state from disk */
                        
                        populate_vhd_listview();
                        SetWindowTextA(g_hStatusBar, "VHD Trimmed successfully (Safety padding applied).");
                    } else {
                        MessageBoxA(hwnd, "Failed to trim VHD.", "Error", MB_ICONERROR);
                    }
                    break;
                }
                case ID_VHD_EXTRACT:  cmd_extract_selected(hwnd); break;
                case ID_VHD_DELETE:   cmd_delete_selected(hwnd); break;
                case ID_VHD_ADD:      cmd_add_selected(hwnd); break;
                
                case IDM_HELP_ABOUT:
                    MessageBoxA(hwnd, APP_NAME " " APP_VERSION
                        "\n\nImplemented: VHD container, MBR partitions, File Extract/Import/Drop, Resize."
                        "\nImplemented: FAT16/32 basic read, format, and add/extract engine, Bootsector Injection.",
                        "About", MB_ICONINFORMATION);
                    break;
            }
            return 0;
        }
        case WM_DESTROY:
            SaveSettings();
            vhd_close();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdline, int show) {
    (void)hPrev; (void)cmdline;
    g_hInstance = hInst;
    WNDCLASSA wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "VhdMasterClass";
    if (!RegisterClassA(&wc)) return 1;

    g_hMainWnd = CreateWindowExA(0, "VhdMasterClass", APP_NAME " " APP_VERSION,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInst, NULL);
    LoadSettings();
    ShowWindow(g_hMainWnd, show);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    return (int)msg.wParam;
}
