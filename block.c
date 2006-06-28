/*
 * QEMU System Emulator block driver
 * 
 * Copyright (c) 2003 Fabrice Bellard
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
#include "vl.h"
#include "block_int.h"

#ifdef _BSD
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/disk.h>
#endif

#ifdef CONFIG_COCOA
#include <paths.h>
#include <sys/param.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOMediaBSDClient.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOCDMedia.h>
//#include <IOKit/storage/IOCDTypes.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef __sun__
#include <sys/dkio.h>
#endif

static BlockDriverState *bdrv_first;
static BlockDriver *first_drv;

#ifdef CONFIG_COCOA
static kern_return_t FindEjectableCDMedia( io_iterator_t *mediaIterator );
static kern_return_t GetBSDPath( io_iterator_t mediaIterator, char *bsdPath, CFIndex maxPathSize );

kern_return_t FindEjectableCDMedia( io_iterator_t *mediaIterator )
{
    kern_return_t       kernResult; 
    mach_port_t     masterPort;
    CFMutableDictionaryRef  classesToMatch;

    kernResult = IOMasterPort( MACH_PORT_NULL, &masterPort );
    if ( KERN_SUCCESS != kernResult ) {
        printf( "IOMasterPort returned %d\n", kernResult );
    }
    
    classesToMatch = IOServiceMatching( kIOCDMediaClass ); 
    if ( classesToMatch == NULL ) {
        printf( "IOServiceMatching returned a NULL dictionary.\n" );
    } else {
    CFDictionarySetValue( classesToMatch, CFSTR( kIOMediaEjectableKey ), kCFBooleanTrue );
    }
    kernResult = IOServiceGetMatchingServices( masterPort, classesToMatch, mediaIterator );
    if ( KERN_SUCCESS != kernResult )
    {
        printf( "IOServiceGetMatchingServices returned %d\n", kernResult );
    }
    
    return kernResult;
}

kern_return_t GetBSDPath( io_iterator_t mediaIterator, char *bsdPath, CFIndex maxPathSize )
{
    io_object_t     nextMedia;
    kern_return_t   kernResult = KERN_FAILURE;
    *bsdPath = '\0';
    nextMedia = IOIteratorNext( mediaIterator );
    if ( nextMedia )
    {
        CFTypeRef   bsdPathAsCFString;
    bsdPathAsCFString = IORegistryEntryCreateCFProperty( nextMedia, CFSTR( kIOBSDNameKey ), kCFAllocatorDefault, 0 );
        if ( bsdPathAsCFString ) {
            size_t devPathLength;
            strcpy( bsdPath, _PATH_DEV );
            strcat( bsdPath, "r" );
            devPathLength = strlen( bsdPath );
            if ( CFStringGetCString( bsdPathAsCFString, bsdPath + devPathLength, maxPathSize - devPathLength, kCFStringEncodingASCII ) ) {
                kernResult = KERN_SUCCESS;
            }
            CFRelease( bsdPathAsCFString );
        }
        IOObjectRelease( nextMedia );
    }
    
    return kernResult;
}

#endif

void bdrv_register(BlockDriver *bdrv)
{
    bdrv->next = first_drv;
    first_drv = bdrv;
}

/* create a new block device (by default it is empty) */
BlockDriverState *bdrv_new(const char *device_name)
{
    BlockDriverState **pbs, *bs;

    bs = qemu_mallocz(sizeof(BlockDriverState));
    if(!bs)
        return NULL;
    pstrcpy(bs->device_name, sizeof(bs->device_name), device_name);
    if (device_name[0] != '\0') {
        /* insert at the end */
        pbs = &bdrv_first;
        while (*pbs != NULL)
            pbs = &(*pbs)->next;
        *pbs = bs;
    }
    return bs;
}

BlockDriver *bdrv_find_format(const char *format_name)
{
    BlockDriver *drv1;
    for(drv1 = first_drv; drv1 != NULL; drv1 = drv1->next) {
        if (!strcmp(drv1->format_name, format_name))
            return drv1;
    }
    return NULL;
}

