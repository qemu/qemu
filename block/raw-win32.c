/*
 * Block driver for RAW files (win32)
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "qemu-common.h"
#include "qemu/timer.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "raw-aio.h"
#include "trace.h"
#include "block/thread-pool.h"
#include "qemu/iov.h"
#include "qapi/qmp/qstring.h"
#include <windows.h>
#include <winioctl.h>

#define FTYPE_FILE 0
#define FTYPE_CD     1
#define FTYPE_HARDDISK 2

typedef struct RawWin32AIOData {
    BlockDriverState *bs;
    HANDLE hfile;
    struct iovec *aio_iov;
    int aio_niov;
    size_t aio_nbytes;
    off64_t aio_offset;
    int aio_type;
} RawWin32AIOData;

typedef struct BDRVRawState {
    HANDLE hfile;
    int type;
    char drive_path[16]; /* format: "d:\" */
    QEMUWin32AIOState *aio;
} BDRVRawState;

/*
 * Read/writes the data to/from a given linear buffer.
 *
 * Returns the number of bytes handles or -errno in case of an error. Short
 * reads are only returned if the end of the file is reached.
 */
static size_t handle_aiocb_rw(RawWin32AIOData *aiocb)
{
    size_t offset = 0;
    int i;

    for (i = 0; i < aiocb->aio_niov; i++) {
        OVERLAPPED ov;
        DWORD ret, ret_count, len;

        memset(&ov, 0, sizeof(ov));
        ov.Offset = (aiocb->aio_offset + offset);
        ov.OffsetHigh = (aiocb->aio_offset + offset) >> 32;
        len = aiocb->aio_iov[i].iov_len;
        if (aiocb->aio_type & QEMU_AIO_WRITE) {
            ret = WriteFile(aiocb->hfile, aiocb->aio_iov[i].iov_base,
                            len, &ret_count, &ov);
        } else {
            ret = ReadFile(aiocb->hfile, aiocb->aio_iov[i].iov_base,
                           len, &ret_count, &ov);
        }
        if (!ret) {
            ret_count = 0;
        }
        if (ret_count != len) {
            offset += ret_count;
            break;
        }
        offset += len;
    }

    return offset;
}

static int aio_worker(void *arg)
{
    RawWin32AIOData *aiocb = arg;
    ssize_t ret = 0;
    size_t count;

    switch (aiocb->aio_type & QEMU_AIO_TYPE_MASK) {
    case QEMU_AIO_READ:
        count = handle_aiocb_rw(aiocb);
        if (count < aiocb->aio_nbytes) {
            /* A short read means that we have reached EOF. Pad the buffer
             * with zeros for bytes after EOF. */
            iov_memset(aiocb->aio_iov, aiocb->aio_niov, count,
                      0, aiocb->aio_nbytes - count);

            count = aiocb->aio_nbytes;
        }
        if (count == aiocb->aio_nbytes) {
            ret = 0;
        } else {
            ret = -EINVAL;
        }
        break;
    case QEMU_AIO_WRITE:
        count = handle_aiocb_rw(aiocb);
        if (count == aiocb->aio_nbytes) {
            ret = 0;
        } else {
            ret = -EINVAL;
        }
        break;
    case QEMU_AIO_FLUSH:
        if (!FlushFileBuffers(aiocb->hfile)) {
            return -EIO;
        }
        break;
    default:
        fprintf(stderr, "invalid aio request (0x%x)\n", aiocb->aio_type);
        ret = -EINVAL;
        break;
    }

    g_free(aiocb);
    return ret;
}

static BlockAIOCB *paio_submit(BlockDriverState *bs, HANDLE hfile,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque, int type)
{
    RawWin32AIOData *acb = g_new(RawWin32AIOData, 1);
    ThreadPool *pool;

    acb->bs = bs;
    acb->hfile = hfile;
    acb->aio_type = type;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
    }
    acb->aio_nbytes = nb_sectors * 512;
    acb->aio_offset = sector_num * 512;

    trace_paio_submit(acb, opaque, sector_num, nb_sectors, type);
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_aio(pool, aio_worker, acb, cb, opaque);
}

int qemu_ftruncate64(int fd, int64_t length)
{
    LARGE_INTEGER li;
    DWORD dw;
    LONG high;
    HANDLE h;
    BOOL res;

    if ((GetVersion() & 0x80000000UL) && (length >> 32) != 0)
	return -1;

    h = (HANDLE)_get_osfhandle(fd);

    /* get current position, ftruncate do not change position */
    li.HighPart = 0;
    li.LowPart = SetFilePointer (h, 0, &li.HighPart, FILE_CURRENT);
    if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
	return -1;
    }

    high = length >> 32;
    dw = SetFilePointer(h, (DWORD) length, &high, FILE_BEGIN);
    if (dw == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
	return -1;
    }
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

