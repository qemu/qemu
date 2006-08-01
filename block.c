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

#define SECTOR_BITS 9
#define SECTOR_SIZE (1 << SECTOR_BITS)

static int bdrv_aio_new_em(BlockDriverAIOCB *acb);
static int bdrv_aio_read_em(BlockDriverAIOCB *acb, int64_t sector_num,
                              uint8_t *buf, int nb_sectors);
static int bdrv_aio_write_em(BlockDriverAIOCB *acb, int64_t sector_num,
                               const uint8_t *buf, int nb_sectors);
static void bdrv_aio_cancel_em(BlockDriverAIOCB *acb);
static void bdrv_aio_delete_em(BlockDriverAIOCB *acb);
static int bdrv_read_em(BlockDriverState *bs, int64_t sector_num, 
                        uint8_t *buf, int nb_sectors);
static int bdrv_write_em(BlockDriverState *bs, int64_t sector_num,
                         const uint8_t *buf, int nb_sectors);

static BlockDriverState *bdrv_first;
static BlockDriver *first_drv;

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

int path_is_absolute(const char *path)
{
    const char *p;
    p = strchr(path, ':');
    if (p)
        p++;
    else
        p = path;
    return (*p == PATH_SEP);
}

/* if filename is absolute, just copy it to dest. Otherwise, build a
   path to it by considering it is relative to base_path. URL are
   supported. */
void path_combine(char *dest, int dest_size,
                  const char *base_path,
                  const char *filename)
{
    const char *p, *p1;
    int len;

    if (dest_size <= 0)
        return;
    if (path_is_absolute(filename)) {
        pstrcpy(dest, dest_size, filename);
    } else {
        p = strchr(base_path, ':');
        if (p)
            p++;
        else
            p = base_path;
        p1 = strrchr(base_path, PATH_SEP);
        if (p1)
            p1++;
        else
            p1 = base_path;
        if (p1 > p)
            p = p1;
        len = p - base_path;
        if (len > dest_size - 1)
            len = dest_size - 1;
        memcpy(dest, base_path, len);
        dest[len] = '\0';
        pstrcat(dest, dest_size, filename);
    }
}


