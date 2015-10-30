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

#include "qemu/buffer.h"

#define BUFFER_MIN_INIT_SIZE     4096
#define BUFFER_MIN_SHRINK_SIZE  65536

void buffer_init(Buffer *buffer, const char *name, ...)
{
    va_list ap;

    va_start(ap, name);
    buffer->name = g_strdup_vprintf(name, ap);
    va_end(ap);
}

void buffer_shrink(Buffer *buffer)
{
    /*
     * Only shrink in case the used size is *much* smaller than the
     * capacity, to avoid bumping up & down the buffers all the time.
     * realloc() isn't exactly cheap ...
     */
    if (buffer->offset < (buffer->capacity >> 3) &&
        buffer->capacity > BUFFER_MIN_SHRINK_SIZE) {
        return;
    }

    buffer->capacity = pow2ceil(buffer->offset);
    buffer->capacity = MAX(buffer->capacity, BUFFER_MIN_SHRINK_SIZE);
    buffer->buffer = g_realloc(buffer->buffer, buffer->capacity);
}

void buffer_reserve(Buffer *buffer, size_t len)
{
    if ((buffer->capacity - buffer->offset) < len) {
        buffer->capacity = pow2ceil(buffer->offset + len);
        buffer->capacity = MAX(buffer->capacity, BUFFER_MIN_INIT_SIZE);
        buffer->buffer = g_realloc(buffer->buffer, buffer->capacity);
    }
}

gboolean buffer_empty(Buffer *buffer)
{
    return buffer->offset == 0;
}

uint8_t *buffer_end(Buffer *buffer)
{
    return buffer->buffer + buffer->offset;
}

void buffer_reset(Buffer *buffer)
{
    buffer->offset = 0;
}

void buffer_free(Buffer *buffer)
{
    g_free(buffer->buffer);
    g_free(buffer->name);
    buffer->offset = 0;
    buffer->capacity = 0;
    buffer->buffer = NULL;
    buffer->name = NULL;
}

void buffer_append(Buffer *buffer, const void *data, size_t len)
{
    memcpy(buffer->buffer + buffer->offset, data, len);
    buffer->offset += len;
}

void buffer_advance(Buffer *buffer, size_t len)
{
    memmove(buffer->buffer, buffer->buffer + len,
            (buffer->offset - len));
    buffer->offset -= len;
}

void buffer_move_empty(Buffer *to, Buffer *from)
{
    assert(to->offset == 0);

    g_free(to->buffer);
    to->offset = from->offset;
    to->capacity = from->capacity;
    to->buffer = from->buffer;

    from->offset = 0;
    from->capacity = 0;
    from->buffer = NULL;
}

void buffer_move(Buffer *to, Buffer *from)
{
    if (to->offset == 0) {
        buffer_move_empty(to, from);
        return;
    }

    buffer_reserve(to, from->offset);
    buffer_append(to, from->buffer, from->offset);

    g_free(from->buffer);
    from->offset = 0;
    from->capacity = 0;
    from->buffer = NULL;
}