static void raw_detach_aio_context(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    if (s->aio) {
        win32_aio_detach_aio_context(s->aio, bdrv_get_aio_context(bs));
    }
}

static void raw_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
    BDRVRawState *s = bs->opaque;

    if (s->aio) {
        win32_aio_attach_aio_context(s->aio, new_context);
    }
}

static void raw_probe_alignment(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    DWORD sectorsPerCluster, freeClusters, totalClusters, count;
    DISK_GEOMETRY_EX dg;
    BOOL status;

    if (s->type == FTYPE_CD) {
        bs->request_alignment = 2048;
        return;
    }
    if (s->type == FTYPE_HARDDISK) {
        status = DeviceIoControl(s->hfile, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                                 NULL, 0, &dg, sizeof(dg), &count, NULL);
        if (status != 0) {
            bs->request_alignment = dg.Geometry.BytesPerSector;
            return;
        }
        /* try GetDiskFreeSpace too */
    }

    if (s->drive_path[0]) {
        GetDiskFreeSpace(s->drive_path, &sectorsPerCluster,
                         &dg.Geometry.BytesPerSector,
                         &freeClusters, &totalClusters);
        bs->request_alignment = dg.Geometry.BytesPerSector;
    }
}

static void raw_parse_flags(int flags, int *access_flags, DWORD *overlapped)
{
    assert(access_flags != NULL);
    assert(overlapped != NULL);

    if (flags & BDRV_O_RDWR) {
        *access_flags = GENERIC_READ | GENERIC_WRITE;
    } else {
        *access_flags = GENERIC_READ;
    }

    *overlapped = FILE_ATTRIBUTE_NORMAL;
    if (flags & BDRV_O_NATIVE_AIO) {
        *overlapped |= FILE_FLAG_OVERLAPPED;
    }
    if (flags & BDRV_O_NOCACHE) {
        *overlapped |= FILE_FLAG_NO_BUFFERING;
    }
}

static void raw_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    /* The filename does not have to be prefixed by the protocol name, since
     * "file" is the default protocol; therefore, the return value of this
     * function call can be ignored. */
    strstart(filename, "file:", &filename);

    qdict_put_obj(options, "filename", QOBJECT(qstring_from_str(filename)));
}

static QemuOptsList raw_runtime_opts = {
    .name = "raw",
    .head = QTAILQ_HEAD_INITIALIZER(raw_runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "File name of the image",
        },
        { /* end of list */ }
    },
};

static int raw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVRawState *s = bs->opaque;
    int access_flags;
    DWORD overlapped;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename;
    int ret;

    s->type = FTYPE_FILE;

    opts = qemu_opts_create(&raw_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    filename = qemu_opt_get(opts, "filename");

    raw_parse_flags(flags, &access_flags, &overlapped);

    if (filename[0] && filename[1] == ':') {
        snprintf(s->drive_path, sizeof(s->drive_path), "%c:\\", filename[0]);
    } else if (filename[0] == '\\' && filename[1] == '\\') {
        s->drive_path[0] = 0;
    } else {
        /* Relative path.  */
        char buf[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, buf);
        snprintf(s->drive_path, sizeof(s->drive_path), "%c:\\", buf[0]);
    }

    s->hfile = CreateFile(filename, access_flags,
                          FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, overlapped, NULL);
    if (s->hfile == INVALID_HANDLE_VALUE) {
        int err = GetLastError();

        if (err == ERROR_ACCESS_DENIED) {
            ret = -EACCES;
        } else {
            ret = -EINVAL;
        }
        goto fail;
    }

    if (flags & BDRV_O_NATIVE_AIO) {
        s->aio = win32_aio_init();
        if (s->aio == NULL) {
            CloseHandle(s->hfile);
            error_setg(errp, "Could not initialize AIO");
            ret = -EINVAL;
            goto fail;
        }

        ret = win32_aio_attach(s->aio, s->hfile);
        if (ret < 0) {
            win32_aio_cleanup(s->aio);
            CloseHandle(s->hfile);
            error_setg_errno(errp, -ret, "Could not enable AIO");
            goto fail;
        }

        win32_aio_attach_aio_context(s->aio, bdrv_get_aio_context(bs));
    }

    raw_probe_alignment(bs);
    ret = 0;
fail:
    qemu_opts_del(opts);
    return ret;
}

static BlockAIOCB *raw_aio_readv(BlockDriverState *bs,
                         int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
                         BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    if (s->aio) {
        return win32_aio_submit(bs, s->aio, s->hfile, sector_num, qiov,
                                nb_sectors, cb, opaque, QEMU_AIO_READ); 
    } else {
        return paio_submit(bs, s->hfile, sector_num, qiov, nb_sectors,
                           cb, opaque, QEMU_AIO_READ);
    }
}