int bdrv_create(BlockDriver *drv, 
                const char *filename, int64_t size_in_sectors,
                const char *backing_file, int flags)
{
    if (!drv->bdrv_create)
        return -ENOTSUP;
    return drv->bdrv_create(filename, size_in_sectors, backing_file, flags);
}

#ifdef _WIN32
void get_tmp_filename(char *filename, int size)
{
    char* p = strrchr(filename, '/');

    if (p == NULL)
	return;

    /* XXX: find a better function */
    tmpnam(p);
    *p = '/';
}
#else
void get_tmp_filename(char *filename, int size)
{
    int fd;
    /* XXX: race condition possible */
    pstrcpy(filename, size, "/tmp/vl.XXXXXX");
    fd = mkstemp(filename);
    close(fd);
}
#endif

/* XXX: force raw format if block or character device ? It would
   simplify the BSD case */
static BlockDriver *find_image_format(const char *filename)
{
    int fd, ret, score, score_max;
    BlockDriver *drv1, *drv;
    uint8_t *buf;
    size_t bufsize = 1024;

    fd = open(filename, O_RDONLY | O_BINARY | O_LARGEFILE);
    if (fd < 0) {
        buf = NULL;
        ret = 0;
    } else {
#ifdef DIOCGSECTORSIZE
        {
            unsigned int sectorsize = 512;
            if (!ioctl(fd, DIOCGSECTORSIZE, &sectorsize) &&
                sectorsize > bufsize)
                bufsize = sectorsize;
        }
#endif
#ifdef CONFIG_COCOA
        u_int32_t   blockSize = 512;
        if ( !ioctl( fd, DKIOCGETBLOCKSIZE, &blockSize ) && blockSize > bufsize) {
            bufsize = blockSize;
        }
#endif
        buf = qemu_malloc(bufsize);
        if (!buf)
            return NULL;
        ret = read(fd, buf, bufsize);
        if (ret < 0) {
            close(fd);
            qemu_free(buf);
            return NULL;
        }
        close(fd);
    }
    
    drv = NULL;
    score_max = 0;
    for(drv1 = first_drv; drv1 != NULL; drv1 = drv1->next) {
        score = drv1->bdrv_probe(buf, ret, filename);
        if (score > score_max) {
            score_max = score;
            drv = drv1;
        }
    }
    qemu_free(buf);
    return drv;
}

int bdrv_open(BlockDriverState *bs, const char *filename, int snapshot)
{
#ifdef CONFIG_COCOA
    if ( strncmp( filename, "/dev/cdrom", 10 ) == 0 ) {
        kern_return_t kernResult;
        io_iterator_t mediaIterator;
        char bsdPath[ MAXPATHLEN ];
        int fd;
 
        kernResult = FindEjectableCDMedia( &mediaIterator );
        kernResult = GetBSDPath( mediaIterator, bsdPath, sizeof( bsdPath ) );
    
        if ( bsdPath[ 0 ] != '\0' ) {
            strcat(bsdPath,"s0");
            /* some CDs don't have a partition 0 */
            fd = open(bsdPath, O_RDONLY | O_BINARY | O_LARGEFILE);
            if (fd < 0) {
                bsdPath[strlen(bsdPath)-1] = '1';
            } else {
                close(fd);
            }
            filename = bsdPath;
        }
        
        if ( mediaIterator )
            IOObjectRelease( mediaIterator );
    }
#endif
    return bdrv_open2(bs, filename, snapshot, NULL);
}

