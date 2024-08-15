/* vim:set shiftwidth=4 ts=4: */
/*
 * QEMU Block driver for virtual VFAT (shadows a local directory)
 *
 * Copyright (c) 2004,2005 Johannes E. Schindelin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <dirent.h>
#include <glib/gstdio.h>
#include "qapi/error.h"
#include "block/block-io.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/bswap.h"
#include "migration/blocker.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"

#ifndef S_IWGRP
#define S_IWGRP 0
#endif
#ifndef S_IWOTH
#define S_IWOTH 0
#endif

/* TODO: add ":bootsector=blabla.img:" */
/* LATER TODO: add automatic boot sector generation from
    BOOTEASY.ASM and Ranish Partition Manager
    Note that DOS assumes the system files to be the first files in the
    file system (test if the boot sector still relies on that fact)! */
/* MAYBE TODO: write block-visofs.c */
/* TODO: call try_commit() only after a timeout */

/* #define DEBUG */

#ifdef DEBUG

#define DLOG(a) a

static void checkpoint(void);

#else

#define DLOG(a)

#endif

/* bootsector OEM name. see related compatibility problems at:
 * https://jdebp.eu/FGA/volume-boot-block-oem-name-field.html
 * http://seasip.info/Misc/oemid.html
 */
#define BOOTSECTOR_OEM_NAME "MSWIN4.1"

#define DIR_DELETED 0xe5
#define DIR_KANJI DIR_DELETED
#define DIR_KANJI_FAKE 0x05
#define DIR_FREE 0x00

/* dynamic array functions */
typedef struct array_t {
    char* pointer;
    unsigned int size,next,item_size;
} array_t;

static inline void array_init(array_t* array,unsigned int item_size)
{
    array->pointer = NULL;
    array->size=0;
    array->next=0;
    array->item_size=item_size;
}

static inline void array_free(array_t* array)
{
    g_free(array->pointer);
    array->size=array->next=0;
}

/* does not automatically grow */
static inline void* array_get(array_t* array,unsigned int index) {
    assert(index < array->next);
    assert(array->pointer);
    return array->pointer + index * array->item_size;
}

static inline void array_ensure_allocated(array_t *array, int index)
{
    if((index + 1) * array->item_size > array->size) {
        int new_size = (index + 32) * array->item_size;
        array->pointer = g_realloc(array->pointer, new_size);
        assert(array->pointer);
        memset(array->pointer + array->size, 0, new_size - array->size);
        array->size = new_size;
        array->next = index + 1;
    }
}

static inline void* array_get_next(array_t* array) {
    unsigned int next = array->next;

    array_ensure_allocated(array, next);
    array->next = next + 1;
    return array_get(array, next);
}

static inline void* array_insert(array_t* array,unsigned int index,unsigned int count) {
    if((array->next+count)*array->item_size>array->size) {
        int increment=count*array->item_size;
        array->pointer=g_realloc(array->pointer,array->size+increment);
        if(!array->pointer)
            return NULL;
        array->size+=increment;
    }
    memmove(array->pointer+(index+count)*array->item_size,
                array->pointer+index*array->item_size,
                (array->next-index)*array->item_size);
    array->next+=count;
    return array->pointer+index*array->item_size;
}

static inline int array_remove_slice(array_t* array,int index, int count)
{
    assert(index >=0);
    assert(count > 0);
    assert(index + count <= array->next);

    memmove(array->pointer + index * array->item_size,
            array->pointer + (index + count) * array->item_size,
            (array->next - index - count) * array->item_size);

    array->next -= count;
    return 0;
}

static int array_remove(array_t* array,int index)
{
    return array_remove_slice(array, index, 1);
}

/* return the index for a given member */
static int array_index(array_t* array, void* pointer)
{
    size_t offset = (char*)pointer - array->pointer;
    assert((offset % array->item_size) == 0);
    assert(offset/array->item_size < array->next);
    return offset/array->item_size;
}

/* These structures are used to fake a disk and the VFAT filesystem.
 * For this reason we need to use QEMU_PACKED. */

typedef struct bootsector_t {
    uint8_t jump[3];
    uint8_t name[8];
    uint16_t sector_size;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint16_t root_entries;
    uint16_t total_sectors16;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors;
    union {
        struct {
            uint8_t drive_number;
            uint8_t reserved1;
            uint8_t signature;
            uint32_t id;
            uint8_t volume_label[11];
            uint8_t fat_type[8];
            uint8_t ignored[0x1c0];
        } QEMU_PACKED fat16;
        struct {
            uint32_t sectors_per_fat;
            uint16_t flags;
            uint8_t major,minor;
            uint32_t first_cluster_of_root_dir;
            uint16_t info_sector;
            uint16_t backup_boot_sector;
            uint8_t reserved[12];
            uint8_t drive_number;
            uint8_t reserved1;
            uint8_t signature;
            uint32_t id;
            uint8_t volume_label[11];
            uint8_t fat_type[8];
            uint8_t ignored[0x1a4];
        } QEMU_PACKED fat32;
    } u;
    uint8_t magic[2];
} QEMU_PACKED bootsector_t;

typedef struct {
    uint8_t head;
    uint8_t sector;
    uint8_t cylinder;
} mbr_chs_t;

typedef struct partition_t {
    uint8_t attributes; /* 0x80 = bootable */
    mbr_chs_t start_CHS;
    uint8_t   fs_type; /* 0x1 = FAT12, 0x6 = FAT16, 0xe = FAT16_LBA, 0xb = FAT32, 0xc = FAT32_LBA */
    mbr_chs_t end_CHS;
    uint32_t start_sector_long;
    uint32_t length_sector_long;
} QEMU_PACKED partition_t;

typedef struct mbr_t {
    uint8_t ignored[0x1b8];
    uint32_t nt_id;
    uint8_t ignored2[2];
    partition_t partition[4];
    uint8_t magic[2];
} QEMU_PACKED mbr_t;

typedef struct direntry_t {
    uint8_t name[8 + 3];
    uint8_t attributes;
    uint8_t reserved[2];
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t begin_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t begin;
    uint32_t size;
} QEMU_PACKED direntry_t;

/* this structure are used to transparently access the files */

typedef struct mapping_t {
    /* begin is the first cluster, end is the last+1 */
    uint32_t begin,end;
    /* as s->directory is growable, no pointer may be used here */
    unsigned int dir_index;
    /* the clusters of a file may be in any order; this points to the first */
    int first_mapping_index;
    union {
        /* offset is
         * - the offset in the file (in clusters) for a file, or
         * - the next cluster of the directory for a directory
         */
        struct {
            uint32_t offset;
        } file;
        struct {
            int parent_mapping_index;
            int first_dir_index;
        } dir;
    } info;
    /* path contains the full path, i.e. it always starts with s->path */
    char* path;

    enum {
        MODE_UNDEFINED = 0,
        MODE_NORMAL = 1,
        MODE_MODIFIED = 2,
        MODE_DIRECTORY = 4,
        MODE_DELETED = 8,
    } mode;
    int read_only;
} mapping_t;

#ifdef DEBUG
static void print_direntry(const struct direntry_t*);
static void print_mapping(const struct mapping_t* mapping);
#endif

/* here begins the real VVFAT driver */

typedef struct BDRVVVFATState {
    CoMutex lock;
    BlockDriverState* bs; /* pointer to parent */
    unsigned char first_sectors[0x40*0x200];

    int fat_type; /* 16 or 32 */
    array_t fat,directory,mapping;
    char volume_label[11];

    uint32_t offset_to_bootsector; /* 0 for floppy, 0x3f for disk */

    unsigned int cluster_size;
    unsigned int sectors_per_cluster;
    unsigned int sectors_per_fat;
    uint32_t last_cluster_of_root_directory;
    /* how many entries are available in root directory (0 for FAT32) */
    uint16_t root_entries;
    uint32_t sector_count; /* total number of sectors of the partition */
    uint32_t cluster_count; /* total number of clusters of this partition */
    uint32_t max_fat_value;
    uint32_t offset_to_fat;
    uint32_t offset_to_root_dir;

    int current_fd;
    mapping_t* current_mapping;
    unsigned char* cluster; /* points to current cluster */
    unsigned char* cluster_buffer; /* points to a buffer to hold temp data */
    unsigned int current_cluster;

    /* write support */
    char* qcow_filename;
    BdrvChild* qcow;
    void* fat2;
    char* used_clusters;
    array_t commits;
    const char* path;
    int downcase_short_names;

    Error *migration_blocker;
} BDRVVVFATState;

/* take the sector position spos and convert it to Cylinder/Head/Sector position
 * if the position is outside the specified geometry, fill maximum value for CHS
 * and return 1 to signal overflow.
 */
static int sector2CHS(mbr_chs_t *chs, int spos, int cyls, int heads, int secs)
{
    int head,sector;
    sector   = spos % secs;  spos /= secs;
    head     = spos % heads; spos /= heads;
    if (spos >= cyls) {
        /* Overflow,
        it happens if 32bit sector positions are used, while CHS is only 24bit.
        Windows/Dos is said to take 1023/255/63 as nonrepresentable CHS */
        chs->head     = 0xFF;
        chs->sector   = 0xFF;
        chs->cylinder = 0xFF;
        return 1;
    }
    chs->head     = (uint8_t)head;
    chs->sector   = (uint8_t)( (sector+1) | ((spos>>8)<<6) );
    chs->cylinder = (uint8_t)spos;
    return 0;
}

static void init_mbr(BDRVVVFATState *s, int cyls, int heads, int secs)
{
    /* TODO: if the files mbr.img and bootsect.img exist, use them */
    mbr_t* real_mbr=(mbr_t*)s->first_sectors;
    partition_t* partition = &(real_mbr->partition[0]);
    int lba;

    memset(s->first_sectors,0,512);

    /* Win NT Disk Signature */
    real_mbr->nt_id= cpu_to_le32(0xbe1afdfa);

    partition->attributes=0x80; /* bootable */

    /* LBA is used when partition is outside the CHS geometry */
    lba  = sector2CHS(&partition->start_CHS, s->offset_to_bootsector,
                     cyls, heads, secs);
    lba |= sector2CHS(&partition->end_CHS,   s->bs->total_sectors - 1,
                     cyls, heads, secs);

    /*LBA partitions are identified only by start/length_sector_long not by CHS*/
    partition->start_sector_long  = cpu_to_le32(s->offset_to_bootsector);
    partition->length_sector_long = cpu_to_le32(s->bs->total_sectors
                                                - s->offset_to_bootsector);

    /* FAT12/FAT16/FAT32 */
    /* DOS uses different types when partition is LBA,
       probably to prevent older versions from using CHS on them */
    partition->fs_type = s->fat_type == 12 ? 0x1 :
                         s->fat_type == 16 ? (lba ? 0xe : 0x06) :
                       /*s->fat_type == 32*/ (lba ? 0xc : 0x0b);

    real_mbr->magic[0]=0x55; real_mbr->magic[1]=0xaa;
}

/* direntry functions */

static direntry_t *create_long_filename(BDRVVVFATState *s, const char *filename)
{
    int number_of_entries, i;
    glong length;
    direntry_t *entry;

    gunichar2 *longname = g_utf8_to_utf16(filename, -1, NULL, &length, NULL);
    if (!longname) {
        fprintf(stderr, "vvfat: invalid UTF-8 name: %s\n", filename);
        return NULL;
    }

    number_of_entries = DIV_ROUND_UP(length * 2, 26);

    for(i=0;i<number_of_entries;i++) {
        entry=array_get_next(&(s->directory));
        entry->attributes=0xf;
        entry->reserved[0]=0;
        entry->begin=0;
        entry->name[0]=(number_of_entries-i)|(i==0?0x40:0);
    }
    for(i=0;i<26*number_of_entries;i++) {
        int offset=(i%26);
        if(offset<10) offset=1+offset;
        else if(offset<22) offset=14+offset-10;
        else offset=28+offset-22;
        entry=array_get(&(s->directory),s->directory.next-1-(i/26));
        if (i >= 2 * length + 2) {
            entry->name[offset] = 0xff;
        } else if (i % 2 == 0) {
            entry->name[offset] = longname[i / 2] & 0xff;
        } else {
            entry->name[offset] = longname[i / 2] >> 8;
        }
    }
    g_free(longname);
    return array_get(&(s->directory),s->directory.next-number_of_entries);
}

static char is_free(const direntry_t* direntry)
{
    return direntry->name[0] == DIR_DELETED || direntry->name[0] == DIR_FREE;
}

static char is_volume_label(const direntry_t* direntry)
{
    return direntry->attributes == 0x28;
}

static char is_long_name(const direntry_t* direntry)
{
    return direntry->attributes == 0xf;
}

static char is_short_name(const direntry_t* direntry)
{
    return !is_volume_label(direntry) && !is_long_name(direntry)
        && !is_free(direntry);
}

static char is_directory(const direntry_t* direntry)
{
    return direntry->attributes & 0x10 && direntry->name[0] != DIR_DELETED;
}

