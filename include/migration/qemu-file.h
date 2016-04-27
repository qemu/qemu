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
#ifndef QEMU_FILE_H
#define QEMU_FILE_H 1
#include "exec/cpu-common.h"


/* This function writes a chunk of data to a file at the given position.
 * The pos argument can be ignored if the file is only being used for
 * streaming.  The handler must write all of the data or return a negative
 * errno value.
 */
typedef ssize_t (QEMUFilePutBufferFunc)(void *opaque, const uint8_t *buf,
                                        int64_t pos, size_t size);

/* Read a chunk of data from a file at the given position.  The pos argument
 * can be ignored if the file is only be used for streaming.  The number of
 * bytes actually read should be returned.
 */
typedef ssize_t (QEMUFileGetBufferFunc)(void *opaque, uint8_t *buf,
                                        int64_t pos, size_t size);

/* Close a file
 *
 * Return negative error number on error, 0 or positive value on success.
 *
 * The meaning of return value on success depends on the specific back-end being
 * used.
 */
typedef int (QEMUFileCloseFunc)(void *opaque);

/* Called to return the OS file descriptor associated to the QEMUFile.
 */
typedef int (QEMUFileGetFD)(void *opaque);

/* Called to change the blocking mode of the file
 */
typedef int (QEMUFileSetBlocking)(void *opaque, bool enabled);

/*
 * This function writes an iovec to file. The handler must write all
 * of the data or return a negative errno value.
 */
typedef ssize_t (QEMUFileWritevBufferFunc)(void *opaque, struct iovec *iov,
                                           int iovcnt, int64_t pos);

/*
 * This function provides hooks around different
 * stages of RAM migration.
 * 'opaque' is the backend specific data in QEMUFile
 * 'data' is call specific data associated with the 'flags' value
 */
typedef int (QEMURamHookFunc)(QEMUFile *f, void *opaque, uint64_t flags,
                              void *data);

/*
 * Constants used by ram_control_* hooks
 */
#define RAM_CONTROL_SETUP     0
#define RAM_CONTROL_ROUND     1
#define RAM_CONTROL_HOOK      2
#define RAM_CONTROL_FINISH    3
#define RAM_CONTROL_BLOCK_REG 4

/*
 * This function allows override of where the RAM page
 * is saved (such as RDMA, for example.)
 */
typedef size_t (QEMURamSaveFunc)(QEMUFile *f, void *opaque,
                               ram_addr_t block_offset,
                               ram_addr_t offset,
                               size_t size,
                               uint64_t *bytes_sent);

/*
 * Return a QEMUFile for comms in the opposite direction
 */
typedef QEMUFile *(QEMURetPathFunc)(void *opaque);

/*
 * Stop any read or write (depending on flags) on the underlying
 * transport on the QEMUFile.
 * Existing blocking reads/writes must be woken
 * Returns 0 on success, -err on error
 */
typedef int (QEMUFileShutdownFunc)(void *opaque, bool rd, bool wr);

typedef struct QEMUFileOps {
    QEMUFilePutBufferFunc *put_buffer;
    QEMUFileGetBufferFunc *get_buffer;
    QEMUFileCloseFunc *close;
    QEMUFileGetFD *get_fd;
    QEMUFileSetBlocking *set_blocking;
    QEMUFileWritevBufferFunc *writev_buffer;
    QEMURetPathFunc *get_return_path;
    QEMUFileShutdownFunc *shut_down;
} QEMUFileOps;

typedef struct QEMUFileHooks {
    QEMURamHookFunc *before_ram_iterate;
    QEMURamHookFunc *after_ram_iterate;
    QEMURamHookFunc *hook_ram_load;
    QEMURamSaveFunc *save_page;
} QEMUFileHooks;

struct QEMUSizedBuffer {
    struct iovec *iov;
    size_t n_iov;
    size_t size; /* total allocated size in all iov's */
    size_t used; /* number of used bytes */
};

