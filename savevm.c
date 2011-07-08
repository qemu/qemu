/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <zlib.h>

/* Needed early for CONFIG_BSD etc. */
#include "config-host.h"

#ifndef _WIN32
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/select.h>
#ifdef CONFIG_BSD
#include <sys/stat.h>
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#include <libutil.h>
#else
#include <util.h>
#endif
#ifdef __linux__
#include <pty.h>
#include <malloc.h>
#include <linux/rtc.h>
#endif
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#include <sys/timeb.h>
#include <mmsystem.h>
#define getopt_long_only getopt_long
#define memalign(align, size) malloc(size)
#endif

#include "qemu-common.h"
#include "hw/hw.h"
#include "hw/qdev.h"
#include "net.h"
#include "monitor.h"
#include "sysemu.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "audio/audio.h"
#include "migration.h"
#include "qemu_socket.h"
#include "qemu-queue.h"
#include "cpus.h"

#define SELF_ANNOUNCE_ROUNDS 5

#ifndef ETH_P_RARP
#define ETH_P_RARP 0x8035
#endif
#define ARP_HTYPE_ETH 0x0001
#define ARP_PTYPE_IP 0x0800
#define ARP_OP_REQUEST_REV 0x3

static int announce_self_create(uint8_t *buf,
				uint8_t *mac_addr)
{
    /* Ethernet header. */
    memset(buf, 0xff, 6);         /* destination MAC addr */
    memcpy(buf + 6, mac_addr, 6); /* source MAC addr */
    *(uint16_t *)(buf + 12) = htons(ETH_P_RARP); /* ethertype */

    /* RARP header. */
    *(uint16_t *)(buf + 14) = htons(ARP_HTYPE_ETH); /* hardware addr space */
    *(uint16_t *)(buf + 16) = htons(ARP_PTYPE_IP); /* protocol addr space */
    *(buf + 18) = 6; /* hardware addr length (ethernet) */
    *(buf + 19) = 4; /* protocol addr length (IPv4) */
    *(uint16_t *)(buf + 20) = htons(ARP_OP_REQUEST_REV); /* opcode */
    memcpy(buf + 22, mac_addr, 6); /* source hw addr */
    memset(buf + 28, 0x00, 4);     /* source protocol addr */
    memcpy(buf + 32, mac_addr, 6); /* target hw addr */
    memset(buf + 38, 0x00, 4);     /* target protocol addr */

    /* Padding to get up to 60 bytes (ethernet min packet size, minus FCS). */
    memset(buf + 42, 0x00, 18);

    return 60; /* len (FCS will be added by hardware) */
}

static void qemu_announce_self_iter(NICState *nic, void *opaque)
{
    uint8_t buf[60];
    int len;

    len = announce_self_create(buf, nic->conf->macaddr.a);

    qemu_send_packet_raw(&nic->nc, buf, len);
}


static void qemu_announce_self_once(void *opaque)
{
    static int count = SELF_ANNOUNCE_ROUNDS;
    QEMUTimer *timer = *(QEMUTimer **)opaque;

    qemu_foreach_nic(qemu_announce_self_iter, NULL);

    if (--count) {
        /* delay 50ms, 150ms, 250ms, ... */
        qemu_mod_timer(timer, qemu_get_clock_ms(rt_clock) +
                       50 + (SELF_ANNOUNCE_ROUNDS - count - 1) * 100);
    } else {
	    qemu_del_timer(timer);
	    qemu_free_timer(timer);
    }
}

void qemu_announce_self(void)
{
	static QEMUTimer *timer;
	timer = qemu_new_timer_ms(rt_clock, qemu_announce_self_once, &timer);
	qemu_announce_self_once(&timer);
}

/***********************************************************/
/* savevm/loadvm support */

#define IO_BUF_SIZE 32768

struct QEMUFile {
    QEMUFilePutBufferFunc *put_buffer;
    QEMUFileGetBufferFunc *get_buffer;
    QEMUFileCloseFunc *close;
    QEMUFileRateLimit *rate_limit;
    QEMUFileSetRateLimit *set_rate_limit;
    QEMUFileGetRateLimit *get_rate_limit;
    void *opaque;
    int is_write;

    int64_t buf_offset; /* start of buffer when writing, end of buffer
                           when reading */
    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];

    int has_error;
};

typedef struct QEMUFileStdio
{
    FILE *stdio_file;
    QEMUFile *file;
} QEMUFileStdio;

typedef struct QEMUFileSocket
{
    int fd;
    QEMUFile *file;
} QEMUFileSocket;

static int socket_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileSocket *s = opaque;
    ssize_t len;

    do {
        len = recv(s->fd, (void *)buf, size, 0);
    } while (len == -1 && socket_error() == EINTR);

    if (len == -1)
        len = -socket_error();

    return len;
}

static int socket_close(void *opaque)
{
    QEMUFileSocket *s = opaque;
    qemu_free(s);
    return 0;
}

static int stdio_put_buffer(void *opaque, const uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    return fwrite(buf, 1, size, s->stdio_file);
}

static int stdio_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    FILE *fp = s->stdio_file;
    int bytes;

    do {
        clearerr(fp);
        bytes = fread(buf, 1, size, fp);
    } while ((bytes == 0) && ferror(fp) && (errno == EINTR));
    return bytes;
}

static int stdio_pclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    int ret;
    ret = pclose(s->stdio_file);
    qemu_free(s);
    return ret;
}

static int stdio_fclose(void *opaque)
{
    QEMUFileStdio *s = opaque;
    fclose(s->stdio_file);
    qemu_free(s);
    return 0;
}

QEMUFile *qemu_popen(FILE *stdio_file, const char *mode)
{
    QEMUFileStdio *s;

    if (stdio_file == NULL || mode == NULL || (mode[0] != 'r' && mode[0] != 'w') || mode[1] != 0) {
        fprintf(stderr, "qemu_popen: Argument validity check failed\n");
        return NULL;
    }

    s = qemu_mallocz(sizeof(QEMUFileStdio));

    s->stdio_file = stdio_file;

    if(mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, NULL, stdio_get_buffer, stdio_pclose, 
				 NULL, NULL, NULL);
    } else {
        s->file = qemu_fopen_ops(s, stdio_put_buffer, NULL, stdio_pclose, 
				 NULL, NULL, NULL);
    }
    return s->file;
}

QEMUFile *qemu_popen_cmd(const char *command, const char *mode)
{
    FILE *popen_file;

    popen_file = popen(command, mode);
    if(popen_file == NULL) {
        return NULL;
    }

    return qemu_popen(popen_file, mode);
}

int qemu_stdio_fd(QEMUFile *f)
{
    QEMUFileStdio *p;
    int fd;

    p = (QEMUFileStdio *)f->opaque;
    fd = fileno(p->stdio_file);

    return fd;
}