static inline char is_dot(const direntry_t* direntry)
{
    return is_short_name(direntry) && direntry->name[0] == '.';
}

static char is_file(const direntry_t* direntry)
{
    return is_short_name(direntry) && !is_directory(direntry);
}

static inline uint32_t begin_of_direntry(const direntry_t* direntry)
{
    return le16_to_cpu(direntry->begin)|(le16_to_cpu(direntry->begin_hi)<<16);
}

static inline uint32_t filesize_of_direntry(const direntry_t* direntry)
{
    return le32_to_cpu(direntry->size);
}

static void set_begin_of_direntry(direntry_t* direntry, uint32_t begin)
{
    direntry->begin = cpu_to_le16(begin & 0xffff);
    direntry->begin_hi = cpu_to_le16((begin >> 16) & 0xffff);
}

static bool valid_filename(const unsigned char *name)
{
    unsigned char c;
    if (!strcmp((const char*)name, ".") || !strcmp((const char*)name, "..")) {
        return false;
    }
    for (; (c = *name); name++) {
        if (!((c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              c > 127 ||
              strchr(" $%'-_@~`!(){}^#&.+,;=[]", c) != NULL))
        {
            return false;
        }
    }
    return true;
}

static uint8_t to_valid_short_char(gunichar c)
{
    c = g_unichar_toupper(c);
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') ||
        strchr("$%'-_@~`!(){}^#&", c) != NULL) {
        return c;
    } else {
        return 0;
    }
}

static direntry_t *create_short_filename(BDRVVVFATState *s,
                                         const char *filename,
                                         unsigned int directory_start)
{
    int i, j = 0;
    direntry_t *entry = array_get_next(&(s->directory));
    const gchar *p, *last_dot = NULL;
    gunichar c;
    bool lossy_conversion = false;
    char tail[8];

    if (!entry) {
        return NULL;
    }
    memset(entry->name, 0x20, sizeof(entry->name));

    /* copy filename and search last dot */
    for (p = filename; ; p = g_utf8_next_char(p)) {
        c = g_utf8_get_char(p);
        if (c == '\0') {
            break;
        } else if (c == '.') {
            if (j == 0) {
                /* '.' at start of filename */
                lossy_conversion = true;
            } else {
                if (last_dot) {
                    lossy_conversion = true;
                }
                last_dot = p;
            }
        } else if (!last_dot) {
            /* first part of the name; copy it */
            uint8_t v = to_valid_short_char(c);
            if (j < 8 && v) {
                entry->name[j++] = v;
            } else {
                lossy_conversion = true;
            }
        }
    }

    /* copy extension (if any) */
    if (last_dot) {
        j = 0;
        for (p = g_utf8_next_char(last_dot); ; p = g_utf8_next_char(p)) {
            c = g_utf8_get_char(p);
            if (c == '\0') {
                break;
            } else {
                /* extension; copy it */
                uint8_t v = to_valid_short_char(c);
                if (j < 3 && v) {
                    entry->name[8 + (j++)] = v;
                } else {
                    lossy_conversion = true;
                }
            }
        }
    }

    if (entry->name[0] == DIR_KANJI) {
        entry->name[0] = DIR_KANJI_FAKE;
    }

    /* numeric-tail generation */
    for (j = 0; j < 8; j++) {
        if (entry->name[j] == ' ') {
            break;
        }
    }
    for (i = lossy_conversion ? 1 : 0; i < 999999; i++) {
        direntry_t *entry1;
        if (i > 0) {
            int len = snprintf(tail, sizeof(tail), "~%u", (unsigned)i);
            assert(len <= 7);
            memcpy(entry->name + MIN(j, 8 - len), tail, len);
        }
        for (entry1 = array_get(&(s->directory), directory_start);
             entry1 < entry; entry1++) {
            if (!is_long_name(entry1) &&
                !memcmp(entry1->name, entry->name, 11)) {
                break; /* found dupe */
            }
        }
        if (entry1 == entry) {
            /* no dupe found */
            return entry;
        }
    }
    return NULL;
}

/* fat functions */

static inline uint8_t fat_chksum(const direntry_t* entry)
{
    uint8_t chksum=0;
    int i;

    for (i = 0; i < ARRAY_SIZE(entry->name); i++) {
        chksum = (((chksum & 0xfe) >> 1) |
                  ((chksum & 0x01) ? 0x80 : 0)) + entry->name[i];
    }

    return chksum;
}

/* if return_time==0, this returns the fat_date, else the fat_time */
static uint16_t fat_datetime(time_t time,int return_time) {
    struct tm* t;
    struct tm t1;
    t = &t1;
    localtime_r(&time,t);
    if(return_time)
        return cpu_to_le16((t->tm_sec/2)|(t->tm_min<<5)|(t->tm_hour<<11));
    return cpu_to_le16((t->tm_mday)|((t->tm_mon+1)<<5)|((t->tm_year-80)<<9));
}

static inline void fat_set(BDRVVVFATState* s,unsigned int cluster,uint32_t value)
{
    if(s->fat_type==32) {
        uint32_t* entry=array_get(&(s->fat),cluster);
        *entry=cpu_to_le32(value);
    } else if(s->fat_type==16) {
        uint16_t* entry=array_get(&(s->fat),cluster);
        *entry=cpu_to_le16(value&0xffff);
    } else {
        int offset = (cluster*3/2);
        unsigned char* p = array_get(&(s->fat), offset);
        switch (cluster&1) {
        case 0:
                p[0] = value&0xff;
                p[1] = (p[1]&0xf0) | ((value>>8)&0xf);
                break;
        case 1:
                p[0] = (p[0]&0xf) | ((value&0xf)<<4);
                p[1] = (value>>4);
                break;
        }
    }
}

static inline uint32_t fat_get(BDRVVVFATState* s,unsigned int cluster)
{
    if(s->fat_type==32) {
        uint32_t* entry=array_get(&(s->fat),cluster);
        return le32_to_cpu(*entry);
    } else if(s->fat_type==16) {
        uint16_t* entry=array_get(&(s->fat),cluster);
        return le16_to_cpu(*entry);
    } else {
        const uint8_t* x=(uint8_t*)(s->fat.pointer)+cluster*3/2;
        return ((x[0]|(x[1]<<8))>>(cluster&1?4:0))&0x0fff;
    }
}

static inline int fat_eof(BDRVVVFATState* s,uint32_t fat_entry)
{
    if(fat_entry>s->max_fat_value-8)
        return -1;
    return 0;
}

static inline void init_fat(BDRVVVFATState* s)
{
    if (s->fat_type == 12) {
        array_init(&(s->fat),1);
        array_ensure_allocated(&(s->fat),
                s->sectors_per_fat * 0x200 * 3 / 2 - 1);
    } else {
        array_init(&(s->fat),(s->fat_type==32?4:2));
        array_ensure_allocated(&(s->fat),
                s->sectors_per_fat * 0x200 / s->fat.item_size - 1);
    }
    memset(s->fat.pointer,0,s->fat.size);

    switch(s->fat_type) {
        case 12: s->max_fat_value=0xfff; break;
        case 16: s->max_fat_value=0xffff; break;
        case 32: s->max_fat_value=0x0fffffff; break;
        default: s->max_fat_value=0; /* error... */
    }

}

static inline direntry_t* create_short_and_long_name(BDRVVVFATState* s,
        unsigned int directory_start, const char* filename, int is_dot)
{
    int long_index = s->directory.next;
    direntry_t* entry = NULL;
    direntry_t* entry_long = NULL;

    if(is_dot) {
        entry=array_get_next(&(s->directory));
        memset(entry->name, 0x20, sizeof(entry->name));
        memcpy(entry->name,filename,strlen(filename));
        return entry;
    }

    entry_long=create_long_filename(s,filename);
    entry = create_short_filename(s, filename, directory_start);

    /* calculate checksum; propagate to long name */
    if(entry_long) {
        uint8_t chksum=fat_chksum(entry);

        /* calculate anew, because realloc could have taken place */
        entry_long=array_get(&(s->directory),long_index);
        while(entry_long<entry && is_long_name(entry_long)) {
            entry_long->reserved[1]=chksum;
            entry_long++;
        }
    }

    return entry;
}

/*
 * Read a directory. (the index of the corresponding mapping must be passed).
 */
