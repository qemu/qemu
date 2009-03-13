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
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <zlib.h>

/* Needed early for HOST_BSD etc. */
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
#if defined(__NetBSD__)
#include <net/if_tap.h>
#endif
#ifdef __linux__
#include <linux/if_tun.h>
#endif
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/select.h>
#ifdef HOST_BSD
#include <sys/stat.h>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <libutil.h>
#else
#include <util.h>
#endif
#elif defined (__GLIBC__) && defined (__FreeBSD_kernel__)
#include <freebsd/stdlib.h>
#else
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
#include "net.h"
#include "monitor.h"
#include "sysemu.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "block.h"
#include "audio/audio.h"
#include "migration.h"
#include "qemu_socket.h"

/* point to the block driver where the snapshots are managed */
static BlockDriverState *bs_snapshots;

#define SELF_ANNOUNCE_ROUNDS 5
#define ETH_P_EXPERIMENTAL 0x01F1 /* just a number */
//#define ETH_P_EXPERIMENTAL 0x0012 /* make it the size of the packet */
#define EXPERIMENTAL_MAGIC 0xf1f23f4f

static int announce_self_create(uint8_t *buf, 
				uint8_t *mac_addr)
{
    uint32_t magic = EXPERIMENTAL_MAGIC;
    uint16_t proto = htons(ETH_P_EXPERIMENTAL);

    /* FIXME: should we send a different packet (arp/rarp/ping)? */

    memset(buf, 0xff, 6);         /* h_dst */
    memcpy(buf + 6, mac_addr, 6); /* h_src */
    memcpy(buf + 12, &proto, 2);  /* h_proto */
    memcpy(buf + 14, &magic, 4);  /* magic */

    return 18; /* len */
}

void qemu_announce_self(void)
{
    int i, j, len;
    VLANState *vlan;
    VLANClientState *vc;
    uint8_t buf[256];

    for (i = 0; i < MAX_NICS; i++) {
        if (!nd_table[i].used)
            continue;
        len = announce_self_create(buf, nd_table[i].macaddr);
        vlan = nd_table[i].vlan;
        for(vc = vlan->first_client; vc != NULL; vc = vc->next) {
            for (j=0; j < SELF_ANNOUNCE_ROUNDS; j++)
                vc->fd_read(vc->opaque, buf, len);
        }
    }
}

/***********************************************************/
/* savevm/loadvm support */

#define IO_BUF_SIZE 32768

struct QEMUFile {
    QEMUFilePutBufferFunc *put_buffer;
    QEMUFileGetBufferFunc *get_buffer;
    QEMUFileCloseFunc *close;
    QEMUFileRateLimit *rate_limit;
    void *opaque;
    int is_write;

    int64_t buf_offset; /* start of buffer when writing, end of buffer
                           when reading */
    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t buf[IO_BUF_SIZE];

    int has_error;
};

typedef struct QEMUFilePopen
{
    FILE *popen_file;
    QEMUFile *file;
} QEMUFilePopen;

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
        len = recv(s->fd, buf, size, 0);
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

static int popen_put_buffer(void *opaque, const uint8_t *buf, int64_t pos, int size)
{
    QEMUFilePopen *s = opaque;
    return fwrite(buf, 1, size, s->popen_file);
}

static int popen_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFilePopen *s = opaque;
    return fread(buf, 1, size, s->popen_file);
}

static int popen_close(void *opaque)
{
    QEMUFilePopen *s = opaque;
    pclose(s->popen_file);
    qemu_free(s);
    return 0;
}

QEMUFile *qemu_popen(FILE *popen_file, const char *mode)
{
    QEMUFilePopen *s;

    if (popen_file == NULL || mode == NULL || (mode[0] != 'r' && mode[0] != 'w') || mode[1] != 0) {
        fprintf(stderr, "qemu_popen: Argument validity check failed\n");
        return NULL;
    }

    s = qemu_mallocz(sizeof(QEMUFilePopen));

    s->popen_file = popen_file;

    if(mode[0] == 'r') {
        s->file = qemu_fopen_ops(s, NULL, popen_get_buffer, popen_close, NULL);
    } else {
        s->file = qemu_fopen_ops(s, popen_put_buffer, NULL, popen_close, NULL);
    }
    fprintf(stderr, "qemu_popen: returning result of qemu_fopen_ops\n");
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

QEMUFile *qemu_fopen_socket(int fd)
{
    QEMUFileSocket *s = qemu_mallocz(sizeof(QEMUFileSocket));

    s->fd = fd;
    s->file = qemu_fopen_ops(s, NULL, socket_get_buffer, socket_close, NULL);
    return s->file;
}

typedef struct QEMUFileStdio
{
    FILE *outfile;
} QEMUFileStdio;

static int file_put_buffer(void *opaque, const uint8_t *buf,
                            int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->outfile, pos, SEEK_SET);
    fwrite(buf, 1, size, s->outfile);
    return size;
}