QEMUFile *qemu_fdopen(int fd, const char *mode)
{
    QEMUFileStdio *s;

    if (mode == NULL ||
	(mode[0] != 'r' && mode[0] != 'w') ||
	mode[1] != 'b' || mode[2] != 0) {
        fprintf(stderr, "qemu_fdopen: Argument validity check failed\n");
        return NULL;
    }

    s = qemu_mallocz(sizeof(QEMUFileStdio));
    s->stdio_file = fdopen(fd, mode);
    if (!s->stdio_file)
        goto fail;

    if(mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, NULL, stdio_get_buffer, stdio_fclose, 
				 NULL, NULL, NULL);
    } else {
        s->file = qemu_fopen_ops(s, stdio_put_buffer, NULL, stdio_fclose, 
				 NULL, NULL, NULL);
    }
    return s->file;

fail:
    qemu_free(s);
    return NULL;
}

QEMUFile *qemu_fopen_socket(int fd)
{
    QEMUFileSocket *s = qemu_mallocz(sizeof(QEMUFileSocket));

    s->fd = fd;
    s->file = qemu_fopen_ops(s, NULL, socket_get_buffer, socket_close, 
			     NULL, NULL, NULL);
    return s->file;
}

static int file_put_buffer(void *opaque, const uint8_t *buf,
                            int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->stdio_file, pos, SEEK_SET);
    return fwrite(buf, 1, size, s->stdio_file);
}

static int file_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->stdio_file, pos, SEEK_SET);
    return fread(buf, 1, size, s->stdio_file);
}

QEMUFile *qemu_fopen(const char *filename, const char *mode)
{
    QEMUFileStdio *s;

    if (mode == NULL ||
	(mode[0] != 'r' && mode[0] != 'w') ||
	mode[1] != 'b' || mode[2] != 0) {
        fprintf(stderr, "qemu_fopen: Argument validity check failed\n");
        return NULL;
    }

    s = qemu_mallocz(sizeof(QEMUFileStdio));

    s->stdio_file = fopen(filename, mode);
    if (!s->stdio_file)
        goto fail;
    
    if(mode[0] == 'w') {
        s->file = qemu_fopen_ops(s, file_put_buffer, NULL, stdio_fclose, 
				 NULL, NULL, NULL);
    } else {
        s->file = qemu_fopen_ops(s, NULL, file_get_buffer, stdio_fclose, 
			       NULL, NULL, NULL);
    }
    return s->file;
fail:
    qemu_free(s);
    return NULL;
}

static int block_put_buffer(void *opaque, const uint8_t *buf,
                           int64_t pos, int size)
{
    bdrv_save_vmstate(opaque, buf, pos, size);
    return size;
}

static int block_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    return bdrv_load_vmstate(opaque, buf, pos, size);
}

static int bdrv_fclose(void *opaque)
{
    return 0;
}

static QEMUFile *qemu_fopen_bdrv(BlockDriverState *bs, int is_writable)
{
    if (is_writable)
        return qemu_fopen_ops(bs, block_put_buffer, NULL, bdrv_fclose, 
			      NULL, NULL, NULL);
    return qemu_fopen_ops(bs, NULL, block_get_buffer, bdrv_fclose, NULL, NULL, NULL);
}

QEMUFile *qemu_fopen_ops(void *opaque, QEMUFilePutBufferFunc *put_buffer,
                         QEMUFileGetBufferFunc *get_buffer,
                         QEMUFileCloseFunc *close,
                         QEMUFileRateLimit *rate_limit,
                         QEMUFileSetRateLimit *set_rate_limit,
                         QEMUFileGetRateLimit *get_rate_limit)
{
    QEMUFile *f;

    f = qemu_mallocz(sizeof(QEMUFile));

    f->opaque = opaque;
    f->put_buffer = put_buffer;
    f->get_buffer = get_buffer;
    f->close = close;
    f->rate_limit = rate_limit;
    f->set_rate_limit = set_rate_limit;
    f->get_rate_limit = get_rate_limit;
    f->is_write = 0;

    return f;
}

int qemu_file_has_error(QEMUFile *f)
{
    return f->has_error;
}

void qemu_file_set_error(QEMUFile *f)
{
    f->has_error = 1;
}

void qemu_fflush(QEMUFile *f)
{
    if (!f->put_buffer)
        return;

    if (f->is_write && f->buf_index > 0) {
        int len;

        len = f->put_buffer(f->opaque, f->buf, f->buf_offset, f->buf_index);
        if (len > 0)
            f->buf_offset += f->buf_index;
        else
            f->has_error = 1;
        f->buf_index = 0;
    }
}

static void qemu_fill_buffer(QEMUFile *f)
{
    int len;

    if (!f->get_buffer)
        return;

    if (f->is_write)
        abort();

    len = f->get_buffer(f->opaque, f->buf, f->buf_offset, IO_BUF_SIZE);
    if (len > 0) {
        f->buf_index = 0;
        f->buf_size = len;
        f->buf_offset += len;
    } else if (len != -EAGAIN)
        f->has_error = 1;
}

int qemu_fclose(QEMUFile *f)
{
    int ret = 0;
    qemu_fflush(f);
    if (f->close)
        ret = f->close(f->opaque);
    qemu_free(f);
    return ret;
}

void qemu_file_put_notify(QEMUFile *f)
{
    f->put_buffer(f->opaque, NULL, 0, 0);
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, int size)
{
    int l;

    if (!f->has_error && f->is_write == 0 && f->buf_index > 0) {
        fprintf(stderr,
                "Attempted to write to buffer while read buffer is not empty\n");
        abort();
    }

    while (!f->has_error && size > 0) {
        l = IO_BUF_SIZE - f->buf_index;
        if (l > size)
            l = size;
        memcpy(f->buf + f->buf_index, buf, l);
        f->is_write = 1;
        f->buf_index += l;
        buf += l;
        size -= l;
        if (f->buf_index >= IO_BUF_SIZE)
            qemu_fflush(f);
    }
}

void qemu_put_byte(QEMUFile *f, int v)
{
    if (!f->has_error && f->is_write == 0 && f->buf_index > 0) {
        fprintf(stderr,
                "Attempted to write to buffer while read buffer is not empty\n");
        abort();
    }

    f->buf[f->buf_index++] = v;
    f->is_write = 1;
    if (f->buf_index >= IO_BUF_SIZE)
        qemu_fflush(f);
}

int qemu_get_buffer(QEMUFile *f, uint8_t *buf, int size1)
{
    int size, l;

    if (f->is_write)
        abort();

    size = size1;
    while (size > 0) {
        l = f->buf_size - f->buf_index;
        if (l == 0) {
            qemu_fill_buffer(f);
            l = f->buf_size - f->buf_index;
            if (l == 0)
                break;
        }
        if (l > size)
            l = size;
        memcpy(buf, f->buf + f->buf_index, l);
        f->buf_index += l;
        buf += l;
        size -= l;
    }
    return size1 - size;
}

static int qemu_peek_byte(QEMUFile *f)
{
    if (f->is_write)
        abort();

    if (f->buf_index >= f->buf_size) {
        qemu_fill_buffer(f);
        if (f->buf_index >= f->buf_size)
            return 0;
    }
    return f->buf[f->buf_index];
}