int bdrv_open2(BlockDriverState *bs, const char *filename, int snapshot,
               BlockDriver *drv)
{
    int ret;
    char tmp_filename[1024];
    
    bs->read_only = 0;
    bs->is_temporary = 0;
    bs->encrypted = 0;

    if (snapshot) {
        BlockDriverState *bs1;
        int64_t total_size;
        
        /* if snapshot, we create a temporary backing file and open it
           instead of opening 'filename' directly */

        /* if there is a backing file, use it */
        bs1 = bdrv_new("");
        if (!bs1) {
            return -1;
        }
        if (bdrv_open(bs1, filename, 0) < 0) {
            bdrv_delete(bs1);
            return -1;
        }
        total_size = bs1->total_sectors;
        bdrv_delete(bs1);
        
        get_tmp_filename(tmp_filename, sizeof(tmp_filename));
        /* XXX: use cow for linux as it is more efficient ? */
        if (bdrv_create(&bdrv_qcow, tmp_filename, 
                        total_size, filename, 0) < 0) {
            return -1;
        }
        filename = tmp_filename;
        bs->is_temporary = 1;
    }

    pstrcpy(bs->filename, sizeof(bs->filename), filename);
    if (!drv) {
        drv = find_image_format(filename);
        if (!drv)
            return -1;
    }
    bs->drv = drv;
    bs->opaque = qemu_mallocz(drv->instance_size);
    if (bs->opaque == NULL && drv->instance_size > 0)
        return -1;
    
    ret = drv->bdrv_open(bs, filename);
    if (ret < 0) {
        qemu_free(bs->opaque);
        return -1;
    }
#ifndef _WIN32
    if (bs->is_temporary) {
        unlink(filename);
    }
#endif
    if (bs->backing_file[0] != '\0' && drv->bdrv_is_allocated) {
        /* if there is a backing file, use it */
        bs->backing_hd = bdrv_new("");
        if (!bs->backing_hd) {
        fail:
            bdrv_close(bs);
            return -1;
        }
        if (bdrv_open(bs->backing_hd, bs->backing_file, 0) < 0)
            goto fail;
    }

    bs->inserted = 1;

    /* call the change callback */
    if (bs->change_cb)
        bs->change_cb(bs->change_opaque);

    return 0;
}

void bdrv_close(BlockDriverState *bs)
{
    if (bs->inserted) {
        if (bs->backing_hd)
            bdrv_delete(bs->backing_hd);
        bs->drv->bdrv_close(bs);
        qemu_free(bs->opaque);
#ifdef _WIN32
        if (bs->is_temporary) {
            unlink(bs->filename);
        }
#endif
        bs->opaque = NULL;
        bs->drv = NULL;
        bs->inserted = 0;

        /* call the change callback */
        if (bs->change_cb)
            bs->change_cb(bs->change_opaque);
    }
}

void bdrv_delete(BlockDriverState *bs)
{
    /* XXX: remove the driver list */
    bdrv_close(bs);
    qemu_free(bs);
}

/* commit COW file into the raw image */
int bdrv_commit(BlockDriverState *bs)
{
    int64_t i;
    int n, j;
    unsigned char sector[512];

    if (!bs->inserted)
        return -ENOENT;

    if (bs->read_only) {
	return -EACCES;
    }

    if (!bs->backing_hd) {
	return -ENOTSUP;
    }

    for (i = 0; i < bs->total_sectors;) {
        if (bs->drv->bdrv_is_allocated(bs, i, 65536, &n)) {
            for(j = 0; j < n; j++) {
                if (bdrv_read(bs, i, sector, 1) != 0) {
                    return -EIO;
                }

                if (bdrv_write(bs->backing_hd, i, sector, 1) != 0) {
                    return -EIO;
                }
                i++;
	    }
	} else {
            i += n;
        }
    }

    if (bs->drv->bdrv_make_empty)
	return bs->drv->bdrv_make_empty(bs);

    return 0;
}

