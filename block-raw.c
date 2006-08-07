/*
 * Block driver for RAW files
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
#include "vl.h"
#include "block_int.h"
#include <assert.h>
#ifndef _WIN32
#include <aio.h>

#ifndef QEMU_TOOL
#include "exec-all.h"
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

typedef struct BDRVRawState {
    int fd;
} BDRVRawState;

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

static int raw_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVRawState *s = bs->opaque;
    int fd, open_flags;

#ifdef CONFIG_COCOA
    if (strstart(filename, "/dev/cdrom", NULL)) {
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
    open_flags = O_BINARY;
    if ((flags & BDRV_O_ACCESS) == O_RDWR) {
        open_flags |= O_RDWR;
    } else {
        open_flags |= O_RDONLY;
        bs->read_only = 1;
    }
    if (flags & BDRV_O_CREAT)
        open_flags |= O_CREAT | O_TRUNC;

    fd = open(filename, open_flags, 0644);
    if (fd < 0)
        return -errno;
    s->fd = fd;
    return 0;
}

/* XXX: use host sector size if necessary with:
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
*/

static int raw_pread(BlockDriverState *bs, int64_t offset, 
                     uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    int ret;
    
    lseek(s->fd, offset, SEEK_SET);
    ret = read(s->fd, buf, count);
    return ret;
}

static int raw_pwrite(BlockDriverState *bs, int64_t offset, 
                      const uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    int ret;
    
    lseek(s->fd, offset, SEEK_SET);
    ret = write(s->fd, buf, count);
    return ret;
}

/***********************************************************/
/* Unix AOP using POSIX AIO */

typedef struct RawAIOCB {
    BlockDriverAIOCB common;
    struct aiocb aiocb;
    struct RawAIOCB *next;
} RawAIOCB;

static int aio_sig_num = SIGUSR2;
static RawAIOCB *first_aio; /* AIO issued */
static int aio_initialized = 0;

static void aio_signal_handler(int signum)
{
#ifndef QEMU_TOOL
    CPUState *env = cpu_single_env;
    if (env) {
        /* stop the currently executing cpu because a timer occured */
        cpu_interrupt(env, CPU_INTERRUPT_EXIT);
#ifdef USE_KQEMU
        if (env->kqemu_enabled) {
            kqemu_cpu_interrupt(env);
        }
#endif
    }
#endif
}

void qemu_aio_init(void)
{
    struct sigaction act;

    aio_initialized = 1;
    
    sigfillset(&act.sa_mask);
    act.sa_flags = 0; /* do not restart syscalls to interrupt select() */
    act.sa_handler = aio_signal_handler;
    sigaction(aio_sig_num, &act, NULL);

    {
        /* XXX: aio thread exit seems to hang on RH 9 */
        struct aioinit ai;
        memset(&ai, 0, sizeof(ai));
        ai.aio_threads = 2;
        ai.aio_num = 1;
        ai.aio_idle_time = 365 * 100000;
        aio_init(&ai);
    }
}

void qemu_aio_poll(void)
{
    RawAIOCB *acb, **pacb;
    int ret;

    for(;;) {
        pacb = &first_aio;
        for(;;) {
            acb = *pacb;
            if (!acb)
                goto the_end;
            ret = aio_error(&acb->aiocb);
            if (ret == ECANCELED) {
                /* remove the request */
                *pacb = acb->next;
                qemu_aio_release(acb);
            } else if (ret != EINPROGRESS) {
                /* end of aio */
                if (ret == 0) {
                    ret = aio_return(&acb->aiocb);
                    if (ret == acb->aiocb.aio_nbytes)
                        ret = 0;
                    else
                        ret = -1;
                } else {
                    ret = -ret;
                }
                /* remove the request */
                *pacb = acb->next;
                /* call the callback */
                acb->common.cb(acb->common.opaque, ret);
                qemu_aio_release(acb);
                break;
            } else {
                pacb = &acb->next;
            }
        }
    }
 the_end: ;
}