int qemu_get_byte(QEMUFile *f)
{
    if (f->is_write)
        abort();

    if (f->buf_index >= f->buf_size) {
        qemu_fill_buffer(f);
        if (f->buf_index >= f->buf_size)
            return 0;
    }
    return f->buf[f->buf_index++];
}

int64_t qemu_ftell(QEMUFile *f)
{
    return f->buf_offset - f->buf_size + f->buf_index;
}

int64_t qemu_fseek(QEMUFile *f, int64_t pos, int whence)
{
    if (whence == SEEK_SET) {
        /* nothing to do */
    } else if (whence == SEEK_CUR) {
        pos += qemu_ftell(f);
    } else {
        /* SEEK_END not supported */
        return -1;
    }
    if (f->put_buffer) {
        qemu_fflush(f);
        f->buf_offset = pos;
    } else {
        f->buf_offset = pos;
        f->buf_index = 0;
        f->buf_size = 0;
    }
    return pos;
}

int qemu_file_rate_limit(QEMUFile *f)
{
    if (f->rate_limit)
        return f->rate_limit(f->opaque);

    return 0;
}

int64_t qemu_file_get_rate_limit(QEMUFile *f)
{
    if (f->get_rate_limit)
        return f->get_rate_limit(f->opaque);

    return 0;
}

int64_t qemu_file_set_rate_limit(QEMUFile *f, int64_t new_rate)
{
    /* any failed or completed migration keeps its state to allow probing of
     * migration data, but has no associated file anymore */
    if (f && f->set_rate_limit)
        return f->set_rate_limit(f->opaque, new_rate);

    return 0;
}

void qemu_put_be16(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be32(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 24);
    qemu_put_byte(f, v >> 16);
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
    qemu_put_be32(f, v >> 32);
    qemu_put_be32(f, v);
}

