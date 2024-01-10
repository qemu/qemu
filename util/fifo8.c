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

void fifo8_create(Fifo8 *fifo, uint32_t capacity)
{
    fifo->data = g_new(uint8_t, capacity);
    fifo->capacity = capacity;
    fifo->head = 0;
    fifo->num = 0;
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

static const uint8_t *fifo8_peekpop_buf(Fifo8 *fifo, uint32_t max,
                                        uint32_t *numptr, bool do_pop)
{
    uint8_t *ret;
    uint32_t num;

    assert(max > 0 && max <= fifo->num);
    num = MIN(fifo->capacity - fifo->head, max);
    ret = &fifo->data[fifo->head];

    if (do_pop) {
        fifo->head += num;
        fifo->head %= fifo->capacity;
        fifo->num -= num;
    }
    if (numptr) {
        *numptr = num;
    }
    return ret;
}

const uint8_t *fifo8_peek_buf(Fifo8 *fifo, uint32_t max, uint32_t *numptr)
{
    return fifo8_peekpop_buf(fifo, max, numptr, false);
}

const uint8_t *fifo8_pop_buf(Fifo8 *fifo, uint32_t max, uint32_t *numptr)
{
    return fifo8_peekpop_buf(fifo, max, numptr, true);
}

void fifo8_reset(Fifo8 *fifo)
{
    fifo->num = 0;
    fifo->head = 0;
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
