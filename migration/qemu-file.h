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

#include <zlib.h>
#include "exec/cpu-common.h"
#include "io/channel.h"

QEMUFile *qemu_file_new_input(QIOChannel *ioc);
QEMUFile *qemu_file_new_output(QIOChannel *ioc);
int qemu_fclose(QEMUFile *f);

/*
 * qemu_file_transferred:
 *
 * No flush is performed and the reported amount will include the size
 * of any queued buffers, on top of the amount actually transferred.
 *
 * Returns: the total bytes transferred and queued
 */
uint64_t qemu_file_transferred(QEMUFile *f);

/*
 * put_buffer without copying the buffer.
 * The buffer should be available till it is sent asynchronously.
 */
void qemu_put_buffer_async(QEMUFile *f, const uint8_t *buf, size_t size,
                           bool may_free);

#include "migration/qemu-file-types.h"

size_t coroutine_mixed_fn qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset);
size_t coroutine_mixed_fn qemu_get_buffer_in_place(QEMUFile *f, uint8_t **buf, size_t size);
ssize_t qemu_put_compression_data(QEMUFile *f, z_stream *stream,
                                  const uint8_t *p, size_t size);
int qemu_put_qemu_file(QEMUFile *f_des, QEMUFile *f_src);
bool qemu_file_buffer_empty(QEMUFile *file);

/*
 * Note that you can only peek continuous bytes from where the current pointer
 * is; you aren't guaranteed to be able to peak to +n bytes unless you've
 * previously peeked +n-1.
 */
int coroutine_mixed_fn qemu_peek_byte(QEMUFile *f, int offset);
void qemu_file_skip(QEMUFile *f, int size);
int qemu_file_get_error_obj_any(QEMUFile *f1, QEMUFile *f2, Error **errp);
void qemu_file_set_error_obj(QEMUFile *f, int ret, Error *err);
int qemu_file_get_error_obj(QEMUFile *f, Error **errp);
void qemu_file_set_error(QEMUFile *f, int ret);
int qemu_file_shutdown(QEMUFile *f);
QEMUFile *qemu_file_get_return_path(QEMUFile *f);
int qemu_fflush(QEMUFile *f);
void qemu_file_set_blocking(QEMUFile *f, bool block);
int qemu_file_get_to_fd(QEMUFile *f, int fd, size_t size);
void qemu_set_offset(QEMUFile *f, off_t off, int whence);
off_t qemu_get_offset(QEMUFile *f);
void qemu_put_buffer_at(QEMUFile *f, const uint8_t *buf, size_t buflen,
                        off_t pos);
size_t qemu_get_buffer_at(QEMUFile *f, const uint8_t *buf, size_t buflen,
                          off_t pos);

QIOChannel *qemu_file_get_ioc(QEMUFile *file);

#endif