unsigned int qemu_get_be16(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

unsigned int qemu_get_be32(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 24;
    v |= qemu_get_byte(f) << 16;
    v |= qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    uint64_t v;
    v = (uint64_t)qemu_get_be32(f) << 32;
    v |= qemu_get_be32(f);
    return v;
}

/* bool */

static int get_bool(QEMUFile *f, void *pv, size_t size)
{
    bool *v = pv;
    *v = qemu_get_byte(f);
    return 0;
}

static void put_bool(QEMUFile *f, void *pv, size_t size)
{
    bool *v = pv;
    qemu_put_byte(f, *v);
}

const VMStateInfo vmstate_info_bool = {
    .name = "bool",
    .get  = get_bool,
    .put  = put_bool,
};

/* 8 bit int */

static int get_int8(QEMUFile *f, void *pv, size_t size)
{
    int8_t *v = pv;
    qemu_get_s8s(f, v);
    return 0;
}

static void put_int8(QEMUFile *f, void *pv, size_t size)
{
    int8_t *v = pv;
    qemu_put_s8s(f, v);
}

const VMStateInfo vmstate_info_int8 = {
    .name = "int8",
    .get  = get_int8,
    .put  = put_int8,
};

/* 16 bit int */

static int get_int16(QEMUFile *f, void *pv, size_t size)
{
    int16_t *v = pv;
    qemu_get_sbe16s(f, v);
    return 0;
}

static void put_int16(QEMUFile *f, void *pv, size_t size)
{
    int16_t *v = pv;
    qemu_put_sbe16s(f, v);
}

const VMStateInfo vmstate_info_int16 = {
    .name = "int16",
    .get  = get_int16,
    .put  = put_int16,
};

/* 32 bit int */

static int get_int32(QEMUFile *f, void *pv, size_t size)
{
    int32_t *v = pv;
    qemu_get_sbe32s(f, v);
    return 0;
}

static void put_int32(QEMUFile *f, void *pv, size_t size)
{
    int32_t *v = pv;
    qemu_put_sbe32s(f, v);
}

const VMStateInfo vmstate_info_int32 = {
    .name = "int32",
    .get  = get_int32,
    .put  = put_int32,
};

/* 32 bit int. See that the received value is the same than the one
   in the field */

static int get_int32_equal(QEMUFile *f, void *pv, size_t size)
{
    int32_t *v = pv;
    int32_t v2;
    qemu_get_sbe32s(f, &v2);

    if (*v == v2)
        return 0;
    return -EINVAL;
}

const VMStateInfo vmstate_info_int32_equal = {
    .name = "int32 equal",
    .get  = get_int32_equal,
    .put  = put_int32,
};

/* 32 bit int. See that the received value is the less or the same
   than the one in the field */

static int get_int32_le(QEMUFile *f, void *pv, size_t size)
{
    int32_t *old = pv;
    int32_t new;
    qemu_get_sbe32s(f, &new);

    if (*old <= new)
        return 0;
    return -EINVAL;
}

const VMStateInfo vmstate_info_int32_le = {
    .name = "int32 equal",
    .get  = get_int32_le,
    .put  = put_int32,
};

/* 64 bit int */

static int get_int64(QEMUFile *f, void *pv, size_t size)
{
    int64_t *v = pv;
    qemu_get_sbe64s(f, v);
    return 0;
}

static void put_int64(QEMUFile *f, void *pv, size_t size)
{
    int64_t *v = pv;
    qemu_put_sbe64s(f, v);
}

const VMStateInfo vmstate_info_int64 = {
    .name = "int64",
    .get  = get_int64,
    .put  = put_int64,
};

/* 8 bit unsigned int */

static int get_uint8(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_get_8s(f, v);
    return 0;
}

static void put_uint8(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_put_8s(f, v);
}

const VMStateInfo vmstate_info_uint8 = {
    .name = "uint8",
    .get  = get_uint8,
    .put  = put_uint8,
};

/* 16 bit unsigned int */

static int get_uint16(QEMUFile *f, void *pv, size_t size)
{
    uint16_t *v = pv;
    qemu_get_be16s(f, v);
    return 0;
}

static void put_uint16(QEMUFile *f, void *pv, size_t size)
{
    uint16_t *v = pv;
    qemu_put_be16s(f, v);
}

const VMStateInfo vmstate_info_uint16 = {
    .name = "uint16",
    .get  = get_uint16,
    .put  = put_uint16,
};

/* 32 bit unsigned int */

static int get_uint32(QEMUFile *f, void *pv, size_t size)
{
    uint32_t *v = pv;
    qemu_get_be32s(f, v);
    return 0;
}

static void put_uint32(QEMUFile *f, void *pv, size_t size)
{
    uint32_t *v = pv;
    qemu_put_be32s(f, v);
}

const VMStateInfo vmstate_info_uint32 = {
    .name = "uint32",
    .get  = get_uint32,
    .put  = put_uint32,
};

/* 32 bit uint. See that the received value is the same than the one
   in the field */

static int get_uint32_equal(QEMUFile *f, void *pv, size_t size)
{
    uint32_t *v = pv;
    uint32_t v2;
    qemu_get_be32s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint32_equal = {
    .name = "uint32 equal",
    .get  = get_uint32_equal,
    .put  = put_uint32,
};

/* 64 bit unsigned int */

static int get_uint64(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;
    qemu_get_be64s(f, v);
    return 0;
}

static void put_uint64(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;
    qemu_put_be64s(f, v);
}

const VMStateInfo vmstate_info_uint64 = {
    .name = "uint64",
    .get  = get_uint64,
    .put  = put_uint64,
};

/* 8 bit int. See that the received value is the same than the one
   in the field */

static int get_uint8_equal(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    uint8_t v2;
    qemu_get_8s(f, &v2);

    if (*v == v2)
        return 0;
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint8_equal = {
    .name = "uint8 equal",
    .get  = get_uint8_equal,
    .put  = put_uint8,
};

/* 16 bit unsigned int int. See that the received value is the same than the one
   in the field */

static int get_uint16_equal(QEMUFile *f, void *pv, size_t size)
{
    uint16_t *v = pv;
    uint16_t v2;
    qemu_get_be16s(f, &v2);

    if (*v == v2)
        return 0;
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint16_equal = {
    .name = "uint16 equal",
    .get  = get_uint16_equal,
    .put  = put_uint16,
};

/* timers  */

static int get_timer(QEMUFile *f, void *pv, size_t size)
{
    QEMUTimer *v = pv;
    qemu_get_timer(f, v);
    return 0;
}

static void put_timer(QEMUFile *f, void *pv, size_t size)
{
    QEMUTimer *v = pv;
    qemu_put_timer(f, v);
}

const VMStateInfo vmstate_info_timer = {
    .name = "timer",
    .get  = get_timer,
    .put  = put_timer,
};

/* uint8_t buffers */

static int get_buffer(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_get_buffer(f, v, size);
    return 0;
}

static void put_buffer(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_put_buffer(f, v, size);
}

const VMStateInfo vmstate_info_buffer = {
    .name = "buffer",
    .get  = get_buffer,
    .put  = put_buffer,
};

/* unused buffers: space that was used for some fields that are
   not useful anymore */

static int get_unused_buffer(QEMUFile *f, void *pv, size_t size)
{
    uint8_t buf[1024];
    int block_len;

    while (size > 0) {
        block_len = MIN(sizeof(buf), size);
        size -= block_len;
        qemu_get_buffer(f, buf, block_len);
    }
   return 0;
}

static void put_unused_buffer(QEMUFile *f, void *pv, size_t size)
{
    static const uint8_t buf[1024];
    int block_len;

    while (size > 0) {
        block_len = MIN(sizeof(buf), size);
        size -= block_len;
        qemu_put_buffer(f, buf, block_len);
    }
}

const VMStateInfo vmstate_info_unused_buffer = {
    .name = "unused_buffer",
    .get  = get_unused_buffer,
    .put  = put_unused_buffer,
};

typedef struct CompatEntry {
    char idstr[256];
    int instance_id;
} CompatEntry;

typedef struct SaveStateEntry {
    QTAILQ_ENTRY(SaveStateEntry) entry;
    char idstr[256];
    int instance_id;
    int alias_id;
    int version_id;
    int section_id;
    SaveSetParamsHandler *set_params;
    SaveLiveStateHandler *save_live_state;
    SaveStateHandler *save_state;
    LoadStateHandler *load_state;
    const VMStateDescription *vmsd;
    void *opaque;
    CompatEntry *compat;
    int no_migrate;
} SaveStateEntry;


static QTAILQ_HEAD(savevm_handlers, SaveStateEntry) savevm_handlers =
    QTAILQ_HEAD_INITIALIZER(savevm_handlers);
static int global_section_id;

static int calculate_new_instance_id(const char *idstr)
{
    SaveStateEntry *se;
    int instance_id = 0;

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (strcmp(idstr, se->idstr) == 0
            && instance_id <= se->instance_id) {
            instance_id = se->instance_id + 1;
        }
    }
    return instance_id;
}

static int calculate_compat_instance_id(const char *idstr)
{
    SaveStateEntry *se;
    int instance_id = 0;

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (!se->compat)
            continue;

        if (strcmp(idstr, se->compat->idstr) == 0
            && instance_id <= se->compat->instance_id) {
            instance_id = se->compat->instance_id + 1;
        }
    }
    return instance_id;
}

/* TODO: Individual devices generally have very little idea about the rest
   of the system, so instance_id should be removed/replaced.
   Meanwhile pass -1 as instance_id if you do not already have a clearly
   distinguishing id for all instances of your device class. */
int register_savevm_live(DeviceState *dev,
                         const char *idstr,
                         int instance_id,
                         int version_id,
                         SaveSetParamsHandler *set_params,
                         SaveLiveStateHandler *save_live_state,
                         SaveStateHandler *save_state,
                         LoadStateHandler *load_state,
                         void *opaque)
{
    SaveStateEntry *se;

    se = qemu_mallocz(sizeof(SaveStateEntry));
    se->version_id = version_id;
    se->section_id = global_section_id++;
    se->set_params = set_params;
    se->save_live_state = save_live_state;
    se->save_state = save_state;
    se->load_state = load_state;
    se->opaque = opaque;
    se->vmsd = NULL;
    se->no_migrate = 0;

    if (dev && dev->parent_bus && dev->parent_bus->info->get_dev_path) {
        char *id = dev->parent_bus->info->get_dev_path(dev);
        if (id) {
            pstrcpy(se->idstr, sizeof(se->idstr), id);
            pstrcat(se->idstr, sizeof(se->idstr), "/");
            qemu_free(id);

            se->compat = qemu_mallocz(sizeof(CompatEntry));
            pstrcpy(se->compat->idstr, sizeof(se->compat->idstr), idstr);
            se->compat->instance_id = instance_id == -1 ?
                         calculate_compat_instance_id(idstr) : instance_id;
            instance_id = -1;
        }
    }
    pstrcat(se->idstr, sizeof(se->idstr), idstr);

    if (instance_id == -1) {
        se->instance_id = calculate_new_instance_id(se->idstr);
    } else {
        se->instance_id = instance_id;
    }
    assert(!se->compat || se->instance_id == 0);
    /* add at the end of list */
    QTAILQ_INSERT_TAIL(&savevm_handlers, se, entry);
    return 0;
}

int register_savevm(DeviceState *dev,
                    const char *idstr,
                    int instance_id,
                    int version_id,
                    SaveStateHandler *save_state,
                    LoadStateHandler *load_state,
                    void *opaque)
{
    return register_savevm_live(dev, idstr, instance_id, version_id,
                                NULL, NULL, save_state, load_state, opaque);
}

void unregister_savevm(DeviceState *dev, const char *idstr, void *opaque)
{
    SaveStateEntry *se, *new_se;
    char id[256] = "";

    if (dev && dev->parent_bus && dev->parent_bus->info->get_dev_path) {
        char *path = dev->parent_bus->info->get_dev_path(dev);
        if (path) {
            pstrcpy(id, sizeof(id), path);
            pstrcat(id, sizeof(id), "/");
            qemu_free(path);
        }
    }
    pstrcat(id, sizeof(id), idstr);

    QTAILQ_FOREACH_SAFE(se, &savevm_handlers, entry, new_se) {
        if (strcmp(se->idstr, id) == 0 && se->opaque == opaque) {
            QTAILQ_REMOVE(&savevm_handlers, se, entry);
            if (se->compat) {
                qemu_free(se->compat);
            }
            qemu_free(se);
        }
    }
}

/* mark a device as not to be migrated, that is the device should be
   unplugged before migration */
void register_device_unmigratable(DeviceState *dev, const char *idstr,
                                                            void *opaque)
{
    SaveStateEntry *se;
    char id[256] = "";

    if (dev && dev->parent_bus && dev->parent_bus->info->get_dev_path) {
        char *path = dev->parent_bus->info->get_dev_path(dev);
        if (path) {
            pstrcpy(id, sizeof(id), path);
            pstrcat(id, sizeof(id), "/");
            qemu_free(path);
        }
    }
    pstrcat(id, sizeof(id), idstr);

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (strcmp(se->idstr, id) == 0 && se->opaque == opaque) {
            se->no_migrate = 1;
        }
    }
}

int vmstate_register_with_alias_id(DeviceState *dev, int instance_id,
                                   const VMStateDescription *vmsd,
                                   void *opaque, int alias_id,
                                   int required_for_version)
{
    SaveStateEntry *se;

    /* If this triggers, alias support can be dropped for the vmsd. */
    assert(alias_id == -1 || required_for_version >= vmsd->minimum_version_id);

    se = qemu_mallocz(sizeof(SaveStateEntry));
    se->version_id = vmsd->version_id;
    se->section_id = global_section_id++;
    se->save_live_state = NULL;
    se->save_state = NULL;
    se->load_state = NULL;
    se->opaque = opaque;
    se->vmsd = vmsd;
    se->alias_id = alias_id;
    se->no_migrate = vmsd->unmigratable;

    if (dev && dev->parent_bus && dev->parent_bus->info->get_dev_path) {
        char *id = dev->parent_bus->info->get_dev_path(dev);
        if (id) {
            pstrcpy(se->idstr, sizeof(se->idstr), id);
            pstrcat(se->idstr, sizeof(se->idstr), "/");
            qemu_free(id);

            se->compat = qemu_mallocz(sizeof(CompatEntry));
            pstrcpy(se->compat->idstr, sizeof(se->compat->idstr), vmsd->name);
            se->compat->instance_id = instance_id == -1 ?
                         calculate_compat_instance_id(vmsd->name) : instance_id;
            instance_id = -1;
        }
    }
    pstrcat(se->idstr, sizeof(se->idstr), vmsd->name);

    if (instance_id == -1) {
        se->instance_id = calculate_new_instance_id(se->idstr);
    } else {
        se->instance_id = instance_id;
    }
    assert(!se->compat || se->instance_id == 0);
    /* add at the end of list */
    QTAILQ_INSERT_TAIL(&savevm_handlers, se, entry);
    return 0;
}

int vmstate_register(DeviceState *dev, int instance_id,
                     const VMStateDescription *vmsd, void *opaque)
{
    return vmstate_register_with_alias_id(dev, instance_id, vmsd,
                                          opaque, -1, 0);
}

void vmstate_unregister(DeviceState *dev, const VMStateDescription *vmsd,
                        void *opaque)
{
    SaveStateEntry *se, *new_se;

    QTAILQ_FOREACH_SAFE(se, &savevm_handlers, entry, new_se) {
        if (se->vmsd == vmsd && se->opaque == opaque) {
            QTAILQ_REMOVE(&savevm_handlers, se, entry);
            if (se->compat) {
                qemu_free(se->compat);
            }
            qemu_free(se);
        }
    }
}

static void vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque);
static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque);