QEMUFile *qemu_fopen_ops(void *opaque, const QEMUFileOps *ops);
QEMUFile *qemu_fopen(const char *filename, const char *mode);
QEMUFile *qemu_fdopen(int fd, const char *mode);
QEMUFile *qemu_fopen_socket(int fd, const char *mode);
QEMUFile *qemu_popen_cmd(const char *command, const char *mode);
QEMUFile *qemu_bufopen(const char *mode, QEMUSizedBuffer *input);
void qemu_file_set_hooks(QEMUFile *f, const QEMUFileHooks *hooks);
int qemu_get_fd(QEMUFile *f);
int qemu_fclose(QEMUFile *f);
int64_t qemu_ftell(QEMUFile *f);
int64_t qemu_ftell_fast(QEMUFile *f);
void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, size_t size);
void qemu_put_byte(QEMUFile *f, int v);
/*
 * put_buffer without copying the buffer.
 * The buffer should be available till it is sent asynchronously.
 */
void qemu_put_buffer_async(QEMUFile *f, const uint8_t *buf, size_t size);
bool qemu_file_mode_is_not_valid(const char *mode);
bool qemu_file_is_writable(QEMUFile *f);

QEMUSizedBuffer *qsb_create(const uint8_t *buffer, size_t len);
void qsb_free(QEMUSizedBuffer *);
size_t qsb_set_length(QEMUSizedBuffer *qsb, size_t length);
size_t qsb_get_length(const QEMUSizedBuffer *qsb);
ssize_t qsb_get_buffer(const QEMUSizedBuffer *, off_t start, size_t count,
                       uint8_t *buf);
ssize_t qsb_write_at(QEMUSizedBuffer *qsb, const uint8_t *buf,
                     off_t pos, size_t count);


/*
 * For use on files opened with qemu_bufopen
 */
const QEMUSizedBuffer *qemu_buf_get(QEMUFile *f);

static inline void qemu_put_ubyte(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, (int)v);
}

#define qemu_put_sbyte qemu_put_byte

void qemu_put_be16(QEMUFile *f, unsigned int v);
void qemu_put_be32(QEMUFile *f, unsigned int v);
void qemu_put_be64(QEMUFile *f, uint64_t v);
size_t qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset);
size_t qemu_get_buffer(QEMUFile *f, uint8_t *buf, size_t size);
size_t qemu_get_buffer_in_place(QEMUFile *f, uint8_t **buf, size_t size);
ssize_t qemu_put_compression_data(QEMUFile *f, const uint8_t *p, size_t size,
                                  int level);
int qemu_put_qemu_file(QEMUFile *f_des, QEMUFile *f_src);

/*
 * Note that you can only peek continuous bytes from where the current pointer
 * is; you aren't guaranteed to be able to peak to +n bytes unless you've
 * previously peeked +n-1.
 */
int qemu_peek_byte(QEMUFile *f, int offset);
int qemu_get_byte(QEMUFile *f);
void qemu_file_skip(QEMUFile *f, int size);
void qemu_update_position(QEMUFile *f, size_t size);

static inline unsigned int qemu_get_ubyte(QEMUFile *f)
{
    return (unsigned int)qemu_get_byte(f);
}

#define qemu_get_sbyte qemu_get_byte

unsigned int qemu_get_be16(QEMUFile *f);
unsigned int qemu_get_be32(QEMUFile *f);
uint64_t qemu_get_be64(QEMUFile *f);

int qemu_file_rate_limit(QEMUFile *f);
void qemu_file_reset_rate_limit(QEMUFile *f);
void qemu_file_set_rate_limit(QEMUFile *f, int64_t new_rate);
int64_t qemu_file_get_rate_limit(QEMUFile *f);
int qemu_file_get_error(QEMUFile *f);
void qemu_file_set_error(QEMUFile *f, int ret);
int qemu_file_shutdown(QEMUFile *f);
QEMUFile *qemu_file_get_return_path(QEMUFile *f);
void qemu_fflush(QEMUFile *f);
void qemu_file_set_blocking(QEMUFile *f, bool block);

