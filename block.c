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
#include "qemu-common.h"
#ifndef QEMU_IMG
#include "console.h"
#endif
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

typedef struct BlockDriverAIOCBSync {
    BlockDriverAIOCB common;
    QEMUBH *bh;
    int ret;
} BlockDriverAIOCBSync;

static BlockDriverAIOCB *bdrv_aio_read_em(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
static BlockDriverAIOCB *bdrv_aio_write_em(BlockDriverState *bs,
        int64_t sector_num, const uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
static void bdrv_aio_cancel_em(BlockDriverAIOCB *acb);
static int bdrv_read_em(BlockDriverState *bs, int64_t sector_num,
                        uint8_t *buf, int nb_sectors);
static int bdrv_write_em(BlockDriverState *bs, int64_t sector_num,
                         const uint8_t *buf, int nb_sectors);

BlockDriverState *bdrv_first;
static BlockDriver *first_drv;

int path_is_absolute(const char *path)
{
    const char *p;
#ifdef _WIN32
    /* specific case for names like: "\\.\d:" */
    if (*path == '/' || *path == '\\')
        return 1;
#endif
    p = strchr(path, ':');
    if (p)
        p++;
    else
        p = path;
#ifdef _WIN32
    return (*p == '/' || *p == '\\');
#else
    return (*p == '/');
#endif
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
        p1 = strrchr(base_path, '/');
#ifdef _WIN32
        {
            const char *p2;
            p2 = strrchr(base_path, '\\');
            if (!p1 || p2 > p1)
                p1 = p2;
        }
#endif
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


static void bdrv_register(BlockDriver *bdrv)
{
    if (!bdrv->bdrv_aio_read) {
        /* add AIO emulation layer */
        bdrv->bdrv_aio_read = bdrv_aio_read_em;
        bdrv->bdrv_aio_write = bdrv_aio_write_em;
        bdrv->bdrv_aio_cancel = bdrv_aio_cancel_em;
        bdrv->aiocb_size = sizeof(BlockDriverAIOCBSync);
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
    char temp_dir[MAX_PATH];

    GetTempPath(MAX_PATH, temp_dir);
    GetTempFileName(temp_dir, "qem", 0, filename);
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

#ifdef _WIN32
static int is_windows_drive_prefix(const char *filename)
{
    return (((filename[0] >= 'a' && filename[0] <= 'z') ||
             (filename[0] >= 'A' && filename[0] <= 'Z')) &&
            filename[1] == ':');
}

static int is_windows_drive(const char *filename)
{
    if (is_windows_drive_prefix(filename) &&
        filename[2] == '\0')
        return 1;
    if (strstart(filename, "\\\\.\\", NULL) ||
        strstart(filename, "//./", NULL))
        return 1;
    return 0;
}
#endif

static BlockDriver *find_protocol(const char *filename)
{
    BlockDriver *drv1;
    char protocol[128];
    int len;
    const char *p;

#ifdef _WIN32
    if (is_windows_drive(filename) ||
        is_windows_drive_prefix(filename))
        return &bdrv_raw;
#endif
    p = strchr(filename, ':');
    if (!p)
        return &bdrv_raw;
    len = p - filename;
    if (len > sizeof(protocol) - 1)
        len = sizeof(protocol) - 1;
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

    /* detect host devices. By convention, /dev/cdrom[N] is always
       recognized as a host CDROM */
    if (strstart(filename, "/dev/cdrom", NULL))
        return &bdrv_host_device;
#ifdef _WIN32
    if (is_windows_drive(filename))
        return &bdrv_host_device;
#else
    {
        struct stat st;
        if (stat(filename, &st) >= 0 &&
            (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))) {
            return &bdrv_host_device;
        }
    }
#endif

    drv = find_protocol(filename);
    /* no need to test disk image formats for vvfat */
    if (drv == &bdrv_vvfat)
        return drv;

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
    char tmp_filename[PATH_MAX];
    char backing_filename[PATH_MAX];

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
        realpath(filename, backing_filename);
        if (bdrv_create(&bdrv_qcow2, tmp_filename,
                        total_size, backing_filename, 0) < 0) {
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
        open_flags = BDRV_O_RDWR | (flags & BDRV_O_DIRECT);
    else
        open_flags = flags & ~(BDRV_O_FILE | BDRV_O_SNAPSHOT);
    ret = drv->bdrv_open(bs, filename, open_flags);
    if (ret == -EACCES && !(flags & BDRV_O_FILE)) {
        ret = drv->bdrv_open(bs, filename, BDRV_O_RDONLY);
        bs->read_only = 1;
    }
    if (ret < 0) {
        qemu_free(bs->opaque);
        bs->opaque = NULL;
        bs->drv = NULL;
        return ret;
    }
    if (drv->bdrv_getlength) {
        bs->total_sectors = bdrv_getlength(bs) >> SECTOR_BITS;
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
            return -ENOMEM;
        }
        path_combine(backing_filename, sizeof(backing_filename),
                     filename, bs->backing_file);
        if (bdrv_open(bs->backing_hd, backing_filename, 0) < 0)
            goto fail;
    }

    /* call the change callback */
    bs->media_changed = 1;
    if (bs->change_cb)
        bs->change_cb(bs->change_opaque);

    return 0;
}

void bdrv_close(BlockDriverState *bs)
{
    if (bs->drv) {
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

        /* call the change callback */
        bs->media_changed = 1;
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
    BlockDriver *drv = bs->drv;
    int64_t i, total_sectors;
    int n, j;
    unsigned char sector[512];

    if (!drv)
        return -ENOMEDIUM;

    if (bs->read_only) {
	return -EACCES;
    }

    if (!bs->backing_hd) {
	return -ENOTSUP;
    }

    total_sectors = bdrv_getlength(bs) >> SECTOR_BITS;
    for (i = 0; i < total_sectors;) {
        if (drv->bdrv_is_allocated(bs, i, 65536, &n)) {
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

    if (drv->bdrv_make_empty)
	return drv->bdrv_make_empty(bs);

    return 0;
}

/* return < 0 if error. See bdrv_write() for the return codes */
int bdrv_read(BlockDriverState *bs, int64_t sector_num,
              uint8_t *buf, int nb_sectors)
{
    BlockDriver *drv = bs->drv;

    if (!drv)
        return -ENOMEDIUM;

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
            return -EINVAL;
        else {
	    bs->rd_bytes += (unsigned) len;
	    bs->rd_ops ++;
            return 0;
	}
    } else {
        return drv->bdrv_read(bs, sector_num, buf, nb_sectors);
    }
}

/* Return < 0 if error. Important errors are:
  -EIO         generic I/O error (may happen for all errors)
  -ENOMEDIUM   No media inserted.
  -EINVAL      Invalid sector number or nb_sectors
  -EACCES      Trying to write a read-only device
*/
int bdrv_write(BlockDriverState *bs, int64_t sector_num,
               const uint8_t *buf, int nb_sectors)
{
    BlockDriver *drv = bs->drv;
    if (!bs->drv)
        return -ENOMEDIUM;
    if (bs->read_only)
        return -EACCES;
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
        else {
	    bs->wr_bytes += (unsigned) len;
	    bs->wr_ops ++;
            return 0;
	}
    } else {
        return drv->bdrv_write(bs, sector_num, buf, nb_sectors);
    }
}

static int bdrv_pread_em(BlockDriverState *bs, int64_t offset,
                         uint8_t *buf, int count1)
{
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
                          const uint8_t *buf, int count1)
{
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

/**
 * Read with byte offsets (needed only for file protocols)
 */
int bdrv_pread(BlockDriverState *bs, int64_t offset,
               void *buf1, int count1)
{
    BlockDriver *drv = bs->drv;

    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_pread)
        return bdrv_pread_em(bs, offset, buf1, count1);
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
        return -ENOMEDIUM;
    if (!drv->bdrv_pwrite)
        return bdrv_pwrite_em(bs, offset, buf1, count1);
    return drv->bdrv_pwrite(bs, offset, buf1, count1);
}

/**
 * Truncate file to 'offset' bytes (needed only for file protocols)
 */
int bdrv_truncate(BlockDriverState *bs, int64_t offset)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
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
        return -ENOMEDIUM;
    if (!drv->bdrv_getlength) {
        /* legacy mode */
        return bs->total_sectors * SECTOR_SIZE;
    }
    return drv->bdrv_getlength(bs);
}

/* return 0 as number of sectors if no device present or error */
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr)
{
    int64_t length;
    length = bdrv_getlength(bs);
    if (length < 0)
        length = 0;
    else
        length = length >> SECTOR_BITS;
    *nb_sectors_ptr = length;
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

int bdrv_is_sg(BlockDriverState *bs)
{
    return bs->sg;
}

/* XXX: no longer used */
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
    if (!bs->drv) {
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

#ifndef QEMU_IMG
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
        if (bs->drv) {
            term_printf(" file=");
	    term_print_filename(bs->filename);
            if (bs->backing_file[0] != '\0') {
                term_printf(" backing_file=");
		term_print_filename(bs->backing_file);
	    }
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

/* The "info blockstats" command. */
void bdrv_info_stats (void)
{
    BlockDriverState *bs;

    for (bs = bdrv_first; bs != NULL; bs = bs->next) {
	term_printf ("%s:"
		     " rd_bytes=%" PRIu64
		     " wr_bytes=%" PRIu64
		     " rd_operations=%" PRIu64
		     " wr_operations=%" PRIu64
		     "\n",
		     bs->device_name,
		     bs->rd_bytes, bs->wr_bytes,
		     bs->rd_ops, bs->wr_ops);
    }
}
#endif

void bdrv_get_backing_filename(BlockDriverState *bs,
                               char *filename, int filename_size)
{
    if (!bs->backing_hd) {
        pstrcpy(filename, filename_size, "");
    } else {
        pstrcpy(filename, filename_size, bs->backing_file);
    }
}

int bdrv_write_compressed(BlockDriverState *bs, int64_t sector_num,
                          const uint8_t *buf, int nb_sectors)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_write_compressed)
        return -ENOTSUP;
    return drv->bdrv_write_compressed(bs, sector_num, buf, nb_sectors);
}

int bdrv_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_get_info)
        return -ENOTSUP;
    memset(bdi, 0, sizeof(*bdi));
    return drv->bdrv_get_info(bs, bdi);
}

/**************************************************************/
/* handling of snapshots */

int bdrv_snapshot_create(BlockDriverState *bs,
                         QEMUSnapshotInfo *sn_info)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_snapshot_create)
        return -ENOTSUP;
    return drv->bdrv_snapshot_create(bs, sn_info);
}