int vmstate_load_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, int version_id)
{
    VMStateField *field = vmsd->fields;
    int ret;

    if (version_id > vmsd->version_id) {
        return -EINVAL;
    }
    if (version_id < vmsd->minimum_version_id_old) {
        return -EINVAL;
    }
    if  (version_id < vmsd->minimum_version_id) {
        return vmsd->load_state_old(f, opaque, version_id);
    }
    if (vmsd->pre_load) {
        int ret = vmsd->pre_load(opaque);
        if (ret)
            return ret;
    }
    while(field->name) {
        if ((field->field_exists &&
             field->field_exists(opaque, version_id)) ||
            (!field->field_exists &&
             field->version_id <= version_id)) {
            void *base_addr = opaque + field->offset;
            int i, n_elems = 1;
            int size = field->size;

            if (field->flags & VMS_VBUFFER) {
                size = *(int32_t *)(opaque+field->size_offset);
                if (field->flags & VMS_MULTIPLY) {
                    size *= field->size;
                }
            }
            if (field->flags & VMS_ARRAY) {
                n_elems = field->num;
            } else if (field->flags & VMS_VARRAY_INT32) {
                n_elems = *(int32_t *)(opaque+field->num_offset);
            } else if (field->flags & VMS_VARRAY_UINT32) {
                n_elems = *(uint32_t *)(opaque+field->num_offset);
            } else if (field->flags & VMS_VARRAY_UINT16) {
                n_elems = *(uint16_t *)(opaque+field->num_offset);
            } else if (field->flags & VMS_VARRAY_UINT8) {
                n_elems = *(uint8_t *)(opaque+field->num_offset);
            }
            if (field->flags & VMS_POINTER) {
                base_addr = *(void **)base_addr + field->start;
            }
            for (i = 0; i < n_elems; i++) {
                void *addr = base_addr + size * i;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    addr = *(void **)addr;
                }
                if (field->flags & VMS_STRUCT) {
                    ret = vmstate_load_state(f, field->vmsd, addr, field->vmsd->version_id);
                } else {
                    ret = field->info->get(f, addr, size);

                }
                if (ret < 0) {
                    return ret;
                }
            }
        }
        field++;
    }
    ret = vmstate_subsection_load(f, vmsd, opaque);
    if (ret != 0) {
        return ret;
    }
    if (vmsd->post_load) {
        return vmsd->post_load(opaque, version_id);
    }
    return 0;
}

void vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                        void *opaque)
{
    VMStateField *field = vmsd->fields;

    if (vmsd->pre_save) {
        vmsd->pre_save(opaque);
    }
    while(field->name) {
        if (!field->field_exists ||
            field->field_exists(opaque, vmsd->version_id)) {
            void *base_addr = opaque + field->offset;
            int i, n_elems = 1;
            int size = field->size;

            if (field->flags & VMS_VBUFFER) {
                size = *(int32_t *)(opaque+field->size_offset);
                if (field->flags & VMS_MULTIPLY) {
                    size *= field->size;
                }
            }
            if (field->flags & VMS_ARRAY) {
                n_elems = field->num;
            } else if (field->flags & VMS_VARRAY_INT32) {
                n_elems = *(int32_t *)(opaque+field->num_offset);
            } else if (field->flags & VMS_VARRAY_UINT16) {
                n_elems = *(uint16_t *)(opaque+field->num_offset);
            } else if (field->flags & VMS_VARRAY_UINT8) {
                n_elems = *(uint8_t *)(opaque+field->num_offset);
            }
            if (field->flags & VMS_POINTER) {
                base_addr = *(void **)base_addr + field->start;
            }
            for (i = 0; i < n_elems; i++) {
                void *addr = base_addr + size * i;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    addr = *(void **)addr;
                }
                if (field->flags & VMS_STRUCT) {
                    vmstate_save_state(f, field->vmsd, addr);
                } else {
                    field->info->put(f, addr, size);
                }
            }
        }
        field++;
    }
    vmstate_subsection_save(f, vmsd, opaque);
}