static inline void qemu_put_be64s(QEMUFile *f, const uint64_t *pv)
{
    qemu_put_be64(f, *pv);
}

static inline void qemu_put_be32s(QEMUFile *f, const uint32_t *pv)
{
    qemu_put_be32(f, *pv);
}

static inline void qemu_put_be16s(QEMUFile *f, const uint16_t *pv)
{
    qemu_put_be16(f, *pv);
}

static inline void qemu_put_8s(QEMUFile *f, const uint8_t *pv)
{
    qemu_put_byte(f, *pv);
}

static inline void qemu_get_be64s(QEMUFile *f, uint64_t *pv)
{
    *pv = qemu_get_be64(f);
}

static inline void qemu_get_be32s(QEMUFile *f, uint32_t *pv)
{
    *pv = qemu_get_be32(f);
}

static inline void qemu_get_be16s(QEMUFile *f, uint16_t *pv)
{
    *pv = qemu_get_be16(f);
}

static inline void qemu_get_8s(QEMUFile *f, uint8_t *pv)
{
    *pv = qemu_get_byte(f);
}

// Signed versions for type safety
static inline void qemu_put_sbuffer(QEMUFile *f, const int8_t *buf, size_t size)
{
    qemu_put_buffer(f, (const uint8_t *)buf, size);
}

static inline void qemu_put_sbe16(QEMUFile *f, int v)
{
    qemu_put_be16(f, (unsigned int)v);
}

static inline void qemu_put_sbe32(QEMUFile *f, int v)
{
    qemu_put_be32(f, (unsigned int)v);
}

static inline void qemu_put_sbe64(QEMUFile *f, int64_t v)
{
    qemu_put_be64(f, (uint64_t)v);
}

static inline size_t qemu_get_sbuffer(QEMUFile *f, int8_t *buf, int size)
{
    return qemu_get_buffer(f, (uint8_t *)buf, size);
}

static inline int qemu_get_sbe16(QEMUFile *f)
{
    return (int)qemu_get_be16(f);
}

static inline int qemu_get_sbe32(QEMUFile *f)
{
    return (int)qemu_get_be32(f);
}

static inline int64_t qemu_get_sbe64(QEMUFile *f)
{
    return (int64_t)qemu_get_be64(f);
}

static inline void qemu_put_s8s(QEMUFile *f, const int8_t *pv)
{
    qemu_put_8s(f, (const uint8_t *)pv);
}

static inline void qemu_put_sbe16s(QEMUFile *f, const int16_t *pv)
{
    qemu_put_be16s(f, (const uint16_t *)pv);
}

static inline void qemu_put_sbe32s(QEMUFile *f, const int32_t *pv)
{
    qemu_put_be32s(f, (const uint32_t *)pv);
}

static inline void qemu_put_sbe64s(QEMUFile *f, const int64_t *pv)
{
    qemu_put_be64s(f, (const uint64_t *)pv);
}

static inline void qemu_get_s8s(QEMUFile *f, int8_t *pv)
{
    qemu_get_8s(f, (uint8_t *)pv);
}

static inline void qemu_get_sbe16s(QEMUFile *f, int16_t *pv)
{
    qemu_get_be16s(f, (uint16_t *)pv);
}

static inline void qemu_get_sbe32s(QEMUFile *f, int32_t *pv)
{
    qemu_get_be32s(f, (uint32_t *)pv);
}

static inline void qemu_get_sbe64s(QEMUFile *f, int64_t *pv)
{
    qemu_get_be64s(f, (uint64_t *)pv);
}

size_t qemu_get_counted_string(QEMUFile *f, char buf[256]);

#endif
