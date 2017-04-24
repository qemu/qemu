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

#ifndef MIGRATION_QEMU_FILE_H
#define MIGRATION_QEMU_FILE_H

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
    QEMUFileGetBufferFunc *get_buffer;
    QEMUFileCloseFunc *close;
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

QEMUFile *qemu_fopen_ops(void *opaque, const QEMUFileOps *ops);
void qemu_file_set_hooks(QEMUFile *f, const QEMUFileHooks *hooks);
int qemu_get_fd(QEMUFile *f);
int qemu_fclose(QEMUFile *f);
int64_t qemu_ftell(QEMUFile *f);
int64_t qemu_ftell_fast(QEMUFile *f);
/*
 * put_buffer without copying the buffer.
 * The buffer should be available till it is sent asynchronously.
 */
void qemu_put_buffer_async(QEMUFile *f, const uint8_t *buf, size_t size,
                           bool may_free);
bool qemu_file_mode_is_not_valid(const char *mode);
bool qemu_file_is_writable(QEMUFile *f);

#include "migration/qemu-file-types.h"

size_t qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset);
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
void qemu_file_skip(QEMUFile *f, int size);
void qemu_update_position(QEMUFile *f, size_t size);
void qemu_file_reset_rate_limit(QEMUFile *f);
void qemu_file_set_rate_limit(QEMUFile *f, int64_t new_rate);
int64_t qemu_file_get_rate_limit(QEMUFile *f);
int qemu_file_get_error(QEMUFile *f);
void qemu_file_set_error(QEMUFile *f, int ret);
int qemu_file_shutdown(QEMUFile *f);
QEMUFile *qemu_file_get_return_path(QEMUFile *f);
void qemu_fflush(QEMUFile *f);
void qemu_file_set_blocking(QEMUFile *f, bool block);

size_t qemu_get_counted_string(QEMUFile *f, char buf[256]);

void ram_control_before_iterate(QEMUFile *f, uint64_t flags);
void ram_control_after_iterate(QEMUFile *f, uint64_t flags);
void ram_control_load_hook(QEMUFile *f, uint64_t flags, void *data);

/* Whenever this is found in the data stream, the flags
 * will be passed to ram_control_load_hook in the incoming-migration
 * side. This lets before_ram_iterate/after_ram_iterate add
 * transport-specific sections to the RAM migration data.
 */
#define RAM_SAVE_FLAG_HOOK     0x80

#define RAM_SAVE_CONTROL_NOT_SUPP -1000
#define RAM_SAVE_CONTROL_DELAYED  -2000

size_t ram_control_save_page(QEMUFile *f, ram_addr_t block_offset,
                             ram_addr_t offset, size_t size,
                             uint64_t *bytes_sent);

#endif