static int vmstate_load(QEMUFile *f, SaveStateEntry *se, int version_id)
{
    if (!se->vmsd) {         /* Old style */
        return se->load_state(f, se->opaque, version_id);
    }
    return vmstate_load_state(f, se->vmsd, se->opaque, version_id);
}

static void vmstate_save(QEMUFile *f, SaveStateEntry *se)
{
    if (!se->vmsd) {         /* Old style */
        se->save_state(f, se->opaque);
        return;
    }
    vmstate_save_state(f,se->vmsd, se->opaque);
}

#define QEMU_VM_FILE_MAGIC           0x5145564d
#define QEMU_VM_FILE_VERSION_COMPAT  0x00000002
#define QEMU_VM_FILE_VERSION         0x00000003

#define QEMU_VM_EOF                  0x00
#define QEMU_VM_SECTION_START        0x01
#define QEMU_VM_SECTION_PART         0x02
#define QEMU_VM_SECTION_END          0x03
#define QEMU_VM_SECTION_FULL         0x04
#define QEMU_VM_SUBSECTION           0x05

bool qemu_savevm_state_blocked(Monitor *mon)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (se->no_migrate) {
            monitor_printf(mon, "state blocked by non-migratable device '%s'\n",
                           se->idstr);
            return true;
        }
    }
    return false;
}

int qemu_savevm_state_begin(Monitor *mon, QEMUFile *f, int blk_enable,
                            int shared)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if(se->set_params == NULL) {
            continue;
	}
	se->set_params(blk_enable, shared, se->opaque);
    }
    
    qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
    qemu_put_be32(f, QEMU_VM_FILE_VERSION);

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        int len;

        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_START);
        qemu_put_be32(f, se->section_id);

        /* ID string */
        len = strlen(se->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)se->idstr, len);

        qemu_put_be32(f, se->instance_id);
        qemu_put_be32(f, se->version_id);

        se->save_live_state(mon, f, QEMU_VM_SECTION_START, se->opaque);
    }

    if (qemu_file_has_error(f)) {
        qemu_savevm_state_cancel(mon, f);
        return -EIO;
    }

    return 0;
}

int qemu_savevm_state_iterate(Monitor *mon, QEMUFile *f)
{
    SaveStateEntry *se;
    int ret = 1;

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_PART);
        qemu_put_be32(f, se->section_id);

        ret = se->save_live_state(mon, f, QEMU_VM_SECTION_PART, se->opaque);
        if (!ret) {
            /* Do not proceed to the next vmstate before this one reported
               completion of the current stage. This serializes the migration
               and reduces the probability that a faster changing state is
               synchronized over and over again. */
            break;
        }
    }

    if (ret)
        return 1;

    if (qemu_file_has_error(f)) {
        qemu_savevm_state_cancel(mon, f);
        return -EIO;
    }

    return 0;
}

int qemu_savevm_state_complete(Monitor *mon, QEMUFile *f)
{
    SaveStateEntry *se;

    cpu_synchronize_all_states();

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_END);
        qemu_put_be32(f, se->section_id);

        se->save_live_state(mon, f, QEMU_VM_SECTION_END, se->opaque);
    }

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        int len;

	if (se->save_state == NULL && se->vmsd == NULL)
	    continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_FULL);
        qemu_put_be32(f, se->section_id);

        /* ID string */
        len = strlen(se->idstr);
        qemu_put_byte(f, len);
        qemu_put_buffer(f, (uint8_t *)se->idstr, len);

        qemu_put_be32(f, se->instance_id);
        qemu_put_be32(f, se->version_id);

        vmstate_save(f, se);
    }

    qemu_put_byte(f, QEMU_VM_EOF);

    if (qemu_file_has_error(f))
        return -EIO;

    return 0;
}

void qemu_savevm_state_cancel(Monitor *mon, QEMUFile *f)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (se->save_live_state) {
            se->save_live_state(mon, f, -1, se->opaque);
        }
    }
}

static int qemu_savevm_state(Monitor *mon, QEMUFile *f)
{
    int saved_vm_running;
    int ret;

    saved_vm_running = vm_running;
    vm_stop(VMSTOP_SAVEVM);

    if (qemu_savevm_state_blocked(mon)) {
        ret = -EINVAL;
        goto out;
    }

    ret = qemu_savevm_state_begin(mon, f, 0, 0);
    if (ret < 0)
        goto out;

    do {
        ret = qemu_savevm_state_iterate(mon, f);
        if (ret < 0)
            goto out;
    } while (ret == 0);

    ret = qemu_savevm_state_complete(mon, f);

out:
    if (qemu_file_has_error(f))
        ret = -EIO;

    if (!ret && saved_vm_running)
        vm_start();

    return ret;
}

static SaveStateEntry *find_se(const char *idstr, int instance_id)
{
    SaveStateEntry *se;

    QTAILQ_FOREACH(se, &savevm_handlers, entry) {
        if (!strcmp(se->idstr, idstr) &&
            (instance_id == se->instance_id ||
             instance_id == se->alias_id))
            return se;
        /* Migrating from an older version? */
        if (strstr(se->idstr, idstr) && se->compat) {
            if (!strcmp(se->compat->idstr, idstr) &&
                (instance_id == se->compat->instance_id ||
                 instance_id == se->alias_id))
                return se;
        }
    }
    return NULL;
}

static const VMStateDescription *vmstate_get_subsection(const VMStateSubsection *sub, char *idstr)
{
    while(sub && sub->needed) {
        if (strcmp(idstr, sub->vmsd->name) == 0) {
            return sub->vmsd;
        }
        sub++;
    }
    return NULL;
}