static int read_directory(BDRVVVFATState* s, int mapping_index)
{
    mapping_t* mapping = array_get(&(s->mapping), mapping_index);
    direntry_t* direntry;
    const char* dirname = mapping->path;
    int first_cluster = mapping->begin;
    int parent_index = mapping->info.dir.parent_mapping_index;
    mapping_t* parent_mapping = (mapping_t*)
        (parent_index >= 0 ? array_get(&(s->mapping), parent_index) : NULL);
    int first_cluster_of_parent = parent_mapping ? parent_mapping->begin : -1;

    DIR* dir=opendir(dirname);
    struct dirent* entry;
    int i;

    assert(mapping->mode & MODE_DIRECTORY);

    if(!dir) {
        mapping->end = mapping->begin;
        return -1;
    }

    i = mapping->info.dir.first_dir_index =
            first_cluster == 0 ? 0 : s->directory.next;

    if (first_cluster != 0) {
        /* create the top entries of a subdirectory */
        (void)create_short_and_long_name(s, i, ".", 1);
        (void)create_short_and_long_name(s, i, "..", 1);
    }

    /* actually read the directory, and allocate the mappings */
    while((entry=readdir(dir))) {
        unsigned int length=strlen(dirname)+2+strlen(entry->d_name);
        char* buffer;
        struct stat st;
        int is_dot=!strcmp(entry->d_name,".");
        int is_dotdot=!strcmp(entry->d_name,"..");

        if (first_cluster == 0 && s->directory.next >= s->root_entries - 1) {
            fprintf(stderr, "Too many entries in root directory\n");
            closedir(dir);
            return -2;
        }

        if(first_cluster == 0 && (is_dotdot || is_dot))
            continue;

        buffer = g_malloc(length);
        snprintf(buffer,length,"%s/%s",dirname,entry->d_name);

        if(stat(buffer,&st)<0) {
            g_free(buffer);
            continue;
        }

        /* create directory entry for this file */
        if (!is_dot && !is_dotdot) {
            direntry = create_short_and_long_name(s, i, entry->d_name, 0);
        } else {
            direntry = array_get(&(s->directory), is_dot ? i : i + 1);
        }
        direntry->attributes=(S_ISDIR(st.st_mode)?0x10:0x20);
        direntry->reserved[0]=direntry->reserved[1]=0;
        direntry->ctime=fat_datetime(st.st_ctime,1);
        direntry->cdate=fat_datetime(st.st_ctime,0);
        direntry->adate=fat_datetime(st.st_atime,0);
        direntry->begin_hi=0;
        direntry->mtime=fat_datetime(st.st_mtime,1);
        direntry->mdate=fat_datetime(st.st_mtime,0);
        if(is_dotdot)
            set_begin_of_direntry(direntry, first_cluster_of_parent);
        else if(is_dot)
            set_begin_of_direntry(direntry, first_cluster);
        else
            direntry->begin=0; /* do that later */
        if (st.st_size > 0x7fffffff) {
            fprintf(stderr, "File %s is larger than 2GB\n", buffer);
            g_free(buffer);
            closedir(dir);
            return -2;
        }
        direntry->size=cpu_to_le32(S_ISDIR(st.st_mode)?0:st.st_size);

        /* create mapping for this file */
        if(!is_dot && !is_dotdot && (S_ISDIR(st.st_mode) || st.st_size)) {
            s->current_mapping = array_get_next(&(s->mapping));
            s->current_mapping->begin=0;
            s->current_mapping->end=st.st_size;
            /*
             * we get the direntry of the most recent direntry, which
             * contains the short name and all the relevant information.
             */
            s->current_mapping->dir_index=s->directory.next-1;
            s->current_mapping->first_mapping_index = -1;
            if (S_ISDIR(st.st_mode)) {
                s->current_mapping->mode = MODE_DIRECTORY;
                s->current_mapping->info.dir.parent_mapping_index =
                    mapping_index;
            } else {
                s->current_mapping->mode = MODE_UNDEFINED;
                s->current_mapping->info.file.offset = 0;
            }
            s->current_mapping->path=buffer;
            s->current_mapping->read_only =
                (st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0;
        } else {
            g_free(buffer);
        }
    }
    closedir(dir);

    /* fill with zeroes up to the end of the cluster */
    while(s->directory.next%(0x10*s->sectors_per_cluster)) {
        direntry = array_get_next(&(s->directory));
        memset(direntry,0,sizeof(direntry_t));
    }

    if (s->fat_type != 32 &&
        mapping_index == 0 &&
        s->directory.next < s->root_entries) {
        /* root directory */
        int cur = s->directory.next;
        array_ensure_allocated(&(s->directory), s->root_entries - 1);
        s->directory.next = s->root_entries;
        memset(array_get(&(s->directory), cur), 0,
                (s->root_entries - cur) * sizeof(direntry_t));
    }

    /* re-get the mapping, since s->mapping was possibly realloc()ed */
    mapping = array_get(&(s->mapping), mapping_index);
    first_cluster += (s->directory.next - mapping->info.dir.first_dir_index)
        * 0x20 / s->cluster_size;
    mapping->end = first_cluster;

    direntry = array_get(&(s->directory), mapping->dir_index);
    set_begin_of_direntry(direntry, mapping->begin);

    return 0;
}

static inline int32_t sector2cluster(BDRVVVFATState* s,off_t sector_num)
{
    return (sector_num - s->offset_to_root_dir) / s->sectors_per_cluster;
}

static inline off_t cluster2sector(BDRVVVFATState* s, uint32_t cluster_num)
{
    return s->offset_to_root_dir + s->sectors_per_cluster * cluster_num;
}

static int init_directories(BDRVVVFATState* s,
                            const char *dirname, int heads, int secs,
                            Error **errp)
{
    bootsector_t* bootsector;
    mapping_t* mapping;
    unsigned int i;
    unsigned int cluster;

    memset(&(s->first_sectors[0]),0,0x40*0x200);

    s->cluster_size=s->sectors_per_cluster*0x200;
    s->cluster_buffer=g_malloc(s->cluster_size);

    /*
     * The formula: sc = spf+1+spf*spc*(512*8/fat_type),
     * where sc is sector_count,
     * spf is sectors_per_fat,
     * spc is sectors_per_clusters, and
     * fat_type = 12, 16 or 32.
     */
    i = 1+s->sectors_per_cluster*0x200*8/s->fat_type;
    s->sectors_per_fat=(s->sector_count+i)/i; /* round up */

    s->offset_to_fat = s->offset_to_bootsector + 1;
    s->offset_to_root_dir = s->offset_to_fat + s->sectors_per_fat * 2;

    array_init(&(s->mapping),sizeof(mapping_t));
    array_init(&(s->directory),sizeof(direntry_t));

    /* add volume label */
    {
        direntry_t* entry=array_get_next(&(s->directory));
        entry->attributes=0x28; /* archive | volume label */
        memcpy(entry->name, s->volume_label, sizeof(entry->name));
    }

    /* Now build FAT, and write back information into directory */
    init_fat(s);

    /* TODO: if there are more entries, bootsector has to be adjusted! */
    s->root_entries = 0x02 * 0x10 * s->sectors_per_cluster;
    s->cluster_count=sector2cluster(s, s->sector_count);

    mapping = array_get_next(&(s->mapping));
    mapping->begin = 0;
    mapping->dir_index = 0;
    mapping->info.dir.parent_mapping_index = -1;
    mapping->first_mapping_index = -1;
    mapping->path = g_strdup(dirname);
    i = strlen(mapping->path);
    if (i > 0 && mapping->path[i - 1] == '/')
        mapping->path[i - 1] = '\0';
    mapping->mode = MODE_DIRECTORY;
    mapping->read_only = 0;
    s->path = mapping->path;

    for (i = 0, cluster = 0; i < s->mapping.next; i++) {
        /* MS-DOS expects the FAT to be 0 for the root directory
         * (except for the media byte). */
        /* LATER TODO: still true for FAT32? */
        int fix_fat = (i != 0);
        mapping = array_get(&(s->mapping), i);

        if (mapping->mode & MODE_DIRECTORY) {
            char *path = mapping->path;
            mapping->begin = cluster;
            if(read_directory(s, i)) {
                error_setg(errp, "Could not read directory %s", path);
                return -1;
            }
            mapping = array_get(&(s->mapping), i);
        } else {
            assert(mapping->mode == MODE_UNDEFINED);
            mapping->mode=MODE_NORMAL;
            mapping->begin = cluster;
            if (mapping->end > 0) {
                direntry_t* direntry = array_get(&(s->directory),
                        mapping->dir_index);

                mapping->end = cluster + 1 + (mapping->end-1)/s->cluster_size;
                set_begin_of_direntry(direntry, mapping->begin);
            } else {
                mapping->end = cluster + 1;
                fix_fat = 0;
            }
        }

        assert(mapping->begin < mapping->end);

        /* next free cluster */
        cluster = mapping->end;

        if(cluster > s->cluster_count) {
            error_setg(errp,
                       "Directory does not fit in FAT%d (capacity %.2f MB)",
                       s->fat_type, s->sector_count / 2000.0);
            return -1;
        }

        /* fix fat for entry */
        if (fix_fat) {
            int j;
            for(j = mapping->begin; j < mapping->end - 1; j++)
                fat_set(s, j, j+1);
            fat_set(s, mapping->end - 1, s->max_fat_value);
        }
    }

    mapping = array_get(&(s->mapping), 0);
    s->last_cluster_of_root_directory = mapping->end;

    /* the FAT signature */
    fat_set(s,0,s->max_fat_value);
    fat_set(s,1,s->max_fat_value);

    s->current_mapping = NULL;

    bootsector = (bootsector_t *)(s->first_sectors
                                  + s->offset_to_bootsector * 0x200);
    bootsector->jump[0]=0xeb;
    bootsector->jump[1]=0x3e;
    bootsector->jump[2]=0x90;
    memcpy(bootsector->name, BOOTSECTOR_OEM_NAME, 8);
    bootsector->sector_size=cpu_to_le16(0x200);
    bootsector->sectors_per_cluster=s->sectors_per_cluster;
    bootsector->reserved_sectors=cpu_to_le16(1);
    bootsector->number_of_fats=0x2; /* number of FATs */
    bootsector->root_entries = cpu_to_le16(s->root_entries);
    bootsector->total_sectors16=s->sector_count>0xffff?0:cpu_to_le16(s->sector_count);
    /* media descriptor: hard disk=0xf8, floppy=0xf0 */
    bootsector->media_type = (s->offset_to_bootsector > 0 ? 0xf8 : 0xf0);
    s->fat.pointer[0] = bootsector->media_type;
    bootsector->sectors_per_fat=cpu_to_le16(s->sectors_per_fat);
    bootsector->sectors_per_track = cpu_to_le16(secs);
    bootsector->number_of_heads = cpu_to_le16(heads);
    bootsector->hidden_sectors = cpu_to_le32(s->offset_to_bootsector);
    bootsector->total_sectors=cpu_to_le32(s->sector_count>0xffff?s->sector_count:0);

    /* LATER TODO: if FAT32, this is wrong */
    /* drive_number: fda=0, hda=0x80 */
    bootsector->u.fat16.drive_number = s->offset_to_bootsector == 0 ? 0 : 0x80;
    bootsector->u.fat16.signature=0x29;
    bootsector->u.fat16.id=cpu_to_le32(0xfabe1afd);

    memcpy(bootsector->u.fat16.volume_label, s->volume_label,
           sizeof(bootsector->u.fat16.volume_label));
    memcpy(bootsector->u.fat16.fat_type,
           s->fat_type == 12 ? "FAT12   " : "FAT16   ", 8);
    bootsector->magic[0]=0x55; bootsector->magic[1]=0xaa;

    return 0;
}

#ifdef DEBUG
static BDRVVVFATState *vvv = NULL;
#endif

static int enable_write_target(BlockDriverState *bs, Error **errp);
static int coroutine_fn is_consistent(BDRVVVFATState *s);

static QemuOptsList runtime_opts = {
    .name = "vvfat",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "dir",
            .type = QEMU_OPT_STRING,
            .help = "Host directory to map to the vvfat device",
        },
        {
            .name = "fat-type",
            .type = QEMU_OPT_NUMBER,
            .help = "FAT type (12, 16 or 32)",
        },
        {
            .name = "floppy",
            .type = QEMU_OPT_BOOL,
            .help = "Create a floppy rather than a hard disk image",
        },
        {
            .name = "label",
            .type = QEMU_OPT_STRING,
            .help = "Use a volume label other than QEMU VVFAT",
        },
        {
            .name = "rw",
            .type = QEMU_OPT_BOOL,
            .help = "Make the image writable",
        },
        { /* end of list */ }
    },
};

static void vvfat_parse_filename(const char *filename, QDict *options,
                                 Error **errp)
{
    int fat_type = 0;
    bool floppy = false;
    bool rw = false;
    int i;

    if (!strstart(filename, "fat:", NULL)) {
        error_setg(errp, "File name string must start with 'fat:'");
        return;
    }

    /* Parse options */
    if (strstr(filename, ":32:")) {
        fat_type = 32;
    } else if (strstr(filename, ":16:")) {
        fat_type = 16;
    } else if (strstr(filename, ":12:")) {
        fat_type = 12;
    }

    if (strstr(filename, ":floppy:")) {
        floppy = true;
    }

    if (strstr(filename, ":rw:")) {
        rw = true;
    }

    /* Get the directory name without options */
    i = strrchr(filename, ':') - filename;
    assert(i >= 3);
    if (filename[i - 2] == ':' && qemu_isalpha(filename[i - 1])) {
        /* workaround for DOS drive names */
        filename += i - 1;
    } else {
        filename += i + 1;
    }

    /* Fill in the options QDict */
    qdict_put_str(options, "dir", filename);
    qdict_put_int(options, "fat-type", fat_type);
    qdict_put_bool(options, "floppy", floppy);
    qdict_put_bool(options, "rw", rw);
}

static int vvfat_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVVVFATState *s = bs->opaque;
    int cyls, heads, secs;
    bool floppy;
    const char *dirname, *label;
    QemuOpts *opts;
    int ret;

    GRAPH_RDLOCK_GUARD_MAINLOOP();

#ifdef DEBUG
    vvv = s;
