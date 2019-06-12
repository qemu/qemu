/*
 * QEMU generic buffers
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QEMU_BUFFER_H
#define QEMU_BUFFER_H


typedef struct Buffer Buffer;

/**
 * Buffer:
 *
 * The Buffer object provides a simple dynamically resizing
 * array, with separate tracking of capacity and usage. This
 * is typically useful when buffering I/O or processing data.
 */

struct Buffer {
    char *name;
    size_t capacity;
    size_t offset;
    uint64_t avg_size;
    uint8_t *buffer;
};

/**
 * buffer_init:
 * @buffer: the buffer object
 * @name: buffer name
 *
 * Optionally attach a name to the buffer, to make it easier
 * to identify in debug traces.
 */
void buffer_init(Buffer *buffer, const char *name, ...)
        GCC_FMT_ATTR(2, 3);

/**
 * buffer_shrink:
 * @buffer: the buffer object
 *
 * Try to shrink the buffer.  Checks current buffer capacity and size
 * and reduces capacity in case only a fraction of the buffer is
 * actually used.
 */
void buffer_shrink(Buffer *buffer);

/**
 * buffer_reserve:
 * @buffer: the buffer object
 * @len: the minimum required free space
 *
 * Ensure that the buffer has space allocated for at least
 * @len bytes. If the current buffer is too small, it will
 * be reallocated, possibly to a larger size than requested.
 */
void buffer_reserve(Buffer *buffer, size_t len);

/**
 * buffer_reset:
 * @buffer: the buffer object
 *
 * Reset the length of the stored data to zero, but do
 * not free / reallocate the memory buffer
 */
void buffer_reset(Buffer *buffer);

/**
 * buffer_free:
 * @buffer: the buffer object
 *
 * Reset the length of the stored data to zero and also
 * free the internal memory buffer
 */
void buffer_free(Buffer *buffer);

/**
 * buffer_append:
 * @buffer: the buffer object
 * @data: the data block to append
 * @len: the length of @data in bytes
 *
 * Append the contents of @data to the end of the buffer.
 * The caller must ensure that the buffer has sufficient
 * free space for @len bytes, typically by calling the
 * buffer_reserve() method prior to appending.
 */
void buffer_append(Buffer *buffer, const void *data, size_t len);

/**
 * buffer_advance:
 * @buffer: the buffer object
 * @len: the number of bytes to skip
 *
 * Remove @len bytes of data from the head of the buffer.
 * The internal buffer will not be reallocated, so will
 * have at least @len bytes of free space after this
 * call completes
 */
void buffer_advance(Buffer *buffer, size_t len);

/**
 * buffer_end:
 * @buffer: the buffer object
 *
 * Get a pointer to the tail end of the internal buffer
 * The returned pointer is only valid until the next
 * call to buffer_reserve().
 *
 * Returns: the tail of the buffer
 */
uint8_t *buffer_end(Buffer *buffer);

/**
 * buffer_empty:
 * @buffer: the buffer object
 *
 * Determine if the buffer contains any current data
 *
 * Returns: true if the buffer holds data, false otherwise
 */
gboolean buffer_empty(Buffer *buffer);

/**
 * buffer_move_empty:
 * @to: destination buffer object
 * @from: source buffer object
 *
 * Moves buffer, without copying data.  'to' buffer must be empty.
 * 'from' buffer is empty and zero-sized on return.
 */
void buffer_move_empty(Buffer *to, Buffer *from);

/**
 * buffer_move:
 * @to: destination buffer object
 * @from: source buffer object
 *
 * Moves buffer, copying data (unless 'to' buffer happens to be empty).
 * 'from' buffer is empty and zero-sized on return.
 */
void buffer_move(Buffer *to, Buffer *from);

#endif /* QEMU_BUFFER_H */
