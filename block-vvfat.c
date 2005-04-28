/*
 * QEMU Block driver for virtual VFAT (shadows a local directory)
 * 
 * Copyright (c) 2004 Johannes E. Schindelin
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
#include <assert.h>
#include "vl.h"
#include "block_int.h"

// TODO: new file
// TODO: delete file
// TODO: make root directory larger
// TODO: make directory clusters connected, so they are reserved anyway... add a member which tells how many clusters are reserved after a directory
// TODO: introduce another member in mapping_t which says where the directory resides in s->directory (for mkdir and rmdir) 
// in _read and _write, before treating direntries or file contents, get_mapping to know what it is.
// TODO: mkdir
// TODO: rmdir 

// TODO: when commit_data'ing a direntry and is_consistent, commit_remove
// TODO: reset MODE_MODIFIED when commit_remove'ing

#define DEBUG

/* dynamic array functions */
typedef struct array_t {
    char* pointer;
    unsigned int size,next,item_size;
} array_t;

static inline void array_init(array_t* array,unsigned int item_size)
{
    array->pointer=0;
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

/* make sure that memory is reserved at pointer[index*item_size] */
static inline void* array_get(array_t* array,unsigned int index) {
    if((index+1)*array->item_size>array->size) {
	int new_size=(index+32)*array->item_size;
	array->pointer=realloc(array->pointer,new_size);
	if(!array->pointer)
	    return 0;
	array->size=new_size;
	array->next=index+1;
    }
    return array->pointer+index*array->item_size;
}

static inline void* array_get_next(array_t* array) {
    unsigned int next=array->next;
    void* result=array_get(array,next);
    array->next=next+1;
    return result;
}

static inline void* array_insert(array_t* array,unsigned int index,unsigned int count) {
    if((array->next+count)*array->item_size>array->size) {
	int increment=count*array->item_size;
	array->pointer=realloc(array->pointer,array->size+increment);
	if(!array->pointer)
	    return 0;
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
    buf=malloc(is*count);
    memcpy(buf,from,is*count);

    if(index_to<index_from)
	memmove(to+is*count,to,from-to);
    else
	memmove(from,from+is*count,to-from);
    
    memcpy(to,buf,is*count);

    free(buf);

    return 0;
}

int array_remove(array_t* array,int index)
{
    if(array_roll(array,array->next-1,index,1))
	return -1;
    array->next--;
    return 0;
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
    uint16_t zero;
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

typedef struct partition_t {
    uint8_t attributes; /* 0x80 = bootable */
    uint8_t start_head;
    uint8_t start_sector;
    uint8_t start_cylinder;
    uint8_t fs_type; /* 0x6 = FAT16, 0xb = FAT32 */
    uint8_t end_head;
    uint8_t end_sector;
    uint8_t end_cylinder;
    uint32_t start_sector_long;
    uint32_t end_sector_long;
} __attribute__((packed)) partition_t;

typedef struct mbr_t {
    uint8_t ignored[0x1be];
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
    /* begin is the first cluster, end is the last+1,
     * offset is the offset in the file in clusters of this slice */
    off_t begin,end,offset;
    char* filename;

    /* as s->directory is growable, no pointer may be used here */
    unsigned int dir_index;
    enum { MODE_NORMAL,MODE_UNDEFINED,MODE_MODIFIED,MODE_DELETED,MODE_DIRECTORY } mode;
} mapping_t;

/* this structure is used to hold sectors which need to be written, but it's
 * not known yet where to write them. */

typedef struct commit_t {
    uint32_t cluster_num;
    uint8_t* buf;
} commit_t;

/* write support exists for fat, direntry and file contents */
typedef enum {
    WRITE_UNDEFINED,WRITE_FAT,WRITE_DIRENTRY,WRITE_DATA
} write_action_t;

/* here begins the real VVFAT driver */

typedef struct BDRVVVFATState {
    unsigned int first_sectors_number; /* 1 for a single partition, 0x40 for a disk with partition table */
    unsigned char first_sectors[0x40*0x200];
    
    int fat_type; /* 16 or 32 */
    array_t fat,directory,mapping;
   
    unsigned int cluster_size;
    unsigned int sectors_per_cluster;
    unsigned int sectors_per_fat;
    unsigned int sectors_of_root_directory;
    unsigned int sectors_for_directory;
    unsigned int faked_sectors; /* how many sectors are faked before file data */
    uint32_t sector_count; /* total number of sectors of the partition */
    uint32_t cluster_count; /* total number of clusters of this partition */
    unsigned int first_file_mapping; /* index of the first mapping which is not a directory, but a file */
    uint32_t max_fat_value;
   
    int current_fd;
    char current_fd_is_writable; /* =0 if read only, !=0 if read/writable */
    mapping_t* current_mapping;
    unsigned char* cluster;
    unsigned int current_cluster;

    /* write support */
    array_t commit;
    /* for each file, the file contents, the direntry, and the fat entries are
     * written, but not necessarily in that order */
    write_action_t action[3];
} BDRVVVFATState;


static int vvfat_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    if (strstart(filename, "fat:", NULL) ||
        strstart(filename, "fatrw:", NULL))
	return 100;
    return 0;
}

static void init_mbr(BDRVVVFATState* s)
{
    /* TODO: if the files mbr.img and bootsect.img exist, use them */
    mbr_t* real_mbr=(mbr_t*)s->first_sectors;
    partition_t* partition=&(real_mbr->partition[0]);

    memset(s->first_sectors,0,512);
   
    partition->attributes=0x80; /* bootable */
    partition->start_head=1;
    partition->start_sector=1;
    partition->start_cylinder=0;
    partition->fs_type=(s->fat_type==16?0x6:0xb); /* FAT16/FAT32 */
    partition->end_head=0xf;
    partition->end_sector=0xff; /* end sector & upper 2 bits of cylinder */;
    partition->end_cylinder=0xff; /* lower 8 bits of end cylinder */;
    partition->start_sector_long=cpu_to_le32(0x3f);
    partition->end_sector_long=cpu_to_le32(s->sector_count);

    real_mbr->magic[0]=0x55; real_mbr->magic[1]=0xaa;
}

/* dest is assumed to hold 258 bytes, and pads with 0xffff up to next multiple of 26 */
static inline int short2long_name(unsigned char* dest,const char* src)
{
    int i;
    for(i=0;i<129 && src[i];i++) {
        dest[2*i]=src[i];
	dest[2*i+1]=0;
    }
    dest[2*i]=dest[2*i+1]=0;
    for(i=2*i+2;(i%26);i++)
	dest[i]=0xff;
    return i;
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
    for(i=0;i<length;i++) {
	int offset=(i%26);
	if(offset<10) offset=1+offset;
	else if(offset<22) offset=14+offset-10;
	else offset=28+offset-22;
	entry=array_get(&(s->directory),s->directory.next-1-(i/26));
	entry->name[offset]=buffer[i];
    }
    return array_get(&(s->directory),s->directory.next-number_of_entries);
}

/* fat functions */

static inline uint8_t fat_chksum(direntry_t* entry)
{
    uint8_t chksum=0;
    int i;

    for(i=0;i<11;i++)
	chksum=(((chksum&0xfe)>>1)|((chksum&0x01)?0x80:0))
	    +(unsigned char)entry->name[i];
    
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
    if(s->fat_type==12) {
	assert(0); /* TODO */
    } else if(s->fat_type==16) {
	uint16_t* entry=array_get(&(s->fat),cluster);
	*entry=cpu_to_le16(value&0xffff);
    } else {
	uint32_t* entry=array_get(&(s->fat),cluster);
	*entry=cpu_to_le32(value);
    }
}

static inline uint32_t fat_get(BDRVVVFATState* s,unsigned int cluster)
{
    //fprintf(stderr,"want to get fat for cluster %d\n",cluster);
    if(s->fat_type==12) {
	const uint8_t* x=s->fat.pointer+cluster*3/2;
	return ((x[0]|(x[1]<<8))>>(cluster&1?4:0))&0x0fff;
    } else if(s->fat_type==16) {
	uint16_t* entry=array_get(&(s->fat),cluster);
	return le16_to_cpu(*entry);
    } else {
	uint32_t* entry=array_get(&(s->fat),cluster);
	return le32_to_cpu(*entry);
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
    int i;
    
    array_init(&(s->fat),(s->fat_type==32?4:2));
    array_get(&(s->fat),s->sectors_per_fat*0x200/s->fat.item_size-1);
    memset(s->fat.pointer,0,s->fat.size);
    fat_set(s,0,0x7ffffff8);
    
    for(i=1;i<s->sectors_for_directory/s->sectors_per_cluster-1;i++)
	fat_set(s,i,i+1);
    fat_set(s,i,0x7fffffff);

    switch(s->fat_type) {
	case 12: s->max_fat_value=0xfff; break;
	case 16: s->max_fat_value=0xffff; break;
	case 32: s->max_fat_value=0xfffffff; break;
	default: s->max_fat_value=0; /* error... */
    }

}

static inline int long2unix_name(unsigned char* dest,int dest_size,direntry_t* direntry_short) {
    int i=-1,j;
    int chksum=fat_chksum(direntry_short);
    while(1) {
	char* buf=(char*)(direntry_short+i);
	if((buf[0]&0x3f)!=-i || direntry_short[i].reserved[1]!=chksum ||
		direntry_short[i].attributes!=0xf) {
	    if(i<-1)
		return -3;
	    /* take short name */
	    for(j=7;j>0 && direntry_short->name[j]==' ';j--);
	    if(j+1>dest_size)
		return -1;
	    strncpy(dest,direntry_short->name,j+1);
	    dest+=j+1; dest_size-=j+1;
	    for(j=2;j>=0 && direntry_short->extension[j]==' ';j--);
	    if(j>=0) {
		if(j+2>dest_size)
		    return -1;
		dest[0]='.';
	        strncpy(dest+1,direntry_short->extension,j+1);
	    }
	    return 0;
	}
	for(j=0;j<13;j++) {
	    dest_size--;
	    if(dest_size<0)
		return -2;
	    dest[0]=buf[2*j+((j<5)?1:(j<11)?4:6)];
	    if(dest[0]==0 && (buf[0]&0x40)!=0)
		return 0;
	    dest++;
	}
	/* last entry, but no trailing \0? */
	if(buf[0]&0x40)
	    return -3;
	i--;
    }
}

static inline direntry_t* create_short_filename(BDRVVVFATState* s,unsigned int directory_start,const char* filename,int is_dot)
{
    int i,long_index=s->directory.next;
    direntry_t* entry=0;
    direntry_t* entry_long=0;

    if(is_dot) {
	entry=array_get_next(&(s->directory));
	memset(entry->name,0x20,11);
	memcpy(entry->name,filename,strlen(filename));
	return entry;
    }
    
    for(i=1;i<8 && filename[i] && filename[i]!='.';i++);

    entry_long=create_long_filename(s,filename);
   
    entry=array_get_next(&(s->directory));
    memset(entry->name,0x20,11);
    strncpy(entry->name,filename,i);
    
    if(filename[i]) {
	int len=strlen(filename);
        for(i=len;i>0 && filename[i-1]!='.';i--);
        if(i>0)
            memcpy(entry->extension,filename+i,(len-i>3?3:len-i));
    }

    /* upcase & remove unwanted characters */
    for(i=10;i>=0;i--) {
	if(i==10 || i==7) for(;i>1 && entry->name[i]==' ';i--);
	if(entry->name[i]<=' ' || entry->name[i]>0x7f
		|| strchr("*?<>|\":/\\[];,+='",entry->name[i]))
	    entry->name[i]='_';
        else if(entry->name[i]>='a' && entry->name[i]<='z')
            entry->name[i]+='A'-'a';
    }

    /* mangle duplicates */
    while(1) {
	direntry_t* entry1=array_get(&(s->directory),directory_start);
	int j;

	for(;entry1<entry;entry1++)
	    if(!(entry1->attributes&0xf) && !memcmp(entry1->name,entry->name,11))
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
	while(entry_long<entry
		    && entry_long->attributes==0xf) {
	    entry_long->reserved[1]=chksum;
	    entry_long++;
	}
    }

    return entry;
}

static int read_directory(BDRVVVFATState* s,const char* dirname,
		int first_cluster_of_parent)
{

    DIR* dir=opendir(dirname);
    struct dirent* entry;
    struct stat st;
    unsigned int start_of_directory=s->directory.next;
    /* mappings before first_file_mapping are directories */
    unsigned int first_directory_mapping=s->first_file_mapping;
    unsigned int first_cluster=(start_of_directory/0x10/s->sectors_per_cluster);
    int i;

    if(!dir)
	return -1;
    
    while((entry=readdir(dir))) {
	unsigned int length=strlen(dirname)+2+strlen(entry->d_name);
        char* buffer;
	direntry_t* direntry;
	int is_dot=!strcmp(entry->d_name,".");
	int is_dotdot=!strcmp(entry->d_name,"..");

	if(start_of_directory==1 && (is_dotdot || is_dot))
	    continue;
	
	buffer=(char*)malloc(length);
	snprintf(buffer,length,"%s/%s",dirname,entry->d_name);

	if(stat(buffer,&st)<0) {
	    free(buffer);
            continue;
	}

	/* create directory entry for this file */
	//fprintf(stderr,"create direntry at %d (cluster %d) for %s\n",s->directory.next,s->directory.next/0x10/s->sectors_per_cluster,entry->d_name);
	direntry=create_short_filename(s,start_of_directory,entry->d_name,is_dot||is_dotdot);
	direntry->attributes=(S_ISDIR(st.st_mode)?0x10:0x20);
	direntry->reserved[0]=direntry->reserved[1]=0;
	direntry->ctime=fat_datetime(st.st_ctime,1);
	direntry->cdate=fat_datetime(st.st_ctime,0);
	direntry->adate=fat_datetime(st.st_atime,0);
	direntry->begin_hi=0;
	direntry->mtime=fat_datetime(st.st_mtime,1);
	direntry->mdate=fat_datetime(st.st_mtime,0);
	if(is_dotdot)
	    direntry->begin=cpu_to_le16(first_cluster_of_parent);
	else if(is_dot)
	    direntry->begin=cpu_to_le16(first_cluster);
	else
	    direntry->begin=cpu_to_le16(0); /* do that later */
	direntry->size=cpu_to_le32(st.st_size);

	/* create mapping for this file */
	if(!is_dot && !is_dotdot) {
	    if(S_ISDIR(st.st_mode))
		s->current_mapping=(mapping_t*)array_insert(&(s->mapping),s->first_file_mapping++,1);
	    else
		s->current_mapping=(mapping_t*)array_get_next(&(s->mapping));
	    s->current_mapping->begin=0;
	    s->current_mapping->end=st.st_size;
	    s->current_mapping->offset=0;
	    s->current_mapping->filename=buffer;
	    s->current_mapping->dir_index=s->directory.next-1;
	    s->current_mapping->mode=(S_ISDIR(st.st_mode)?MODE_DIRECTORY:MODE_UNDEFINED);
	}
    }
    closedir(dir);

    /* fill with zeroes up to the end of the cluster */
    while(s->directory.next%(0x10*s->sectors_per_cluster)) {
	direntry_t* direntry=array_get_next(&(s->directory));
	memset(direntry,0,sizeof(direntry_t));
    }

    /* reserve next cluster also (for new files) */
    for(i=0;i<0x10*s->sectors_per_cluster;i++) {
	direntry_t* direntry=array_get_next(&(s->directory));
	memset(direntry,0,sizeof(direntry_t));
    }

    /* was it the first directory? */
    if(start_of_directory==1) {
	mapping_t* mapping=array_insert(&(s->mapping),0,1);
	mapping->filename=strdup(dirname);
	mapping->mode=MODE_DIRECTORY;
	mapping->begin=0;
	mapping->end=1;
	mapping->offset=0;
	mapping->dir_index=0xffffffff;
	s->sectors_of_root_directory=s->directory.next/0x10;
    }

    /* recurse directories */
    {
	int i;

	//fprintf(stderr,"iterating subdirectories of %s (first cluster %d): %d to %d\n",dirname,first_cluster,first_directory_mapping,last_directory_mapping);
	for(i=first_directory_mapping;i<s->first_file_mapping;i++) {
	    mapping_t* mapping=array_get(&(s->mapping),i);
	    direntry_t* direntry=array_get(&(s->directory),mapping->dir_index);
	    /* the directory to be read can add more subdirectories */
	    int last_dir_mapping=s->first_file_mapping;
	    
	    assert(mapping->mode==MODE_DIRECTORY);
	    /* first, tell the mapping where the directory will start */
	    mapping->begin=s->directory.next/0x10/s->sectors_per_cluster;
	    if(i>0) {
		mapping[-1].end=mapping->begin;
		assert(mapping[-1].begin<mapping->begin);
	    }
	    /* then tell the direntry */
	    direntry->begin=cpu_to_le16(mapping->begin);
	    //fprintf(stderr,"read directory %s (begin %d)\n",mapping->filename,(int)mapping->begin);
	    /* then read it */
	    if(read_directory(s,mapping->filename,first_cluster))
		return -1;

	    if(last_dir_mapping!=s->first_file_mapping) {
		int diff=s->first_file_mapping-last_dir_mapping;
		assert(diff>0);

		if(last_dir_mapping!=i+1) {
		    int count=last_dir_mapping-i-1;
		    int to=s->first_file_mapping-count;

		    assert(count>0);
		    assert(to>i+1);
		    array_roll(&(s->mapping),to,i+1,count);
		    /* could have changed due to realloc */
		    mapping=array_get(&(s->mapping),i);
		    mapping->end=mapping[1].begin;
		}
		i+=diff;
	    }
	}
    }

    return 0;
}

static int init_directory(BDRVVVFATState* s,const char* dirname)
{
    bootsector_t* bootsector=(bootsector_t*)&(s->first_sectors[(s->first_sectors_number-1)*0x200]);
    unsigned int i;
    unsigned int cluster;

    memset(&(s->first_sectors[0]),0,0x40*0x200);

    /* TODO: if FAT32, this is probably wrong */
    s->sectors_per_fat=0xfc;
    s->sectors_per_cluster=0x10;
    s->cluster_size=s->sectors_per_cluster*0x200;
    s->cluster=malloc(s->cluster_size);
    
    array_init(&(s->mapping),sizeof(mapping_t));
    array_init(&(s->directory),sizeof(direntry_t));
    array_init(&(s->commit),sizeof(commit_t));

    /* add volume label */
    {
	direntry_t* entry=array_get_next(&(s->directory));
	entry->attributes=0x28; /* archive | volume label */
	snprintf(entry->name,11,"QEMU VVFAT");
    }

    if(read_directory(s,dirname,0))
	return -1;

    /* make sure that the number of directory entries is multiple of 0x200/0x20 (to fit the last sector exactly) */
    s->sectors_for_directory=s->directory.next/0x10;

    s->faked_sectors=s->first_sectors_number+s->sectors_per_fat*2+s->sectors_for_directory;
    s->cluster_count=(s->sector_count-s->faked_sectors)/s->sectors_per_cluster;

    /* Now build FAT, and write back information into directory */
    init_fat(s);

    cluster=s->sectors_for_directory/s->sectors_per_cluster;
    assert(s->sectors_for_directory%s->sectors_per_cluster==0);

    /* set the end of the last read directory */
    if(s->first_file_mapping>0) {
	mapping_t* mapping=array_get(&(s->mapping),s->first_file_mapping-1);
	mapping->end=cluster;
    }

    for(i=1;i<s->mapping.next;i++) {
	mapping_t* mapping=array_get(&(s->mapping),i);
	direntry_t* direntry=array_get(&(s->directory),mapping->dir_index);
	if(mapping->mode==MODE_DIRECTORY) {
	    /* directory */
	    int i;
#ifdef DEBUG
	    fprintf(stderr,"assert: %s %d < %d\n",mapping->filename,(int)mapping->begin,(int)mapping->end);
#endif
	    assert(mapping->begin<mapping->end);
	    for(i=mapping->begin;i<mapping->end-1;i++)
		fat_set(s,i,i+1);
	    fat_set(s,i,0x7fffffff);
	} else {
	    /* as the space is virtual, we can be sloppy about it */
	    unsigned int end_cluster=cluster+mapping->end/s->cluster_size;

	    if(end_cluster>=s->cluster_count) {
		fprintf(stderr,"Directory does not fit in FAT%d\n",s->fat_type);
		return -1;
	    }
	    mapping->begin=cluster;
	    mapping->mode=MODE_NORMAL;
	    mapping->offset=0;
	    direntry->size=cpu_to_le32(mapping->end);
	    if(direntry->size==0) {
		direntry->begin=0;
		mapping->end=cluster;
		continue;
	    }

	    direntry->begin=cpu_to_le16(cluster);
	    mapping->end=end_cluster+1;
	    for(;cluster<end_cluster;cluster++)
	        fat_set(s,cluster,cluster+1);
	    fat_set(s,cluster,0x7fffffff);
	    cluster++;
	}
    }

    s->current_mapping=0;

    bootsector->jump[0]=0xeb;
    bootsector->jump[1]=0x3e;
    bootsector->jump[2]=0x90;
    memcpy(bootsector->name,"QEMU    ",8);
    bootsector->sector_size=cpu_to_le16(0x200);
    bootsector->sectors_per_cluster=s->sectors_per_cluster;
    bootsector->reserved_sectors=cpu_to_le16(1);
    bootsector->number_of_fats=0x2; /* number of FATs */
    bootsector->root_entries=cpu_to_le16(s->sectors_of_root_directory*0x10);
    bootsector->zero=0;
    bootsector->media_type=(s->first_sectors_number==1?0xf0:0xf8); /* media descriptor */
    bootsector->sectors_per_fat=cpu_to_le16(s->sectors_per_fat);
    bootsector->sectors_per_track=cpu_to_le16(0x3f);
    bootsector->number_of_heads=cpu_to_le16(0x10);
    bootsector->hidden_sectors=cpu_to_le32(s->first_sectors_number==1?0:0x3f);
    /* TODO: if FAT32, adjust */
    bootsector->total_sectors=cpu_to_le32(s->sector_count);

    /* TODO: if FAT32, this is wrong */
    bootsector->u.fat16.drive_number=0x80; /* assume this is hda (TODO) */
    bootsector->u.fat16.current_head=0;
    bootsector->u.fat16.signature=0x29;
    bootsector->u.fat16.id=cpu_to_le32(0xfabe1afd);

    memcpy(bootsector->u.fat16.volume_label,"QEMU VVFAT ",11);
    memcpy(bootsector->fat_type,(s->fat_type==12?"FAT12   ":s->fat_type==16?"FAT16   ":"FAT32   "),8);
    bootsector->magic[0]=0x55; bootsector->magic[1]=0xaa;

    return 0;
}

static int vvfat_open(BlockDriverState *bs, const char* dirname)
{
    BDRVVVFATState *s = bs->opaque;
    int i;

    /* TODO: automatically determine which FAT type */
    s->fat_type=16;
    s->sector_count=0xec04f;

    s->current_cluster=0xffffffff;
    s->first_file_mapping=0;

    /* TODO: if simulating a floppy, this is 1, because there is no partition table */
    s->first_sectors_number=0x40;
    
    if (strstart(dirname, "fat:", &dirname)) {
        /* read only is the default for safety */
        bs->read_only = 1;
    } else if (strstart(dirname, "fatrw:", &dirname)) {
        /* development only for now */
        bs->read_only = 0;
    } else {
        return -1;
    }
    if(init_directory(s,dirname))
	return -1;

    if(s->first_sectors_number==0x40)
	init_mbr(s);

    /* TODO: this could be wrong for FAT32 */
    bs->cyls=1023; bs->heads=15; bs->secs=63;
    bs->total_sectors=bs->cyls*bs->heads*bs->secs;

    /* write support */
    for(i=0;i<3;i++)
	s->action[i]=WRITE_UNDEFINED;
    return 0;
}

static inline void vvfat_close_current_file(BDRVVVFATState *s)
{
    if(s->current_mapping) {
	s->current_mapping = 0;
	close(s->current_fd);
    }
}

/* mappings between index1 and index2-1 are supposed to be ordered
 * return value is the index of the last mapping for which end>cluster_num
 */
static inline int find_mapping_for_cluster_aux(BDRVVVFATState* s,int cluster_num,int index1,int index2)
{
    int index3=index1+1;
    //fprintf(stderr,"find_aux: cluster_num=%d, index1=%d,index2=%d\n",cluster_num,index1,index2);
    while(1) {
	mapping_t* mapping;
	index3=(index1+index2)/2;
	mapping=array_get(&(s->mapping),index3);
	//fprintf(stderr,"index3: %d = (%d+%d)/2, end: %d\n",index3,index1,index2,(int)mapping->end);
	if(mapping->end>cluster_num) {
	    assert(index2!=index3 || index2==0);
	    if(index2==index3)
		return index2;
	    index2=index3;
	} else {
	    if(index1==index3)
		return index2;
	    index1=index3;
	}
	assert(index1<=index2);
    }
}

static inline mapping_t* find_mapping_for_cluster(BDRVVVFATState* s,int cluster_num)
{
    int index=find_mapping_for_cluster_aux(s,cluster_num,0,s->mapping.next);
    mapping_t* mapping;
    if(index>=s->mapping.next)
	return 0;
    mapping=array_get(&(s->mapping),index);
    if(mapping->begin>cluster_num)
	return 0;
    return mapping;
}

static int open_file(BDRVVVFATState* s,mapping_t* mapping,int flags)
{
    if(!mapping)
	return -1;
    assert(flags==O_RDONLY || flags==O_RDWR);
    if(!s->current_mapping ||
	    strcmp(s->current_mapping->filename,mapping->filename) ||
	    (flags==O_RDWR && !s->current_fd_is_writable)) {
	/* open file */
	int fd = open(mapping->filename, flags | O_BINARY | O_LARGEFILE);
	if(fd<0)
	    return -1;
	vvfat_close_current_file(s);
	s->current_fd = fd;
	s->current_fd_is_writable = (flags==O_RDWR?-1:0);
	s->current_mapping = mapping;
    }
    return 0;
}

static inline int read_cluster(BDRVVVFATState *s,int cluster_num)
{
    if(s->current_cluster != cluster_num) {
	int result=0;
	off_t offset;
	if(!s->current_mapping
		|| s->current_mapping->begin>cluster_num
		|| s->current_mapping->end<=cluster_num) {
	    /* binary search of mappings for file */
	    mapping_t* mapping=find_mapping_for_cluster(s,cluster_num);
	    if(open_file(s,mapping,O_RDONLY))
		return -2;
	}

	offset=s->cluster_size*(cluster_num-s->current_mapping->begin+s->current_mapping->offset);
	if(lseek(s->current_fd, offset, SEEK_SET)!=offset)
	    return -3;
	result=read(s->current_fd,s->cluster,s->cluster_size);
	if(result<0) {
	    s->current_cluster = -1;
	    return -1;
	}
	s->current_cluster = cluster_num;
    }
    return 0;
}

static int vvfat_read(BlockDriverState *bs, int64_t sector_num, 
                    uint8_t *buf, int nb_sectors)
{
    BDRVVVFATState *s = bs->opaque;
    int i;

    //    fprintf(stderr,"vvfat_read: sector %d+%d\n",(int)sector_num,nb_sectors);

    for(i=0;i<nb_sectors;i++,sector_num++) {
	if(sector_num<s->faked_sectors) {
		if(sector_num<s->first_sectors_number)
		    memcpy(buf+i*0x200,&(s->first_sectors[sector_num*0x200]),0x200);
		else if(sector_num-s->first_sectors_number<s->sectors_per_fat)
			memcpy(buf+i*0x200,&(s->fat.pointer[(sector_num-s->first_sectors_number)*0x200]),0x200);
		else if(sector_num-s->first_sectors_number-s->sectors_per_fat<s->sectors_per_fat)
			memcpy(buf+i*0x200,&(s->fat.pointer[(sector_num-s->first_sectors_number-s->sectors_per_fat)*0x200]),0x200);
		else if(sector_num-s->first_sectors_number-s->sectors_per_fat*2<s->sectors_for_directory)
			memcpy(buf+i*0x200,&(s->directory.pointer[(sector_num-s->first_sectors_number-s->sectors_per_fat*2)*0x200]),0x200);
	} else {
            uint32_t sector=sector_num-s->first_sectors_number-s->sectors_per_fat*2,
	        sector_offset_in_cluster=(sector%s->sectors_per_cluster),
                cluster_num=sector/s->sectors_per_cluster;
		if(read_cluster(s, cluster_num) != 0) {
			//fprintf(stderr,"failed to read cluster %d\n",(int)cluster_num);
			// TODO: strict: return -1;
			memset(buf+i*0x200,0,0x200);
			continue;
		}
		memcpy(buf+i*0x200,s->cluster+sector_offset_in_cluster*0x200,0x200);
	}
    }
    return 0;
}

static void print_direntry(direntry_t* direntry)
{
    if(!direntry)
	return;
    if(direntry->attributes==0xf) {
	unsigned char* c=(unsigned char*)direntry;
	int i;
	for(i=1;i<11 && c[i] && c[i]!=0xff;i+=2)
	    fputc(c[i],stderr);
	for(i=14;i<26 && c[i] && c[i]!=0xff;i+=2)
	    fputc(c[i],stderr);
	for(i=28;i<32 && c[i] && c[i]!=0xff;i+=2)
	    fputc(c[i],stderr);
	fputc('\n',stderr);
    } else {
	int i;
	for(i=0;i<11;i++)
	    fputc(direntry->name[i],stderr);
	fprintf(stderr,"attributes=0x%02x begin=%d size=%d\n",
		direntry->attributes,
		direntry->begin,direntry->size);
    }
}

static void print_changed_sector(BlockDriverState *bs,int64_t sector_num,const uint8_t *buf)
{
    BDRVVVFATState *s = bs->opaque;

    if(sector_num<s->first_sectors_number)
	return;
    if(sector_num<s->first_sectors_number+s->sectors_per_fat*2) {
	int first=((sector_num-s->first_sectors_number)%s->sectors_per_fat);
	int first_fat_entry=first*0x200/2;
	int i;

	fprintf(stderr, "fat:\n");
	for(i=0;i<0x200;i+=2) {
	    uint16_t* f=array_get(&(s->fat),first_fat_entry+i/2);
	    if(memcmp(buf+i,f,2))
		fprintf(stderr,"%d(%d->%d) ",first_fat_entry+i/2,*f,*(uint16_t*)(buf+i));
	}
	fprintf(stderr, "\n");
    } else if(sector_num<s->faked_sectors) {
	direntry_t* d=(direntry_t*)buf;
	int i;
	fprintf(stderr, "directory:\n");
	for(i=0;i<0x200/sizeof(direntry_t);i++) {
	    direntry_t* d_old=(direntry_t*)(s->directory.pointer+0x200*(sector_num-s->first_sectors_number-s->sectors_per_fat*2)+i*sizeof(direntry_t));
	    if(memcmp(d+i,d_old,sizeof(direntry_t))) {
		fprintf(stderr, "old: "); print_direntry(d_old);
		fprintf(stderr, "new: "); print_direntry(d+i);
		fprintf(stderr, "\n");
	    }
	}
    } else {
	int sec=(sector_num-s->first_sectors_number-2*s->sectors_per_fat);
	fprintf(stderr, "\tcluster: %d(+%d sectors)\n",sec/s->sectors_per_cluster,sec%s->sectors_per_cluster);
    }
}

char direntry_is_free(const direntry_t* direntry)
{
    return direntry->name[0]==0 || direntry->name[0]==0xe5;
}

/* TODO: use this everywhere */
static inline uint32_t begin_of_direntry(direntry_t* direntry)
{
    return le16_to_cpu(direntry->begin)|(le16_to_cpu(direntry->begin_hi)<<16);
}

int consistency_check1(BDRVVVFATState *s) {
    /* check all mappings */
    int i;
    for(i=0;i<s->mapping.next;i++) {
	mapping_t* mapping=array_get(&(s->mapping),i);
	int j;
	for(j=mapping->begin;j<mapping->end-1;j++)
	    assert(fat_get(s,j)==j+1);
	assert(fat_get(s,j)==(0x7fffffff&s->max_fat_value));
    }
    return 0;
}

int consistency_check2(BDRVVVFATState *s) {
    /* check fat entries: consecutive fat entries should be mapped in one mapping */
    int i;
    /* TODO: i=0 (mappings for direntries have to be sorted) */
    for(i=s->sectors_for_directory/s->sectors_per_cluster;i<s->fat.next-1;i++) {
	uint32_t j=fat_get(s,i);
	if(j!=i+1 && j!=0 && !fat_eof(s,j)) {
	    mapping_t* mapping=find_mapping_for_cluster(s,i+1);
	    assert(mapping->begin==i+1);
	}
    }
    return 0;
}

int consistency_check3(BDRVVVFATState *s) {
    /* check that for each file there is exactly one mapping per cluster */
    int i,count_non_next=0;
    for(i=0;i<s->mapping.next;i++) {
	mapping_t* mapping=array_get(&(s->mapping),i);
	/* TODO: when directories are correctly adapted, add them here */
	assert(mapping->begin<mapping->end);
	if(mapping->mode==MODE_NORMAL) {
	    int j,count=0,count_next=0;
	    for(j=0;j<s->mapping.next;j++) {
		mapping_t* other=array_get(&(s->mapping),j);
		if(mapping->begin<other->end&&mapping->end>other->begin)
		    count++;
		if(mapping->end==other->begin)
		    count_next++;
	    }
	    assert(count==1); /* no overlapping mappings */
	    assert(count_next==1 || count_next==0); /* every mapping except the last one has a successor */
	    if(!count_next)
		count_non_next++;
	}
    }
    assert(count_non_next==1); /* only one last mapping */
    return 0;
}

static inline commit_t* commit_get_next(BDRVVVFATState* s)
{
    commit_t* commit=array_get_next(&(s->commit));
    if((commit->buf=malloc(s->cluster_size))==0) {
	/* out of memory */
	s->commit.next--;
	return 0;
    }
    return commit;
}

int commit_remove(BDRVVVFATState* s,commit_t* commit)
{
    int index=commit-(commit_t*)s->commit.pointer;
    free(commit->buf);
    if(array_roll(&(s->commit),s->commit.next-1,index,1))
	return -1;
    s->commit.next--;
    return 0;
}

/* TODO: the plan for write support:
 *
 * it seems that the direntries are written first, then the data is committed
 * to the free sectors, then fat 1 is updated, then fat2.
 *
 * Plan: when sectors are written, do the following:
 *
 * - if they are in a directory, check if the entry has changed. if yes,
 *   look what has changed (different strategies for name, begin & size).
 *
 *   if it is new (old entry is only 0's or has E5 at the start), create it,
 *   and also create mapping, but in a special mode "undefined" (TODO),
 *   because we cannot know which clusters belong to it yet.
 *
 *   if it is zeroed, or has E5 at the start, look if has just moved. If yes,
 *   copy the entry to the new position. If no, delete the file.
 *
 * - if they are in data, and the cluster is undefined, add it to the commit
 *   list. if the cluster is defined (find_mapping), then write it into the
 *   corresponding file.
 *
 *   If it is the last cluster (TODO: add a function
 *   fat_get(s,cluster); ), make sure not to write a complete cluster_size.
 *
 *   If the data is in current_cluster, update s->cluster.
 *
 * - if they are in fat 1, update mappings, look in the commit list
 *   (assertions!) and if the cluster is now known (or changed from undefined
 *   state to defined state, like when begin or size changed in a direntry),
 *   write it.
 *
 * - if they are in fat 2, make sure they match with current fat.
 *
 */

void mapping_modify_from_direntry(BDRVVVFATState* s,mapping_t* mapping,direntry_t* direntry)
{
    int begin=le16_to_cpu(direntry->begin),
        end=begin+le32_to_cpu(direntry->size)/s->cluster_size+1,
	i;
    mapping->mode = MODE_MODIFIED;
    /* TODO: what if begin==0 (size==0)? */
    mapping->begin = begin;
    /* TODO: why not just mapping->end = begin+1 ? */
    for(i=begin+1;i<end && (fat_get(s,i)==0 || fat_get(s,i)==i+1);i++);
    mapping->end = i;
}

mapping_t* find_mapping_for_direntry(BDRVVVFATState* s,direntry_t* direntry)
{
    int i;
    int dir_index=direntry-((direntry_t*)s->directory.pointer);
    
    /* TODO: support allocation for new clusters for directories (new/larger directory */
    assert(dir_index<0x200/0x20*s->sectors_for_directory);
    
    for(i=0;i<s->mapping.next;i++) {
	mapping_t* mapping=array_get(&(s->mapping),i);
	if(mapping->dir_index==dir_index && mapping->offset==0 &&
		mapping->mode!=MODE_UNDEFINED)
	    return mapping;
    }
    return 0;
}

static inline uint32_t sector2cluster(BDRVVVFATState* s,off_t sector_num)
{
    return (sector_num-s->first_sectors_number-2*s->sectors_per_fat)/s->sectors_per_cluster;
}

static inline uint32_t sector_offset_in_cluster(BDRVVVFATState* s,off_t sector_num)
{
    return (sector_num-s->first_sectors_number-2*s->sectors_per_fat)%s->sectors_per_cluster;
}

static commit_t* get_commit_for_cluster(BDRVVVFATState* s,uint32_t cluster_num)
{
    int i;
    for(i=0;i<s->commit.next;i++) {
	commit_t* commit=array_get(&(s->commit),i);
	if(commit->cluster_num==cluster_num)
	    return commit;
    }
    return 0;
}

static inline commit_t* create_or_get_commit_for_sector(BDRVVVFATState* s,off_t sector_num)
{
    int i;
    commit_t* commit;
    uint32_t cluster_num=sector2cluster(s,sector_num);

    for(i=0;i<s->commit.next;i++) {
	commit=array_get(&(s->commit),i);
	if(commit->cluster_num==cluster_num)
	    return commit;
    }

    commit=commit_get_next(s);
    commit->cluster_num=cluster_num;
    /* we can ignore read errors here */
    read_cluster(s,cluster_num);
    memcpy(commit->buf,s->cluster,s->cluster_size);
    return commit;
}

static direntry_t* get_direntry_for_mapping(BDRVVVFATState* s,mapping_t* mapping)
{
    if(mapping->mode==MODE_UNDEFINED)
	return 0;
    if(mapping->dir_index>=0x200/0x20*s->sectors_for_directory)
	return 0;
    return (direntry_t*)(s->directory.pointer+sizeof(direntry_t)*mapping->dir_index);
}

static void print_mappings(BDRVVVFATState* s)
{
    int i;
    fprintf(stderr,"mapping:\n");
    for(i=0;i<s->mapping.next;i++) {
	mapping_t* m=array_get(&(s->mapping),i);
	direntry_t* d=get_direntry_for_mapping(s,m);
	fprintf(stderr,"%02d %d-%d (%d) %s (dir: %d)",i,(int)m->begin,(int)m->end,(int)m->offset,m->filename,m->dir_index);
	print_direntry(d);
	fprintf(stderr,"\n");
    }
    fprintf(stderr,"mappings end.\n");
}

/* TODO: statify all functions */

/* This function is only meant for file contents.
 * It will return an error if used for other sectors. */
static int write_cluster(BDRVVVFATState* s,uint32_t cluster_num,const uint8_t* buf)
{
    /* sector_offset is the sector_num relative to the first cluster */
    mapping_t* mapping=find_mapping_for_cluster(s,cluster_num);
    direntry_t* direntry;
    int next_cluster,write_size,last_cluster;
    off_t offset;

    /* if this cluster is free, return error */
    next_cluster=fat_get(s,cluster_num);
    if(next_cluster<2)
	return -1;
    
    /* TODO: MODE_DIRECTORY */
    if(!mapping || mapping->mode==MODE_UNDEFINED || mapping->mode==MODE_DIRECTORY)
	return -1;
    direntry=get_direntry_for_mapping(s,mapping);
    if(!direntry)
	return -2;

    /* get size to write */
    last_cluster=fat_eof(s,next_cluster);
    write_size=!last_cluster?s->cluster_size:
	(le32_to_cpu(direntry->size)%s->cluster_size);
    if(write_size<=0)
	return 0;
    //fprintf(stderr,"next_cluster: %d (%d), write_size: %d, %d, %d\n",next_cluster,s->max_fat_value-8,write_size,direntry->size,s->cluster_size);

    if(open_file(s,mapping,O_RDWR))
	return -4;
   
    offset=(cluster_num-mapping->begin+mapping->offset)*s->cluster_size;
    if(lseek(s->current_fd,offset,SEEK_SET)!=offset)
	return -3;
    if(write(s->current_fd,buf,write_size)!=write_size) {
	lseek(s->current_fd,0,SEEK_END);
	vvfat_close_current_file(s);
	return -2;
    }

    /* seek to end of file, so it doesn't get truncated */
    if(!last_cluster)
	lseek(s->current_fd,0,SEEK_END);
    else {
	ftruncate(s->current_fd,le32_to_cpu(direntry->size));
	vvfat_close_current_file(s);
    }

    /* update s->cluster if necessary */
    if(cluster_num==s->current_cluster && s->cluster!=buf)
	memcpy(s->cluster,buf,s->cluster_size);

    return 0;
}

/* this function returns !=0 on error */
int mapping_is_consistent(BDRVVVFATState* s,mapping_t* mapping)
{
    direntry_t* direntry=get_direntry_for_mapping(s,mapping);
    uint32_t cluster_count=0;
    int commit_count=0; /* number of commits for this file (we also write incomplete files; think "append") */
    //fprintf(stderr,"check direntry for %s\n",mapping->filename);
    while(mapping) {
	int i;
	assert(mapping->begin<mapping->end);
	for(i=mapping->begin;i<mapping->end-1;i++) {
	    if(i<=0 || fat_get(s,i)!=i+1) {
		/*fprintf(stderr,"the fat mapping of %d is not %d, but %d\n",
			i,i+1,fat_get(s,i));*/
		return -1;
	    }
	    if(get_commit_for_cluster(s,i))
		commit_count++;
	}
	if(get_commit_for_cluster(s,i))
	    commit_count++;

	cluster_count+=mapping->end-mapping->begin;
	
	i=fat_get(s,mapping->end-1);
	if(fat_eof(s,i))
	    break;

	mapping=find_mapping_for_cluster(s,i);
	if(!mapping) {
	    //fprintf(stderr,"No mapping found for %d\n",i);
	    print_mappings(s);
	    return -2;
	}
    }

    if(cluster_count!=(le32_to_cpu(direntry->size)+s->cluster_size-1)/s->cluster_size) {
	//fprintf(stderr,"cluster_count is %d, but size is %d\n",cluster_count,le32_to_cpu(direntry->size));
	return -3;
    }

    if(commit_count==0)
	return -4;

    //fprintf(stderr,"okay\n");
    return 0;
}

/* TODO: remember what comes third, and what's first in this OS:
 * FAT, direntry or data.
 * If the last written sector is either last in cluster or sector_num+nb_sectors-1,
 * 	- commit every cluster for this file if mapping_is_consistent()==0
 * 	- if the last written sector is first_action, and last_action=third_action, clear commit
 */

static int commit_cluster_aux(BDRVVVFATState* s,commit_t* commit)
{
    int result=write_cluster(s,commit->cluster_num,commit->buf);
    return result;
}


static int commit_cluster(BDRVVVFATState* s,uint32_t cluster_num)
{
    commit_t* commit;

    /* commit the sectors of this cluster */
    commit=get_commit_for_cluster(s,cluster_num);
    if(commit)
	return commit_cluster_aux(s,commit);
    return 0;
}

/* this function checks the consistency for the direntry which belongs to
 * the mapping. if everything is found consistent, the data is committed.
 * this returns 0 if no error occurred (even if inconsistencies were found) */
static inline int commit_data_if_consistent(BDRVVVFATState* s,mapping_t* mapping,write_action_t action)
{
    direntry_t* direntry;
    
    if(!mapping)
	return 0;

    //fprintf(stderr,"7\n");
#define d(x) fprintf(stderr,#x "\n")
    direntry=get_direntry_for_mapping(s,mapping);

    //d(8);

    assert(action==WRITE_FAT || action==WRITE_DIRENTRY || action==WRITE_DATA);

    //d(9);
    //fprintf(stderr,"mapping: 0x%x s=0x%x\n",(uint32_t)mapping,(uint32_t)s);
    /*fprintf(stderr,"commit? file=%s, action=%s\n",
	    mapping->filename,action==WRITE_FAT?"fat":action==WRITE_DIRENTRY?"direntry":"data");*/

    //d(10);
    if(s->action[2]==WRITE_UNDEFINED) {
	int i;
	for(i=2;i>0 && s->action[i-1]==WRITE_UNDEFINED;i--);
	if(i>0 && action!=s->action[i-1])
	    s->action[i]=action;
	assert(i<2 || s->action[0]!=s->action[2]);
    }
    //d(11);
    
    if(mapping_is_consistent(s,mapping)==0) {
	uint32_t cluster_num=begin_of_direntry(direntry);
	off_t remaining_bytes=le32_to_cpu(direntry->size);
	//fprintf(stderr,"the data for %s was found consistent\n",mapping->filename);
	while(remaining_bytes>0) {
	    commit_t* commit=get_commit_for_cluster(s,cluster_num);
	    if(!commit)
		continue;
		
	    //fprintf(stderr,"commit_cluster %d (%d), remaining: %d\n",cluster_num,s->max_fat_value-15,(int)remaining_bytes);
	    assert(cluster_num>1);
	    assert(cluster_num<s->max_fat_value-15);
	    if(commit_cluster(s,cluster_num)) {
		fprintf(stderr,"error committing cluster %d\n",cluster_num);
		return -1;
	    }
	    cluster_num=fat_get(s,cluster_num);
	    remaining_bytes-=s->cluster_size;
	    /* TODO: if(action==s->action[2]) {
		commit_t* commit=get_commit_for_cluster(s,cluster_num);
		commit_remove(s,commit);
	    } */
	}
    }
    //print_mappings(s);
    //fprintf(stderr,"finish vvfat_write\n");
    return 0;
}

static int vvfat_write(BlockDriverState *bs, int64_t sector_num, 
                    const uint8_t *buf, int nb_sectors)
{
    BDRVVVFATState *s = bs->opaque;
    int i;

    /* fprintf(stderr,"vvfat_write %d+%d (%s)\n",(int)sector_num,nb_sectors,
		    (sector_num>=s->faked_sectors?"data":
		     (sector_num>=s->first_sectors_number+2*s->sectors_per_fat?"directory":
		      (sector_num>=s->first_sectors_number+s->sectors_per_fat?"fat 2":
		       (sector_num>=s->first_sectors_number?"fat 1":"boot sector"))))); */

    for(i=0;i<nb_sectors;i++,sector_num++,buf+=0x200) {
	print_changed_sector(bs,sector_num,buf);

	if(sector_num<s->first_sectors_number) {
	    /* change the bootsector or partition table? no! */
	    return -1;
	} else if(sector_num<s->first_sectors_number+s->sectors_per_fat) {
	    /* FAT 1 */
	    int fat_entries_per_cluster=s->cluster_size*8/s->fat_type;
	    int first_cluster=(sector_num-s->first_sectors_number)*fat_entries_per_cluster,i;
	    mapping_t* mapping=0;

	    /* write back */
	    memcpy(s->fat.pointer+0x200*(sector_num-s->first_sectors_number),
		    buf,0x200);

	    /* for each changed FAT entry, */
	    for(i=0;i<fat_entries_per_cluster;i++) {
		int new_value;
		
		/* TODO: MODE_DIRENTRY */
		if(first_cluster+i<s->sectors_for_directory/s->sectors_per_cluster)
		    continue;

		new_value=fat_get(s,first_cluster+i);

		/* check the current fat entry */
		if(new_value<2 || (new_value>=s->max_fat_value-0xf && !fat_eof(s,new_value))) {
		    /* free, reserved or bad cluster */
		    mapping=find_mapping_for_cluster(s,first_cluster+i);
		    //assert(!mapping || mapping->mode==MODE_DELETED);
		    if(mapping && mapping->mode==MODE_DELETED &&
			    first_cluster+i+1==mapping->end)
			array_remove(&(s->mapping),mapping-(mapping_t*)s->mapping.pointer);
		    mapping=0;
		    continue;
		}

		/* get the mapping for the current entry */
		if(!mapping || mapping->begin>new_value || mapping->end<=new_value) {
		    mapping=find_mapping_for_cluster(s,first_cluster+i);
		}

		print_mappings(s);
		fprintf(stderr,"fat_get(%d)=%d\n",first_cluster+i,new_value);
		/* TODO: what if there's no mapping? this is valid. */
		/* TODO: refactor the rest of this clause so it can be called when the direntry changes, too */
		assert(mapping);

		if(new_value>1 && new_value<s->max_fat_value-0xf) {
		    /* the cluster new_value points to is valid */

		    if(first_cluster+i+1==new_value) {
			/* consecutive cluster */
			if(mapping->end<=new_value)
			    mapping->end=new_value+1;
		    } else {
			mapping_t* next_mapping;
			
			/* the current mapping ends here */
			mapping->end=first_cluster+i+1;
			
			/* the next mapping */
			next_mapping=find_mapping_for_cluster(s,new_value);
			if(next_mapping) {
			    assert(mapping!=next_mapping);
			    /* assert next mapping's filename is the same */
			    assert(next_mapping->filename==mapping->filename);
			    assert(next_mapping->dir_index==mapping->dir_index);
			    /* assert next mapping is MODIFIED or UNDEFINED */
			    assert(next_mapping->mode==MODE_MODIFIED || next_mapping->mode==MODE_UNDEFINED);
			} else {
			    int index=find_mapping_for_cluster_aux(s,new_value,0,s->mapping.next);
			    next_mapping=array_insert(&(s->mapping),index,1);
			    next_mapping->filename=mapping->filename;
			    next_mapping->dir_index=mapping->dir_index;
			    next_mapping->mode=MODE_MODIFIED;
			    next_mapping->begin=0;
			}
			/* adjust offset of next mapping */
			next_mapping->offset=mapping->offset+mapping->end-mapping->begin;
			/* set begin and possible end */
			if(next_mapping->begin!=new_value) {
			    next_mapping->begin=new_value;
			    next_mapping->end=new_value+1;
			}
			if(commit_data_if_consistent(s,mapping,WRITE_FAT))
			    return -4;
			mapping=0;
		    }
		} else if(fat_eof(s,new_value)) {
		    /* the last cluster of the file */
		    mapping->end=first_cluster+i+1;
		    if(commit_data_if_consistent(s,mapping,WRITE_FAT))
			return -4;
		    mapping=0;
		}
	    }
	} else if(sector_num<s->first_sectors_number+2*s->sectors_per_fat) {
	    /* FAT 2: check if it is the same as FAT 1 */
	    if(memcmp(array_get(&(s->fat),sector_num-s->first_sectors_number),buf,0x200))
		return -1; /* mismatch */
	} else if(sector_num<s->faked_sectors) {
	    /* direntry */
	    /* - if they are in a directory, check if the entry has changed.
	     *   if yes, look what has changed (different strategies for name,
	     *   begin & size).
	     *
	     *   if it is new (old entry is only 0's or has E5 at the start),
	     *   create it, and also create mapping, but in a special mode
	     *   "undefined", because we cannot know which clusters belong
	     *   to it yet.
	     *
	     *   if it is zeroed, or has E5 at the start, look if has just
	     *   moved. If yes, copy the entry to the new position. If no,
	     *   delete the file.
	     */
	    mapping_t* dir_mapping=find_mapping_for_cluster(s,sector2cluster(s,sector_num));
	    direntry_t *original=array_get(&(s->directory),sector_num-s->first_sectors_number-2*s->sectors_per_fat);
	    direntry_t *new_=(direntry_t*)buf;
	    int first_dir_index=(sector_num-s->first_sectors_number-2*s->sectors_per_fat)*0x200/0x20;
	    int j;

#if 0
	    fprintf(stderr,"direntry: consistency check\n");

	    if(s->commit.next==0) {
		consistency_check1(s);
		consistency_check2(s);
		consistency_check3(s);
	    }
#endif

	    assert(sizeof(direntry_t)==0x20);

	    for(j=0;j<0x200/0x20;j++) {
		//fprintf(stderr,"compare direntry %d: 0x%x,0x%x\n",j,(uint32_t)original+j,(uint32_t)new_+j);
		if(memcmp(original+j,new_+j,sizeof(direntry_t))) {
		    //fprintf(stderr,"different\n");
		    /* TODO: in create_short_filename, 0xe5->0x05 is not yet handled! */
		    if(direntry_is_free(original+j)) {
			mapping_t* mapping;
			char buffer[4096];
			int fd,i;

			if(new_[j].attributes==0xf)
			    continue; /* long entry */

			print_mappings(s);
			//fprintf(stderr,"sector: %d cluster: %d\n",(int)sector_num,(int)sector2cluster(s,sector_num));

			/* construct absolute path */
			strncpy(buffer,dir_mapping->filename,4096);
			i=strlen(buffer);
			if(i+2>=4096)
				return -1;
			buffer[i]='/';
			if(long2unix_name(buffer+i+1,4096-i-1,new_+j))
				return -2;

			/* new file/directory */
			if(new_[j].attributes&0x10) {
#ifdef _WIN32
#define SEVENFIVEFIVE
#else
#define SEVENFIVEFIVE ,0755
#endif
			    if(mkdir(buffer SEVENFIVEFIVE))
				return -3;
			    /* TODO: map direntry.begin as directory, together with new array_t direntries */
			    assert(0);
			} else {
			    fd=open(buffer,O_CREAT|O_EXCL,0644);
			    if(!fd)
				return -3;
			    close(fd);
			}

			/* create mapping */
			i=find_mapping_for_cluster_aux(s,begin_of_direntry(new_+j),0,s->mapping.next);
			mapping=array_insert(&(s->mapping),i,1);
			mapping->filename=strdup(buffer);
			mapping->offset=0;
			/* back pointer to direntry */
			mapping->dir_index=first_dir_index+j;
			/* set mode to modified */
			mapping->mode=MODE_MODIFIED;
			/* set begin to direntry.begin */
			mapping->begin=begin_of_direntry(new_+j);
			/* set end to begin+1 */
			mapping->end=mapping->begin+1;
			/* commit file contents */
			if(commit_data_if_consistent(s,mapping,WRITE_DIRENTRY)) {
			    fprintf(stderr,"error committing file contents for new file %s!\n",buffer);
			    return -4;
			}
		    } else if(direntry_is_free(new_+j)) {
			assert(0);
			/* TODO: delete file */
			/* TODO: write direntry */
			/* TODO: modify mapping: set mode=deleted */
		    } else {
			/* modified file */
			mapping_t* mapping=0;
			/* if direntry.begin has changed,
			 * set mode to modified,
			 * adapt begin,
			 * adapt end */
			/* TODO: handle rename */
			assert(!memcmp(new_[j].name,original[j].name,11));
			//fprintf(stderr,"1\n");
			if(new_[j].begin!=original[j].begin || new_[j].size/s->cluster_size!=original[j].size/s->cluster_size) {
			//fprintf(stderr,"2\n");
			    mapping = find_mapping_for_direntry(s,original+j);
			//fprintf(stderr,"3\n");
			    if(!mapping) /* this should never happen! */
				return -2;
			    mapping_modify_from_direntry(s,mapping,new_+j);
			    //fprintf(stderr,"4\n");
			    if(commit_data_if_consistent(s,mapping,WRITE_DIRENTRY)) {
				fprintf(stderr,"big error\n");
				return -4;
			    }
			}
			/* TODO: handle modified times and other attributes */

			//fprintf(stderr,"5: mapping=0x%x, s=0x%x, s->mapping.pointer=0x%x\n",(uint32_t)mapping,(uint32_t)s,(uint32_t)s->mapping.pointer);
			//fprintf(stderr,"6\n");
		    }
		}
	    }
	    /* write back direntries */
	    memcpy(original,new_,0x200);
	} else {
	    /* data */
	    off_t sector=sector_num-s->first_sectors_number-2*s->sectors_per_fat;
	    off_t cluster=sector/s->sectors_per_cluster;
	    mapping_t* mapping=find_mapping_for_cluster(s,cluster);
	    if(mapping && mapping->mode==MODE_DELETED)
		return -3; /* this is an error: no writes to these clusters before committed */
	    {
		/* as of yet, undefined: put into commits */
		commit_t* commit=create_or_get_commit_for_sector(s,sector_num);

		if(!commit)
		    return -1; /* out of memory */
		memcpy(commit->buf+0x200*sector_offset_in_cluster(s,sector_num),buf,0x200);

		//fprintf(stderr,"mapping: 0x%x\n",(uint32_t)mapping);
		if(commit_data_if_consistent(s,mapping,WRITE_DATA))
		    return -4;
	    }
	}
    }
    return 0;
}

static void vvfat_close(BlockDriverState *bs)
{
    BDRVVVFATState *s = bs->opaque;

    vvfat_close_current_file(s);
    array_free(&(s->fat));
    array_free(&(s->directory));
    array_free(&(s->mapping));
    if(s->cluster)
        free(s->cluster);
}

BlockDriver bdrv_vvfat = {
    "vvfat",
    sizeof(BDRVVVFATState),
    vvfat_probe,
    vvfat_open,
    vvfat_read,
    vvfat_write,
    vvfat_close,
};