int bdrv_snapshot_goto(BlockDriverState *bs,
                       const char *snapshot_id)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_snapshot_goto)
        return -ENOTSUP;
    return drv->bdrv_snapshot_goto(bs, snapshot_id);
}

int bdrv_snapshot_delete(BlockDriverState *bs, const char *snapshot_id)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_snapshot_delete)
        return -ENOTSUP;
    return drv->bdrv_snapshot_delete(bs, snapshot_id);
}

int bdrv_snapshot_list(BlockDriverState *bs,
                       QEMUSnapshotInfo **psn_info)
{
    BlockDriver *drv = bs->drv;
    if (!drv)
        return -ENOMEDIUM;
    if (!drv->bdrv_snapshot_list)
        return -ENOTSUP;
    return drv->bdrv_snapshot_list(bs, psn_info);
}

#define NB_SUFFIXES 4

char *get_human_readable_size(char *buf, int buf_size, int64_t size)
{
    static const char suffixes[NB_SUFFIXES] = "KMGT";
    int64_t base;
    int i;

    if (size <= 999) {
        snprintf(buf, buf_size, "%" PRId64, size);
    } else {
        base = 1024;
        for(i = 0; i < NB_SUFFIXES; i++) {
            if (size < (10 * base)) {
                snprintf(buf, buf_size, "%0.1f%c",
                         (double)size / base,
                         suffixes[i]);
                break;
            } else if (size < (1000 * base) || i == (NB_SUFFIXES - 1)) {
                snprintf(buf, buf_size, "%" PRId64 "%c",
                         ((size + (base >> 1)) / base),
                         suffixes[i]);
                break;
            }
            base = base * 1024;
        }
    }
    return buf;
}