static int file_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileStdio *s = opaque;
    fseek(s->outfile, pos, SEEK_SET);
    return fread(buf, 1, size, s->outfile);
}

static int file_close(void *opaque)
{
    QEMUFileStdio *s = opaque;
    fclose(s->outfile);
    qemu_free(s);
    return 0;
}

QEMUFile *qemu_fopen(const char *filename, const char *mode)
{
    QEMUFileStdio *s;

    s = qemu_mallocz(sizeof(QEMUFileStdio));

    s->outfile = fopen(filename, mode);
    if (!s->outfile)
        goto fail;

    if (!strcmp(mode, "wb"))
        return qemu_fopen_ops(s, file_put_buffer, NULL, file_close, NULL);
    else if (!strcmp(mode, "rb"))
        return qemu_fopen_ops(s, NULL, file_get_buffer, file_close, NULL);

fail:
    if (s->outfile)
        fclose(s->outfile);
    qemu_free(s);
    return NULL;
}

typedef struct QEMUFileBdrv
{
    BlockDriverState *bs;
    int64_t base_offset;
} QEMUFileBdrv;

static int bdrv_put_buffer(void *opaque, const uint8_t *buf,
                           int64_t pos, int size)
{
    QEMUFileBdrv *s = opaque;
    bdrv_pwrite(s->bs, s->base_offset + pos, buf, size);
    return size;
}

static int bdrv_get_buffer(void *opaque, uint8_t *buf, int64_t pos, int size)
{
    QEMUFileBdrv *s = opaque;
    return bdrv_pread(s->bs, s->base_offset + pos, buf, size);
}

static int bdrv_fclose(void *opaque)
{
    QEMUFileBdrv *s = opaque;
    qemu_free(s);
    return 0;
}

static QEMUFile *qemu_fopen_bdrv(BlockDriverState *bs, int64_t offset, int is_writable)
{
    QEMUFileBdrv *s;

    s = qemu_mallocz(sizeof(QEMUFileBdrv));

    s->bs = bs;
    s->base_offset = offset;

    if (is_writable)
        return qemu_fopen_ops(s, bdrv_put_buffer, NULL, bdrv_fclose, NULL);

    return qemu_fopen_ops(s, NULL, bdrv_get_buffer, bdrv_fclose, NULL);
}

QEMUFile *qemu_fopen_ops(void *opaque, QEMUFilePutBufferFunc *put_buffer,
                         QEMUFileGetBufferFunc *get_buffer,
                         QEMUFileCloseFunc *close,
                         QEMUFileRateLimit *rate_limit)
{
    QEMUFile *f;

    f = qemu_mallocz(sizeof(QEMUFile));

    f->opaque = opaque;
    f->put_buffer = put_buffer;
    f->get_buffer = get_buffer;
    f->close = close;
    f->rate_limit = rate_limit;
    f->is_write = 0;

    return f;
}