static BlockAIOCB *raw_aio_writev(BlockDriverState *bs,
                          int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
                          BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    if (s->aio) {
        return win32_aio_submit(bs, s->aio, s->hfile, sector_num, qiov,
                                nb_sectors, cb, opaque, QEMU_AIO_WRITE); 
    } else {
        return paio_submit(bs, s->hfile, sector_num, qiov, nb_sectors,
                           cb, opaque, QEMU_AIO_WRITE);
    }
}

static BlockAIOCB *raw_aio_flush(BlockDriverState *bs,
                         BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    return paio_submit(bs, s->hfile, 0, NULL, 0, cb, opaque, QEMU_AIO_FLUSH);
}

static void raw_close(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    if (s->aio) {
        win32_aio_detach_aio_context(s->aio, bdrv_get_aio_context(bs));
        win32_aio_cleanup(s->aio);
        s->aio = NULL;
    }

    CloseHandle(s->hfile);
    if (bs->open_flags & BDRV_O_TEMPORARY) {
        unlink(bs->filename);
    }
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVRawState *s = bs->opaque;
    LONG low, high;
    DWORD dwPtrLow;

    low = offset;
    high = offset >> 32;

    /*
     * An error has occurred if the return value is INVALID_SET_FILE_POINTER
     * and GetLastError doesn't return NO_ERROR.
     */
    dwPtrLow = SetFilePointer(s->hfile, low, &high, FILE_BEGIN);
    if (dwPtrLow == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
        fprintf(stderr, "SetFilePointer error: %lu\n", GetLastError());
        return -EIO;
    }
    if (SetEndOfFile(s->hfile) == 0) {
        fprintf(stderr, "SetEndOfFile error: %lu\n", GetLastError());
        return -EIO;
    }
    return 0;
}

static int64_t raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    LARGE_INTEGER l;
    ULARGE_INTEGER available, total, total_free;
    DISK_GEOMETRY_EX dg;
    DWORD count;
    BOOL status;

    switch(s->type) {
    case FTYPE_FILE:
        l.LowPart = GetFileSize(s->hfile, (PDWORD)&l.HighPart);
        if (l.LowPart == 0xffffffffUL && GetLastError() != NO_ERROR)
            return -EIO;
        break;
    case FTYPE_CD:
        if (!GetDiskFreeSpaceEx(s->drive_path, &available, &total, &total_free))
            return -EIO;
        l.QuadPart = total.QuadPart;
        break;
    case FTYPE_HARDDISK:
        status = DeviceIoControl(s->hfile, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                                 NULL, 0, &dg, sizeof(dg), &count, NULL);
        if (status != 0) {
            l = dg.DiskSize;
        }
        break;
    default:
        return -EIO;
    }
    return l.QuadPart;
}

static int64_t raw_get_allocated_file_size(BlockDriverState *bs)
{
    typedef DWORD (WINAPI * get_compressed_t)(const char *filename,
                                              DWORD * high);
    get_compressed_t get_compressed;
    struct _stati64 st;
    const char *filename = bs->filename;
    /* WinNT support GetCompressedFileSize to determine allocate size */
    get_compressed =
        (get_compressed_t) GetProcAddress(GetModuleHandle("kernel32"),
                                            "GetCompressedFileSizeA");
    if (get_compressed) {
        DWORD high, low;
        low = get_compressed(filename, &high);
        if (low != 0xFFFFFFFFlu || GetLastError() == NO_ERROR) {
            return (((int64_t) high) << 32) + low;
        }
    }

    if (_stati64(filename, &st) < 0) {
        return -1;
    }
    return st.st_size;
}

static int raw_create(const char *filename, QemuOpts *opts, Error **errp)
{
    int fd;
    int64_t total_size = 0;

    strstart(filename, "file:", &filename);

    /* Read out options */
    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);

    fd = qemu_open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
                   0644);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Could not create file");
        return -EIO;
    }
    set_sparse(fd);
    ftruncate(fd, total_size);
    qemu_close(fd);
    return 0;
}


static QemuOptsList raw_create_opts = {
    .name = "raw-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(raw_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        { /* end of list */ }
    }
};

BlockDriver bdrv_file = {
    .format_name	= "file",
    .protocol_name	= "file",
    .instance_size	= sizeof(BDRVRawState),
    .bdrv_needs_filename = true,
    .bdrv_parse_filename = raw_parse_filename,
    .bdrv_file_open     = raw_open,
    .bdrv_close         = raw_close,
    .bdrv_create        = raw_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,

    .bdrv_aio_readv     = raw_aio_readv,
    .bdrv_aio_writev    = raw_aio_writev,
    .bdrv_aio_flush     = raw_aio_flush,

    .bdrv_truncate	= raw_truncate,
    .bdrv_getlength	= raw_getlength,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,

    .create_opts        = &raw_create_opts,
};