/* wait until at least one AIO was handled */
static sigset_t wait_oset;

void qemu_aio_wait_start(void)
{
    sigset_t set;

    if (!aio_initialized)
        qemu_aio_init();
    sigemptyset(&set);
    sigaddset(&set, aio_sig_num);
    sigprocmask(SIG_BLOCK, &set, &wait_oset);
}

void qemu_aio_wait(void)
{
    sigset_t set;
    int nb_sigs;

#ifndef QEMU_TOOL
    if (qemu_bh_poll())
        return;
#endif
    sigemptyset(&set);
    sigaddset(&set, aio_sig_num);
    sigwait(&set, &nb_sigs);
    qemu_aio_poll();
}

void qemu_aio_wait_end(void)
{
    sigprocmask(SIG_SETMASK, &wait_oset, NULL);
}

static RawAIOCB *raw_aio_setup(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    RawAIOCB *acb;

    acb = qemu_aio_get(bs, cb, opaque);
    if (!acb)
        return NULL;
    acb->aiocb.aio_fildes = s->fd;
    acb->aiocb.aio_sigevent.sigev_signo = aio_sig_num;
    acb->aiocb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    acb->aiocb.aio_buf = buf;
    acb->aiocb.aio_nbytes = nb_sectors * 512;
    acb->aiocb.aio_offset = sector_num * 512;
    acb->next = first_aio;
    first_aio = acb;
    return acb;
}

static BlockDriverAIOCB *raw_aio_read(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    RawAIOCB *acb;

    acb = raw_aio_setup(bs, sector_num, buf, nb_sectors, cb, opaque);
    if (!acb)
        return NULL;
    if (aio_read(&acb->aiocb) < 0) {
        qemu_aio_release(acb);
        return NULL;
    } 
    return &acb->common;
}

static BlockDriverAIOCB *raw_aio_write(BlockDriverState *bs,
        int64_t sector_num, const uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    RawAIOCB *acb;

    acb = raw_aio_setup(bs, sector_num, (uint8_t*)buf, nb_sectors, cb, opaque);
    if (!acb)
        return NULL;
    if (aio_write(&acb->aiocb) < 0) {
        qemu_aio_release(acb);
        return NULL;
    } 
    return &acb->common;
}

static void raw_aio_cancel(BlockDriverAIOCB *blockacb)
{
    int ret;
    RawAIOCB *acb = (RawAIOCB *)blockacb;
    RawAIOCB **pacb;

    ret = aio_cancel(acb->aiocb.aio_fildes, &acb->aiocb);
    if (ret == AIO_NOTCANCELED) {
        /* fail safe: if the aio could not be canceled, we wait for
           it */
        while (aio_error(&acb->aiocb) == EINPROGRESS);
    }

    /* remove the callback from the queue */
    pacb = &first_aio;
    for(;;) {
        if (*pacb == NULL) {
            break;
        } else if (*pacb == acb) {
            *pacb = acb->next;
            qemu_aio_release(acb);
            break;
        }
        pacb = &acb->next;
    }
}

static void raw_close(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    close(s->fd);
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVRawState *s = bs->opaque;
    if (ftruncate(s->fd, offset) < 0)
        return -errno;
    return 0;
}

static int64_t  raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int fd = s->fd;
    int64_t size;
#ifdef _BSD
    struct stat sb;
#endif
#ifdef __sun__
    struct dk_minfo minfo;
    int rv;
#endif

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
    return size;
}