int qemu_file_has_error(QEMUFile *f)
{
    return f->has_error;
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

typedef struct SaveStateEntry {
    char idstr[256];
    int instance_id;
    int version_id;
    int section_id;
    SaveLiveStateHandler *save_live_state;
    SaveStateHandler *save_state;
    LoadStateHandler *load_state;
    void *opaque;
    struct SaveStateEntry *next;
} SaveStateEntry;

static SaveStateEntry *first_se;

/* TODO: Individual devices generally have very little idea about the rest
   of the system, so instance_id should be removed/replaced.
   Meanwhile pass -1 as instance_id if you do not already have a clearly
   distinguishing id for all instances of your device class. */
int register_savevm_live(const char *idstr,
                         int instance_id,
                         int version_id,
                         SaveLiveStateHandler *save_live_state,
                         SaveStateHandler *save_state,
                         LoadStateHandler *load_state,
                         void *opaque)
{
    SaveStateEntry *se, **pse;
    static int global_section_id;

    se = qemu_malloc(sizeof(SaveStateEntry));
    pstrcpy(se->idstr, sizeof(se->idstr), idstr);
    se->instance_id = (instance_id == -1) ? 0 : instance_id;
    se->version_id = version_id;
    se->section_id = global_section_id++;
    se->save_live_state = save_live_state;
    se->save_state = save_state;
    se->load_state = load_state;
    se->opaque = opaque;
    se->next = NULL;

    /* add at the end of list */
    pse = &first_se;
    while (*pse != NULL) {
        if (instance_id == -1
                && strcmp(se->idstr, (*pse)->idstr) == 0
                && se->instance_id <= (*pse)->instance_id)
            se->instance_id = (*pse)->instance_id + 1;
        pse = &(*pse)->next;
    }
    *pse = se;
    return 0;
}

int register_savevm(const char *idstr,
                    int instance_id,
                    int version_id,
                    SaveStateHandler *save_state,
                    LoadStateHandler *load_state,
                    void *opaque)
{
    return register_savevm_live(idstr, instance_id, version_id,
                                NULL, save_state, load_state, opaque);
}

#define QEMU_VM_FILE_MAGIC           0x5145564d
#define QEMU_VM_FILE_VERSION_COMPAT  0x00000002
#define QEMU_VM_FILE_VERSION         0x00000003

#define QEMU_VM_EOF                  0x00
#define QEMU_VM_SECTION_START        0x01
#define QEMU_VM_SECTION_PART         0x02
#define QEMU_VM_SECTION_END          0x03
#define QEMU_VM_SECTION_FULL         0x04

int qemu_savevm_state_begin(QEMUFile *f)
{
    SaveStateEntry *se;

    qemu_put_be32(f, QEMU_VM_FILE_MAGIC);
    qemu_put_be32(f, QEMU_VM_FILE_VERSION);

    for (se = first_se; se != NULL; se = se->next) {
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

        se->save_live_state(f, QEMU_VM_SECTION_START, se->opaque);
    }

    if (qemu_file_has_error(f))
        return -EIO;

    return 0;
}

int qemu_savevm_state_iterate(QEMUFile *f)
{
    SaveStateEntry *se;
    int ret = 1;

    for (se = first_se; se != NULL; se = se->next) {
        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_PART);
        qemu_put_be32(f, se->section_id);

        ret &= !!se->save_live_state(f, QEMU_VM_SECTION_PART, se->opaque);
    }

    if (ret)
        return 1;

    if (qemu_file_has_error(f))
        return -EIO;

    return 0;
}

int qemu_savevm_state_complete(QEMUFile *f)
{
    SaveStateEntry *se;

    for (se = first_se; se != NULL; se = se->next) {
        if (se->save_live_state == NULL)
            continue;

        /* Section type */
        qemu_put_byte(f, QEMU_VM_SECTION_END);
        qemu_put_be32(f, se->section_id);

        se->save_live_state(f, QEMU_VM_SECTION_END, se->opaque);
    }

    for(se = first_se; se != NULL; se = se->next) {
        int len;

	if (se->save_state == NULL)
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

        se->save_state(f, se->opaque);
    }

    qemu_put_byte(f, QEMU_VM_EOF);

    if (qemu_file_has_error(f))
        return -EIO;

    return 0;
}

int qemu_savevm_state(QEMUFile *f)
{
    int saved_vm_running;
    int ret;

    saved_vm_running = vm_running;
    vm_stop(0);

    bdrv_flush_all();

    ret = qemu_savevm_state_begin(f);
    if (ret < 0)
        goto out;

    do {
        ret = qemu_savevm_state_iterate(f);
        if (ret < 0)
            goto out;
    } while (ret == 0);

    ret = qemu_savevm_state_complete(f);

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

    for(se = first_se; se != NULL; se = se->next) {
        if (!strcmp(se->idstr, idstr) &&
            instance_id == se->instance_id)
            return se;
    }
    return NULL;
}

typedef struct LoadStateEntry {
    SaveStateEntry *se;
    int section_id;
    int version_id;
    struct LoadStateEntry *next;
} LoadStateEntry;

static int qemu_loadvm_state_v2(QEMUFile *f)
{
    SaveStateEntry *se;
    int len, ret, instance_id, record_len, version_id;
    int64_t total_len, end_pos, cur_pos;
    char idstr[256];

    total_len = qemu_get_be64(f);
    end_pos = total_len + qemu_ftell(f);
    for(;;) {
        if (qemu_ftell(f) >= end_pos)
            break;
        len = qemu_get_byte(f);
        qemu_get_buffer(f, (uint8_t *)idstr, len);
        idstr[len] = '\0';
        instance_id = qemu_get_be32(f);
        version_id = qemu_get_be32(f);
        record_len = qemu_get_be32(f);
        cur_pos = qemu_ftell(f);
        se = find_se(idstr, instance_id);
        if (!se) {
            fprintf(stderr, "qemu: warning: instance 0x%x of device '%s' not present in current VM\n",
                    instance_id, idstr);
        } else {
            ret = se->load_state(f, se->opaque, version_id);
            if (ret < 0) {
                fprintf(stderr, "qemu: warning: error while loading state for instance 0x%x of device '%s'\n",
                        instance_id, idstr);
            }
        }
        /* always seek to exact end of record */
        qemu_fseek(f, cur_pos + record_len, SEEK_SET);
    }

    if (qemu_file_has_error(f))
        return -EIO;

    return 0;
}