char *bdrv_snapshot_dump(char *buf, int buf_size, QEMUSnapshotInfo *sn)
{
    char buf1[128], date_buf[128], clock_buf[128];
#ifdef _WIN32
    struct tm *ptm;
#else
    struct tm tm;
#endif
    time_t ti;
    int64_t secs;

    if (!sn) {
        snprintf(buf, buf_size,
                 "%-10s%-20s%7s%20s%15s",
                 "ID", "TAG", "VM SIZE", "DATE", "VM CLOCK");
    } else {
        ti = sn->date_sec;
#ifdef _WIN32
        ptm = localtime(&ti);
        strftime(date_buf, sizeof(date_buf),
                 "%Y-%m-%d %H:%M:%S", ptm);
#else
        localtime_r(&ti, &tm);
        strftime(date_buf, sizeof(date_buf),
                 "%Y-%m-%d %H:%M:%S", &tm);
#endif
        secs = sn->vm_clock_nsec / 1000000000;
        snprintf(clock_buf, sizeof(clock_buf),
                 "%02d:%02d:%02d.%03d",
                 (int)(secs / 3600),
                 (int)((secs / 60) % 60),
                 (int)(secs % 60),
                 (int)((sn->vm_clock_nsec / 1000000) % 1000));
        snprintf(buf, buf_size,
                 "%-10s%-20s%7s%20s%15s",
                 sn->id_str, sn->name,
                 get_human_readable_size(buf1, sizeof(buf1), sn->vm_state_size),
                 date_buf,
                 clock_buf);
    }
    return buf;
}


