/* vim:set shiftwidth=4 ts=8: */
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
#include <sys/stat.h>
#include <dirent.h>
#include "qemu-common.h"
#include "block_int.h"
#include "module.h"

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

#undef stderr
#define stderr STDERR
FILE* stderr = NULL;

static void checkpoint(void);

#ifdef __MINGW32__
void nonono(const char* file, int line, const char* msg) {
    fprintf(stderr, "Nonono! %s:%d %s\n", file, line, msg);
    exit(-5);
}
#undef assert
#define assert(a) do {if (!(a)) nonono(__FILE__, __LINE__, #a);}while(0)
#endif

#else

#define DLOG(a)

#endif

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
    if(array->pointer)
        free(array->pointer);
    array->size=array->next=0;
}

/* does not automatically grow */
static inline void* array_get(array_t* array,unsigned int index) {
    assert(index < array->next);
    return array->pointer + index * array->item_size;
}

static inline int array_ensure_allocated(array_t* array, int index)
{
    if((index + 1) * array->item_size > array->size) {
	int new_size = (index + 32) * array->item_size;
	array->pointer = qemu_realloc(array->pointer, new_size);
	if (!array->pointer)
	    return -1;
	array->size = new_size;
	array->next = index + 1;
    }

    return 0;
}

static inline void* array_get_next(array_t* array) {
    unsigned int next = array->next;
    void* result;

    if (array_ensure_allocated(array, next) < 0)
	return NULL;

    array->next = next + 1;
    result = array_get(array, next);

    return result;
}

static inline void* array_insert(array_t* array,unsigned int index,unsigned int count) {
    if((array->next+count)*array->item_size>array->size) {
	int increment=count*array->item_size;
	array->pointer=qemu_realloc(array->pointer,array->size+increment);
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

/* this performs a "roll", so that the element which was at index_from becomes
 * index_to, but the order of all other elements is preserved. */
static inline int array_roll(array_t* array,int index_to,int index_from,int count)
{
    char* buf;
    char* from;
    char* to;
    int is;

    if(!array ||
	    index_to<0 || index_to>=array->next ||
	    index_from<0 || index_from>=array->next)
	return -1;

    if(index_to==index_from)
	return 0;

    is=array->item_size;
    from=array->pointer+index_from*is;
    to=array->pointer+index_to*is;
    buf=qemu_malloc(is*count);
    memcpy(buf,from,is*count);

    if(index_to<index_from)
	memmove(to+is*count,to,from-to);
    else
	memmove(from,from+is*count,to-from);

    memcpy(to,buf,is*count);

    free(buf);

    return 0;
}

static inline int array_remove_slice(array_t* array,int index, int count)
{
    assert(index >=0);
    assert(count > 0);
    assert(index + count <= array->next);
    if(array_roll(array,array->next-1,index,count))
	return -1;
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
 * For this reason we need to use __attribute__((packed)). */

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
	    uint8_t current_head;
	    uint8_t signature;
	    uint32_t id;
	    uint8_t volume_label[11];
	} __attribute__((packed)) fat16;
	struct {
	    uint32_t sectors_per_fat;
	    uint16_t flags;
	    uint8_t major,minor;
	    uint32_t first_cluster_of_root_directory;
	    uint16_t info_sector;
	    uint16_t backup_boot_sector;
	    uint16_t ignored;
	} __attribute__((packed)) fat32;
    } u;
    uint8_t fat_type[8];
    uint8_t ignored[0x1c0];
    uint8_t magic[2];
} __attribute__((packed)) bootsector_t;

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
} __attribute__((packed)) partition_t;

typedef struct mbr_t {
    uint8_t ignored[0x1b8];
    uint32_t nt_id;
    uint8_t ignored2[2];
    partition_t partition[4];
    uint8_t magic[2];
} __attribute__((packed)) mbr_t;

typedef struct direntry_t {
    uint8_t name[8];
    uint8_t extension[3];
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
} __attribute__((packed)) direntry_t;

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
	 * - the next cluster of the directory for a directory, and
	 * - the address of the buffer for a faked entry
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

    enum { MODE_UNDEFINED = 0, MODE_NORMAL = 1, MODE_MODIFIED = 2,
	MODE_DIRECTORY = 4, MODE_FAKED = 8,
	MODE_DELETED = 16, MODE_RENAMED = 32 } mode;
    int read_only;
} mapping_t;

#ifdef DEBUG
static void print_direntry(const struct direntry_t*);
static void print_mapping(const struct mapping_t* mapping);
#endif

/* here begins the real VVFAT driver */

typedef struct BDRVVVFATState {
    BlockDriverState* bs; /* pointer to parent */
    unsigned int first_sectors_number; /* 1 for a single partition, 0x40 for a disk with partition table */
    unsigned char first_sectors[0x40*0x200];

    int fat_type; /* 16 or 32 */
    array_t fat,directory,mapping;

    unsigned int cluster_size;
    unsigned int sectors_per_cluster;
    unsigned int sectors_per_fat;
    unsigned int sectors_of_root_directory;
    uint32_t last_cluster_of_root_directory;
    unsigned int faked_sectors; /* how many sectors are faked before file data */
    uint32_t sector_count; /* total number of sectors of the partition */
    uint32_t cluster_count; /* total number of clusters of this partition */
    uint32_t max_fat_value;

    int current_fd;
    mapping_t* current_mapping;
    unsigned char* cluster; /* points to current cluster */
    unsigned char* cluster_buffer; /* points to a buffer to hold temp data */
    unsigned int current_cluster;

    /* write support */
    BlockDriverState* write_target;
    char* qcow_filename;
    BlockDriverState* qcow;
    void* fat2;
    char* used_clusters;
    array_t commits;
    const char* path;
    int downcase_short_names;
} BDRVVVFATState;

/* take the sector position spos and convert it to Cylinder/Head/Sector position
 * if the position is outside the specified geometry, fill maximum value for CHS
 * and return 1 to signal overflow.
 */