#endif

    opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    dirname = qemu_opt_get(opts, "dir");
    if (!dirname) {
        error_setg(errp, "vvfat block driver requires a 'dir' option");
        ret = -EINVAL;
        goto fail;
    }

    s->fat_type = qemu_opt_get_number(opts, "fat-type", 0);
    floppy = qemu_opt_get_bool(opts, "floppy", false);

    memset(s->volume_label, ' ', sizeof(s->volume_label));
    label = qemu_opt_get(opts, "label");
    if (label) {
        size_t label_length = strlen(label);
        if (label_length > 11) {
            error_setg(errp, "vvfat label cannot be longer than 11 bytes");
            ret = -EINVAL;
            goto fail;
        }
        memcpy(s->volume_label, label, label_length);
    } else {
        memcpy(s->volume_label, "QEMU VVFAT", 10);
    }

    if (floppy) {
        /* 1.44MB or 2.88MB floppy.  2.88MB can be FAT12 (default) or FAT16. */
        if (!s->fat_type) {
            s->fat_type = 12;
            secs = 36;
            s->sectors_per_cluster = 2;
        } else {
            secs = s->fat_type == 12 ? 18 : 36;
            s->sectors_per_cluster = 1;
        }
        cyls = 80;
        heads = 2;
    } else {
        /* 32MB or 504MB disk*/
        if (!s->fat_type) {
            s->fat_type = 16;
        }
        s->offset_to_bootsector = 0x3f;
        cyls = s->fat_type == 12 ? 64 : 1024;
        heads = 16;
        secs = 63;
    }

    switch (s->fat_type) {
    case 32:
        warn_report("FAT32 has not been tested. You are welcome to do so!");
        break;
    case 16:
    case 12:
        break;
    default:
        error_setg(errp, "Valid FAT types are only 12, 16 and 32");
        ret = -EINVAL;
        goto fail;
    }


    s->bs = bs;

    /* LATER TODO: if FAT32, adjust */
    s->sectors_per_cluster=0x10;

    s->current_cluster=0xffffffff;

    s->qcow = NULL;
    s->qcow_filename = NULL;
    s->fat2 = NULL;
    s->downcase_short_names = 1;

    DLOG(fprintf(stderr, "vvfat %s chs %d,%d,%d\n",
                 dirname, cyls, heads, secs));

    s->sector_count = cyls * heads * secs - s->offset_to_bootsector;
    bs->total_sectors = cyls * heads * secs;

    if (qemu_opt_get_bool(opts, "rw", false)) {
        if (!bdrv_is_read_only(bs)) {
            ret = enable_write_target(bs, errp);
            if (ret < 0) {
                goto fail;
            }
        } else {
            ret = -EPERM;
            error_setg(errp,
                       "Unable to set VVFAT to 'rw' when drive is read-only");
            goto fail;
        }
    } else {
        ret = bdrv_apply_auto_read_only(bs, NULL, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    if (init_directories(s, dirname, heads, secs, errp)) {
        ret = -EIO;
        goto fail;
    }

    s->sector_count = s->offset_to_root_dir
                    + s->sectors_per_cluster * s->cluster_count;

    /* Disable migration when vvfat is used rw */
    if (s->qcow) {
        error_setg(&s->migration_blocker,
                   "The vvfat (rw) format used by node '%s' "
                   "does not support live migration",
                   bdrv_get_device_or_node_name(bs));
        ret = migrate_add_blocker_normal(&s->migration_blocker, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    if (s->offset_to_bootsector > 0) {
        init_mbr(s, cyls, heads, secs);
    }

    qemu_co_mutex_init(&s->lock);

    qemu_opts_del(opts);

    return 0;

fail:
    g_free(s->qcow_filename);
    s->qcow_filename = NULL;
    g_free(s->cluster_buffer);
    s->cluster_buffer = NULL;
    g_free(s->used_clusters);
    s->used_clusters = NULL;

    qemu_opts_del(opts);
    return ret;
}

static void vvfat_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl.request_alignment = BDRV_SECTOR_SIZE; /* No sub-sector I/O */
}

static inline void vvfat_close_current_file(BDRVVVFATState *s)
{
    if(s->current_mapping) {
        s->current_mapping = NULL;
        if (s->current_fd) {
                qemu_close(s->current_fd);
                s->current_fd = 0;
        }
    }
    s->current_cluster = -1;
}

/* mappings between index1 and index2-1 are supposed to be ordered
 * return value is the index of the last mapping for which end>cluster_num
 */
static inline int find_mapping_for_cluster_aux(BDRVVVFATState* s,int cluster_num,int index1,int index2)
{
    while(1) {
        int index3;
        mapping_t* mapping;
        index3=(index1+index2)/2;
        mapping=array_get(&(s->mapping),index3);
        assert(mapping->begin < mapping->end);
        if(mapping->begin>=cluster_num) {
            assert(index2!=index3 || index2==0);
            if(index2==index3)
                return index1;
            index2=index3;
        } else {
            if(index1==index3)
                return mapping->end<=cluster_num ? index2 : index1;
            index1=index3;
        }
        assert(index1<=index2);
        DLOG(mapping=array_get(&(s->mapping),index1);
        assert(mapping->begin<=cluster_num);
        assert(index2 >= s->mapping.next ||
                ((mapping = array_get(&(s->mapping),index2)) &&
                mapping->end>cluster_num)));
    }
}

static inline mapping_t* find_mapping_for_cluster(BDRVVVFATState* s,int cluster_num)
{
    int index=find_mapping_for_cluster_aux(s,cluster_num,0,s->mapping.next);
    mapping_t* mapping;
    if(index>=s->mapping.next)
        return NULL;
    mapping=array_get(&(s->mapping),index);
    if(mapping->begin>cluster_num)
        return NULL;
    assert(mapping->begin<=cluster_num && mapping->end>cluster_num);
    return mapping;
}

static int open_file(BDRVVVFATState* s,mapping_t* mapping)
{
    if(!mapping)
        return -1;
    if(!s->current_mapping ||
            strcmp(s->current_mapping->path,mapping->path)) {
        /* open file */
        int fd = qemu_open_old(mapping->path,
                               O_RDONLY | O_BINARY | O_LARGEFILE);
        if(fd<0)
            return -1;
        vvfat_close_current_file(s);
        s->current_fd = fd;
    }

    s->current_mapping = mapping;
    return 0;
}

static inline int read_cluster(BDRVVVFATState *s,int cluster_num)
{
    if(s->current_cluster != cluster_num) {
        int result=0;
        off_t offset;
        assert(!s->current_mapping || s->current_fd || (s->current_mapping->mode & MODE_DIRECTORY));
        if(!s->current_mapping
                || s->current_mapping->begin>cluster_num
                || s->current_mapping->end<=cluster_num) {
            /* binary search of mappings for file */
            mapping_t* mapping=find_mapping_for_cluster(s,cluster_num);

            assert(!mapping || (cluster_num>=mapping->begin && cluster_num<mapping->end));

            if (mapping && mapping->mode & MODE_DIRECTORY) {
                vvfat_close_current_file(s);
                s->current_mapping = mapping;
read_cluster_directory:
                offset = s->cluster_size*(cluster_num-s->current_mapping->begin);
                s->cluster = (unsigned char*)s->directory.pointer+offset
                        + 0x20*s->current_mapping->info.dir.first_dir_index;
                assert(((s->cluster-(unsigned char*)s->directory.pointer)%s->cluster_size)==0);
                assert((char*)s->cluster+s->cluster_size <= s->directory.pointer+s->directory.next*s->directory.item_size);
                s->current_cluster = cluster_num;
                return 0;
            }

            if(open_file(s,mapping))
                return -2;
        } else if (s->current_mapping->mode & MODE_DIRECTORY)
            goto read_cluster_directory;

        assert(s->current_fd);

        offset = s->cluster_size *
            ((cluster_num - s->current_mapping->begin)
            + s->current_mapping->info.file.offset);
        if(lseek(s->current_fd, offset, SEEK_SET)!=offset)
            return -3;
        s->cluster=s->cluster_buffer;
        result=read(s->current_fd,s->cluster,s->cluster_size);
        if(result<0) {
            s->current_cluster = -1;
            return -1;
        }
        s->current_cluster = cluster_num;
    }
    return 0;
}

#ifdef DEBUG
static void print_direntry(const direntry_t* direntry)
{
    int j = 0;
    char buffer[1024];

    fprintf(stderr, "direntry %p: ", direntry);
    if(!direntry)
        return;
    if(is_long_name(direntry)) {
        unsigned char* c=(unsigned char*)direntry;
        int i;
        for(i=1;i<11 && c[i] && c[i]!=0xff;i+=2)
#define ADD_CHAR(c) {buffer[j] = (c); if (buffer[j] < ' ') buffer[j] = 0xb0; j++;}
            ADD_CHAR(c[i]);
        for(i=14;i<26 && c[i] && c[i]!=0xff;i+=2)
            ADD_CHAR(c[i]);
        for(i=28;i<32 && c[i] && c[i]!=0xff;i+=2)
            ADD_CHAR(c[i]);
        buffer[j] = 0;
        fprintf(stderr, "%s\n", buffer);
    } else {
        int i;
        for(i=0;i<11;i++)
            ADD_CHAR(direntry->name[i]);
        buffer[j] = 0;
        fprintf(stderr, "%s attributes=0x%02x begin=%u size=%u\n",
                buffer,
                direntry->attributes,
                begin_of_direntry(direntry),le32_to_cpu(direntry->size));
    }
}

static void print_mapping(const mapping_t* mapping)
{
    fprintf(stderr, "mapping (%p): begin, end = %u, %u, dir_index = %u, "
        "first_mapping_index = %d, name = %s, mode = 0x%x, " ,
        mapping, mapping->begin, mapping->end, mapping->dir_index,
        mapping->first_mapping_index, mapping->path, mapping->mode);

    if (mapping->mode & MODE_DIRECTORY)
        fprintf(stderr, "parent_mapping_index = %d, first_dir_index = %d\n", mapping->info.dir.parent_mapping_index, mapping->info.dir.first_dir_index);
    else
        fprintf(stderr, "offset = %u\n", mapping->info.file.offset);
}
#endif

static int coroutine_fn GRAPH_RDLOCK
vvfat_read(BlockDriverState *bs, int64_t sector_num, uint8_t *buf, int nb_sectors)
{
    BDRVVVFATState *s = bs->opaque;
    int i;

    for(i=0;i<nb_sectors;i++,sector_num++) {
        if (sector_num >= bs->total_sectors)
           return -1;
        if (s->qcow) {
            int64_t n;
            int ret;
            ret = bdrv_co_is_allocated(s->qcow->bs, sector_num * BDRV_SECTOR_SIZE,
                                       (nb_sectors - i) * BDRV_SECTOR_SIZE, &n);
            if (ret < 0) {
                return ret;
            }
            if (ret) {
                DLOG(fprintf(stderr, "sectors %" PRId64 "+%" PRId64
                             " allocated\n", sector_num,
                             n >> BDRV_SECTOR_BITS));
                if (bdrv_co_pread(s->qcow, sector_num * BDRV_SECTOR_SIZE, n,
                                  buf + i * 0x200, 0) < 0) {
                    return -1;
                }
                i += (n >> BDRV_SECTOR_BITS) - 1;
                sector_num += (n >> BDRV_SECTOR_BITS) - 1;
                continue;
            }
            DLOG(fprintf(stderr, "sector %" PRId64 " not allocated\n",
                         sector_num));
        }
        if (sector_num < s->offset_to_root_dir) {
            if (sector_num < s->offset_to_fat) {
                memcpy(buf + i * 0x200,
                       &(s->first_sectors[sector_num * 0x200]),
                       0x200);
            } else if (sector_num < s->offset_to_fat + s->sectors_per_fat) {
                memcpy(buf + i * 0x200,
                       &(s->fat.pointer[(sector_num
                                       - s->offset_to_fat) * 0x200]),
                       0x200);
            } else if (sector_num < s->offset_to_root_dir) {
                memcpy(buf + i * 0x200,
                       &(s->fat.pointer[(sector_num - s->offset_to_fat
                                       - s->sectors_per_fat) * 0x200]),
                       0x200);
            }
        } else {
            uint32_t sector = sector_num - s->offset_to_root_dir,
            sector_offset_in_cluster=(sector%s->sectors_per_cluster),
            cluster_num=sector/s->sectors_per_cluster;
            if(cluster_num > s->cluster_count || read_cluster(s, cluster_num) != 0) {
                /* LATER TODO: strict: return -1; */
                memset(buf+i*0x200,0,0x200);
                continue;
            }
            memcpy(buf+i*0x200,s->cluster+sector_offset_in_cluster*0x200,0x200);
        }
    }
    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
vvfat_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    int ret;
    BDRVVVFATState *s = bs->opaque;
    uint64_t sector_num = offset >> BDRV_SECTOR_BITS;
    int nb_sectors = bytes >> BDRV_SECTOR_BITS;
    void *buf;

    assert(QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(bytes, BDRV_SECTOR_SIZE));

    buf = g_try_malloc(bytes);
    if (bytes && buf == NULL) {
        return -ENOMEM;
    }

    qemu_co_mutex_lock(&s->lock);
    ret = vvfat_read(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);

    qemu_iovec_from_buf(qiov, 0, buf, bytes);
    g_free(buf);

    return ret;
}

/* LATER TODO: statify all functions */

/*
 * Idea of the write support (use snapshot):
 *
 * 1. check if all data is consistent, recording renames, modifications,
 *    new files and directories (in s->commits).
 *
 * 2. if the data is not consistent, stop committing
 *
 * 3. handle renames, and create new files and directories (do not yet
 *    write their contents)
 *
 * 4. walk the directories, fixing the mapping and direntries, and marking
 *    the handled mappings as not deleted
 *
 * 5. commit the contents of the files
 *
 * 6. handle deleted files and directories
 *
 */

typedef struct commit_t {
    char* path;
    union {
        struct { uint32_t cluster; } rename;
        struct { int dir_index; uint32_t modified_offset; } writeout;
        struct { uint32_t first_cluster; } new_file;
        struct { uint32_t cluster; } mkdir;
    } param;
    /* DELETEs and RMDIRs are handled differently: see handle_deletes() */
    enum {
        ACTION_RENAME, ACTION_WRITEOUT, ACTION_NEW_FILE, ACTION_MKDIR
    } action;
} commit_t;

static void clear_commits(BDRVVVFATState* s)
{
    int i;
DLOG(fprintf(stderr, "clear_commits (%u commits)\n", s->commits.next));
    for (i = 0; i < s->commits.next; i++) {
        commit_t* commit = array_get(&(s->commits), i);
        assert(commit->path || commit->action == ACTION_WRITEOUT);
        if (commit->action != ACTION_WRITEOUT) {
            assert(commit->path);
            g_free(commit->path);
        } else
            assert(commit->path == NULL);
    }
    s->commits.next = 0;
}

static void schedule_rename(BDRVVVFATState* s,
        uint32_t cluster, char* new_path)
{
    commit_t* commit = array_get_next(&(s->commits));
    commit->path = new_path;
    commit->param.rename.cluster = cluster;
    commit->action = ACTION_RENAME;
}

static void schedule_writeout(BDRVVVFATState* s,
        int dir_index, uint32_t modified_offset)
{
    commit_t* commit = array_get_next(&(s->commits));
    commit->path = NULL;
    commit->param.writeout.dir_index = dir_index;
    commit->param.writeout.modified_offset = modified_offset;
    commit->action = ACTION_WRITEOUT;
}

static void schedule_new_file(BDRVVVFATState* s,
        char* path, uint32_t first_cluster)
{
    commit_t* commit = array_get_next(&(s->commits));
    commit->path = path;
    commit->param.new_file.first_cluster = first_cluster;
    commit->action = ACTION_NEW_FILE;
}

static void schedule_mkdir(BDRVVVFATState* s, uint32_t cluster, char* path)
{
    commit_t* commit = array_get_next(&(s->commits));
    commit->path = path;
    commit->param.mkdir.cluster = cluster;
    commit->action = ACTION_MKDIR;
}

typedef struct {
    /*
     * Since the sequence number is at most 0x3f, and the filename
     * length is at most 13 times the sequence number, the maximal
     * filename length is 0x3f * 13 bytes.
     */
    unsigned char name[0x3f * 13 + 1];
    gunichar2 name2[0x3f * 13 + 1];
    int checksum, len;
    int sequence_number;
} long_file_name;

static void lfn_init(long_file_name* lfn)
{
   lfn->sequence_number = lfn->len = 0;
   lfn->checksum = 0x100;
}

/* return 0 if parsed successfully, > 0 if no long name, < 0 if error */
static int parse_long_name(long_file_name* lfn,
        const direntry_t* direntry)
{
    int i, j, offset;
    const unsigned char* pointer = (const unsigned char*)direntry;

    if (!is_long_name(direntry))
        return 1;

    if (pointer[0] & 0x40) {
        /* first entry; do some initialization */
        lfn->sequence_number = pointer[0] & 0x3f;
        lfn->checksum = pointer[13];
        lfn->name[0] = 0;
        lfn->name[lfn->sequence_number * 13] = 0;
    } else if ((pointer[0] & 0x3f) != --lfn->sequence_number) {
        /* not the expected sequence number */
        return -1;
    } else if (pointer[13] != lfn->checksum) {
        /* not the expected checksum */
        return -2;
    } else if (pointer[12] || pointer[26] || pointer[27]) {
        /* invalid zero fields */
        return -3;
    }

    offset = 13 * (lfn->sequence_number - 1);
    for (i = 0, j = 1; i < 13; i++, j+=2) {
        if (j == 11)
            j = 14;
        else if (j == 26)
            j = 28;

        if (pointer[j] == 0 && pointer[j + 1] == 0) {
            /* end of long file name */
            break;
        }
        gunichar2 c = (pointer[j + 1] << 8) + pointer[j];
        lfn->name2[offset + i] = c;
    }

    if (pointer[0] & 0x40) {
        /* first entry; set len */
        lfn->len = offset + i;
    }
    if ((pointer[0] & 0x3f) == 0x01) {
        /* last entry; finalize entry */
        glong olen;
        gchar *utf8 = g_utf16_to_utf8(lfn->name2, lfn->len, NULL, &olen, NULL);
        if (!utf8) {
            return -4;
        }
        lfn->len = olen;
        memcpy(lfn->name, utf8, olen + 1);
        g_free(utf8);
    }

    return 0;
}

/* returns 0 if successful, >0 if no short_name, and <0 on error */
static int parse_short_name(BDRVVVFATState* s,
        long_file_name* lfn, direntry_t* direntry)
{
    int i, j;

    if (!is_short_name(direntry))
        return 1;

    for (j = 7; j >= 0 && direntry->name[j] == ' '; j--);
    for (i = 0; i <= j; i++) {
        uint8_t c = direntry->name[i];
        if (c != to_valid_short_char(c)) {
            return -1;
        } else if (s->downcase_short_names) {
            lfn->name[i] = qemu_tolower(direntry->name[i]);
        } else {
            lfn->name[i] = direntry->name[i];
        }
    }

    for (j = 2; j >= 0 && direntry->name[8 + j] == ' '; j--) {
    }
    if (j >= 0) {
        lfn->name[i++] = '.';
        lfn->name[i + j + 1] = '\0';
        for (;j >= 0; j--) {
            uint8_t c = direntry->name[8 + j];
            if (c != to_valid_short_char(c)) {
                return -2;
            } else if (s->downcase_short_names) {
                lfn->name[i + j] = qemu_tolower(c);
            } else {
                lfn->name[i + j] = c;
            }
        }
    } else
        lfn->name[i + j + 1] = '\0';

    if (lfn->name[0] == DIR_KANJI_FAKE) {
        lfn->name[0] = DIR_KANJI;
    }
    lfn->len = strlen((char*)lfn->name);

    return 0;
}

static inline uint32_t modified_fat_get(BDRVVVFATState* s,
        unsigned int cluster)
{
    if (cluster < s->last_cluster_of_root_directory) {
        if (cluster + 1 == s->last_cluster_of_root_directory)
            return s->max_fat_value;
        else
            return cluster + 1;
    }

    if (s->fat_type==32) {
        uint32_t* entry=((uint32_t*)s->fat2)+cluster;
        return le32_to_cpu(*entry);
    } else if (s->fat_type==16) {
        uint16_t* entry=((uint16_t*)s->fat2)+cluster;
        return le16_to_cpu(*entry);
    } else {
        const uint8_t* x=s->fat2+cluster*3/2;
        return ((x[0]|(x[1]<<8))>>(cluster&1?4:0))&0x0fff;
    }
}

static inline bool coroutine_fn GRAPH_RDLOCK
cluster_was_modified(BDRVVVFATState *s, uint32_t cluster_num)
{
    int was_modified = 0;
    int i;

    if (s->qcow == NULL) {
        return 0;
    }

    for (i = 0; !was_modified && i < s->sectors_per_cluster; i++) {
        was_modified = bdrv_co_is_allocated(s->qcow->bs,
                                            (cluster2sector(s, cluster_num) +
                                             i) * BDRV_SECTOR_SIZE,
                                            BDRV_SECTOR_SIZE, NULL);
    }

    /*
     * Note that this treats failures to learn allocation status the
     * same as if an allocation has occurred.  It's as safe as
     * anything else, given that a failure to learn allocation status
     * will probably result in more failures.
     */
    return !!was_modified;
}

static const char* get_basename(const char* path)
{
    char* basename = strrchr(path, '/');
    if (basename == NULL)
        return path;
    else
        return basename + 1; /* strip '/' */
}

/*
 * The array s->used_clusters holds the states of the clusters. If it is
 * part of a file, it has bit 2 set, in case of a directory, bit 1. If it
 * was modified, bit 3 is set.
 * If any cluster is allocated, but not part of a file or directory, this
 * driver refuses to commit.
 */
typedef enum {
     USED_DIRECTORY = 1, USED_FILE = 2, USED_ANY = 3, USED_ALLOCATED = 4
} used_t;

/*
 * get_cluster_count_for_direntry() not only determines how many clusters
 * are occupied by direntry, but also if it was renamed or modified.
 *
 * A file is thought to be renamed *only* if there already was a file with
 * exactly the same first cluster, but a different name.
 *
 * Further, the files/directories handled by this function are
 * assumed to be *not* deleted (and *only* those).
 */
static uint32_t coroutine_fn GRAPH_RDLOCK
get_cluster_count_for_direntry(BDRVVVFATState* s, direntry_t* direntry, const char* path)
{
    /*
     * This is a little bit tricky:
     * IF the guest OS just inserts a cluster into the file chain,
     * and leaves the rest alone, (i.e. the original file had clusters
     * 15 -> 16, but now has 15 -> 32 -> 16), then the following happens:
     *
     * - do_commit will write the cluster into the file at the given
     *   offset, but
     *
     * - the cluster which is overwritten should be moved to a later
     *   position in the file.
     *
     * I am not aware that any OS does something as braindead, but this
     * situation could happen anyway when not committing for a long time.
     * Just to be sure that this does not bite us, detect it, and copy the
     * contents of the clusters to-be-overwritten into the qcow.
     */
    int copy_it = 0;
    int was_modified = 0;
    int32_t ret = 0;

    uint32_t cluster_num = begin_of_direntry(direntry);
    uint32_t offset = 0;
    mapping_t* mapping = NULL;
    const char* basename2 = NULL;

    vvfat_close_current_file(s);

    /* the root directory */
    if (cluster_num == 0)
        return 0;

    /* write support */
    if (s->qcow) {
        basename2 = get_basename(path);

        mapping = find_mapping_for_cluster(s, cluster_num);

        if (mapping) {
            const char* basename;

            assert(mapping->mode & MODE_DELETED);
            mapping->mode &= ~MODE_DELETED;

            basename = get_basename(mapping->path);

            assert(mapping->mode & MODE_NORMAL);

            /* rename */
            if (strcmp(basename, basename2))
                schedule_rename(s, cluster_num, g_strdup(path));
        } else if (is_file(direntry))
            /* new file */
            schedule_new_file(s, g_strdup(path), cluster_num);
        else {
            abort();
            return 0;
        }
    }

    while(1) {
        if (s->qcow) {
            if (!copy_it && cluster_was_modified(s, cluster_num)) {
                if (mapping == NULL ||
                        mapping->begin > cluster_num ||
                        mapping->end <= cluster_num)
                mapping = find_mapping_for_cluster(s, cluster_num);


                if (mapping &&
                        (mapping->mode & MODE_DIRECTORY) == 0) {

                    /* was modified in qcow */
                    if (offset != s->cluster_size
                            * ((cluster_num - mapping->begin)
                            + mapping->info.file.offset)) {
                        /* offset of this cluster in file chain has changed */
                        abort();
                        copy_it = 1;
                    } else if (offset == 0) {
                        const char* basename = get_basename(mapping->path);

                        if (strcmp(basename, basename2))
                            copy_it = 1;
                    }
                    assert(mapping->first_mapping_index == -1
                            || mapping->info.file.offset > 0);

                    /* need to write out? */
                    if (!was_modified && is_file(direntry)) {
                        was_modified = 1;
                        schedule_writeout(s, mapping->dir_index, offset);
                    }
                }
            }

            if (copy_it) {
                int i;
                /*
                 * This is horribly inefficient, but that is okay, since
                 * it is rarely executed, if at all.
                 */
                int64_t offs = cluster2sector(s, cluster_num);

                vvfat_close_current_file(s);
                for (i = 0; i < s->sectors_per_cluster; i++) {
                    int res;

                    res = bdrv_co_is_allocated(s->qcow->bs,
                                               (offs + i) * BDRV_SECTOR_SIZE,
                                               BDRV_SECTOR_SIZE, NULL);
                    if (res < 0) {
                        return -1;
                    }
                    if (!res) {
                        res = vvfat_read(s->bs, offs, s->cluster_buffer, 1);
                        if (res) {
                            return -1;
                        }
                        res = bdrv_co_pwrite(s->qcow, offs * BDRV_SECTOR_SIZE,
                                             BDRV_SECTOR_SIZE, s->cluster_buffer,
                                             0);
                        if (res < 0) {
                            return -2;
                        }
                    }
                }
            }
        }

        ret++;
        if (s->used_clusters[cluster_num] & USED_ANY)
            return 0;
        s->used_clusters[cluster_num] = USED_FILE;

        cluster_num = modified_fat_get(s, cluster_num);

        if (fat_eof(s, cluster_num))
            return ret;
        else if (cluster_num < 2 || cluster_num > s->max_fat_value - 16)
            return -1;

        offset += s->cluster_size;
    }
}

/*
 * This function looks at the modified data (qcow).
 * It returns 0 upon inconsistency or error, and the number of clusters
 * used by the directory, its subdirectories and their files.
 */
static int coroutine_fn GRAPH_RDLOCK
check_directory_consistency(BDRVVVFATState *s, int cluster_num, const char* path)
{
    int ret = 0;
    unsigned char* cluster = g_malloc(s->cluster_size);
    direntry_t* direntries = (direntry_t*)cluster;
    mapping_t* mapping = find_mapping_for_cluster(s, cluster_num);

    long_file_name lfn;
    int path_len = strlen(path);
    char path2[PATH_MAX + 1];

    assert(path_len < PATH_MAX); /* len was tested before! */
    pstrcpy(path2, sizeof(path2), path);
    path2[path_len] = '/';
    path2[path_len + 1] = '\0';

    if (mapping) {
        const char* basename = get_basename(mapping->path);
        const char* basename2 = get_basename(path);

        assert(mapping->mode & MODE_DIRECTORY);

        assert(mapping->mode & MODE_DELETED);
        mapping->mode &= ~MODE_DELETED;

        if (strcmp(basename, basename2))
            schedule_rename(s, cluster_num, g_strdup(path));
    } else
        /* new directory */
        schedule_mkdir(s, cluster_num, g_strdup(path));

    lfn_init(&lfn);
    do {
        int i;
        int subret = 0;

        ret++;

        if (s->used_clusters[cluster_num] & USED_ANY) {
            fprintf(stderr, "cluster %d used more than once\n", (int)cluster_num);
            goto fail;
        }
        s->used_clusters[cluster_num] = USED_DIRECTORY;

DLOG(fprintf(stderr, "read cluster %d (sector %d)\n", (int)cluster_num, (int)cluster2sector(s, cluster_num)));
        subret = vvfat_read(s->bs, cluster2sector(s, cluster_num), cluster,
                s->sectors_per_cluster);
        if (subret) {
            fprintf(stderr, "Error fetching direntries\n");
        fail:
            g_free(cluster);
            return 0;
        }

        for (i = 0; i < 0x10 * s->sectors_per_cluster; i++) {
            int cluster_count = 0;

DLOG(fprintf(stderr, "check direntry %d:\n", i); print_direntry(direntries + i));
            if (is_volume_label(direntries + i) || is_dot(direntries + i) ||
                    is_free(direntries + i))
                continue;

            subret = parse_long_name(&lfn, direntries + i);
            if (subret < 0) {
                fprintf(stderr, "Error in long name\n");
                goto fail;
            }
            if (subret == 0 || is_free(direntries + i))
                continue;

            if (fat_chksum(direntries+i) != lfn.checksum) {
                subret = parse_short_name(s, &lfn, direntries + i);
                if (subret < 0) {
                    fprintf(stderr, "Error in short name (%d)\n", subret);
                    goto fail;
                }
                if (subret > 0 || !strcmp((char*)lfn.name, ".")
                        || !strcmp((char*)lfn.name, ".."))
                    continue;
            }
            lfn.checksum = 0x100; /* cannot use long name twice */

            if (!valid_filename(lfn.name)) {
                fprintf(stderr, "Invalid file name\n");
                goto fail;
            }
            if (path_len + 1 + lfn.len >= PATH_MAX) {
                fprintf(stderr, "Name too long: %s/%s\n", path, lfn.name);
                goto fail;
            }
            pstrcpy(path2 + path_len + 1, sizeof(path2) - path_len - 1,
                    (char*)lfn.name);

            if (is_directory(direntries + i)) {
                if (begin_of_direntry(direntries + i) == 0) {
                    DLOG(fprintf(stderr, "invalid begin for directory: %s\n", path2); print_direntry(direntries + i));
                    goto fail;
                }
                cluster_count = check_directory_consistency(s,
                        begin_of_direntry(direntries + i), path2);
                if (cluster_count == 0) {
                    DLOG(fprintf(stderr, "problem in directory %s:\n", path2); print_direntry(direntries + i));
                    goto fail;
                }
            } else if (is_file(direntries + i)) {
                /* check file size with FAT */
                cluster_count = get_cluster_count_for_direntry(s, direntries + i, path2);
                if (cluster_count !=
            DIV_ROUND_UP(le32_to_cpu(direntries[i].size), s->cluster_size)) {
                    DLOG(fprintf(stderr, "Cluster count mismatch\n"));
                    goto fail;
                }
            } else
                abort(); /* cluster_count = 0; */

            ret += cluster_count;
        }

        cluster_num = modified_fat_get(s, cluster_num);
    } while(!fat_eof(s, cluster_num));

    g_free(cluster);
    return ret;
}

/* returns 1 on success */
static int coroutine_fn GRAPH_RDLOCK
is_consistent(BDRVVVFATState* s)
{
    int i, check;
    int used_clusters_count = 0;

DLOG(checkpoint());
    /*
     * - get modified FAT
     * - compare the two FATs (TODO)
     * - get buffer for marking used clusters
     * - recurse direntries from root (using bs->bdrv_pread to make
     *    sure to get the new data)
     *   - check that the FAT agrees with the size
     *   - count the number of clusters occupied by this directory and
     *     its files
     * - check that the cumulative used cluster count agrees with the
     *   FAT
     * - if all is fine, return number of used clusters
     */
    if (s->fat2 == NULL) {
        int size = 0x200 * s->sectors_per_fat;
        s->fat2 = g_malloc(size);
        memcpy(s->fat2, s->fat.pointer, size);
    }
    check = vvfat_read(s->bs,
            s->offset_to_fat, s->fat2, s->sectors_per_fat);
    if (check) {
        fprintf(stderr, "Could not copy fat\n");
        return 0;
    }
    assert (s->used_clusters);
    for (i = 0; i < sector2cluster(s, s->sector_count); i++)
        s->used_clusters[i] &= ~USED_ANY;

    clear_commits(s);

    /* mark every mapped file/directory as deleted.
     * (check_directory_consistency() will unmark those still present). */
    if (s->qcow)
        for (i = 0; i < s->mapping.next; i++) {
            mapping_t* mapping = array_get(&(s->mapping), i);
            if (mapping->first_mapping_index < 0)
                mapping->mode |= MODE_DELETED;
        }

    used_clusters_count = check_directory_consistency(s, 0, s->path);
    if (used_clusters_count <= 0) {
        DLOG(fprintf(stderr, "problem in directory\n"));
        return 0;
    }

    check = s->last_cluster_of_root_directory;
    for (i = check; i < sector2cluster(s, s->sector_count); i++) {
        if (modified_fat_get(s, i)) {
            if(!s->used_clusters[i]) {
                DLOG(fprintf(stderr, "FAT was modified (%d), but cluster is not used?\n", i));
                return 0;
            }
            check++;
        }

        if (s->used_clusters[i] == USED_ALLOCATED) {
            /* allocated, but not used... */
            DLOG(fprintf(stderr, "unused, modified cluster: %d\n", i));
            return 0;
        }
    }

    if (check != used_clusters_count)
        return 0;

    return used_clusters_count;
}

static inline void adjust_mapping_indices(BDRVVVFATState* s,
        int offset, int adjust)
{
    int i;

    for (i = 0; i < s->mapping.next; i++) {
        mapping_t* mapping = array_get(&(s->mapping), i);

#define ADJUST_MAPPING_INDEX(name) \
        if (mapping->name >= offset) \
            mapping->name += adjust

        ADJUST_MAPPING_INDEX(first_mapping_index);
        if (mapping->mode & MODE_DIRECTORY)
            ADJUST_MAPPING_INDEX(info.dir.parent_mapping_index);
    }
}

/* insert or update mapping */
static mapping_t* insert_mapping(BDRVVVFATState* s,
        uint32_t begin, uint32_t end)
{
    /*
     * - find mapping where mapping->begin >= begin,
     * - if mapping->begin > begin: insert
     *   - adjust all references to mappings!
     * - else: adjust
     * - replace name
     */
    int index = find_mapping_for_cluster_aux(s, begin, 0, s->mapping.next);
    mapping_t* mapping = NULL;
    mapping_t* first_mapping = array_get(&(s->mapping), 0);

    if (index < s->mapping.next && (mapping = array_get(&(s->mapping), index))
            && mapping->begin < begin) {
        mapping->end = begin;
        index++;
        mapping = array_get(&(s->mapping), index);
    }
    if (index >= s->mapping.next || mapping->begin > begin) {
        mapping = array_insert(&(s->mapping), index, 1);
        mapping->path = NULL;
        adjust_mapping_indices(s, index, +1);
    }

    mapping->begin = begin;
    mapping->end = end;

DLOG(mapping_t* next_mapping;
assert(index + 1 >= s->mapping.next ||
((next_mapping = array_get(&(s->mapping), index + 1)) &&
 next_mapping->begin >= end)));

    if (s->current_mapping && first_mapping != (mapping_t*)s->mapping.pointer)
        s->current_mapping = array_get(&(s->mapping),
                s->current_mapping - first_mapping);

    return mapping;
}

static int remove_mapping(BDRVVVFATState* s, int mapping_index)
{
    mapping_t* mapping = array_get(&(s->mapping), mapping_index);
    mapping_t* first_mapping = array_get(&(s->mapping), 0);

    /* free mapping */
    if (mapping->first_mapping_index < 0) {
        g_free(mapping->path);
    }

    /* remove from s->mapping */
    array_remove(&(s->mapping), mapping_index);

    /* adjust all references to mappings */
    adjust_mapping_indices(s, mapping_index, -1);

    if (s->current_mapping && first_mapping != (mapping_t*)s->mapping.pointer)
        s->current_mapping = array_get(&(s->mapping),
                s->current_mapping - first_mapping);

    return 0;
}

static void adjust_dirindices(BDRVVVFATState* s, int offset, int adjust)
{
    int i;
    for (i = 0; i < s->mapping.next; i++) {
        mapping_t* mapping = array_get(&(s->mapping), i);
        if (mapping->dir_index >= offset)
            mapping->dir_index += adjust;
        if ((mapping->mode & MODE_DIRECTORY) &&
                mapping->info.dir.first_dir_index >= offset)
            mapping->info.dir.first_dir_index += adjust;
    }
}

static direntry_t* insert_direntries(BDRVVVFATState* s,
        int dir_index, int count)
{
    /*
     * make room in s->directory,
     * adjust_dirindices
     */
    direntry_t* result = array_insert(&(s->directory), dir_index, count);
    if (result == NULL)
        return NULL;
    adjust_dirindices(s, dir_index, count);
    return result;
}

static int remove_direntries(BDRVVVFATState* s, int dir_index, int count)
{
    int ret = array_remove_slice(&(s->directory), dir_index, count);
    if (ret)
        return ret;
    adjust_dirindices(s, dir_index, -count);
    return 0;
}

/*
 * Adapt the mappings of the cluster chain starting at first cluster
 * (i.e. if a file starts at first_cluster, the chain is followed according
 * to the modified fat, and the corresponding entries in s->mapping are
 * adjusted)
 */
static int commit_mappings(BDRVVVFATState* s,
        uint32_t first_cluster, int dir_index)
{
    mapping_t* mapping = find_mapping_for_cluster(s, first_cluster);
    direntry_t* direntry = array_get(&(s->directory), dir_index);
    uint32_t cluster = first_cluster;

    vvfat_close_current_file(s);

    assert(mapping);
    assert(mapping->begin == first_cluster);
    mapping->first_mapping_index = -1;
    mapping->dir_index = dir_index;
    mapping->mode = (dir_index <= 0 || is_directory(direntry)) ?
        MODE_DIRECTORY : MODE_NORMAL;

    while (!fat_eof(s, cluster)) {
        uint32_t c, c1;

        for (c = cluster, c1 = modified_fat_get(s, c); c + 1 == c1;
                c = c1, c1 = modified_fat_get(s, c1));

        c++;
        if (c > mapping->end) {
            int index = array_index(&(s->mapping), mapping);
            int i, max_i = s->mapping.next - index;
            for (i = 1; i < max_i && mapping[i].begin < c; i++);
            while (--i > 0)
                remove_mapping(s, index + 1);
        }
        assert(mapping == array_get(&(s->mapping), s->mapping.next - 1)
                || mapping[1].begin >= c);
        mapping->end = c;

        if (!fat_eof(s, c1)) {
            int i = find_mapping_for_cluster_aux(s, c1, 0, s->mapping.next);
            mapping_t* next_mapping = i >= s->mapping.next ? NULL :
                array_get(&(s->mapping), i);

            if (next_mapping == NULL || next_mapping->begin > c1) {
                int i1 = array_index(&(s->mapping), mapping);

                next_mapping = insert_mapping(s, c1, c1+1);

                if (c1 < c)
                    i1++;
                mapping = array_get(&(s->mapping), i1);
            }

            next_mapping->dir_index = mapping->dir_index;
            next_mapping->first_mapping_index =
                mapping->first_mapping_index < 0 ?
                array_index(&(s->mapping), mapping) :
                mapping->first_mapping_index;
            next_mapping->path = mapping->path;
            next_mapping->mode = mapping->mode;
            next_mapping->read_only = mapping->read_only;
            if (mapping->mode & MODE_DIRECTORY) {
                next_mapping->info.dir.parent_mapping_index =
                        mapping->info.dir.parent_mapping_index;
                next_mapping->info.dir.first_dir_index =
                        mapping->info.dir.first_dir_index +
                        0x10 * s->sectors_per_cluster *
                        (mapping->end - mapping->begin);
            } else
                next_mapping->info.file.offset = mapping->info.file.offset +
                        (mapping->end - mapping->begin);

            mapping = next_mapping;
        }

        cluster = c1;
    }

    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
commit_direntries(BDRVVVFATState* s, int dir_index, int parent_mapping_index)
{
    direntry_t* direntry = array_get(&(s->directory), dir_index);
    uint32_t first_cluster = dir_index == 0 ? 0 : begin_of_direntry(direntry);
    mapping_t* mapping = find_mapping_for_cluster(s, first_cluster);
    int factor = 0x10 * s->sectors_per_cluster;
    int old_cluster_count, new_cluster_count;
    int current_dir_index;
    int first_dir_index;
    int ret, i;
    uint32_t c;

    assert(direntry);
    assert(mapping);
    assert(mapping->begin == first_cluster);
    assert(mapping->info.dir.first_dir_index < s->directory.next);
    assert(mapping->mode & MODE_DIRECTORY);
    assert(dir_index == 0 || is_directory(direntry));

    DLOG(fprintf(stderr, "commit_direntries for %s, parent_mapping_index %d\n",
                 mapping->path, parent_mapping_index));

    current_dir_index = mapping->info.dir.first_dir_index;
    first_dir_index = current_dir_index;
    mapping->info.dir.parent_mapping_index = parent_mapping_index;

    if (first_cluster == 0) {
        old_cluster_count = new_cluster_count =
            s->last_cluster_of_root_directory;
    } else {
        for (old_cluster_count = 0, c = first_cluster; !fat_eof(s, c);
                c = fat_get(s, c))
            old_cluster_count++;

        for (new_cluster_count = 0, c = first_cluster; !fat_eof(s, c);
                c = modified_fat_get(s, c))
            new_cluster_count++;
    }

    if (new_cluster_count > old_cluster_count) {
        if (insert_direntries(s,
                current_dir_index + factor * old_cluster_count,
                factor * (new_cluster_count - old_cluster_count)) == NULL)
            return -1;
    } else if (new_cluster_count < old_cluster_count)
        remove_direntries(s,
                current_dir_index + factor * new_cluster_count,
                factor * (old_cluster_count - new_cluster_count));

    for (c = first_cluster; !fat_eof(s, c); c = modified_fat_get(s, c)) {
        direntry_t *first_direntry;

        direntry = array_get(&(s->directory), current_dir_index);
        ret = vvfat_read(s->bs, cluster2sector(s, c), (uint8_t *)direntry,
                s->sectors_per_cluster);
        if (ret)
            return ret;

        /* The first directory entry on the filesystem is the volume name */
        first_direntry = (direntry_t*) s->directory.pointer;
        assert(!memcmp(first_direntry->name, s->volume_label, 11));

        current_dir_index += factor;
    }

    ret = commit_mappings(s, first_cluster, dir_index);
    if (ret)
        return ret;

    /* recurse */
    for (i = 0; i < factor * new_cluster_count; i++) {
        direntry = array_get(&(s->directory), first_dir_index + i);
        if (is_directory(direntry) && !is_dot(direntry)) {
            mapping = find_mapping_for_cluster(s, first_cluster);
            if (mapping == NULL) {
                return -1;
            }
            assert(mapping->mode & MODE_DIRECTORY);
            ret = commit_direntries(s, first_dir_index + i,
                array_index(&(s->mapping), mapping));
            if (ret)
                return ret;
        }
    }

    return 0;
}

/* commit one file (adjust contents, adjust mapping),
   return first_mapping_index */
static int coroutine_fn GRAPH_RDLOCK
commit_one_file(BDRVVVFATState* s, int dir_index, uint32_t offset)
{
    direntry_t* direntry = array_get(&(s->directory), dir_index);
    uint32_t c = begin_of_direntry(direntry);
    uint32_t first_cluster = c;
    mapping_t* mapping = find_mapping_for_cluster(s, c);
    uint32_t size = filesize_of_direntry(direntry);
    char *cluster;
    uint32_t i;
    int fd = 0;

    assert(offset < size);
    assert((offset % s->cluster_size) == 0);

    if (mapping == NULL) {
        return -1;
    }

    for (i = 0; i < offset; i += s->cluster_size) {
        c = modified_fat_get(s, c);
    }

    fd = qemu_open_old(mapping->path, O_RDWR | O_CREAT | O_BINARY, 0666);
    if (fd < 0) {
        fprintf(stderr, "Could not open %s... (%s, %d)\n", mapping->path,
                strerror(errno), errno);
        return fd;
    }
    if (offset > 0) {
        if (lseek(fd, offset, SEEK_SET) != offset) {
            qemu_close(fd);
            return -3;
        }
    }

    cluster = g_malloc(s->cluster_size);

    while (offset < size) {
        uint32_t c1;
        int rest_size = (size - offset > s->cluster_size ?
                s->cluster_size : size - offset);
        int ret;

        c1 = modified_fat_get(s, c);

        assert((size - offset == 0 && fat_eof(s, c)) ||
                (size > offset && c >=2 && !fat_eof(s, c)));

        ret = vvfat_read(s->bs, cluster2sector(s, c),
            (uint8_t*)cluster, DIV_ROUND_UP(rest_size, 0x200));

        if (ret < 0) {
            qemu_close(fd);
            g_free(cluster);
            return ret;
        }

        if (write(fd, cluster, rest_size) < 0) {
            qemu_close(fd);
            g_free(cluster);
            return -2;
        }

        offset += rest_size;
        c = c1;
    }

    if (ftruncate(fd, size)) {
        perror("ftruncate()");
        qemu_close(fd);
        g_free(cluster);
        return -4;
    }
    qemu_close(fd);
    g_free(cluster);

    return commit_mappings(s, first_cluster, dir_index);
}

#ifdef DEBUG
/* test, if all mappings point to valid direntries */
static void check1(BDRVVVFATState* s)
{
    int i;
    for (i = 0; i < s->mapping.next; i++) {
        mapping_t* mapping = array_get(&(s->mapping), i);
        if (mapping->mode & MODE_DELETED) {
            fprintf(stderr, "deleted\n");
            continue;
        }
        assert(mapping->dir_index < s->directory.next);
        direntry_t* direntry = array_get(&(s->directory), mapping->dir_index);
        assert(mapping->begin == begin_of_direntry(direntry) || mapping->first_mapping_index >= 0);
        if (mapping->mode & MODE_DIRECTORY) {
            assert(mapping->info.dir.first_dir_index + 0x10 * s->sectors_per_cluster * (mapping->end - mapping->begin) <= s->directory.next);
            assert((mapping->info.dir.first_dir_index % (0x10 * s->sectors_per_cluster)) == 0);
        }
    }
}

/* test, if all direntries have mappings */
static void check2(BDRVVVFATState* s)
{
    int i;
    int first_mapping = -1;

    for (i = 0; i < s->directory.next; i++) {
        direntry_t* direntry = array_get(&(s->directory), i);

        if (is_short_name(direntry) && begin_of_direntry(direntry)) {
            mapping_t* mapping = find_mapping_for_cluster(s, begin_of_direntry(direntry));
            assert(mapping);
            assert(mapping->dir_index == i || is_dot(direntry));
            assert(mapping->begin == begin_of_direntry(direntry) || is_dot(direntry));
        }

        if ((i % (0x10 * s->sectors_per_cluster)) == 0) {
            /* cluster start */
            int j, count = 0;

            for (j = 0; j < s->mapping.next; j++) {
                mapping_t* mapping = array_get(&(s->mapping), j);
                if (mapping->mode & MODE_DELETED)
                    continue;
                if (mapping->mode & MODE_DIRECTORY) {
                    if (mapping->info.dir.first_dir_index <= i && mapping->info.dir.first_dir_index + 0x10 * s->sectors_per_cluster > i) {
                        assert(++count == 1);
                        if (mapping->first_mapping_index == -1)
                            first_mapping = array_index(&(s->mapping), mapping);
                        else
                            assert(first_mapping == mapping->first_mapping_index);
                        if (mapping->info.dir.parent_mapping_index < 0)
                            assert(j == 0);
                        else {
                            mapping_t* parent = array_get(&(s->mapping), mapping->info.dir.parent_mapping_index);
                            assert(parent->mode & MODE_DIRECTORY);
                            assert(parent->info.dir.first_dir_index < mapping->info.dir.first_dir_index);
                        }
                    }
                }
            }
            if (count == 0)
                first_mapping = -1;
        }
    }
}
#endif

static int handle_renames_and_mkdirs(BDRVVVFATState* s)
{
    int i;

#ifdef DEBUG
    fprintf(stderr, "handle_renames\n");
    for (i = 0; i < s->commits.next; i++) {
        commit_t* commit = array_get(&(s->commits), i);
        fprintf(stderr, "%d, %s (%u, %d)\n", i,
                commit->path ? commit->path : "(null)",
                commit->param.rename.cluster, commit->action);
    }
#endif

    for (i = 0; i < s->commits.next;) {
        commit_t* commit = array_get(&(s->commits), i);
        if (commit->action == ACTION_RENAME) {
            mapping_t* mapping = find_mapping_for_cluster(s,
                    commit->param.rename.cluster);
            char *old_path;

            if (mapping == NULL) {
                return -1;
            }
            old_path = mapping->path;
            assert(commit->path);
            mapping->path = commit->path;
            if (rename(old_path, mapping->path))
                return -2;

            if (mapping->mode & MODE_DIRECTORY) {
                int l1 = strlen(mapping->path);
                int l2 = strlen(old_path);
                int diff = l1 - l2;
                direntry_t* direntry = array_get(&(s->directory),
                        mapping->info.dir.first_dir_index);
                uint32_t c = mapping->begin;
                int j = 0;

                /* recurse */
                while (!fat_eof(s, c)) {
                    do {
                        direntry_t *d = direntry + j;

                        if (is_file(d) || (is_directory(d) && !is_dot(d))) {
                            int l;
                            char *new_path;
                            mapping_t* m = find_mapping_for_cluster(s,
                                    begin_of_direntry(d));
                            if (m == NULL) {
                                return -1;
                            }
                            l = strlen(m->path);
                            new_path = g_malloc(l + diff + 1);

                            assert(!strncmp(m->path, mapping->path, l2));

                            pstrcpy(new_path, l + diff + 1, mapping->path);
                            pstrcpy(new_path + l1, l + diff + 1 - l1,
                                    m->path + l2);

                            schedule_rename(s, m->begin, new_path);
                        }
                        j++;
                    } while (j % (0x10 * s->sectors_per_cluster) != 0);
                    c = fat_get(s, c);
                }
            }

            g_free(old_path);
            array_remove(&(s->commits), i);
            continue;
        } else if (commit->action == ACTION_MKDIR) {
            mapping_t* mapping;
            int j, parent_path_len;

            if (g_mkdir(commit->path, 0755)) {
                return -5;
            }

            mapping = insert_mapping(s, commit->param.mkdir.cluster,
                    commit->param.mkdir.cluster + 1);
            if (mapping == NULL)
                return -6;

            mapping->mode = MODE_DIRECTORY;
            mapping->read_only = 0;
            mapping->path = commit->path;
            j = s->directory.next;
            assert(j);
            insert_direntries(s, s->directory.next,
                    0x10 * s->sectors_per_cluster);
            mapping->info.dir.first_dir_index = j;

            parent_path_len = strlen(commit->path)
                - strlen(get_basename(commit->path)) - 1;
            for (j = 0; j < s->mapping.next; j++) {
                mapping_t* m = array_get(&(s->mapping), j);
                if (m->first_mapping_index < 0 && m != mapping &&
                        !strncmp(m->path, mapping->path, parent_path_len) &&
                        strlen(m->path) == parent_path_len)
                    break;
            }
            assert(j < s->mapping.next);
            mapping->info.dir.parent_mapping_index = j;

            array_remove(&(s->commits), i);
            continue;
        }

        i++;
    }
    return 0;
}

/*
 * TODO: make sure that the short name is not matching *another* file
 */
static int coroutine_fn GRAPH_RDLOCK handle_commits(BDRVVVFATState* s)
{
    int i, fail = 0;

    vvfat_close_current_file(s);

    for (i = 0; !fail && i < s->commits.next; i++) {
        commit_t* commit = array_get(&(s->commits), i);
        switch(commit->action) {
        case ACTION_RENAME: case ACTION_MKDIR:
            abort();
            fail = -2;
            break;
        case ACTION_WRITEOUT: {
            direntry_t* entry = array_get(&(s->directory),
                    commit->param.writeout.dir_index);
            uint32_t begin = begin_of_direntry(entry);
            mapping_t* mapping = find_mapping_for_cluster(s, begin);

            assert(mapping);
            assert(mapping->begin == begin);
            assert(commit->path == NULL);

            if (commit_one_file(s, commit->param.writeout.dir_index,
                        commit->param.writeout.modified_offset))
                fail = -3;

            break;
        }
        case ACTION_NEW_FILE: {
            int begin = commit->param.new_file.first_cluster;
            mapping_t* mapping = find_mapping_for_cluster(s, begin);
            direntry_t* entry;
            int j;

            /* find direntry */
            for (j = 0; j < s->directory.next; j++) {
                entry = array_get(&(s->directory), j);
                if (is_file(entry) && begin_of_direntry(entry) == begin)
                    break;
            }

            if (j >= s->directory.next) {
                fail = -6;
                continue;
            }

            /* make sure there exists an initial mapping */
            if (mapping && mapping->begin != begin) {
                mapping->end = begin;
                mapping = NULL;
            }
            if (mapping == NULL) {
                mapping = insert_mapping(s, begin, begin+1);
            }
            /* most members will be fixed in commit_mappings() */
            assert(commit->path);
            mapping->path = commit->path;
            mapping->read_only = 0;
            mapping->mode = MODE_NORMAL;
            mapping->info.file.offset = 0;

            if (commit_one_file(s, j, 0)) {
                fail = -7;
            }

            break;
        }
        default:
            abort();
        }
    }
    if (i > 0 && array_remove_slice(&(s->commits), 0, i))
        return -1;
    return fail;
}

static int handle_deletes(BDRVVVFATState* s)
{
    int i, deferred = 1, deleted = 1;

    /* delete files corresponding to mappings marked as deleted */
    /* handle DELETEs and unused mappings (modified_fat_get(s, mapping->begin) == 0) */
    while (deferred && deleted) {
        deferred = 0;
        deleted = 0;

        for (i = 1; i < s->mapping.next; i++) {
            mapping_t* mapping = array_get(&(s->mapping), i);
            if (mapping->mode & MODE_DELETED) {
                direntry_t* entry = array_get(&(s->directory),
                        mapping->dir_index);

                if (is_free(entry)) {
                    /* remove file/directory */
                    if (mapping->mode & MODE_DIRECTORY) {
                        int j, next_dir_index = s->directory.next,
                        first_dir_index = mapping->info.dir.first_dir_index;

                        if (rmdir(mapping->path) < 0) {
                            if (errno == ENOTEMPTY) {
                                deferred++;
                                continue;
                            } else
                                return -5;
                        }

                        for (j = 1; j < s->mapping.next; j++) {
                            mapping_t* m = array_get(&(s->mapping), j);
                            if (m->mode & MODE_DIRECTORY &&
                                    m->info.dir.first_dir_index >
                                    first_dir_index &&
                                    m->info.dir.first_dir_index <
                                    next_dir_index)
                                next_dir_index =
                                    m->info.dir.first_dir_index;
                        }
                        remove_direntries(s, first_dir_index,
                                next_dir_index - first_dir_index);

                        deleted++;
                    }
                } else {
                    if (unlink(mapping->path))
                        return -4;
                    deleted++;
                }
                DLOG(fprintf(stderr, "DELETE (%d)\n", i); print_mapping(mapping); print_direntry(entry));
                remove_mapping(s, i);
            }
        }
    }

    return 0;
}

/*
 * synchronize mapping with new state:
 *
 * - copy FAT (with bdrv_pread)
 * - mark all filenames corresponding to mappings as deleted
 * - recurse direntries from root (using bs->bdrv_pread)
 * - delete files corresponding to mappings marked as deleted
 */
static int coroutine_fn GRAPH_RDLOCK do_commit(BDRVVVFATState* s)
{
    int ret = 0;

    /* the real meat are the commits. Nothing to do? Move along! */
    if (s->commits.next == 0)
        return 0;

    vvfat_close_current_file(s);

    ret = handle_renames_and_mkdirs(s);
    if (ret) {
        fprintf(stderr, "Error handling renames (%d)\n", ret);
        abort();
        return ret;
    }

    /* copy FAT (with bdrv_pread) */
    memcpy(s->fat.pointer, s->fat2, 0x200 * s->sectors_per_fat);

    /* recurse direntries from root (using bs->bdrv_pread) */
    ret = commit_direntries(s, 0, -1);
    if (ret) {
        fprintf(stderr, "Fatal: error while committing (%d)\n", ret);
        abort();
        return ret;
    }

    ret = handle_commits(s);
    if (ret) {
        fprintf(stderr, "Error handling commits (%d)\n", ret);
        abort();
        return ret;
    }

    ret = handle_deletes(s);
    if (ret) {
        fprintf(stderr, "Error deleting\n");
        abort();
        return ret;
    }

    bdrv_make_empty(s->qcow, NULL);

    memset(s->used_clusters, 0, sector2cluster(s, s->sector_count));

DLOG(checkpoint());
    return 0;
}

static int coroutine_fn GRAPH_RDLOCK try_commit(BDRVVVFATState* s)
{
    vvfat_close_current_file(s);
DLOG(checkpoint());
    if(!is_consistent(s))
        return -1;
    return do_commit(s);
}

static int coroutine_fn GRAPH_RDLOCK
vvfat_write(BlockDriverState *bs, int64_t sector_num,
            const uint8_t *buf, int nb_sectors)
{
    BDRVVVFATState *s = bs->opaque;
    int i, ret;
    int first_cluster, last_cluster;

DLOG(checkpoint());

    /* Check if we're operating in read-only mode */
    if (s->qcow == NULL) {
        return -EACCES;
    }

    vvfat_close_current_file(s);

    if (sector_num == s->offset_to_bootsector && nb_sectors == 1) {
        /*
         * Write on bootsector. Allow only changing the reserved1 field,
         * used to mark volume dirtiness
         */
        unsigned char *bootsector = s->first_sectors
                                    + s->offset_to_bootsector * 0x200;
        /*
         * LATER TODO: if FAT32, this is wrong (see init_directories(),
         * which always creates a FAT16 bootsector)
         */
        const int reserved1_offset = offsetof(bootsector_t, u.fat16.reserved1);

        for (i = 0; i < 0x200; i++) {
            if (i != reserved1_offset && bootsector[i] != buf[i]) {
                fprintf(stderr, "Tried to write to protected bootsector\n");
                return -1;
            }
        }

        /* Update bootsector with the only updatable byte, and return success */
        bootsector[reserved1_offset] = buf[reserved1_offset];
        return 0;
    }

    /*
     * Some sanity checks:
     * - do not allow writing to the boot sector
     */
    if (sector_num < s->offset_to_fat)
        return -1;

    /*
     * Values will be negative for writes to the FAT, which is located before
     * the root directory.
     */
    first_cluster = sector2cluster(s, sector_num);
    last_cluster = sector2cluster(s, sector_num + nb_sectors - 1);

    for (i = first_cluster; i <= last_cluster;) {
        mapping_t *mapping = NULL;

        if (i >= 0) {
            mapping = find_mapping_for_cluster(s, i);
        }

        if (mapping) {
            if (mapping->read_only) {
                fprintf(stderr, "Tried to write to write-protected file %s\n",
                        mapping->path);
                return -1;
            }

            if (mapping->mode & MODE_DIRECTORY) {
                int begin = cluster2sector(s, i);
                int end = begin + s->sectors_per_cluster, k;
                int dir_index;
                const direntry_t* direntries;
                long_file_name lfn;

                lfn_init(&lfn);

                if (begin < sector_num)
                    begin = sector_num;
                if (end > sector_num + nb_sectors)
                    end = sector_num + nb_sectors;
                dir_index  = mapping->dir_index +
                    0x10 * (begin - mapping->begin * s->sectors_per_cluster);
                direntries = (direntry_t*)(buf + 0x200 * (begin - sector_num));

                for (k = 0; k < (end - begin) * 0x10; k++) {
                    /* no access to the direntry of a read-only file */
                    if (is_short_name(direntries + k) &&
                            (direntries[k].attributes & 1)) {
                        if (memcmp(direntries + k,
                                    array_get(&(s->directory), dir_index + k),
                                    sizeof(direntry_t))) {
                            warn_report("tried to write to write-protected "
                                        "file");
                            return -1;
                        }
                    }
                }
            }
            i = mapping->end;
        } else {
            i++;
        }
    }

    /*
     * Use qcow backend. Commit later.
     */
DLOG(fprintf(stderr, "Write to qcow backend: %d + %d\n", (int)sector_num, nb_sectors));
    ret = bdrv_co_pwrite(s->qcow, sector_num * BDRV_SECTOR_SIZE,
                         nb_sectors * BDRV_SECTOR_SIZE, buf, 0);
    if (ret < 0) {
        fprintf(stderr, "Error writing to qcow backend\n");
        return ret;
    }

    for (i = first_cluster; i <= last_cluster; i++) {
        if (i >= 0) {
            s->used_clusters[i] |= USED_ALLOCATED;
        }
    }

DLOG(checkpoint());
    /* TODO: add timeout */
    try_commit(s);

DLOG(checkpoint());
    return 0;
}

static int coroutine_fn GRAPH_RDLOCK
vvfat_co_pwritev(BlockDriverState *bs, int64_t offset, int64_t bytes,
                 QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    int ret;
    BDRVVVFATState *s = bs->opaque;
    uint64_t sector_num = offset >> BDRV_SECTOR_BITS;
    int nb_sectors = bytes >> BDRV_SECTOR_BITS;
    void *buf;

    assert(QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(bytes, BDRV_SECTOR_SIZE));

    buf = g_try_malloc(bytes);
    if (bytes && buf == NULL) {
        return -ENOMEM;
    }
    qemu_iovec_to_buf(qiov, 0, buf, bytes);

    qemu_co_mutex_lock(&s->lock);
    ret = vvfat_write(bs, sector_num, buf, nb_sectors);
    qemu_co_mutex_unlock(&s->lock);

    g_free(buf);

    return ret;
}

static int coroutine_fn vvfat_co_block_status(BlockDriverState *bs,
                                              bool want_zero, int64_t offset,
                                              int64_t bytes, int64_t *n,
                                              int64_t *map,
                                              BlockDriverState **file)
{
    *n = bytes;
    return BDRV_BLOCK_DATA;
}

static void vvfat_qcow_options(BdrvChildRole role, bool parent_is_format,
                               int *child_flags, QDict *child_options,
                               int parent_flags, QDict *parent_options)
{
    qdict_set_default_str(child_options, BDRV_OPT_READ_ONLY, "off");
    qdict_set_default_str(child_options, BDRV_OPT_AUTO_READ_ONLY, "off");
    qdict_set_default_str(child_options, BDRV_OPT_CACHE_NO_FLUSH, "on");
}

static BdrvChildClass child_vvfat_qcow;

static int enable_write_target(BlockDriverState *bs, Error **errp)
{
    BDRVVVFATState *s = bs->opaque;
    BlockDriver *bdrv_qcow = NULL;
    QemuOpts *opts = NULL;
    int ret;
    int size = sector2cluster(s, s->sector_count);
    QDict *options;

    s->used_clusters = g_malloc0(size);

    array_init(&(s->commits), sizeof(commit_t));

    s->qcow_filename = create_tmp_file(errp);
    if (!s->qcow_filename) {
        ret = -ENOENT;
        goto err;
    }

    bdrv_qcow = bdrv_find_format("qcow");
    if (!bdrv_qcow) {
        error_setg(errp, "Failed to locate qcow driver");
        ret = -ENOENT;
        goto err;
    }

    opts = qemu_opts_create(bdrv_qcow->create_opts, NULL, 0, &error_abort);
    qemu_opt_set_number(opts, BLOCK_OPT_SIZE,
                        bs->total_sectors * BDRV_SECTOR_SIZE, &error_abort);
    qemu_opt_set(opts, BLOCK_OPT_BACKING_FILE, "fat:", &error_abort);

    ret = bdrv_create(bdrv_qcow, s->qcow_filename, opts, errp);
    qemu_opts_del(opts);
    if (ret < 0) {
        goto err;
    }

    options = qdict_new();
    qdict_put_str(options, "write-target.driver", "qcow");
    s->qcow = bdrv_open_child(s->qcow_filename, options, "write-target", bs,
                              &child_vvfat_qcow,
                              BDRV_CHILD_DATA | BDRV_CHILD_METADATA,
                              false, errp);
    qobject_unref(options);
    if (!s->qcow) {
        ret = -EINVAL;
        goto err;
    }

#ifndef _WIN32
    unlink(s->qcow_filename);
#endif

    return 0;

err:
    return ret;
}

static void vvfat_child_perm(BlockDriverState *bs, BdrvChild *c,
                             BdrvChildRole role,
                             BlockReopenQueue *reopen_queue,
                             uint64_t perm, uint64_t shared,
                             uint64_t *nperm, uint64_t *nshared)
{
    assert(role & BDRV_CHILD_DATA);
    /* This is a private node, nobody should try to attach to it */
    *nperm = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE;
    *nshared = BLK_PERM_WRITE_UNCHANGED;
}

static void vvfat_close(BlockDriverState *bs)
{
    BDRVVVFATState *s = bs->opaque;

    vvfat_close_current_file(s);
    array_free(&(s->fat));
    array_free(&(s->directory));
    array_free(&(s->mapping));
    g_free(s->cluster_buffer);

    if (s->qcow) {
        migrate_del_blocker(&s->migration_blocker);
    }
}

static const char *const vvfat_strong_runtime_opts[] = {
    "dir",
    "fat-type",
    "floppy",
    "label",
    "rw",

    NULL
};

static BlockDriver bdrv_vvfat = {
    .format_name            = "vvfat",
    .protocol_name          = "fat",
    .instance_size          = sizeof(BDRVVVFATState),

    .bdrv_parse_filename    = vvfat_parse_filename,
    .bdrv_open              = vvfat_open,
    .bdrv_refresh_limits    = vvfat_refresh_limits,
    .bdrv_close             = vvfat_close,
    .bdrv_child_perm        = vvfat_child_perm,

    .bdrv_co_preadv         = vvfat_co_preadv,
    .bdrv_co_pwritev        = vvfat_co_pwritev,
    .bdrv_co_block_status   = vvfat_co_block_status,

    .strong_runtime_opts    = vvfat_strong_runtime_opts,
};

static void bdrv_vvfat_init(void)
{
    child_vvfat_qcow = child_of_bds;
    child_vvfat_qcow.inherit_options = vvfat_qcow_options;
    bdrv_register(&bdrv_vvfat);
}

block_init(bdrv_vvfat_init);

#ifdef DEBUG
static void checkpoint(void)
{
    assert(((mapping_t*)array_get(&(vvv->mapping), 0))->end == 2);
    check1(vvv);
    check2(vvv);
    assert(!vvv->current_mapping || vvv->current_fd || (vvv->current_mapping->mode & MODE_DIRECTORY));
}
#endif