static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque)
{
    const VMStateSubsection *sub = vmsd->subsections;

    if (!sub || !sub->needed) {
        return 0;
    }

    while (qemu_peek_byte(f) == QEMU_VM_SUBSECTION) {
        char idstr[256];
        int ret;
        uint8_t version_id, len;
        const VMStateDescription *sub_vmsd;

        qemu_get_byte(f); /* subsection */
        len = qemu_get_byte(f);
        qemu_get_buffer(f, (uint8_t *)idstr, len);
        idstr[len] = 0;
        version_id = qemu_get_be32(f);

        sub_vmsd = vmstate_get_subsection(sub, idstr);
        if (sub_vmsd == NULL) {
            return -ENOENT;
        }
        assert(!sub_vmsd->subsections);
        ret = vmstate_load_state(f, sub_vmsd, opaque, version_id);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

static void vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque)
{
    const VMStateSubsection *sub = vmsd->subsections;

    while (sub && sub->needed) {
        if (sub->needed(opaque)) {
            const VMStateDescription *vmsd = sub->vmsd;
            uint8_t len;

            qemu_put_byte(f, QEMU_VM_SUBSECTION);
            len = strlen(vmsd->name);
            qemu_put_byte(f, len);
            qemu_put_buffer(f, (uint8_t *)vmsd->name, len);
            qemu_put_be32(f, vmsd->version_id);
            assert(!vmsd->subsections);
            vmstate_save_state(f, vmsd, opaque);
        }
        sub++;
    }
}

typedef struct LoadStateEntry {
    QLIST_ENTRY(LoadStateEntry) entry;
    SaveStateEntry *se;
    int section_id;
    int version_id;
} LoadStateEntry;

int qemu_loadvm_state(QEMUFile *f)
{
    QLIST_HEAD(, LoadStateEntry) loadvm_handlers =
        QLIST_HEAD_INITIALIZER(loadvm_handlers);
    LoadStateEntry *le, *new_le;
    uint8_t section_type;
    unsigned int v;
    int ret;

    if (qemu_savevm_state_blocked(default_mon)) {
        return -EINVAL;
    }

    v = qemu_get_be32(f);
    if (v != QEMU_VM_FILE_MAGIC)
        return -EINVAL;

    v = qemu_get_be32(f);
    if (v == QEMU_VM_FILE_VERSION_COMPAT) {
        fprintf(stderr, "SaveVM v2 format is obsolete and don't work anymore\n");
        return -ENOTSUP;
    }
    if (v != QEMU_VM_FILE_VERSION)
        return -ENOTSUP;

    while ((section_type = qemu_get_byte(f)) != QEMU_VM_EOF) {
        uint32_t instance_id, version_id, section_id;
        SaveStateEntry *se;
        char idstr[257];
        int len;

        switch (section_type) {
        case QEMU_VM_SECTION_START:
        case QEMU_VM_SECTION_FULL:
            /* Read section start */
            section_id = qemu_get_be32(f);
            len = qemu_get_byte(f);
            qemu_get_buffer(f, (uint8_t *)idstr, len);
            idstr[len] = 0;
            instance_id = qemu_get_be32(f);
            version_id = qemu_get_be32(f);

            /* Find savevm section */
            se = find_se(idstr, instance_id);
            if (se == NULL) {
                fprintf(stderr, "Unknown savevm section or instance '%s' %d\n", idstr, instance_id);
                ret = -EINVAL;
                goto out;
            }

            /* Validate version */
            if (version_id > se->version_id) {
                fprintf(stderr, "savevm: unsupported version %d for '%s' v%d\n",
                        version_id, idstr, se->version_id);
                ret = -EINVAL;
                goto out;
            }

            /* Add entry */
            le = qemu_mallocz(sizeof(*le));

            le->se = se;
            le->section_id = section_id;
            le->version_id = version_id;
            QLIST_INSERT_HEAD(&loadvm_handlers, le, entry);

            ret = vmstate_load(f, le->se, le->version_id);
            if (ret < 0) {
                fprintf(stderr, "qemu: warning: error while loading state for instance 0x%x of device '%s'\n",
                        instance_id, idstr);
                goto out;
            }
            break;
        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            section_id = qemu_get_be32(f);

            QLIST_FOREACH(le, &loadvm_handlers, entry) {
                if (le->section_id == section_id) {
                    break;
                }
            }
            if (le == NULL) {
                fprintf(stderr, "Unknown savevm section %d\n", section_id);
                ret = -EINVAL;
                goto out;
            }

            ret = vmstate_load(f, le->se, le->version_id);
            if (ret < 0) {
                fprintf(stderr, "qemu: warning: error while loading state section id %d\n",
                        section_id);
                goto out;
            }
            break;
        default:
            fprintf(stderr, "Unknown savevm section type %d\n", section_type);
            ret = -EINVAL;
            goto out;
        }
    }

    cpu_synchronize_all_post_init();

    ret = 0;

out:
    QLIST_FOREACH_SAFE(le, &loadvm_handlers, entry, new_le) {
        QLIST_REMOVE(le, entry);
        qemu_free(le);
    }

    if (qemu_file_has_error(f))
        ret = -EIO;

    return ret;
}

static int bdrv_snapshot_find(BlockDriverState *bs, QEMUSnapshotInfo *sn_info,
                              const char *name)
{
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i, ret;

    ret = -ENOENT;
    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns < 0)
        return ret;
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        if (!strcmp(sn->id_str, name) || !strcmp(sn->name, name)) {
            *sn_info = *sn;
            ret = 0;
            break;
        }
    }
    qemu_free(sn_tab);
    return ret;
}

/*
 * Deletes snapshots of a given name in all opened images.
 */
static int del_existing_snapshots(Monitor *mon, const char *name)
{
    BlockDriverState *bs;
    QEMUSnapshotInfo sn1, *snapshot = &sn1;
    int ret;

    bs = NULL;
    while ((bs = bdrv_next(bs))) {
        if (bdrv_can_snapshot(bs) &&
            bdrv_snapshot_find(bs, snapshot, name) >= 0)
        {
            ret = bdrv_snapshot_delete(bs, name);
            if (ret < 0) {
                monitor_printf(mon,
                               "Error while deleting snapshot on '%s'\n",
                               bdrv_get_device_name(bs));
                return -1;
            }
        }
    }

    return 0;
}