/**************************************************************/
/* async I/Os */

BlockDriverAIOCB *bdrv_aio_read(BlockDriverState *bs, int64_t sector_num,
                                uint8_t *buf, int nb_sectors,
                                BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriver *drv = bs->drv;
    BlockDriverAIOCB *ret;

    if (!drv)
        return NULL;

    /* XXX: we assume that nb_sectors == 0 is suppored by the async read */
    if (sector_num == 0 && bs->boot_sector_enabled && nb_sectors > 0) {
        memcpy(buf, bs->boot_sector_data, 512);
        sector_num++;
        nb_sectors--;
        buf += 512;
    }

    ret = drv->bdrv_aio_read(bs, sector_num, buf, nb_sectors, cb, opaque);

    if (ret) {
	/* Update stats even though technically transfer has not happened. */
	bs->rd_bytes += (unsigned) nb_sectors * SECTOR_SIZE;
	bs->rd_ops ++;
    }

    return ret;
}

BlockDriverAIOCB *bdrv_aio_write(BlockDriverState *bs, int64_t sector_num,
                                 const uint8_t *buf, int nb_sectors,
                                 BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriver *drv = bs->drv;
    BlockDriverAIOCB *ret;

    if (!drv)
        return NULL;
    if (bs->read_only)
        return NULL;
    if (sector_num == 0 && bs->boot_sector_enabled && nb_sectors > 0) {
        memcpy(bs->boot_sector_data, buf, 512);
    }

    ret = drv->bdrv_aio_write(bs, sector_num, buf, nb_sectors, cb, opaque);

    if (ret) {
	/* Update stats even though technically transfer has not happened. */
	bs->wr_bytes += (unsigned) nb_sectors * SECTOR_SIZE;
	bs->wr_ops ++;
    }

    return ret;
}

void bdrv_aio_cancel(BlockDriverAIOCB *acb)
{
    BlockDriver *drv = acb->bs->drv;

    drv->bdrv_aio_cancel(acb);
}


/**************************************************************/
/* async block device emulation */

#ifdef QEMU_IMG
static BlockDriverAIOCB *bdrv_aio_read_em(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    int ret;
    ret = bdrv_read(bs, sector_num, buf, nb_sectors);
    cb(opaque, ret);
    return NULL;
}

static BlockDriverAIOCB *bdrv_aio_write_em(BlockDriverState *bs,
        int64_t sector_num, const uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    int ret;
    ret = bdrv_write(bs, sector_num, buf, nb_sectors);
    cb(opaque, ret);
    return NULL;
}