/***********************************************/
/* host device */

static int find_cdrom(char *cdrom_name, int cdrom_name_size)
{
    char drives[256], *pdrv = drives;
    UINT type;

    memset(drives, 0, sizeof(drives));
    GetLogicalDriveStrings(sizeof(drives), drives);
    while(pdrv[0] != '\0') {
        type = GetDriveType(pdrv);
        switch(type) {
        case DRIVE_CDROM:
            snprintf(cdrom_name, cdrom_name_size, "\\\\.\\%c:", pdrv[0]);
            return 0;
            break;
        }
        pdrv += lstrlen(pdrv) + 1;
    }
    return -1;
}

static int find_device_type(BlockDriverState *bs, const char *filename)
{
    BDRVRawState *s = bs->opaque;
    UINT type;
    const char *p;

    if (strstart(filename, "\\\\.\\", &p) ||
        strstart(filename, "//./", &p)) {
        if (stristart(p, "PhysicalDrive", NULL))
            return FTYPE_HARDDISK;
        snprintf(s->drive_path, sizeof(s->drive_path), "%c:\\", p[0]);
        type = GetDriveType(s->drive_path);
        switch (type) {
        case DRIVE_REMOVABLE:
        case DRIVE_FIXED:
            return FTYPE_HARDDISK;
        case DRIVE_CDROM:
            return FTYPE_CD;
        default:
            return FTYPE_FILE;
        }
    } else {
        return FTYPE_FILE;
    }
}

static int hdev_probe_device(const char *filename)
{
    if (strstart(filename, "/dev/cdrom", NULL))
        return 100;
    if (is_windows_drive(filename))
        return 100;
    return 0;
}

static void hdev_parse_filename(const char *filename, QDict *options,
                                Error **errp)
{
    /* The prefix is optional, just as for "file". */
    strstart(filename, "host_device:", &filename);

    qdict_put_obj(options, "filename", QOBJECT(qstring_from_str(filename)));
}

static int hdev_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    BDRVRawState *s = bs->opaque;
    int access_flags, create_flags;
    int ret = 0;
    DWORD overlapped;
    char device_name[64];

    Error *local_err = NULL;
    const char *filename;

    QemuOpts *opts = qemu_opts_create(&raw_runtime_opts, NULL, 0,
                                      &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto done;
    }

    filename = qemu_opt_get(opts, "filename");

    if (strstart(filename, "/dev/cdrom", NULL)) {
        if (find_cdrom(device_name, sizeof(device_name)) < 0) {
            error_setg(errp, "Could not open CD-ROM drive");
            ret = -ENOENT;
            goto done;
        }
        filename = device_name;
    } else {
        /* transform drive letters into device name */
        if (((filename[0] >= 'a' && filename[0] <= 'z') ||
             (filename[0] >= 'A' && filename[0] <= 'Z')) &&
            filename[1] == ':' && filename[2] == '\0') {
            snprintf(device_name, sizeof(device_name), "\\\\.\\%c:", filename[0]);
            filename = device_name;
        }
    }
    s->type = find_device_type(bs, filename);

    raw_parse_flags(flags, &access_flags, &overlapped);

    create_flags = OPEN_EXISTING;

    s->hfile = CreateFile(filename, access_flags,
                          FILE_SHARE_READ, NULL,
                          create_flags, overlapped, NULL);
    if (s->hfile == INVALID_HANDLE_VALUE) {
        int err = GetLastError();

        if (err == ERROR_ACCESS_DENIED) {
            ret = -EACCES;
        } else {
            ret = -EINVAL;
        }
        error_setg_errno(errp, -ret, "Could not open device");
        goto done;
    }

done:
    qemu_opts_del(opts);
    return ret;
}

static BlockDriver bdrv_host_device = {
    .format_name	= "host_device",
    .protocol_name	= "host_device",
    .instance_size	= sizeof(BDRVRawState),
    .bdrv_needs_filename = true,
    .bdrv_parse_filename = hdev_parse_filename,
    .bdrv_probe_device	= hdev_probe_device,
    .bdrv_file_open	= hdev_open,
    .bdrv_close		= raw_close,

    .bdrv_aio_readv     = raw_aio_readv,
    .bdrv_aio_writev    = raw_aio_writev,
    .bdrv_aio_flush     = raw_aio_flush,

    .bdrv_detach_aio_context = raw_detach_aio_context,
    .bdrv_attach_aio_context = raw_attach_aio_context,

    .bdrv_getlength      = raw_getlength,
    .has_variable_length = true,

    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,
};

static void bdrv_file_init(void)
{
    bdrv_register(&bdrv_file);
    bdrv_register(&bdrv_host_device);
}

block_init(bdrv_file_init);