void do_savevm(Monitor *mon, const QDict *qdict)
{
    BlockDriverState *bs, *bs1;
    QEMUSnapshotInfo sn1, *sn = &sn1, old_sn1, *old_sn = &old_sn1;
    int ret;
    QEMUFile *f;
    int saved_vm_running;
    uint32_t vm_state_size;
#ifdef _WIN32
    struct _timeb tb;
    struct tm *ptm;
#else
    struct timeval tv;
    struct tm tm;
#endif
    const char *name = qdict_get_try_str(qdict, "name");

    /* Verify if there is a device that doesn't support snapshots and is writable */
    bs = NULL;
    while ((bs = bdrv_next(bs))) {

        if (bdrv_is_removable(bs) || bdrv_is_read_only(bs)) {
            continue;
        }

        if (!bdrv_can_snapshot(bs)) {
            monitor_printf(mon, "Device '%s' is writable but does not support snapshots.\n",
                               bdrv_get_device_name(bs));
            return;
        }
    }

    bs = bdrv_snapshots();
    if (!bs) {
        monitor_printf(mon, "No block device can accept snapshots\n");
        return;
    }

    saved_vm_running = vm_running;
    vm_stop(VMSTOP_SAVEVM);

    memset(sn, 0, sizeof(*sn));

    /* fill auxiliary fields */
#ifdef _WIN32
    _ftime(&tb);
    sn->date_sec = tb.time;
    sn->date_nsec = tb.millitm * 1000000;
#else
    gettimeofday(&tv, NULL);
    sn->date_sec = tv.tv_sec;
    sn->date_nsec = tv.tv_usec * 1000;
#endif
    sn->vm_clock_nsec = qemu_get_clock_ns(vm_clock);

    if (name) {
        ret = bdrv_snapshot_find(bs, old_sn, name);
        if (ret >= 0) {
            pstrcpy(sn->name, sizeof(sn->name), old_sn->name);
            pstrcpy(sn->id_str, sizeof(sn->id_str), old_sn->id_str);
        } else {
            pstrcpy(sn->name, sizeof(sn->name), name);
        }
    } else {
#ifdef _WIN32
        ptm = localtime(&tb.time);
        strftime(sn->name, sizeof(sn->name), "vm-%Y%m%d%H%M%S", ptm);
#else
        /* cast below needed for OpenBSD where tv_sec is still 'long' */
        localtime_r((const time_t *)&tv.tv_sec, &tm);
        strftime(sn->name, sizeof(sn->name), "vm-%Y%m%d%H%M%S", &tm);
#endif
    }

    /* Delete old snapshots of the same name */
    if (name && del_existing_snapshots(mon, name) < 0) {
        goto the_end;
    }

    /* save the VM state */
    f = qemu_fopen_bdrv(bs, 1);
    if (!f) {
        monitor_printf(mon, "Could not open VM state file\n");
        goto the_end;
    }
    ret = qemu_savevm_state(mon, f);
    vm_state_size = qemu_ftell(f);
    qemu_fclose(f);
    if (ret < 0) {
        monitor_printf(mon, "Error %d while writing VM\n", ret);
        goto the_end;
    }

    /* create the snapshots */

    bs1 = NULL;
    while ((bs1 = bdrv_next(bs1))) {
        if (bdrv_can_snapshot(bs1)) {
            /* Write VM state size only to the image that contains the state */
            sn->vm_state_size = (bs == bs1 ? vm_state_size : 0);
            ret = bdrv_snapshot_create(bs1, sn);
            if (ret < 0) {
                monitor_printf(mon, "Error while creating snapshot on '%s'\n",
                               bdrv_get_device_name(bs1));
            }
        }
    }

 the_end:
    if (saved_vm_running)
        vm_start();
}

int load_vmstate(const char *name)
{
    BlockDriverState *bs, *bs_vm_state;
    QEMUSnapshotInfo sn;
    QEMUFile *f;
    int ret;

    bs_vm_state = bdrv_snapshots();
    if (!bs_vm_state) {
        error_report("No block device supports snapshots");
        return -ENOTSUP;
    }

    /* Don't even try to load empty VM states */
    ret = bdrv_snapshot_find(bs_vm_state, &sn, name);
    if (ret < 0) {
        return ret;
    } else if (sn.vm_state_size == 0) {
        error_report("This is a disk-only snapshot. Revert to it offline "
            "using qemu-img.");
        return -EINVAL;
    }

    /* Verify if there is any device that doesn't support snapshots and is
    writable and check if the requested snapshot is available too. */
    bs = NULL;
    while ((bs = bdrv_next(bs))) {

        if (bdrv_is_removable(bs) || bdrv_is_read_only(bs)) {
            continue;
        }

        if (!bdrv_can_snapshot(bs)) {
            error_report("Device '%s' is writable but does not support snapshots.",
                               bdrv_get_device_name(bs));
            return -ENOTSUP;
        }

        ret = bdrv_snapshot_find(bs, &sn, name);
        if (ret < 0) {
            error_report("Device '%s' does not have the requested snapshot '%s'",
                           bdrv_get_device_name(bs), name);
            return ret;
        }
    }

    /* Flush all IO requests so they don't interfere with the new state.  */
    qemu_aio_flush();

    bs = NULL;
    while ((bs = bdrv_next(bs))) {
        if (bdrv_can_snapshot(bs)) {
            ret = bdrv_snapshot_goto(bs, name);
            if (ret < 0) {
                error_report("Error %d while activating snapshot '%s' on '%s'",
                             ret, name, bdrv_get_device_name(bs));
                return ret;
            }
        }
    }

    /* restore the VM state */
    f = qemu_fopen_bdrv(bs_vm_state, 0);
    if (!f) {
        error_report("Could not open VM state file");
        return -EINVAL;
    }

    qemu_system_reset(VMRESET_SILENT);
    ret = qemu_loadvm_state(f);

    qemu_fclose(f);
    if (ret < 0) {
        error_report("Error %d while loading VM state", ret);
        return ret;
    }

    return 0;
}

void do_delvm(Monitor *mon, const QDict *qdict)
{
    BlockDriverState *bs, *bs1;
    int ret;
    const char *name = qdict_get_str(qdict, "name");

    bs = bdrv_snapshots();
    if (!bs) {
        monitor_printf(mon, "No block device supports snapshots\n");
        return;
    }

    bs1 = NULL;
    while ((bs1 = bdrv_next(bs1))) {
        if (bdrv_can_snapshot(bs1)) {
            ret = bdrv_snapshot_delete(bs1, name);
            if (ret < 0) {
                if (ret == -ENOTSUP)
                    monitor_printf(mon,
                                   "Snapshots not supported on device '%s'\n",
                                   bdrv_get_device_name(bs1));
                else
                    monitor_printf(mon, "Error %d while deleting snapshot on "
                                   "'%s'\n", ret, bdrv_get_device_name(bs1));
            }
        }
    }
}

void do_info_snapshots(Monitor *mon)
{
    BlockDriverState *bs, *bs1;
    QEMUSnapshotInfo *sn_tab, *sn, s, *sn_info = &s;
    int nb_sns, i, ret, available;
    int total;
    int *available_snapshots;
    char buf[256];

    bs = bdrv_snapshots();
    if (!bs) {
        monitor_printf(mon, "No available block device supports snapshots\n");
        return;
    }

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns < 0) {
        monitor_printf(mon, "bdrv_snapshot_list: error %d\n", nb_sns);
        return;
    }

    if (nb_sns == 0) {
        monitor_printf(mon, "There is no snapshot available.\n");
        return;
    }

    available_snapshots = qemu_mallocz(sizeof(int) * nb_sns);
    total = 0;
    for (i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        available = 1;
        bs1 = NULL;

        while ((bs1 = bdrv_next(bs1))) {
            if (bdrv_can_snapshot(bs1) && bs1 != bs) {
                ret = bdrv_snapshot_find(bs1, sn_info, sn->id_str);
                if (ret < 0) {
                    available = 0;
                    break;
                }
            }
        }

        if (available) {
            available_snapshots[total] = i;
            total++;
        }
    }

    if (total > 0) {
        monitor_printf(mon, "%s\n", bdrv_snapshot_dump(buf, sizeof(buf), NULL));
        for (i = 0; i < total; i++) {
            sn = &sn_tab[available_snapshots[i]];
            monitor_printf(mon, "%s\n", bdrv_snapshot_dump(buf, sizeof(buf), sn));
        }
    } else {
        monitor_printf(mon, "There is no suitable snapshot available\n");
    }

    qemu_free(sn_tab);
    qemu_free(available_snapshots);

}