int qemu_loadvm_state(QEMUFile *f)
{
    LoadStateEntry *first_le = NULL;
    uint8_t section_type;
    unsigned int v;
    int ret;

    v = qemu_get_be32(f);
    if (v != QEMU_VM_FILE_MAGIC)
        return -EINVAL;

    v = qemu_get_be32(f);
    if (v == QEMU_VM_FILE_VERSION_COMPAT)
        return qemu_loadvm_state_v2(f);
    if (v != QEMU_VM_FILE_VERSION)
        return -ENOTSUP;

    while ((section_type = qemu_get_byte(f)) != QEMU_VM_EOF) {
        uint32_t instance_id, version_id, section_id;
        LoadStateEntry *le;
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
            le->next = first_le;
            first_le = le;

            le->se->load_state(f, le->se->opaque, le->version_id);
            break;
        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            section_id = qemu_get_be32(f);

            for (le = first_le; le && le->section_id != section_id; le = le->next);
            if (le == NULL) {
                fprintf(stderr, "Unknown savevm section %d\n", section_id);
                ret = -EINVAL;
                goto out;
            }

            le->se->load_state(f, le->se->opaque, le->version_id);
            break;
        default:
            fprintf(stderr, "Unknown savevm section type %d\n", section_type);
            ret = -EINVAL;
            goto out;
        }
    }

    ret = 0;

out:
    while (first_le) {
        LoadStateEntry *le = first_le;
        first_le = first_le->next;
        qemu_free(le);
    }

    if (qemu_file_has_error(f))
        ret = -EIO;

    return ret;
}

/* device can contain snapshots */
static int bdrv_can_snapshot(BlockDriverState *bs)
{
    return (bs &&
            !bdrv_is_removable(bs) &&
            !bdrv_is_read_only(bs));
}

/* device must be snapshots in order to have a reliable snapshot */
static int bdrv_has_snapshot(BlockDriverState *bs)
{
    return (bs &&
            !bdrv_is_removable(bs) &&
            !bdrv_is_read_only(bs));
}