static int raw_create(const char *filename, int64_t total_size,
                      const char *backing_file, int flags)
{
    int fd;

    if (flags || backing_file)
        return -ENOTSUP;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 
              0644);
    if (fd < 0)
        return -EIO;
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
    NULL, /* no probe for protocols */
    raw_open,
    NULL,
    NULL,
    raw_close,
    raw_create,
    raw_flush,
    
    .bdrv_aio_read = raw_aio_read,
    .bdrv_aio_write = raw_aio_write,
    .bdrv_aio_cancel = raw_aio_cancel,
    .aiocb_size = sizeof(RawAIOCB),
    .protocol_name = "file",
    .bdrv_pread = raw_pread,
    .bdrv_pwrite = raw_pwrite,
    .bdrv_truncate = raw_truncate,
    .bdrv_getlength = raw_getlength,
};

#else /* _WIN32 */

/* XXX: use another file ? */
#include <winioctl.h>

typedef struct BDRVRawState {
    HANDLE hfile;
} BDRVRawState;

typedef struct RawAIOCB {
    BlockDriverAIOCB common;
    HANDLE hEvent;
    OVERLAPPED ov;
    int count;
} RawAIOCB;

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

static int raw_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVRawState *s = bs->opaque;
    int access_flags, create_flags;
    DWORD overlapped;

    if ((flags & BDRV_O_ACCESS) == O_RDWR) {
        access_flags = GENERIC_READ | GENERIC_WRITE;
    } else {
        access_flags = GENERIC_READ;
    }
    if (flags & BDRV_O_CREAT) {
        create_flags = CREATE_ALWAYS;
    } else {
        create_flags = OPEN_EXISTING;
    }
#ifdef QEMU_TOOL
    overlapped = 0;
#else
    overlapped = FILE_FLAG_OVERLAPPED;
#endif
    s->hfile = CreateFile(filename, access_flags, 
                          FILE_SHARE_READ, NULL,
                          create_flags, overlapped, 0);
    if (s->hfile == INVALID_HANDLE_VALUE) 
        return -1;
    return 0;
}

static int raw_pread(BlockDriverState *bs, int64_t offset, 
                     uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    OVERLAPPED ov;
    DWORD ret_count;
    int ret;
    
    memset(&ov, 0, sizeof(ov));
    ov.Offset = offset;
    ov.OffsetHigh = offset >> 32;
    ret = ReadFile(s->hfile, buf, count, &ret_count, &ov);
    if (!ret) {
        ret = GetOverlappedResult(s->hfile, &ov, &ret_count, TRUE);
        if (!ret)
            return -EIO;
        else
            return ret_count;
    }
    return ret_count;
}

static int raw_pwrite(BlockDriverState *bs, int64_t offset, 
                      const uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    OVERLAPPED ov;
    DWORD ret_count;
    int ret;
    
    memset(&ov, 0, sizeof(ov));
    ov.Offset = offset;
    ov.OffsetHigh = offset >> 32;
    ret = WriteFile(s->hfile, buf, count, &ret_count, &ov);
    if (!ret) {
        ret = GetOverlappedResult(s->hfile, &ov, &ret_count, TRUE);
        if (!ret)
            return -EIO;
        else
            return ret_count;
    }
    return ret_count;
}

#ifndef QEMU_TOOL
static void raw_aio_cb(void *opaque)
{
    RawAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVRawState *s = bs->opaque;
    DWORD ret_count;
    int ret;

    ret = GetOverlappedResult(s->hfile, &acb->ov, &ret_count, TRUE);
    if (!ret || ret_count != acb->count) {
        acb->common.cb(acb->common.opaque, -EIO);
    } else {
        acb->common.cb(acb->common.opaque, 0);
    }
}
#endif

static RawAIOCB *raw_aio_setup(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    RawAIOCB *acb;
    int64_t offset;

    acb = qemu_aio_get(bs, cb, opaque);
    if (acb->hEvent) {
        acb->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!acb->hEvent) {
            qemu_aio_release(acb);
            return NULL;
        }
    }
    memset(&acb->ov, 0, sizeof(acb->ov));
    offset = sector_num * 512;
    acb->ov.Offset = offset;
    acb->ov.OffsetHigh = offset >> 32;
    acb->ov.hEvent = acb->hEvent;
    acb->count = nb_sectors * 512;