/* return -1 if error */
int bdrv_read(BlockDriverState *bs, int64_t sector_num, 
              uint8_t *buf, int nb_sectors)
{
    int ret, n;
    BlockDriver *drv = bs->drv;

    if (!bs->inserted)
        return -1;

    while (nb_sectors > 0) {
        if (sector_num == 0 && bs->boot_sector_enabled) {
            memcpy(buf, bs->boot_sector_data, 512);
            n = 1;
        } else if (bs->backing_hd) {
            if (drv->bdrv_is_allocated(bs, sector_num, nb_sectors, &n)) {
                ret = drv->bdrv_read(bs, sector_num, buf, n);
                if (ret < 0)
                    return -1;
            } else {
                /* read from the base image */
                ret = bdrv_read(bs->backing_hd, sector_num, buf, n);
                if (ret < 0)
                    return -1;
            }
        } else {
            ret = drv->bdrv_read(bs, sector_num, buf, nb_sectors);
            if (ret < 0)
                return -1;
            /* no need to loop */
            break;
        }
        nb_sectors -= n;
        sector_num += n;
        buf += n * 512;
    }
    return 0;
}

/* return -1 if error */
int bdrv_write(BlockDriverState *bs, int64_t sector_num, 
               const uint8_t *buf, int nb_sectors)
{
    if (!bs->inserted)
        return -1;
    if (bs->read_only)
        return -1;
    if (sector_num == 0 && bs->boot_sector_enabled && nb_sectors > 0) {
        memcpy(bs->boot_sector_data, buf, 512);   
    }
    return bs->drv->bdrv_write(bs, sector_num, buf, nb_sectors);
}

void bdrv_get_geometry(BlockDriverState *bs, int64_t *nb_sectors_ptr)
{
    *nb_sectors_ptr = bs->total_sectors;
}

/* force a given boot sector. */
void bdrv_set_boot_sector(BlockDriverState *bs, const uint8_t *data, int size)
{
    bs->boot_sector_enabled = 1;
    if (size > 512)
        size = 512;
    memcpy(bs->boot_sector_data, data, size);
    memset(bs->boot_sector_data + size, 0, 512 - size);
}

void bdrv_set_geometry_hint(BlockDriverState *bs, 
                            int cyls, int heads, int secs)
{
    bs->cyls = cyls;
    bs->heads = heads;
    bs->secs = secs;
}

void bdrv_set_type_hint(BlockDriverState *bs, int type)
{
    bs->type = type;
    bs->removable = ((type == BDRV_TYPE_CDROM ||
                      type == BDRV_TYPE_FLOPPY));
}

void bdrv_set_translation_hint(BlockDriverState *bs, int translation)
{
    bs->translation = translation;
}

void bdrv_get_geometry_hint(BlockDriverState *bs, 
                            int *pcyls, int *pheads, int *psecs)
{
    *pcyls = bs->cyls;
    *pheads = bs->heads;
    *psecs = bs->secs;
}

int bdrv_get_type_hint(BlockDriverState *bs)
{
    return bs->type;
}

int bdrv_get_translation_hint(BlockDriverState *bs)
{
    return bs->translation;
}

int bdrv_is_removable(BlockDriverState *bs)
{
    return bs->removable;
}

int bdrv_is_read_only(BlockDriverState *bs)
{
    return bs->read_only;
}

int bdrv_is_inserted(BlockDriverState *bs)
{
    return bs->inserted;
}

int bdrv_is_locked(BlockDriverState *bs)
{
    return bs->locked;
}

void bdrv_set_locked(BlockDriverState *bs, int locked)
{
    bs->locked = locked;
}

void bdrv_set_change_cb(BlockDriverState *bs, 
                        void (*change_cb)(void *opaque), void *opaque)
{
    bs->change_cb = change_cb;
    bs->change_opaque = opaque;
}

int bdrv_is_encrypted(BlockDriverState *bs)
{
    if (bs->backing_hd && bs->backing_hd->encrypted)
        return 1;
    return bs->encrypted;
}