static BlockDriverState *get_bs_snapshots(void)
{
    BlockDriverState *bs;
    int i;

    if (bs_snapshots)
        return bs_snapshots;
    for(i = 0; i <= nb_drives; i++) {
        bs = drives_table[i].bdrv;
        if (bdrv_can_snapshot(bs))
            goto ok;
    }
    return NULL;
 ok:
    bs_snapshots = bs;
    return bs;
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

void do_savevm(Monitor *mon, const char *name)
{
    BlockDriverState *bs, *bs1;
    QEMUSnapshotInfo sn1, *sn = &sn1, old_sn1, *old_sn = &old_sn1;
    int must_delete, ret, i;
    BlockDriverInfo bdi1, *bdi = &bdi1;
    QEMUFile *f;
    int saved_vm_running;
    uint32_t vm_state_size;
#ifdef _WIN32
    struct _timeb tb;
#else
    struct timeval tv;
#endif

    bs = get_bs_snapshots();
    if (!bs) {
        monitor_printf(mon, "No block device can accept snapshots\n");
        return;
    }

    /* ??? Should this occur after vm_stop?  */
    qemu_aio_flush();

    saved_vm_running = vm_running;
    vm_stop(0);

    must_delete = 0;
    if (name) {
        ret = bdrv_snapshot_find(bs, old_sn, name);
        if (ret >= 0) {
            must_delete = 1;
        }
    }
    memset(sn, 0, sizeof(*sn));
    if (must_delete) {
        pstrcpy(sn->name, sizeof(sn->name), old_sn->name);
        pstrcpy(sn->id_str, sizeof(sn->id_str), old_sn->id_str);
    } else {
        if (name)
            pstrcpy(sn->name, sizeof(sn->name), name);
    }

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
    sn->vm_clock_nsec = qemu_get_clock(vm_clock);

    if (bdrv_get_info(bs, bdi) < 0 || bdi->vm_state_offset <= 0) {
        monitor_printf(mon, "Device %s does not support VM state snapshots\n",
                       bdrv_get_device_name(bs));
        goto the_end;
    }

    /* save the VM state */
    f = qemu_fopen_bdrv(bs, bdi->vm_state_offset, 1);
    if (!f) {
        monitor_printf(mon, "Could not open VM state file\n");
        goto the_end;
    }
    ret = qemu_savevm_state(f);
    vm_state_size = qemu_ftell(f);
    qemu_fclose(f);
    if (ret < 0) {
        monitor_printf(mon, "Error %d while writing VM\n", ret);
        goto the_end;
    }

    /* create the snapshots */

    for(i = 0; i < nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
            if (must_delete) {
                ret = bdrv_snapshot_delete(bs1, old_sn->id_str);
                if (ret < 0) {
                    monitor_printf(mon,
                                   "Error while deleting snapshot on '%s'\n",
                                   bdrv_get_device_name(bs1));
                }
            }
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

void do_loadvm(Monitor *mon, const char *name)
{
    BlockDriverState *bs, *bs1;
    BlockDriverInfo bdi1, *bdi = &bdi1;
    QEMUSnapshotInfo sn;
    QEMUFile *f;
    int i, ret;
    int saved_vm_running;

    bs = get_bs_snapshots();
    if (!bs) {
        monitor_printf(mon, "No block device supports snapshots\n");
        return;
    }

    /* Flush all IO requests so they don't interfere with the new state.  */
    qemu_aio_flush();

    saved_vm_running = vm_running;
    vm_stop(0);

    for(i = 0; i <= nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
            ret = bdrv_snapshot_goto(bs1, name);
            if (ret < 0) {
                if (bs != bs1)
                    monitor_printf(mon, "Warning: ");
                switch(ret) {
                case -ENOTSUP:
                    monitor_printf(mon,
                                   "Snapshots not supported on device '%s'\n",
                                   bdrv_get_device_name(bs1));
                    break;
                case -ENOENT:
                    monitor_printf(mon, "Could not find snapshot '%s' on "
                                   "device '%s'\n",
                                   name, bdrv_get_device_name(bs1));
                    break;
                default:
                    monitor_printf(mon, "Error %d while activating snapshot on"
                                   " '%s'\n", ret, bdrv_get_device_name(bs1));
                    break;
                }
                /* fatal on snapshot block device */
                if (bs == bs1)
                    goto the_end;
            }
        }
    }

    if (bdrv_get_info(bs, bdi) < 0 || bdi->vm_state_offset <= 0) {
        monitor_printf(mon, "Device %s does not support VM state snapshots\n",
                       bdrv_get_device_name(bs));
        return;
    }

    /* Don't even try to load empty VM states */
    ret = bdrv_snapshot_find(bs, &sn, name);
    if ((ret >= 0) && (sn.vm_state_size == 0))
        goto the_end;

    /* restore the VM state */
    f = qemu_fopen_bdrv(bs, bdi->vm_state_offset, 0);
    if (!f) {
        monitor_printf(mon, "Could not open VM state file\n");
        goto the_end;
    }
    ret = qemu_loadvm_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        monitor_printf(mon, "Error %d while loading VM state\n", ret);
    }
 the_end:
    if (saved_vm_running)
        vm_start();
}

void do_delvm(Monitor *mon, const char *name)
{
    BlockDriverState *bs, *bs1;
    int i, ret;

    bs = get_bs_snapshots();
    if (!bs) {
        monitor_printf(mon, "No block device supports snapshots\n");
        return;
    }

    for(i = 0; i <= nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
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
    QEMUSnapshotInfo *sn_tab, *sn;
    int nb_sns, i;
    char buf[256];

    bs = get_bs_snapshots();
    if (!bs) {
        monitor_printf(mon, "No available block device supports snapshots\n");
        return;
    }
    monitor_printf(mon, "Snapshot devices:");
    for(i = 0; i <= nb_drives; i++) {
        bs1 = drives_table[i].bdrv;
        if (bdrv_has_snapshot(bs1)) {
            if (bs == bs1)
                monitor_printf(mon, " %s", bdrv_get_device_name(bs1));
        }
    }
    monitor_printf(mon, "\n");

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);
    if (nb_sns < 0) {
        monitor_printf(mon, "bdrv_snapshot_list: error %d\n", nb_sns);
        return;
    }
    monitor_printf(mon, "Snapshot list (from %s):\n",
                   bdrv_get_device_name(bs));
    monitor_printf(mon, "%s\n", bdrv_snapshot_dump(buf, sizeof(buf), NULL));
    for(i = 0; i < nb_sns; i++) {
        sn = &sn_tab[i];
        monitor_printf(mon, "%s\n", bdrv_snapshot_dump(buf, sizeof(buf), sn));
    }
    qemu_free(sn_tab);
}
