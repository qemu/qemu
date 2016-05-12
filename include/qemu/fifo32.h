/*
 * Generic FIFO32 component, based on FIFO8.
 *
 * Copyright (c) 2016 Jean-Christophe Dubois
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FIFO32_H
#define FIFO32_H

#include "qemu/osdep.h"
#include "qemu/fifo8.h"

typedef struct {
    Fifo8 fifo;
} Fifo32;

/**
 * fifo32_create:
 * @fifo: struct Fifo32 to initialise with new FIFO
 * @capacity: capacity of the newly created FIFO expressed in 32 bit words
 *
 * Create a FIFO of the specified size. Clients should call fifo32_destroy()
 * when finished using the fifo. The FIFO is initially empty.
 */

static inline void fifo32_create(Fifo32 *fifo, uint32_t capacity)
{
    fifo8_create(&fifo->fifo, capacity * sizeof(uint32_t));
}

/**
 * fifo32_destroy:
 * @fifo: FIFO to cleanup
 *
 * Cleanup a FIFO created with fifo32_create(). Frees memory created for FIFO
 * storage. The FIFO is no longer usable after this has been called.
 */

static inline void fifo32_destroy(Fifo32 *fifo)
{
    fifo8_destroy(&fifo->fifo);
}

/**
 * fifo32_num_free:
 * @fifo: FIFO to check
 *
 * Return the number of free uint32_t slots in the FIFO.
 *
 * Returns: Number of free 32 bit words.
 */

static inline uint32_t fifo32_num_free(Fifo32 *fifo)
{
    return DIV_ROUND_UP(fifo8_num_free(&fifo->fifo), sizeof(uint32_t));
}

/**
 * fifo32_num_used:
 * @fifo: FIFO to check
 *
 * Return the number of used uint32_t slots in the FIFO.
 *
 * Returns: Number of used 32 bit words.
 */

static inline uint32_t fifo32_num_used(Fifo32 *fifo)
{
    return DIV_ROUND_UP(fifo8_num_used(&fifo->fifo), sizeof(uint32_t));
}

/**
 * fifo32_push:
 * @fifo: FIFO to push to
 * @data: 32 bits data word to push
 *
 * Push a 32 bits data word to the FIFO. Behaviour is undefined if the FIFO
 * is full. Clients are responsible for checking for fullness using
 * fifo32_is_full().
 */

static inline void fifo32_push(Fifo32 *fifo, uint32_t data)
{
    int i;

    for (i = 0; i < sizeof(data); i++) {
        fifo8_push(&fifo->fifo, data & 0xff);
        data >>= 8;
    }
}

/**
 * fifo32_push_all:
 * @fifo: FIFO to push to
 * @data: data to push
 * @size: number of 32 bit words to push
 *
 * Push a 32 bit word array to the FIFO. Behaviour is undefined if the FIFO
 * is full. Clients are responsible for checking the space left in the FIFO
 * using fifo32_num_free().
 */

static inline void fifo32_push_all(Fifo32 *fifo, const uint32_t *data,
                                   uint32_t num)
{
    int i;

    for (i = 0; i < num; i++) {
        fifo32_push(fifo, data[i]);
    }
}

/**
 * fifo32_pop:
 * @fifo: fifo to pop from
 *
 * Pop a 32 bits data word from the FIFO. Behaviour is undefined if the FIFO
 * is empty. Clients are responsible for checking for emptiness using
 * fifo32_is_empty().
 *
 * Returns: The popped 32 bits data word.
 */

static inline uint32_t fifo32_pop(Fifo32 *fifo)
{
    uint32_t ret = 0;
    int i;

    for (i = 0; i < sizeof(uint32_t); i++) {
        ret |= (fifo8_pop(&fifo->fifo) << (i * 8));
    }

    return ret;
}

/**
 * There is no fifo32_pop_buf() because the data is not stored in the buffer
 * as a set of native-order words.
 */

/**
 * fifo32_reset:
 * @fifo: FIFO to reset
 *
 * Reset a FIFO. All data is discarded and the FIFO is emptied.
 */

static inline void fifo32_reset(Fifo32 *fifo)
{
    fifo8_reset(&fifo->fifo);
}

/**
 * fifo32_is_empty:
 * @fifo: FIFO to check
 *
 * Check if a FIFO is empty.
 *
 * Returns: True if the fifo is empty, false otherwise.
 */

static inline bool fifo32_is_empty(Fifo32 *fifo)
{
    return fifo8_is_empty(&fifo->fifo);
}

/**
 * fifo32_is_full:
 * @fifo: FIFO to check
 *
 * Check if a FIFO is full.
 *
 * Returns: True if the fifo is full, false otherwise.
 */

static inline bool fifo32_is_full(Fifo32 *fifo)
{
    return fifo8_num_free(&fifo->fifo) < sizeof(uint32_t);
}

#define VMSTATE_FIFO32(_field, _state) VMSTATE_FIFO8(_field.fifo, _state)

#endif /* FIFO32_H */