int bdrv_set_key(BlockDriverState *bs, const char *key)
{
    int ret;
    if (bs->backing_hd && bs->backing_hd->encrypted) {
        ret = bdrv_set_key(bs->backing_hd, key);
        if (ret < 0)
            return ret;
        if (!bs->encrypted)
            return 0;
    }
    if (!bs->encrypted || !bs->drv || !bs->drv->bdrv_set_key)
        return -1;
    return bs->drv->bdrv_set_key(bs, key);
}

void bdrv_get_format(BlockDriverState *bs, char *buf, int buf_size)
{
    if (!bs->inserted || !bs->drv) {
        buf[0] = '\0';
    } else {
        pstrcpy(buf, buf_size, bs->drv->format_name);
    }
}

void bdrv_iterate_format(void (*it)(void *opaque, const char *name), 
                         void *opaque)
{
    BlockDriver *drv;

    for (drv = first_drv; drv != NULL; drv = drv->next) {
        it(opaque, drv->format_name);
    }
}

BlockDriverState *bdrv_find(const char *name)
{
    BlockDriverState *bs;

    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
        if (!strcmp(name, bs->device_name))
            return bs;
    }
    return NULL;
}

void bdrv_iterate(void (*it)(void *opaque, const char *name), void *opaque)
{
    BlockDriverState *bs;

    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
        it(opaque, bs->device_name);
    }
}

const char *bdrv_get_device_name(BlockDriverState *bs)
{
    return bs->device_name;
}

void bdrv_flush(BlockDriverState *bs)
{
    if (bs->drv->bdrv_flush)
        bs->drv->bdrv_flush(bs);
    if (bs->backing_hd)
        bdrv_flush(bs->backing_hd);
}

void bdrv_info(void)
{
    BlockDriverState *bs;

    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
        term_printf("%s:", bs->device_name);
        term_printf(" type=");
        switch(bs->type) {
        case BDRV_TYPE_HD:
            term_printf("hd");
            break;
        case BDRV_TYPE_CDROM:
            term_printf("cdrom");
            break;
        case BDRV_TYPE_FLOPPY:
            term_printf("floppy");
            break;
        }
        term_printf(" removable=%d", bs->removable);
        if (bs->removable) {
            term_printf(" locked=%d", bs->locked);
        }
        if (bs->inserted) {
            term_printf(" file=%s", bs->filename);
            if (bs->backing_file[0] != '\0')
                term_printf(" backing_file=%s", bs->backing_file);
            term_printf(" ro=%d", bs->read_only);
            term_printf(" drv=%s", bs->drv->format_name);
            if (bs->encrypted)
                term_printf(" encrypted");
        } else {
            term_printf(" [not inserted]");
        }
        term_printf("\n");
    }
}

/**************************************************************/
/* RAW block driver */

typedef struct BDRVRawState {
    int fd;
} BDRVRawState;

static int raw_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    return 1; /* maybe */
}

static int raw_open(BlockDriverState *bs, const char *filename)
{
    BDRVRawState *s = bs->opaque;
    int fd;
    int64_t size;
#ifdef _BSD
    struct stat sb;
#endif
#ifdef __sun__
    struct dk_minfo minfo;
    int rv;
#endif

    fd = open(filename, O_RDWR | O_BINARY | O_LARGEFILE);
    if (fd < 0) {
        fd = open(filename, O_RDONLY | O_BINARY | O_LARGEFILE);
        if (fd < 0)
            return -1;
        bs->read_only = 1;
    }
#ifdef _BSD
    if (!fstat(fd, &sb) && (S_IFCHR & sb.st_mode)) {
#ifdef DIOCGMEDIASIZE
	if (ioctl(fd, DIOCGMEDIASIZE, (off_t *)&size))
#endif
#ifdef CONFIG_COCOA
        size = LONG_LONG_MAX;
#else
        size = lseek(fd, 0LL, SEEK_END);
#endif
    } else
#endif
#ifdef __sun__
    /*
     * use the DKIOCGMEDIAINFO ioctl to read the size.
     */
    rv = ioctl ( fd, DKIOCGMEDIAINFO, &minfo );
    if ( rv != -1 ) {
        size = minfo.dki_lbsize * minfo.dki_capacity;
    } else /* there are reports that lseek on some devices
              fails, but irc discussion said that contingency
              on contingency was overkill */
#endif
    {
        size = lseek(fd, 0, SEEK_END);
    }
#ifdef _WIN32
    /* On Windows hosts it can happen that we're unable to get file size
       for CD-ROM raw device (it's inherent limitation of the CDFS driver). */
    if (size == -1)
        size = LONG_LONG_MAX;
#endif
    bs->total_sectors = size / 512;
    s->fd = fd;
    return 0;
}