#ifndef QEMU_TOOL
    qemu_add_wait_object(acb->ov.hEvent, raw_aio_cb, acb);
#endif
    return acb;
}

static BlockDriverAIOCB *raw_aio_read(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    RawAIOCB *acb;
    int ret;

    acb = raw_aio_setup(bs, sector_num, buf, nb_sectors, cb, opaque);
    if (!acb)
        return NULL;
    ret = ReadFile(s->hfile, buf, acb->count, NULL, &acb->ov);
    if (!ret) {
        qemu_aio_release(acb);
        return NULL;
    }
#ifdef QEMU_TOOL
    qemu_aio_release(acb);
#endif
    return (BlockDriverAIOCB *)acb;
}

static BlockDriverAIOCB *raw_aio_write(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    RawAIOCB *acb;
    int ret;

    acb = raw_aio_setup(bs, sector_num, buf, nb_sectors, cb, opaque);
    if (!acb)
        return NULL;
    ret = WriteFile(s->hfile, buf, acb->count, NULL, &acb->ov);
    if (!ret) {
        qemu_aio_release(acb);
        return NULL;
    }
#ifdef QEMU_TOOL
    qemu_aio_release(acb);
#endif
    return (BlockDriverAIOCB *)acb;
}

static void raw_aio_cancel(BlockDriverAIOCB *blockacb)
{
#ifndef QEMU_TOOL
    RawAIOCB *acb = (RawAIOCB *)blockacb;
    BlockDriverState *bs = acb->common.bs;
    BDRVRawState *s = bs->opaque;

    qemu_del_wait_object(acb->ov.hEvent, raw_aio_cb, acb);
    /* XXX: if more than one async I/O it is not correct */
    CancelIo(s->hfile);
    qemu_aio_release(acb);
#endif
}

static void raw_flush(BlockDriverState *bs)
{
    /* XXX: add it */
}

static void raw_close(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    CloseHandle(s->hfile);
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVRawState *s = bs->opaque;
    DWORD low, high;

    low = offset;
    high = offset >> 32;
    if (!SetFilePointer(s->hfile, low, &high, FILE_BEGIN))
	return -EIO;
    if (!SetEndOfFile(s->hfile))
        return -EIO;
    return 0;
}

static int64_t  raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    LARGE_INTEGER l;

    l.LowPart = GetFileSize(s->hfile, &l.HighPart);
    if (l.LowPart == 0xffffffffUL && GetLastError() != NO_ERROR)
	return -EIO;
    return l.QuadPart;
}

static int raw_create(const char *filename, int64_t total_size,
                      const char *backing_file, int flags)
{
    int fd;

    if (flags || backing_file)
        return -ENOTSUP;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 
              0644);
    if (fd < 0)
        return -EIO;
    set_sparse(fd);
    ftruncate(fd, total_size * 512);
    close(fd);
    return 0;
}

void qemu_aio_init(void)
{
}

void qemu_aio_poll(void)
{
}

void qemu_aio_wait_start(void)
{
}

void qemu_aio_wait(void)
{
}

void qemu_aio_wait_end(void)
{
}

BlockDriver bdrv_raw = {
    "raw",
    sizeof(BDRVRawState),
    NULL, /* no probe for protocols */
    raw_open,
    NULL,
    NULL,
    raw_close,
    raw_create,
    raw_flush,
    
#if 0
    .bdrv_aio_read = raw_aio_read,
    .bdrv_aio_write = raw_aio_write,
    .bdrv_aio_cancel = raw_aio_cancel,
    .aiocb_size = sizeof(RawAIOCB);
#endif
    .protocol_name = "file",
    .bdrv_pread = raw_pread,
    .bdrv_pwrite = raw_pwrite,
    .bdrv_truncate = raw_truncate,
    .bdrv_getlength = raw_getlength,
};
#endif /* _WIN32 */
