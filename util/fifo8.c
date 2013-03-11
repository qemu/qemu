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

#include "qemu-common.h"
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
    if (fifo->num == fifo->capacity) {
        abort();
    }
    fifo->data[(fifo->head + fifo->num) % fifo->capacity] = data;
    fifo->num++;
}

uint8_t fifo8_pop(Fifo8 *fifo)
{
    uint8_t ret;

    if (fifo->num == 0) {
        abort();
    }
    ret = fifo->data[fifo->head++];
    fifo->head %= fifo->capacity;
    fifo->num--;
    return ret;
}

void fifo8_reset(Fifo8 *fifo)
{
    fifo->num = 0;
}

bool fifo8_is_empty(Fifo8 *fifo)
{
    return (fifo->num == 0);
}

bool fifo8_is_full(Fifo8 *fifo)
{
    return (fifo->num == fifo->capacity);
}

const VMStateDescription vmstate_fifo8 = {
    .name = "Fifo8",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(data, Fifo8, 1, NULL, 0, capacity),
        VMSTATE_UINT32(head, Fifo8),
        VMSTATE_UINT32(num, Fifo8),
        VMSTATE_END_OF_LIST()
    }
};