static int raw_read(BlockDriverState *bs, int64_t sector_num, 
                    uint8_t *buf, int nb_sectors)
{
    BDRVRawState *s = bs->opaque;
    int ret;
    
    lseek(s->fd, sector_num * 512, SEEK_SET);
    ret = read(s->fd, buf, nb_sectors * 512);
    if (ret != nb_sectors * 512) 
        return -1;
    return 0;
}

static int raw_write(BlockDriverState *bs, int64_t sector_num, 
                     const uint8_t *buf, int nb_sectors)
{
    BDRVRawState *s = bs->opaque;
    int ret;
    
    lseek(s->fd, sector_num * 512, SEEK_SET);
    ret = write(s->fd, buf, nb_sectors * 512);
    if (ret != nb_sectors * 512) 
        return -1;
    return 0;
}

static void raw_close(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    close(s->fd);
}

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>

int qemu_ftruncate64(int fd, int64_t length)
{
    LARGE_INTEGER li;
    LONG high;
    HANDLE h;
    BOOL res;

    if ((GetVersion() & 0x80000000UL) && (length >> 32) != 0)
	return -1;

    h = (HANDLE)_get_osfhandle(fd);

    /* get current position, ftruncate do not change position */
    li.HighPart = 0;
    li.LowPart = SetFilePointer (h, 0, &li.HighPart, FILE_CURRENT);
    if (li.LowPart == 0xffffffffUL && GetLastError() != NO_ERROR)
	return -1;

    high = length >> 32;
    if (!SetFilePointer(h, (DWORD) length, &high, FILE_BEGIN))
	return -1;
    res = SetEndOfFile(h);

    /* back to old position */
    SetFilePointer(h, li.LowPart, &li.HighPart, FILE_BEGIN);
    return res ? 0 : -1;
}

static int set_sparse(int fd)
{
    DWORD returned;
    return (int) DeviceIoControl((HANDLE)_get_osfhandle(fd), FSCTL_SET_SPARSE,
				 NULL, 0, NULL, 0, &returned, NULL);
}
#else
static inline int set_sparse(int fd)
{
    return 1;
}
#endif

static int raw_create(const char *filename, int64_t total_size,
                      const char *backing_file, int flags)
{
    int fd;

    if (flags || backing_file)
        return -ENOTSUP;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_LARGEFILE, 
              0644);
    if (fd < 0)
        return -EIO;
    set_sparse(fd);
    ftruncate(fd, total_size * 512);
    close(fd);
    return 0;
}

static void raw_flush(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    fsync(s->fd);
}

BlockDriver bdrv_raw = {
    "raw",
    sizeof(BDRVRawState),
    raw_probe,
    raw_open,
    raw_read,
    raw_write,
    raw_close,
    raw_create,
    raw_flush,
};

void bdrv_init(void)
{
    bdrv_register(&bdrv_raw);
#ifndef _WIN32
    bdrv_register(&bdrv_cow);
#endif
    bdrv_register(&bdrv_qcow);
    bdrv_register(&bdrv_vmdk);
    bdrv_register(&bdrv_cloop);
    bdrv_register(&bdrv_dmg);
    bdrv_register(&bdrv_bochs);
    bdrv_register(&bdrv_vpc);
    bdrv_register(&bdrv_vvfat);
}