void bdrv_register(BlockDriver *bdrv)
{
    if (!bdrv->bdrv_aio_new) {
        /* add AIO emulation layer */
        bdrv->bdrv_aio_new = bdrv_aio_new_em;
        bdrv->bdrv_aio_read = bdrv_aio_read_em;
        bdrv->bdrv_aio_write = bdrv_aio_write_em;
        bdrv->bdrv_aio_cancel = bdrv_aio_cancel_em;
        bdrv->bdrv_aio_delete = bdrv_aio_delete_em;
    } else if (!bdrv->bdrv_read && !bdrv->bdrv_pread) {
        /* add synchronous IO emulation layer */
        bdrv->bdrv_read = bdrv_read_em;
        bdrv->bdrv_write = bdrv_write_em;
    }
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
    tmpnam(filename);
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

static BlockDriver *find_protocol(const char *filename)
{
    BlockDriver *drv1;
    char protocol[128];
    int len;
    const char *p;
    p = strchr(filename, ':');
    if (!p)
        return &bdrv_raw;
    len = p - filename;
    if (len > sizeof(protocol) - 1)
        len = sizeof(protocol) - 1;
#ifdef _WIN32
    if (len == 1) {
        /* specific win32 case for driver letters */
        return &bdrv_raw;
    }
#endif   
    memcpy(protocol, filename, len);
    protocol[len] = '\0';
    for(drv1 = first_drv; drv1 != NULL; drv1 = drv1->next) {
        if (drv1->protocol_name && 
            !strcmp(drv1->protocol_name, protocol))
            return drv1;
    }
    return NULL;
}

/* XXX: force raw format if block or character device ? It would
   simplify the BSD case */
static BlockDriver *find_image_format(const char *filename)
{
    int ret, score, score_max;
    BlockDriver *drv1, *drv;
    uint8_t buf[2048];
    BlockDriverState *bs;
    
    drv = find_protocol(filename);
    /* no need to test disk image formats for vvfat or host specific
       devices */
    if (drv == &bdrv_vvfat)
        return drv;
    if (strstart(filename, "/dev/", NULL))
        return &bdrv_raw;
    
    ret = bdrv_file_open(&bs, filename, BDRV_O_RDONLY);
    if (ret < 0)
        return NULL;
    ret = bdrv_pread(bs, 0, buf, sizeof(buf));
    bdrv_delete(bs);
    if (ret < 0) {
        return NULL;
    }

    score_max = 0;
    for(drv1 = first_drv; drv1 != NULL; drv1 = drv1->next) {
        if (drv1->bdrv_probe) {
            score = drv1->bdrv_probe(buf, ret, filename);
            if (score > score_max) {
                score_max = score;
                drv = drv1;
            }
        }
    }
    return drv;
}

int bdrv_file_open(BlockDriverState **pbs, const char *filename, int flags)
{
    BlockDriverState *bs;
    int ret;

    bs = bdrv_new("");
    if (!bs)
        return -ENOMEM;
    ret = bdrv_open2(bs, filename, flags | BDRV_O_FILE, NULL);
    if (ret < 0) {
        bdrv_delete(bs);
        return ret;
    }
    *pbs = bs;
    return 0;
}

int bdrv_open(BlockDriverState *bs, const char *filename, int flags)
{
    return bdrv_open2(bs, filename, flags, NULL);
}

int bdrv_open2(BlockDriverState *bs, const char *filename, int flags,
               BlockDriver *drv)
{
    int ret, open_flags;
    char tmp_filename[1024];
    char backing_filename[1024];
    
    bs->read_only = 0;
    bs->is_temporary = 0;
    bs->encrypted = 0;

    if (flags & BDRV_O_SNAPSHOT) {
        BlockDriverState *bs1;
        int64_t total_size;
        
        /* if snapshot, we create a temporary backing file and open it
           instead of opening 'filename' directly */

        /* if there is a backing file, use it */
        bs1 = bdrv_new("");
        if (!bs1) {
            return -ENOMEM;
        }
        if (bdrv_open(bs1, filename, 0) < 0) {
            bdrv_delete(bs1);
            return -1;
        }
        total_size = bdrv_getlength(bs1) >> SECTOR_BITS;
        bdrv_delete(bs1);
        
        get_tmp_filename(tmp_filename, sizeof(tmp_filename));
        if (bdrv_create(&bdrv_qcow, tmp_filename, 
                        total_size, filename, 0) < 0) {
            return -1;
        }
        filename = tmp_filename;
        bs->is_temporary = 1;
    }

    pstrcpy(bs->filename, sizeof(bs->filename), filename);
    if (flags & BDRV_O_FILE) {
        drv = find_protocol(filename);
        if (!drv)
            return -ENOENT;
    } else {
        if (!drv) {
            drv = find_image_format(filename);
            if (!drv)
                return -1;
        }
    }
    bs->drv = drv;
    bs->opaque = qemu_mallocz(drv->instance_size);
    if (bs->opaque == NULL && drv->instance_size > 0)
        return -1;
    /* Note: for compatibility, we open disk image files as RDWR, and
       RDONLY as fallback */
    if (!(flags & BDRV_O_FILE))
        open_flags = BDRV_O_RDWR;
    else
        open_flags = flags & ~(BDRV_O_FILE | BDRV_O_SNAPSHOT);
    ret = drv->bdrv_open(bs, filename, open_flags);
    if (ret == -EACCES && !(flags & BDRV_O_FILE)) {
        ret = drv->bdrv_open(bs, filename, BDRV_O_RDONLY);
        bs->read_only = 1;
    }
    if (ret < 0) {
        qemu_free(bs->opaque);
        return ret;
    }

#ifndef _WIN32
    if (bs->is_temporary) {
        unlink(filename);
    }
#endif
    if (bs->backing_file[0] != '\0') {
        /* if there is a backing file, use it */
        bs->backing_hd = bdrv_new("");
        if (!bs->backing_hd) {
        fail:
            bdrv_close(bs);
            return -1;
        }
        path_combine(backing_filename, sizeof(backing_filename),
                     filename, bs->backing_file);
        if (bdrv_open(bs->backing_hd, backing_filename, 0) < 0)
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
    int64_t i, total_sectors;
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

    total_sectors = bdrv_getlength(bs) >> SECTOR_BITS;
    for (i = 0; i < total_sectors;) {
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

/* return < 0 if error */
int bdrv_read(BlockDriverState *bs, int64_t sector_num, 
              uint8_t *buf, int nb_sectors)
{
    BlockDriver *drv = bs->drv;

    if (!bs->inserted)
        return -1;

    if (sector_num == 0 && bs->boot_sector_enabled && nb_sectors > 0) {
            memcpy(buf, bs->boot_sector_data, 512);
        sector_num++;
        nb_sectors--;
        buf += 512;
        if (nb_sectors == 0)
            return 0;
    }
    if (drv->bdrv_pread) {
        int ret, len;
        len = nb_sectors * 512;
        ret = drv->bdrv_pread(bs, sector_num * 512, buf, len);
        if (ret < 0)
            return ret;
        else if (ret != len)
            return -EIO;
        else
            return 0;
    } else {
        return drv->bdrv_read(bs, sector_num, buf, nb_sectors);
    }
}

/* return < 0 if error */
int bdrv_write(BlockDriverState *bs, int64_t sector_num, 
               const uint8_t *buf, int nb_sectors)
{
    BlockDriver *drv = bs->drv;
    if (!bs->inserted)
        return -1;
    if (bs->read_only)
        return -1;
    if (sector_num == 0 && bs->boot_sector_enabled && nb_sectors > 0) {
        memcpy(bs->boot_sector_data, buf, 512);   
    }
    if (drv->bdrv_pwrite) {
        int ret, len;
        len = nb_sectors * 512;
        ret = drv->bdrv_pwrite(bs, sector_num * 512, buf, len);
        if (ret < 0)
            return ret;
        else if (ret != len)
            return -EIO;
        else
            return 0;
    } else {
        return drv->bdrv_write(bs, sector_num, buf, nb_sectors);
    }
}

#if 0
/* not necessary now */
static int bdrv_pread_em(BlockDriverState *bs, int64_t offset, 
                         void *buf1, int count1)
{
    uint8_t *buf = buf1;
    uint8_t tmp_buf[SECTOR_SIZE];
    int len, nb_sectors, count;
    int64_t sector_num;

    count = count1;
    /* first read to align to sector start */
    len = (SECTOR_SIZE - offset) & (SECTOR_SIZE - 1);
    if (len > count)
        len = count;
    sector_num = offset >> SECTOR_BITS;
    if (len > 0) {
        if (bdrv_read(bs, sector_num, tmp_buf, 1) < 0)
            return -EIO;
        memcpy(buf, tmp_buf + (offset & (SECTOR_SIZE - 1)), len);
        count -= len;
        if (count == 0)
            return count1;
        sector_num++;
        buf += len;
    }

    /* read the sectors "in place" */
    nb_sectors = count >> SECTOR_BITS;
    if (nb_sectors > 0) {
        if (bdrv_read(bs, sector_num, buf, nb_sectors) < 0)
            return -EIO;
        sector_num += nb_sectors;
        len = nb_sectors << SECTOR_BITS;
        buf += len;
        count -= len;
    }

    /* add data from the last sector */
    if (count > 0) {
        if (bdrv_read(bs, sector_num, tmp_buf, 1) < 0)
            return -EIO;
        memcpy(buf, tmp_buf, count);
    }
    return count1;
}

static int bdrv_pwrite_em(BlockDriverState *bs, int64_t offset, 
                          const void *buf1, int count1)
{
    const uint8_t *buf = buf1;
    uint8_t tmp_buf[SECTOR_SIZE];
    int len, nb_sectors, count;
    int64_t sector_num;

    count = count1;
    /* first write to align to sector start */
    len = (SECTOR_SIZE - offset) & (SECTOR_SIZE - 1);
    if (len > count)
        len = count;
    sector_num = offset >> SECTOR_BITS;
    if (len > 0) {
        if (bdrv_read(bs, sector_num, tmp_buf, 1) < 0)
            return -EIO;
        memcpy(tmp_buf + (offset & (SECTOR_SIZE - 1)), buf, len);
        if (bdrv_write(bs, sector_num, tmp_buf, 1) < 0)
            return -EIO;
        count -= len;
        if (count == 0)
            return count1;
        sector_num++;
        buf += len;
    }

    /* write the sectors "in place" */
    nb_sectors = count >> SECTOR_BITS;
    if (nb_sectors > 0) {
        if (bdrv_write(bs, sector_num, buf, nb_sectors) < 0)
            return -EIO;
        sector_num += nb_sectors;
        len = nb_sectors << SECTOR_BITS;
        buf += len;
        count -= len;
    }

    /* add data from the last sector */
    if (count > 0) {
        if (bdrv_read(bs, sector_num, tmp_buf, 1) < 0)
            return -EIO;
        memcpy(tmp_buf, buf, count);
        if (bdrv_write(bs, sector_num, tmp_buf, 1) < 0)
            return -EIO;
    }
    return count1;
}
#endif

/**
 * Read with byte offsets (needed only for file protocols) 
 */
int bdrv_pread(BlockDriverState *bs, int64_t offset, 
               void *buf1, int count1)
{
    BlockDriver *drv = bs->drv;

    if (!drv)
        return -ENOENT;
    if (!drv->bdrv_pread)
        return -ENOTSUP;
    return drv->bdrv_pread(bs, offset, buf1, count1);
}

/** 
 * Write with byte offsets (needed only for file protocols) 
 */
int bdrv_pwrite(BlockDriverState *bs, int64_t offset, 
                const void *buf1, int count1)
{
    BlockDriver *drv = bs->drv;

    if (!drv)
        return -ENOENT;
    if (!drv->bdrv_pwrite)
        return -ENOTSUP;
    return drv->bdrv_pwrite(bs, offset, buf1, count1);
}

/**
 * Truncate file to 'offset' bytes (needed only for file protocols)
 */
int bdrv_truncate(BlockDriverState *bs, int64_t offset)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOENT;
    if (!drv->bdrv_truncate)
        return -ENOTSUP;
    return drv->bdrv_truncate(bs, offset);
}

/**
 * Length of a file in bytes. Return < 0 if error or unknown.
 */
int64_t bdrv_getlength(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOENT;
    if (!drv->bdrv_getlength) {
        /* legacy mode */
        return bs->total_sectors * SECTOR_SIZE;
    }
    return drv->bdrv_getlength(bs);
}

void bdrv_get_geometry(BlockDriverState *bs, int64_t *nb_sectors_ptr)
{
    int64_t size;
    size = bdrv_getlength(bs);
    if (size < 0)
        size = 0;
    *nb_sectors_ptr = size >> SECTOR_BITS;
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

void bdrv_get_backing_filename(BlockDriverState *bs, 
                               char *filename, int filename_size)
{
    if (!bs->backing_hd) {
        pstrcpy(filename, filename_size, "");
    } else {
        pstrcpy(filename, filename_size, bs->backing_file);
    }
}


/**************************************************************/
/* async I/Os */

BlockDriverAIOCB *bdrv_aio_new(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    BlockDriverAIOCB *acb;
    acb = qemu_mallocz(sizeof(BlockDriverAIOCB));
    if (!acb)
        return NULL;
    
    acb->bs = bs;
    if (drv->bdrv_aio_new(acb) < 0) {
        qemu_free(acb);
        return NULL;
    }
    return acb;
}

int bdrv_aio_read(BlockDriverAIOCB *acb, int64_t sector_num,
                  uint8_t *buf, int nb_sectors,
                  BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriverState *bs = acb->bs;
    BlockDriver *drv = bs->drv;

    if (!bs->inserted)
        return -1;
    
    /* XXX: we assume that nb_sectors == 0 is suppored by the async read */
    if (sector_num == 0 && bs->boot_sector_enabled && nb_sectors > 0) {
        memcpy(buf, bs->boot_sector_data, 512);
        sector_num++;
        nb_sectors--;
        buf += 512;
    }

    acb->cb = cb;
    acb->cb_opaque = opaque;
    return drv->bdrv_aio_read(acb, sector_num, buf, nb_sectors);
}

int bdrv_aio_write(BlockDriverAIOCB *acb, int64_t sector_num,
                   const uint8_t *buf, int nb_sectors,
                   BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriverState *bs = acb->bs;
    BlockDriver *drv = bs->drv;

    if (!bs->inserted)
            return -1;
    if (bs->read_only)
        return -1;
    if (sector_num == 0 && bs->boot_sector_enabled && nb_sectors > 0) {
        memcpy(bs->boot_sector_data, buf, 512);   
    }

    acb->cb = cb;
    acb->cb_opaque = opaque;
    return drv->bdrv_aio_write(acb, sector_num, buf, nb_sectors);
}

void bdrv_aio_cancel(BlockDriverAIOCB *acb)
    {
    BlockDriverState *bs = acb->bs;
    BlockDriver *drv = bs->drv;

    drv->bdrv_aio_cancel(acb);
    }

void bdrv_aio_delete(BlockDriverAIOCB *acb)
{
    BlockDriverState *bs = acb->bs;
    BlockDriver *drv = bs->drv;

    drv->bdrv_aio_delete(acb);
    qemu_free(acb);
}

/**************************************************************/
/* async block device emulation */

#ifdef QEMU_TOOL
static int bdrv_aio_new_em(BlockDriverAIOCB *acb)
{
    return 0;
}

static int bdrv_aio_read_em(BlockDriverAIOCB *acb, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    int ret;
    ret = bdrv_read(acb->bs, sector_num, buf, nb_sectors);
    acb->cb(acb->cb_opaque, ret);
    return 0;
}

static int bdrv_aio_write_em(BlockDriverAIOCB *acb, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    int ret;
    ret = bdrv_write(acb->bs, sector_num, buf, nb_sectors);
    acb->cb(acb->cb_opaque, ret);
    return 0;
}

static void bdrv_aio_cancel_em(BlockDriverAIOCB *acb)
{
}

static void bdrv_aio_delete_em(BlockDriverAIOCB *acb)
{
}
#else
typedef struct BlockDriverAIOCBSync {
    QEMUBH *bh;
    int ret;
} BlockDriverAIOCBSync;

static void bdrv_aio_bh_cb(void *opaque)
{
    BlockDriverAIOCB *acb = opaque;
    BlockDriverAIOCBSync *acb1 = acb->opaque;
    acb->cb(acb->cb_opaque, acb1->ret);
}

static int bdrv_aio_new_em(BlockDriverAIOCB *acb)
{
    BlockDriverAIOCBSync *acb1;

    acb1 = qemu_mallocz(sizeof(BlockDriverAIOCBSync));
    if (!acb1)
        return -1;
    acb->opaque = acb1;
    acb1->bh = qemu_bh_new(bdrv_aio_bh_cb, acb);
    return 0;
}

static int bdrv_aio_read_em(BlockDriverAIOCB *acb, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    BlockDriverAIOCBSync *acb1 = acb->opaque;
    int ret;
    
    ret = bdrv_read(acb->bs, sector_num, buf, nb_sectors);
    acb1->ret = ret;
    qemu_bh_schedule(acb1->bh);
    return 0;
}

static int bdrv_aio_write_em(BlockDriverAIOCB *acb, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    BlockDriverAIOCBSync *acb1 = acb->opaque;
    int ret;
    
    ret = bdrv_write(acb->bs, sector_num, buf, nb_sectors);
    acb1->ret = ret;
    qemu_bh_schedule(acb1->bh);
    return 0;
}

static void bdrv_aio_cancel_em(BlockDriverAIOCB *acb)
{
    BlockDriverAIOCBSync *acb1 = acb->opaque;
    qemu_bh_cancel(acb1->bh);
}

static void bdrv_aio_delete_em(BlockDriverAIOCB *acb)
{
    BlockDriverAIOCBSync *acb1 = acb->opaque;
    qemu_bh_delete(acb1->bh);
}
#endif /* !QEMU_TOOL */

/**************************************************************/
/* sync block device emulation */

static void bdrv_rw_em_cb(void *opaque, int ret)
{
    *(int *)opaque = ret;
}

#define NOT_DONE 0x7fffffff

static int bdrv_read_em(BlockDriverState *bs, int64_t sector_num, 
                        uint8_t *buf, int nb_sectors)
{
    int async_ret, ret;

    if (!bs->sync_aiocb) {
        bs->sync_aiocb = bdrv_aio_new(bs);
        if (!bs->sync_aiocb)
            return -1;
    }
    async_ret = NOT_DONE;
    qemu_aio_wait_start();
    ret = bdrv_aio_read(bs->sync_aiocb, sector_num, buf, nb_sectors, 
                        bdrv_rw_em_cb, &async_ret);
    if (ret < 0) {
        qemu_aio_wait_end();
        return ret;
    }
    while (async_ret == NOT_DONE) {
        qemu_aio_wait();
    }
    qemu_aio_wait_end();
    return async_ret;
}

static int bdrv_write_em(BlockDriverState *bs, int64_t sector_num,
                         const uint8_t *buf, int nb_sectors)
{
    int async_ret, ret;

    if (!bs->sync_aiocb) {
        bs->sync_aiocb = bdrv_aio_new(bs);
        if (!bs->sync_aiocb)
            return -1;
    }
    async_ret = NOT_DONE;
    qemu_aio_wait_start();
    ret = bdrv_aio_write(bs->sync_aiocb, sector_num, buf, nb_sectors, 
                         bdrv_rw_em_cb, &async_ret);
    if (ret < 0) {
        qemu_aio_wait_end();
        return ret;
    }
    while (async_ret == NOT_DONE) {
        qemu_aio_wait();
    }
    qemu_aio_wait_end();
    return async_ret;
}

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
