#ifndef FIFO_H
#define FIFO_H

#include "migration/vmstate.h"

typedef struct {
    /* All fields are private */
    uint8_t *data;
    uint32_t capacity;
    uint32_t head;
    uint32_t num;
} Fifo8;

/**
 * fifo8_create:
 * @fifo: struct Fifo8 to initialise with new FIFO
 * @capacity: capacity of the newly created FIFO
 *
 * Create a FIFO of the specified size. Clients should call fifo8_destroy()
 * when finished using the fifo. The FIFO is initially empty.
 */

void fifo8_create(Fifo8 *fifo, uint32_t capacity);

/**
 * fifo8_destroy:
 * @fifo: FIFO to cleanup
 *
 * Cleanup a FIFO created with fifo8_create(). Frees memory created for FIFO
  *storage. The FIFO is no longer usable after this has been called.
 */

void fifo8_destroy(Fifo8 *fifo);

/**
 * fifo8_push:
 * @fifo: FIFO to push to
 * @data: data byte to push
 *
 * Push a data byte to the FIFO. Behaviour is undefined if the FIFO is full.
 * Clients are responsible for checking for fullness using fifo8_is_full().
 */

void fifo8_push(Fifo8 *fifo, uint8_t data);

/**
 * fifo8_pop:
 * @fifo: fifo to pop from
 *
 * Pop a data byte from the FIFO. Behaviour is undefined if the FIFO is empty.
 * Clients are responsible for checking for emptyness using fifo8_is_empty().
 *
 * Returns: The popped data byte.
 */

uint8_t fifo8_pop(Fifo8 *fifo);

/**
 * fifo8_reset:
 * @fifo: FIFO to reset
 *
 * Reset a FIFO. All data is discarded and the FIFO is emptied.
 */

void fifo8_reset(Fifo8 *fifo);

/**
 * fifo8_is_empty:
 * @fifo: FIFO to check
 *
 * Check if a FIFO is empty.
 *
 * Returns: True if the fifo is empty, false otherwise.
 */

bool fifo8_is_empty(Fifo8 *fifo);

/**
 * fifo8_is_full:
 * @fifo: FIFO to check
 *
 * Check if a FIFO is full.
 *
 * Returns: True if the fifo is full, false otherwise.
 */

bool fifo8_is_full(Fifo8 *fifo);

extern const VMStateDescription vmstate_fifo8;

#define VMSTATE_FIFO8(_field, _state) {                              \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(Fifo8),                                     \
    .vmsd       = &vmstate_fifo8,                                    \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, Fifo8),       \
}

#endif /* FIFO_H */