static void bdrv_aio_cancel_em(BlockDriverAIOCB *acb)
{
}
#else
static void bdrv_aio_bh_cb(void *opaque)
{
    BlockDriverAIOCBSync *acb = opaque;
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *bdrv_aio_read_em(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriverAIOCBSync *acb;
    int ret;

    acb = qemu_aio_get(bs, cb, opaque);
    if (!acb->bh)
        acb->bh = qemu_bh_new(bdrv_aio_bh_cb, acb);
    ret = bdrv_read(bs, sector_num, buf, nb_sectors);
    acb->ret = ret;
    qemu_bh_schedule(acb->bh);
    return &acb->common;
}

static BlockDriverAIOCB *bdrv_aio_write_em(BlockDriverState *bs,
        int64_t sector_num, const uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BlockDriverAIOCBSync *acb;
    int ret;

    acb = qemu_aio_get(bs, cb, opaque);
    if (!acb->bh)
        acb->bh = qemu_bh_new(bdrv_aio_bh_cb, acb);
    ret = bdrv_write(bs, sector_num, buf, nb_sectors);
    acb->ret = ret;
    qemu_bh_schedule(acb->bh);
    return &acb->common;
}

static void bdrv_aio_cancel_em(BlockDriverAIOCB *blockacb)
{
    BlockDriverAIOCBSync *acb = (BlockDriverAIOCBSync *)blockacb;
    qemu_bh_cancel(acb->bh);
    qemu_aio_release(acb);
}
#endif /* !QEMU_IMG */

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
    int async_ret;
    BlockDriverAIOCB *acb;

    async_ret = NOT_DONE;
    qemu_aio_wait_start();
    acb = bdrv_aio_read(bs, sector_num, buf, nb_sectors,
                        bdrv_rw_em_cb, &async_ret);
    if (acb == NULL) {
        qemu_aio_wait_end();
        return -1;
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
    int async_ret;
    BlockDriverAIOCB *acb;

    async_ret = NOT_DONE;
    qemu_aio_wait_start();
    acb = bdrv_aio_write(bs, sector_num, buf, nb_sectors,
                         bdrv_rw_em_cb, &async_ret);
    if (acb == NULL) {
        qemu_aio_wait_end();
        return -1;
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
    bdrv_register(&bdrv_host_device);
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
    bdrv_register(&bdrv_qcow2);
    bdrv_register(&bdrv_parallels);
}

void *qemu_aio_get(BlockDriverState *bs, BlockDriverCompletionFunc *cb,
                   void *opaque)
{
    BlockDriver *drv;
    BlockDriverAIOCB *acb;

    drv = bs->drv;
    if (drv->free_aiocb) {
        acb = drv->free_aiocb;
        drv->free_aiocb = acb->next;
    } else {
        acb = qemu_mallocz(drv->aiocb_size);
        if (!acb)
            return NULL;
    }
    acb->bs = bs;
    acb->cb = cb;
    acb->opaque = opaque;
    return acb;
}

void qemu_aio_release(void *p)
{
    BlockDriverAIOCB *acb = p;
    BlockDriver *drv = acb->bs->drv;
    acb->next = drv->free_aiocb;
    drv->free_aiocb = acb;
}

/**************************************************************/
/* removable device support */

/**
 * Return TRUE if the media is present
 */
int bdrv_is_inserted(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    int ret;
    if (!drv)
        return 0;
    if (!drv->bdrv_is_inserted)
        return 1;
    ret = drv->bdrv_is_inserted(bs);
    return ret;
}

/**
 * Return TRUE if the media changed since the last call to this
 * function. It is currently only used for floppy disks
 */
int bdrv_media_changed(BlockDriverState *bs)
{
    BlockDriver *drv = bs->drv;
    int ret;

    if (!drv || !drv->bdrv_media_changed)
        ret = -ENOTSUP;
    else
        ret = drv->bdrv_media_changed(bs);
    if (ret == -ENOTSUP)
        ret = bs->media_changed;
    bs->media_changed = 0;
    return ret;
}

/**
 * If eject_flag is TRUE, eject the media. Otherwise, close the tray
 */
void bdrv_eject(BlockDriverState *bs, int eject_flag)
{
    BlockDriver *drv = bs->drv;
    int ret;

    if (!drv || !drv->bdrv_eject) {
        ret = -ENOTSUP;
    } else {
        ret = drv->bdrv_eject(bs, eject_flag);
    }
    if (ret == -ENOTSUP) {
        if (eject_flag)
            bdrv_close(bs);
    }
}

int bdrv_is_locked(BlockDriverState *bs)
{
    return bs->locked;
}

/**
 * Lock or unlock the media (if it is locked, the user won't be able
 * to eject it manually).
 */
void bdrv_set_locked(BlockDriverState *bs, int locked)
{
    BlockDriver *drv = bs->drv;

    bs->locked = locked;
    if (drv && drv->bdrv_set_locked) {
        drv->bdrv_set_locked(bs, locked);
    }
}

/* needed for generic scsi interface */

int bdrv_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    BlockDriver *drv = bs->drv;

    if (drv && drv->bdrv_ioctl)
        return drv->bdrv_ioctl(bs, req, buf);
    return -ENOTSUP;
}