static int sector2CHS(BlockDriverState* bs, mbr_chs_t * chs, int spos){
    int head,sector;
    sector   = spos % (bs->secs);  spos/= bs->secs;
    head     = spos % (bs->heads); spos/= bs->heads;
    if(spos >= bs->cyls){
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

static void init_mbr(BDRVVVFATState* s)
{
    /* TODO: if the files mbr.img and bootsect.img exist, use them */
    mbr_t* real_mbr=(mbr_t*)s->first_sectors;
    partition_t* partition=&(real_mbr->partition[0]);
    int lba;

    memset(s->first_sectors,0,512);

    /* Win NT Disk Signature */
    real_mbr->nt_id= cpu_to_le32(0xbe1afdfa);

    partition->attributes=0x80; /* bootable */

    /* LBA is used when partition is outside the CHS geometry */
    lba = sector2CHS(s->bs, &partition->start_CHS, s->first_sectors_number-1);
    lba|= sector2CHS(s->bs, &partition->end_CHS,   s->sector_count);

    /*LBA partitions are identified only by start/length_sector_long not by CHS*/
    partition->start_sector_long =cpu_to_le32(s->first_sectors_number-1);
    partition->length_sector_long=cpu_to_le32(s->sector_count - s->first_sectors_number+1);

    /* FAT12/FAT16/FAT32 */
    /* DOS uses different types when partition is LBA,
       probably to prevent older versions from using CHS on them */
    partition->fs_type= s->fat_type==12 ? 0x1:
                        s->fat_type==16 ? (lba?0xe:0x06):
                         /*fat_tyoe==32*/ (lba?0xc:0x0b);

    real_mbr->magic[0]=0x55; real_mbr->magic[1]=0xaa;
}

/* direntry functions */

/* dest is assumed to hold 258 bytes, and pads with 0xffff up to next multiple of 26 */
static inline int short2long_name(char* dest,const char* src)
{
    int i;
    int len;
    for(i=0;i<129 && src[i];i++) {
        dest[2*i]=src[i];
	dest[2*i+1]=0;
    }
    len=2*i;
    dest[2*i]=dest[2*i+1]=0;
    for(i=2*i+2;(i%26);i++)
	dest[i]=0xff;
    return len;
}

static inline direntry_t* create_long_filename(BDRVVVFATState* s,const char* filename)
{
    char buffer[258];
    int length=short2long_name(buffer,filename),
        number_of_entries=(length+25)/26,i;
    direntry_t* entry;

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
	entry->name[offset]=buffer[i];
    }
    return array_get(&(s->directory),s->directory.next-number_of_entries);
}

static char is_free(const direntry_t* direntry)
{
    return direntry->name[0]==0xe5 || direntry->name[0]==0x00;
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
    return direntry->attributes & 0x10 && direntry->name[0] != 0xe5;
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

/* fat functions */

static inline uint8_t fat_chksum(const direntry_t* entry)
{
    uint8_t chksum=0;
    int i;

    for(i=0;i<11;i++) {
        unsigned char c;

        c = (i <= 8) ? entry->name[i] : entry->extension[i-8];
        chksum=(((chksum&0xfe)>>1)|((chksum&0x01)?0x80:0)) + c;
    }

    return chksum;
}

/* if return_time==0, this returns the fat_date, else the fat_time */
static uint16_t fat_datetime(time_t time,int return_time) {
    struct tm* t;
#ifdef _WIN32
    t=localtime(&time); /* this is not thread safe */
#else
    struct tm t1;
    t=&t1;
    localtime_r(&time,t);
#endif
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

/* TODO: in create_short_filename, 0xe5->0x05 is not yet handled! */
/* TODO: in parse_short_filename, 0x05->0xe5 is not yet handled! */
static inline direntry_t* create_short_and_long_name(BDRVVVFATState* s,
	unsigned int directory_start, const char* filename, int is_dot)
{
    int i,j,long_index=s->directory.next;
    direntry_t* entry = NULL;
    direntry_t* entry_long = NULL;

    if(is_dot) {
	entry=array_get_next(&(s->directory));
	memset(entry->name,0x20,11);
	memcpy(entry->name,filename,strlen(filename));
	return entry;
    }

    entry_long=create_long_filename(s,filename);

    i = strlen(filename);
    for(j = i - 1; j>0  && filename[j]!='.';j--);
    if (j > 0)
	i = (j > 8 ? 8 : j);
    else if (i > 8)
	i = 8;

    entry=array_get_next(&(s->directory));
    memset(entry->name,0x20,11);
    memcpy(entry->name, filename, i);

    if(j > 0)
	for (i = 0; i < 3 && filename[j+1+i]; i++)
	    entry->extension[i] = filename[j+1+i];

    /* upcase & remove unwanted characters */
    for(i=10;i>=0;i--) {
	if(i==10 || i==7) for(;i>0 && entry->name[i]==' ';i--);
	if(entry->name[i]<=' ' || entry->name[i]>0x7f
		|| strchr(".*?<>|\":/\\[];,+='",entry->name[i]))
	    entry->name[i]='_';
        else if(entry->name[i]>='a' && entry->name[i]<='z')
            entry->name[i]+='A'-'a';
    }

    /* mangle duplicates */
    while(1) {
	direntry_t* entry1=array_get(&(s->directory),directory_start);
	int j;

	for(;entry1<entry;entry1++)
	    if(!is_long_name(entry1) && !memcmp(entry1->name,entry->name,11))
		break; /* found dupe */
	if(entry1==entry) /* no dupe found */
	    break;

	/* use all 8 characters of name */
	if(entry->name[7]==' ') {
	    int j;
	    for(j=6;j>0 && entry->name[j]==' ';j--)
		entry->name[j]='~';
	}

	/* increment number */
	for(j=7;j>0 && entry->name[j]=='9';j--)
	    entry->name[j]='0';
	if(j>0) {
	    if(entry->name[j]<'0' || entry->name[j]>'9')
	        entry->name[j]='0';
	    else
	        entry->name[j]++;
	}
    }

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

    /* actually read the directory, and allocate the mappings */
    while((entry=readdir(dir))) {
	unsigned int length=strlen(dirname)+2+strlen(entry->d_name);
        char* buffer;
	direntry_t* direntry;
        struct stat st;
	int is_dot=!strcmp(entry->d_name,".");
	int is_dotdot=!strcmp(entry->d_name,"..");

	if(first_cluster == 0 && (is_dotdot || is_dot))
	    continue;

	buffer=(char*)qemu_malloc(length);
	snprintf(buffer,length,"%s/%s",dirname,entry->d_name);

	if(stat(buffer,&st)<0) {
	    free(buffer);
            continue;
	}

	/* create directory entry for this file */
	direntry=create_short_and_long_name(s, i, entry->d_name,
		is_dot || is_dotdot);
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
	    free(buffer);
	    return -2;
        }
	direntry->size=cpu_to_le32(S_ISDIR(st.st_mode)?0:st.st_size);

	/* create mapping for this file */
	if(!is_dot && !is_dotdot && (S_ISDIR(st.st_mode) || st.st_size)) {
	    s->current_mapping=(mapping_t*)array_get_next(&(s->mapping));
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
	}
    }
    closedir(dir);

    /* fill with zeroes up to the end of the cluster */
    while(s->directory.next%(0x10*s->sectors_per_cluster)) {
	direntry_t* direntry=array_get_next(&(s->directory));
	memset(direntry,0,sizeof(direntry_t));
    }

/* TODO: if there are more entries, bootsector has to be adjusted! */
#define ROOT_ENTRIES (0x02 * 0x10 * s->sectors_per_cluster)
    if (mapping_index == 0 && s->directory.next < ROOT_ENTRIES) {
	/* root directory */
	int cur = s->directory.next;
	array_ensure_allocated(&(s->directory), ROOT_ENTRIES - 1);
	memset(array_get(&(s->directory), cur), 0,
		(ROOT_ENTRIES - cur) * sizeof(direntry_t));
    }

     /* reget the mapping, since s->mapping was possibly realloc()ed */
    mapping = (mapping_t*)array_get(&(s->mapping), mapping_index);
    first_cluster += (s->directory.next - mapping->info.dir.first_dir_index)
	* 0x20 / s->cluster_size;
    mapping->end = first_cluster;

    direntry = (direntry_t*)array_get(&(s->directory), mapping->dir_index);
    set_begin_of_direntry(direntry, mapping->begin);

    return 0;
}

static inline uint32_t sector2cluster(BDRVVVFATState* s,off_t sector_num)
{
    return (sector_num-s->faked_sectors)/s->sectors_per_cluster;
}

static inline off_t cluster2sector(BDRVVVFATState* s, uint32_t cluster_num)
{
    return s->faked_sectors + s->sectors_per_cluster * cluster_num;
}

static inline uint32_t sector_offset_in_cluster(BDRVVVFATState* s,off_t sector_num)
{
    return (sector_num-s->first_sectors_number-2*s->sectors_per_fat)%s->sectors_per_cluster;
}

#ifdef DBG
static direntry_t* get_direntry_for_mapping(BDRVVVFATState* s,mapping_t* mapping)
{
    if(mapping->mode==MODE_UNDEFINED)
	return 0;
    return (direntry_t*)(s->directory.pointer+sizeof(direntry_t)*mapping->dir_index);
}
#endif

static int init_directories(BDRVVVFATState* s,
	const char* dirname)
{
    bootsector_t* bootsector;
    mapping_t* mapping;
    unsigned int i;
    unsigned int cluster;

    memset(&(s->first_sectors[0]),0,0x40*0x200);

    s->cluster_size=s->sectors_per_cluster*0x200;
    s->cluster_buffer=qemu_malloc(s->cluster_size);

    /*
     * The formula: sc = spf+1+spf*spc*(512*8/fat_type),
     * where sc is sector_count,
     * spf is sectors_per_fat,
     * spc is sectors_per_clusters, and
     * fat_type = 12, 16 or 32.
     */
    i = 1+s->sectors_per_cluster*0x200*8/s->fat_type;
    s->sectors_per_fat=(s->sector_count+i)/i; /* round up */

    array_init(&(s->mapping),sizeof(mapping_t));
    array_init(&(s->directory),sizeof(direntry_t));

    /* add volume label */
    {
	direntry_t* entry=array_get_next(&(s->directory));
	entry->attributes=0x28; /* archive | volume label */
	snprintf((char*)entry->name,11,"QEMU VVFAT");
    }

    /* Now build FAT, and write back information into directory */
    init_fat(s);

    s->faked_sectors=s->first_sectors_number+s->sectors_per_fat*2;
    s->cluster_count=sector2cluster(s, s->sector_count);

    mapping = array_get_next(&(s->mapping));
    mapping->begin = 0;
    mapping->dir_index = 0;
    mapping->info.dir.parent_mapping_index = -1;
    mapping->first_mapping_index = -1;
    mapping->path = strdup(dirname);
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
	    mapping->begin = cluster;
	    if(read_directory(s, i)) {
		fprintf(stderr, "Could not read directory %s\n",
			mapping->path);
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
	    fprintf(stderr,"Directory does not fit in FAT%d (capacity %s)\n",
		    s->fat_type,
		    s->fat_type == 12 ? s->sector_count == 2880 ? "1.44 MB"
								: "2.88 MB"
				      : "504MB");
	    return -EINVAL;
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
    s->sectors_of_root_directory = mapping->end * s->sectors_per_cluster;
    s->last_cluster_of_root_directory = mapping->end;

    /* the FAT signature */
    fat_set(s,0,s->max_fat_value);
    fat_set(s,1,s->max_fat_value);

    s->current_mapping = NULL;

    bootsector=(bootsector_t*)(s->first_sectors+(s->first_sectors_number-1)*0x200);
    bootsector->jump[0]=0xeb;
    bootsector->jump[1]=0x3e;
    bootsector->jump[2]=0x90;
    memcpy(bootsector->name,"QEMU    ",8);
    bootsector->sector_size=cpu_to_le16(0x200);
    bootsector->sectors_per_cluster=s->sectors_per_cluster;
    bootsector->reserved_sectors=cpu_to_le16(1);
    bootsector->number_of_fats=0x2; /* number of FATs */
    bootsector->root_entries=cpu_to_le16(s->sectors_of_root_directory*0x10);
    bootsector->total_sectors16=s->sector_count>0xffff?0:cpu_to_le16(s->sector_count);
    bootsector->media_type=(s->fat_type!=12?0xf8:s->sector_count==5760?0xf9:0xf8); /* media descriptor */
    s->fat.pointer[0] = bootsector->media_type;
    bootsector->sectors_per_fat=cpu_to_le16(s->sectors_per_fat);
    bootsector->sectors_per_track=cpu_to_le16(s->bs->secs);
    bootsector->number_of_heads=cpu_to_le16(s->bs->heads);
    bootsector->hidden_sectors=cpu_to_le32(s->first_sectors_number==1?0:0x3f);
    bootsector->total_sectors=cpu_to_le32(s->sector_count>0xffff?s->sector_count:0);

    /* LATER TODO: if FAT32, this is wrong */
    bootsector->u.fat16.drive_number=s->fat_type==12?0:0x80; /* assume this is hda (TODO) */
    bootsector->u.fat16.current_head=0;
    bootsector->u.fat16.signature=0x29;
    bootsector->u.fat16.id=cpu_to_le32(0xfabe1afd);

    memcpy(bootsector->u.fat16.volume_label,"QEMU VVFAT ",11);
    memcpy(bootsector->fat_type,(s->fat_type==12?"FAT12   ":s->fat_type==16?"FAT16   ":"FAT32   "),8);
    bootsector->magic[0]=0x55; bootsector->magic[1]=0xaa;

    return 0;
}

#ifdef DEBUG
static BDRVVVFATState *vvv = NULL;
#endif

static int enable_write_target(BDRVVVFATState *s);
static int is_consistent(BDRVVVFATState *s);

static int vvfat_open(BlockDriverState *bs, const char* dirname, int flags)
{
    BDRVVVFATState *s = bs->opaque;
    int floppy = 0;
    int i;

#ifdef DEBUG
    vvv = s;
#endif

DLOG(if (stderr == NULL) {
    stderr = fopen("vvfat.log", "a");
    setbuf(stderr, NULL);
})

    s->bs = bs;

    s->fat_type=16;
    /* LATER TODO: if FAT32, adjust */
    s->sectors_per_cluster=0x10;
    /* 504MB disk*/
    bs->cyls=1024; bs->heads=16; bs->secs=63;

    s->current_cluster=0xffffffff;

    s->first_sectors_number=0x40;
    /* read only is the default for safety */
    bs->read_only = 1;
    s->qcow = s->write_target = NULL;
    s->qcow_filename = NULL;
    s->fat2 = NULL;
    s->downcase_short_names = 1;

    if (!strstart(dirname, "fat:", NULL))
	return -1;

    if (strstr(dirname, ":floppy:")) {
	floppy = 1;
	s->fat_type = 12;
	s->first_sectors_number = 1;
	s->sectors_per_cluster=2;
	bs->cyls = 80; bs->heads = 2; bs->secs = 36;
    }

    s->sector_count=bs->cyls*bs->heads*bs->secs;

    if (strstr(dirname, ":32:")) {
	fprintf(stderr, "Big fat greek warning: FAT32 has not been tested. You are welcome to do so!\n");
	s->fat_type = 32;
    } else if (strstr(dirname, ":16:")) {
	s->fat_type = 16;
    } else if (strstr(dirname, ":12:")) {
	s->fat_type = 12;
	s->sector_count=2880;
    }

    if (strstr(dirname, ":rw:")) {
	if (enable_write_target(s))
	    return -1;
	bs->read_only = 0;
    }

    i = strrchr(dirname, ':') - dirname;
    assert(i >= 3);
    if (dirname[i-2] == ':' && qemu_isalpha(dirname[i-1]))
	/* workaround for DOS drive names */
	dirname += i-1;
    else
	dirname += i+1;

    bs->total_sectors=bs->cyls*bs->heads*bs->secs;

    if(init_directories(s, dirname))
	return -1;

    s->sector_count = s->faked_sectors + s->sectors_per_cluster*s->cluster_count;

    if(s->first_sectors_number==0x40)
	init_mbr(s);

    /* for some reason or other, MS-DOS does not like to know about CHS... */
    if (floppy)
	bs->heads = bs->cyls = bs->secs = 0;

    //    assert(is_consistent(s));
    return 0;
}

static inline void vvfat_close_current_file(BDRVVVFATState *s)
{
    if(s->current_mapping) {
	s->current_mapping = NULL;
	if (s->current_fd) {
		close(s->current_fd);
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
    int index3=index1+1;
    while(1) {
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

/*
 * This function simply compares path == mapping->path. Since the mappings
 * are sorted by cluster, this is expensive: O(n).
 */
static inline mapping_t* find_mapping_for_path(BDRVVVFATState* s,
	const char* path)
{
    int i;

    for (i = 0; i < s->mapping.next; i++) {
	mapping_t* mapping = array_get(&(s->mapping), i);
	if (mapping->first_mapping_index < 0 &&
		!strcmp(path, mapping->path))
	    return mapping;
    }

    return NULL;
}

static int open_file(BDRVVVFATState* s,mapping_t* mapping)
{
    if(!mapping)
	return -1;
    if(!s->current_mapping ||
	    strcmp(s->current_mapping->path,mapping->path)) {
	/* open file */
	int fd = open(mapping->path, O_RDONLY | O_BINARY | O_LARGEFILE);
	if(fd<0)
	    return -1;
	vvfat_close_current_file(s);
	s->current_fd = fd;
	s->current_mapping = mapping;
    }
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

	offset=s->cluster_size*(cluster_num-s->current_mapping->begin)+s->current_mapping->info.file.offset;
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
static void hexdump(const void* address, uint32_t len)
{
    const unsigned char* p = address;
    int i, j;

    for (i = 0; i < len; i += 16) {
	for (j = 0; j < 16 && i + j < len; j++)
	    fprintf(stderr, "%02x ", p[i + j]);
	for (; j < 16; j++)
	    fprintf(stderr, "   ");
	fprintf(stderr, " ");
	for (j = 0; j < 16 && i + j < len; j++)
	    fprintf(stderr, "%c", (p[i + j] < ' ' || p[i + j] > 0x7f) ? '.' : p[i + j]);
	fprintf(stderr, "\n");
    }
}

static void print_direntry(const direntry_t* direntry)
{
    int j = 0;
    char buffer[1024];

    fprintf(stderr, "direntry 0x%x: ", (int)direntry);
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
	fprintf(stderr,"%s attributes=0x%02x begin=%d size=%d\n",
		buffer,
		direntry->attributes,
		begin_of_direntry(direntry),le32_to_cpu(direntry->size));
    }
}

static void print_mapping(const mapping_t* mapping)
{
    fprintf(stderr, "mapping (0x%x): begin, end = %d, %d, dir_index = %d, first_mapping_index = %d, name = %s, mode = 0x%x, " , (int)mapping, mapping->begin, mapping->end, mapping->dir_index, mapping->first_mapping_index, mapping->path, mapping->mode);
    if (mapping->mode & MODE_DIRECTORY)
	fprintf(stderr, "parent_mapping_index = %d, first_dir_index = %d\n", mapping->info.dir.parent_mapping_index, mapping->info.dir.first_dir_index);
    else
	fprintf(stderr, "offset = %d\n", mapping->info.file.offset);
}
#endif

static int vvfat_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BDRVVVFATState *s = bs->opaque;
    int i;

    for(i=0;i<nb_sectors;i++,sector_num++) {
	if (sector_num >= s->sector_count)
	   return -1;
	if (s->qcow) {
	    int n;
	    if (s->qcow->drv->bdrv_is_allocated(s->qcow,
			sector_num, nb_sectors-i, &n)) {
DLOG(fprintf(stderr, "sectors %d+%d allocated\n", (int)sector_num, n));
		if (s->qcow->drv->bdrv_read(s->qcow, sector_num, buf+i*0x200, n))
		    return -1;
		i += n - 1;
		sector_num += n - 1;
		continue;
	    }
DLOG(fprintf(stderr, "sector %d not allocated\n", (int)sector_num));
	}
	if(sector_num<s->faked_sectors) {
	    if(sector_num<s->first_sectors_number)
		memcpy(buf+i*0x200,&(s->first_sectors[sector_num*0x200]),0x200);
	    else if(sector_num-s->first_sectors_number<s->sectors_per_fat)
		memcpy(buf+i*0x200,&(s->fat.pointer[(sector_num-s->first_sectors_number)*0x200]),0x200);
	    else if(sector_num-s->first_sectors_number-s->sectors_per_fat<s->sectors_per_fat)
		memcpy(buf+i*0x200,&(s->fat.pointer[(sector_num-s->first_sectors_number-s->sectors_per_fat)*0x200]),0x200);
	} else {
	    uint32_t sector=sector_num-s->faked_sectors,
	    sector_offset_in_cluster=(sector%s->sectors_per_cluster),
	    cluster_num=sector/s->sectors_per_cluster;
	    if(read_cluster(s, cluster_num) != 0) {
		/* LATER TODO: strict: return -1; */
		memset(buf+i*0x200,0,0x200);
		continue;
	    }
	    memcpy(buf+i*0x200,s->cluster+sector_offset_in_cluster*0x200,0x200);
	}
    }
    return 0;
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
DLOG(fprintf(stderr, "clear_commits (%d commits)\n", s->commits.next));
    for (i = 0; i < s->commits.next; i++) {
	commit_t* commit = array_get(&(s->commits), i);
	assert(commit->path || commit->action == ACTION_WRITEOUT);
	if (commit->action != ACTION_WRITEOUT) {
	    assert(commit->path);
	    free(commit->path);
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
	lfn->sequence_number = pointer[0] & 0x3f;
	lfn->checksum = pointer[13];
	lfn->name[0] = 0;
	lfn->name[lfn->sequence_number * 13] = 0;
    } else if ((pointer[0] & 0x3f) != --lfn->sequence_number)
	return -1;
    else if (pointer[13] != lfn->checksum)
	return -2;
    else if (pointer[12] || pointer[26] || pointer[27])
	return -3;

    offset = 13 * (lfn->sequence_number - 1);
    for (i = 0, j = 1; i < 13; i++, j+=2) {
	if (j == 11)
	    j = 14;
	else if (j == 26)
	    j = 28;

	if (pointer[j+1] == 0)
	    lfn->name[offset + i] = pointer[j];
	else if (pointer[j+1] != 0xff || (pointer[0] & 0x40) == 0)
	    return -4;
	else
	    lfn->name[offset + i] = 0;
    }

    if (pointer[0] & 0x40)
	lfn->len = offset + strlen((char*)lfn->name + offset);

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
	if (direntry->name[i] <= ' ' || direntry->name[i] > 0x7f)
	    return -1;
	else if (s->downcase_short_names)
	    lfn->name[i] = qemu_tolower(direntry->name[i]);
	else
	    lfn->name[i] = direntry->name[i];
    }

    for (j = 2; j >= 0 && direntry->extension[j] == ' '; j--);
    if (j >= 0) {
	lfn->name[i++] = '.';
	lfn->name[i + j + 1] = '\0';
	for (;j >= 0; j--) {
	    if (direntry->extension[j] <= ' ' || direntry->extension[j] > 0x7f)
		return -2;
	    else if (s->downcase_short_names)
		lfn->name[i + j] = qemu_tolower(direntry->extension[j]);
	    else
		lfn->name[i + j] = direntry->extension[j];
	}
    } else
	lfn->name[i + j + 1] = '\0';

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

static inline int cluster_was_modified(BDRVVVFATState* s, uint32_t cluster_num)
{
    int was_modified = 0;
    int i, dummy;

    if (s->qcow == NULL)
	return 0;

    for (i = 0; !was_modified && i < s->sectors_per_cluster; i++)
	was_modified = s->qcow->drv->bdrv_is_allocated(s->qcow,
		cluster2sector(s, cluster_num) + i, 1, &dummy);

    return was_modified;
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
static uint32_t get_cluster_count_for_direntry(BDRVVVFATState* s,
	direntry_t* direntry, const char* path)
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
    int first_mapping_index = -1;
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
		schedule_rename(s, cluster_num, strdup(path));
	} else if (is_file(direntry))
	    /* new file */
	    schedule_new_file(s, strdup(path), cluster_num);
	else {
	    assert(0);
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
		    if (offset != mapping->info.file.offset + s->cluster_size
			    * (cluster_num - mapping->begin)) {
			/* offset of this cluster in file chain has changed */
			assert(0);
			copy_it = 1;
		    } else if (offset == 0) {
			const char* basename = get_basename(mapping->path);

			if (strcmp(basename, basename2))
			    copy_it = 1;
			first_mapping_index = array_index(&(s->mapping), mapping);
		    }

		    if (mapping->first_mapping_index != first_mapping_index
			    && mapping->info.file.offset > 0) {
			assert(0);
			copy_it = 1;
		    }

		    /* need to write out? */
		    if (!was_modified && is_file(direntry)) {
			was_modified = 1;
			schedule_writeout(s, mapping->dir_index, offset);
		    }
		}
	    }

	    if (copy_it) {
		int i, dummy;
		/*
		 * This is horribly inefficient, but that is okay, since
		 * it is rarely executed, if at all.
		 */
		int64_t offset = cluster2sector(s, cluster_num);

		vvfat_close_current_file(s);
		for (i = 0; i < s->sectors_per_cluster; i++)
		    if (!s->qcow->drv->bdrv_is_allocated(s->qcow,
				offset + i, 1, &dummy)) {
			if (vvfat_read(s->bs,
				    offset, s->cluster_buffer, 1))
			    return -1;
			if (s->qcow->drv->bdrv_write(s->qcow,
				    offset, s->cluster_buffer, 1))
			    return -2;
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
static int check_directory_consistency(BDRVVVFATState *s,
	int cluster_num, const char* path)
{
    int ret = 0;
    unsigned char* cluster = qemu_malloc(s->cluster_size);
    direntry_t* direntries = (direntry_t*)cluster;
    mapping_t* mapping = find_mapping_for_cluster(s, cluster_num);

    long_file_name lfn;
    int path_len = strlen(path);
    char path2[PATH_MAX];

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
	    schedule_rename(s, cluster_num, strdup(path));
    } else
	/* new directory */
	schedule_mkdir(s, cluster_num, strdup(path));

    lfn_init(&lfn);
    do {
	int i;
	int subret = 0;

	ret++;

	if (s->used_clusters[cluster_num] & USED_ANY) {
	    fprintf(stderr, "cluster %d used more than once\n", (int)cluster_num);
	    return 0;
	}
	s->used_clusters[cluster_num] = USED_DIRECTORY;

DLOG(fprintf(stderr, "read cluster %d (sector %d)\n", (int)cluster_num, (int)cluster2sector(s, cluster_num)));
	subret = vvfat_read(s->bs, cluster2sector(s, cluster_num), cluster,
		s->sectors_per_cluster);
	if (subret) {
	    fprintf(stderr, "Error fetching direntries\n");
	fail:
	    free(cluster);
	    return 0;
	}

	for (i = 0; i < 0x10 * s->sectors_per_cluster; i++) {
	    int cluster_count = 0;

DLOG(fprintf(stderr, "check direntry %d: \n", i); print_direntry(direntries + i));
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
			(le32_to_cpu(direntries[i].size) + s->cluster_size
			 - 1) / s->cluster_size) {
		    DLOG(fprintf(stderr, "Cluster count mismatch\n"));
		    goto fail;
		}
	    } else
		assert(0); /* cluster_count = 0; */

	    ret += cluster_count;
	}

	cluster_num = modified_fat_get(s, cluster_num);
    } while(!fat_eof(s, cluster_num));

    free(cluster);
    return ret;
}

/* returns 1 on success */
static int is_consistent(BDRVVVFATState* s)
{
    int i, check;
    int used_clusters_count = 0;

DLOG(checkpoint());
    /*
     * - get modified FAT
     * - compare the two FATs (TODO)
     * - get buffer for marking used clusters
     * - recurse direntries from root (using bs->bdrv_read to make
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
	s->fat2 = qemu_malloc(size);
	memcpy(s->fat2, s->fat.pointer, size);
    }
    check = vvfat_read(s->bs,
	    s->first_sectors_number, s->fat2, s->sectors_per_fat);
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
    if (mapping->first_mapping_index < 0)
	free(mapping->path);

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
			mapping->end - mapping->begin;

	    mapping = next_mapping;
	}

	cluster = c1;
    }

    return 0;
}

static int commit_direntries(BDRVVVFATState* s,
	int dir_index, int parent_mapping_index)
{
    direntry_t* direntry = array_get(&(s->directory), dir_index);
    uint32_t first_cluster = dir_index == 0 ? 0 : begin_of_direntry(direntry);
    mapping_t* mapping = find_mapping_for_cluster(s, first_cluster);

    int factor = 0x10 * s->sectors_per_cluster;
    int old_cluster_count, new_cluster_count;
    int current_dir_index = mapping->info.dir.first_dir_index;
    int first_dir_index = current_dir_index;
    int ret, i;
    uint32_t c;

DLOG(fprintf(stderr, "commit_direntries for %s, parent_mapping_index %d\n", mapping->path, parent_mapping_index));

    assert(direntry);
    assert(mapping);
    assert(mapping->begin == first_cluster);
    assert(mapping->info.dir.first_dir_index < s->directory.next);
    assert(mapping->mode & MODE_DIRECTORY);
    assert(dir_index == 0 || is_directory(direntry));

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
	void* direntry = array_get(&(s->directory), current_dir_index);
	int ret = vvfat_read(s->bs, cluster2sector(s, c), direntry,
		s->sectors_per_cluster);
	if (ret)
	    return ret;
	assert(!strncmp(s->directory.pointer, "QEMU", 4));
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
static int commit_one_file(BDRVVVFATState* s,
	int dir_index, uint32_t offset)
{
    direntry_t* direntry = array_get(&(s->directory), dir_index);
    uint32_t c = begin_of_direntry(direntry);
    uint32_t first_cluster = c;
    mapping_t* mapping = find_mapping_for_cluster(s, c);
    uint32_t size = filesize_of_direntry(direntry);
    char* cluster = qemu_malloc(s->cluster_size);
    uint32_t i;
    int fd = 0;

    assert(offset < size);
    assert((offset % s->cluster_size) == 0);

    for (i = s->cluster_size; i < offset; i += s->cluster_size)
	c = modified_fat_get(s, c);

    fd = open(mapping->path, O_RDWR | O_CREAT | O_BINARY, 0666);
    if (fd < 0) {
	fprintf(stderr, "Could not open %s... (%s, %d)\n", mapping->path,
		strerror(errno), errno);
	return fd;
    }
    if (offset > 0)
	if (lseek(fd, offset, SEEK_SET) != offset)
	    return -3;

    while (offset < size) {
	uint32_t c1;
	int rest_size = (size - offset > s->cluster_size ?
		s->cluster_size : size - offset);
	int ret;

	c1 = modified_fat_get(s, c);

	assert((size - offset == 0 && fat_eof(s, c)) ||
		(size > offset && c >=2 && !fat_eof(s, c)));

	ret = vvfat_read(s->bs, cluster2sector(s, c),
	    (uint8_t*)cluster, (rest_size + 0x1ff) / 0x200);

	if (ret < 0)
	    return ret;

	if (write(fd, cluster, rest_size) < 0)
	    return -2;

	offset += rest_size;
	c = c1;
    }

    ftruncate(fd, size);
    close(fd);

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
	assert(mapping->dir_index >= 0);
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
	fprintf(stderr, "%d, %s (%d, %d)\n", i, commit->path ? commit->path : "(null)", commit->param.rename.cluster, commit->action);
    }
#endif

    for (i = 0; i < s->commits.next;) {
	commit_t* commit = array_get(&(s->commits), i);
	if (commit->action == ACTION_RENAME) {
	    mapping_t* mapping = find_mapping_for_cluster(s,
		    commit->param.rename.cluster);
	    char* old_path = mapping->path;

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
		int i = 0;

		/* recurse */
		while (!fat_eof(s, c)) {
		    do {
			direntry_t* d = direntry + i;

			if (is_file(d) || (is_directory(d) && !is_dot(d))) {
			    mapping_t* m = find_mapping_for_cluster(s,
				    begin_of_direntry(d));
			    int l = strlen(m->path);
			    char* new_path = qemu_malloc(l + diff + 1);

			    assert(!strncmp(m->path, mapping->path, l2));

                            pstrcpy(new_path, l + diff + 1, mapping->path);
                            pstrcpy(new_path + l1, l + diff + 1 - l1,
                                    m->path + l2);

			    schedule_rename(s, m->begin, new_path);
			}
			i++;
		    } while((i % (0x10 * s->sectors_per_cluster)) != 0);
		    c = fat_get(s, c);
		}
	    }

	    free(old_path);
	    array_remove(&(s->commits), i);
	    continue;
	} else if (commit->action == ACTION_MKDIR) {
	    mapping_t* mapping;
	    int j, parent_path_len;

#ifdef __MINGW32__
            if (mkdir(commit->path))
                return -5;
#else
            if (mkdir(commit->path, 0755))
                return -5;
#endif

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
static int handle_commits(BDRVVVFATState* s)
{
    int i, fail = 0;

    vvfat_close_current_file(s);

    for (i = 0; !fail && i < s->commits.next; i++) {
	commit_t* commit = array_get(&(s->commits), i);
	switch(commit->action) {
	case ACTION_RENAME: case ACTION_MKDIR:
	    assert(0);
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
	    int i;

	    /* find direntry */
	    for (i = 0; i < s->directory.next; i++) {
		entry = array_get(&(s->directory), i);
		if (is_file(entry) && begin_of_direntry(entry) == begin)
		    break;
	    }

	    if (i >= s->directory.next) {
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

	    if (commit_one_file(s, i, 0))
		fail = -7;

	    break;
	}
	default:
	    assert(0);
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
 * - copy FAT (with bdrv_read)
 * - mark all filenames corresponding to mappings as deleted
 * - recurse direntries from root (using bs->bdrv_read)
 * - delete files corresponding to mappings marked as deleted
 */
static int do_commit(BDRVVVFATState* s)
{
    int ret = 0;

    /* the real meat are the commits. Nothing to do? Move along! */
    if (s->commits.next == 0)
	return 0;

    vvfat_close_current_file(s);

    ret = handle_renames_and_mkdirs(s);
    if (ret) {
	fprintf(stderr, "Error handling renames (%d)\n", ret);
	assert(0);
	return ret;
    }

    /* copy FAT (with bdrv_read) */
    memcpy(s->fat.pointer, s->fat2, 0x200 * s->sectors_per_fat);

    /* recurse direntries from root (using bs->bdrv_read) */
    ret = commit_direntries(s, 0, -1);
    if (ret) {
	fprintf(stderr, "Fatal: error while committing (%d)\n", ret);
	assert(0);
	return ret;
    }

    ret = handle_commits(s);
    if (ret) {
	fprintf(stderr, "Error handling commits (%d)\n", ret);
	assert(0);
	return ret;
    }

    ret = handle_deletes(s);
    if (ret) {
	fprintf(stderr, "Error deleting\n");
        assert(0);
	return ret;
    }

    s->qcow->drv->bdrv_make_empty(s->qcow);

    memset(s->used_clusters, 0, sector2cluster(s, s->sector_count));

DLOG(checkpoint());
    return 0;
}

static int try_commit(BDRVVVFATState* s)
{
    vvfat_close_current_file(s);
DLOG(checkpoint());
    if(!is_consistent(s))
	return -1;
    return do_commit(s);
}

static int vvfat_write(BlockDriverState *bs, int64_t sector_num,
                    const uint8_t *buf, int nb_sectors)
{
    BDRVVVFATState *s = bs->opaque;
    int i, ret;

DLOG(checkpoint());

    vvfat_close_current_file(s);

    /*
     * Some sanity checks:
     * - do not allow writing to the boot sector
     * - do not allow to write non-ASCII filenames
     */

    if (sector_num < s->first_sectors_number)
	return -1;

    for (i = sector2cluster(s, sector_num);
	    i <= sector2cluster(s, sector_num + nb_sectors - 1);) {
	mapping_t* mapping = find_mapping_for_cluster(s, i);
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
		    /* do not allow non-ASCII filenames */
		    if (parse_long_name(&lfn, direntries + k) < 0) {
			fprintf(stderr, "Warning: non-ASCII filename\n");
			return -1;
		    }
		    /* no access to the direntry of a read-only file */
		    else if (is_short_name(direntries+k) &&
			    (direntries[k].attributes & 1)) {
			if (memcmp(direntries + k,
				    array_get(&(s->directory), dir_index + k),
				    sizeof(direntry_t))) {
			    fprintf(stderr, "Warning: tried to write to write-protected file\n");
			    return -1;
			}
		    }
		}
	    }
	    i = mapping->end;
	} else
	    i++;
    }

    /*
     * Use qcow backend. Commit later.
     */
DLOG(fprintf(stderr, "Write to qcow backend: %d + %d\n", (int)sector_num, nb_sectors));
    ret = s->qcow->drv->bdrv_write(s->qcow, sector_num, buf, nb_sectors);
    if (ret < 0) {
	fprintf(stderr, "Error writing to qcow backend\n");
	return ret;
    }

    for (i = sector2cluster(s, sector_num);
	    i <= sector2cluster(s, sector_num + nb_sectors - 1); i++)
	if (i >= 0)
	    s->used_clusters[i] |= USED_ALLOCATED;

DLOG(checkpoint());
    /* TODO: add timeout */
    try_commit(s);

DLOG(checkpoint());
    return 0;
}

static int vvfat_is_allocated(BlockDriverState *bs,
	int64_t sector_num, int nb_sectors, int* n)
{
    BDRVVVFATState* s = bs->opaque;
    *n = s->sector_count - sector_num;
    if (*n > nb_sectors)
	*n = nb_sectors;
    else if (*n < 0)
	return 0;
    return 1;
}

static int write_target_commit(BlockDriverState *bs, int64_t sector_num,
	const uint8_t* buffer, int nb_sectors) {
    BDRVVVFATState* s = bs->opaque;
    return try_commit(s);
}

static void write_target_close(BlockDriverState *bs) {
    BDRVVVFATState* s = bs->opaque;
    bdrv_delete(s->qcow);
    free(s->qcow_filename);
}

static BlockDriver vvfat_write_target = {
    "vvfat_write_target", 0, NULL, NULL, NULL,
    write_target_commit,
    write_target_close,
    NULL, NULL, NULL
};

static int enable_write_target(BDRVVVFATState *s)
{
    BlockDriver *bdrv_qcow;
    QEMUOptionParameter *options;
    int size = sector2cluster(s, s->sector_count);
    s->used_clusters = calloc(size, 1);

    array_init(&(s->commits), sizeof(commit_t));

    s->qcow_filename = qemu_malloc(1024);
    get_tmp_filename(s->qcow_filename, 1024);

    bdrv_qcow = bdrv_find_format("qcow");
    options = parse_option_parameters("", bdrv_qcow->create_options, NULL);
    set_option_parameter_int(options, BLOCK_OPT_SIZE, s->sector_count * 512);
    set_option_parameter(options, BLOCK_OPT_BACKING_FILE, "fat:");

    if (bdrv_create(bdrv_qcow, s->qcow_filename, options) < 0)
	return -1;
    s->qcow = bdrv_new("");
    if (s->qcow == NULL || bdrv_open(s->qcow, s->qcow_filename, 0) < 0)
	return -1;

#ifndef _WIN32
    unlink(s->qcow_filename);
#endif

    s->bs->backing_hd = calloc(sizeof(BlockDriverState), 1);
    s->bs->backing_hd->drv = &vvfat_write_target;
    s->bs->backing_hd->opaque = s;

    return 0;
}

static void vvfat_close(BlockDriverState *bs)
{
    BDRVVVFATState *s = bs->opaque;

    vvfat_close_current_file(s);
    array_free(&(s->fat));
    array_free(&(s->directory));
    array_free(&(s->mapping));
    if(s->cluster_buffer)
        free(s->cluster_buffer);
}

static BlockDriver bdrv_vvfat = {
    .format_name	= "vvfat",
    .instance_size	= sizeof(BDRVVVFATState),
    .bdrv_open		= vvfat_open,
    .bdrv_read		= vvfat_read,
    .bdrv_write		= vvfat_write,
    .bdrv_close		= vvfat_close,
    .bdrv_is_allocated	= vvfat_is_allocated,
    .protocol_name	= "fat",
};

static void bdrv_vvfat_init(void)
{
    bdrv_register(&bdrv_vvfat);
}

block_init(bdrv_vvfat_init);

#ifdef DEBUG
static void checkpoint(void) {
    assert(((mapping_t*)array_get(&(vvv->mapping), 0))->end == 2);
    check1(vvv);
    check2(vvv);
    assert(!vvv->current_mapping || vvv->current_fd || (vvv->current_mapping->mode & MODE_DIRECTORY));
#if 0
    if (((direntry_t*)vvv->directory.pointer)[1].attributes != 0xf)
	fprintf(stderr, "Nonono!\n");
    mapping_t* mapping;
    direntry_t* direntry;
    assert(vvv->mapping.size >= vvv->mapping.item_size * vvv->mapping.next);
    assert(vvv->directory.size >= vvv->directory.item_size * vvv->directory.next);
    if (vvv->mapping.next<47)
	return;
    assert((mapping = array_get(&(vvv->mapping), 47)));
    assert(mapping->dir_index < vvv->directory.next);
    direntry = array_get(&(vvv->directory), mapping->dir_index);
    assert(!memcmp(direntry->name, "USB     H  ", 11) || direntry->name[0]==0);
#endif
    return;
    /* avoid compiler warnings: */
    hexdump(NULL, 100);
    remove_mapping(vvv, NULL);
    print_mapping(NULL);
    print_direntry(NULL);
}
#endif
