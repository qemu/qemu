/*
 * Generic FIFO component, implemented as a circular buffer.
 *
 * Copyright (c) 2012 Peter A. G. Crosthwaite
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qemu/fifo8.h"

void fifo8_reset(Fifo8 *fifo)
{
    fifo->num = 0;
    fifo->head = 0;
}

void fifo8_create(Fifo8 *fifo, uint32_t capacity)
{
    fifo->data = g_new(uint8_t, capacity);
    fifo->capacity = capacity;
    fifo8_reset(fifo);
}

void fifo8_destroy(Fifo8 *fifo)
{
    g_free(fifo->data);
}

void fifo8_push(Fifo8 *fifo, uint8_t data)
{
    assert(fifo->num < fifo->capacity);
    fifo->data[(fifo->head + fifo->num) % fifo->capacity] = data;
    fifo->num++;
}

void fifo8_push_all(Fifo8 *fifo, const uint8_t *data, uint32_t num)
{
    uint32_t start, avail;

    assert(fifo->num + num <= fifo->capacity);

    start = (fifo->head + fifo->num) % fifo->capacity;

    if (start + num <= fifo->capacity) {
        memcpy(&fifo->data[start], data, num);
    } else {
        avail = fifo->capacity - start;
        memcpy(&fifo->data[start], data, avail);
        memcpy(&fifo->data[0], &data[avail], num - avail);
    }

    fifo->num += num;
}

uint8_t fifo8_pop(Fifo8 *fifo)
{
    uint8_t ret;

    assert(fifo->num > 0);
    ret = fifo->data[fifo->head++];
    fifo->head %= fifo->capacity;
    fifo->num--;
    return ret;
}

uint8_t fifo8_peek(Fifo8 *fifo)
{
    assert(fifo->num > 0);
    return fifo->data[fifo->head];
}

static const uint8_t *fifo8_peekpop_bufptr(Fifo8 *fifo, uint32_t max,
                                           uint32_t skip, uint32_t *numptr,
                                           bool do_pop)
{
    uint8_t *ret;
    uint32_t num, head;

    assert(max > 0 && max <= fifo->num);
    assert(skip <= fifo->num);
    head = (fifo->head + skip) % fifo->capacity;
    num = MIN(fifo->capacity - head, max);
    ret = &fifo->data[head];

    if (do_pop) {
        fifo->head = head + num;
        fifo->head %= fifo->capacity;
        fifo->num -= num;
    }
    if (numptr) {
        *numptr = num;
    }
    return ret;
}

const uint8_t *fifo8_peek_bufptr(Fifo8 *fifo, uint32_t max, uint32_t *numptr)
{
    return fifo8_peekpop_bufptr(fifo, max, 0, numptr, false);
}

const uint8_t *fifo8_pop_bufptr(Fifo8 *fifo, uint32_t max, uint32_t *numptr)
{
    return fifo8_peekpop_bufptr(fifo, max, 0, numptr, true);
}

static uint32_t fifo8_peekpop_buf(Fifo8 *fifo, uint8_t *dest, uint32_t destlen,
                                  bool do_pop)
{
    const uint8_t *buf;
    uint32_t n1, n2 = 0;
    uint32_t len;

    if (destlen == 0) {
        return 0;
    }

    len = destlen;
    buf = fifo8_peekpop_bufptr(fifo, len, 0, &n1, do_pop);
    if (dest) {
        memcpy(dest, buf, n1);
    }

    /* Add FIFO wraparound if needed */
    len -= n1;
    len = MIN(len, fifo8_num_used(fifo));
    if (len) {
        buf = fifo8_peekpop_bufptr(fifo, len, do_pop ? 0 : n1, &n2, do_pop);
        if (dest) {
            memcpy(&dest[n1], buf, n2);
        }
    }

    return n1 + n2;
}

uint32_t fifo8_pop_buf(Fifo8 *fifo, uint8_t *dest, uint32_t destlen)
{
    return fifo8_peekpop_buf(fifo, dest, destlen, true);
}

uint32_t fifo8_peek_buf(Fifo8 *fifo, uint8_t *dest, uint32_t destlen)
{
    return fifo8_peekpop_buf(fifo, dest, destlen, false);
}

void fifo8_drop(Fifo8 *fifo, uint32_t len)
{
    len -= fifo8_pop_buf(fifo, NULL, len);
    assert(len == 0);
}

bool fifo8_is_empty(Fifo8 *fifo)
{
    return (fifo->num == 0);
}

bool fifo8_is_full(Fifo8 *fifo)
{
    return (fifo->num == fifo->capacity);
}

uint32_t fifo8_num_free(Fifo8 *fifo)
{
    return fifo->capacity - fifo->num;
}

uint32_t fifo8_num_used(Fifo8 *fifo)
{
    return fifo->num;
}

const VMStateDescription vmstate_fifo8 = {
    .name = "Fifo8",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(data, Fifo8, 1, NULL, capacity),
        VMSTATE_UINT32(head, Fifo8),
        VMSTATE_UINT32(num, Fifo8),
        VMSTATE_END_OF_LIST()
    }
};
