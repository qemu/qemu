#ifndef QEMU_FIFO8_H
#define QEMU_FIFO8_H


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
 * fifo8_push_all:
 * @fifo: FIFO to push to
 * @data: data to push
 * @size: number of bytes to push
 *
 * Push a byte array to the FIFO. Behaviour is undefined if the FIFO is full.
 * Clients are responsible for checking the space left in the FIFO using
 * fifo8_num_free().
 */

void fifo8_push_all(Fifo8 *fifo, const uint8_t *data, uint32_t num);

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
 * fifo8_pop_buf:
 * @fifo: FIFO to pop from
 * @max: maximum number of bytes to pop
 * @num: actual number of returned bytes
 *
 * Pop a number of elements from the FIFO up to a maximum of max. The buffer
 * containing the popped data is returned. This buffer points directly into
 * the FIFO backing store and data is invalidated once any of the fifo8_* APIs
 * are called on the FIFO.
 *
 * The function may return fewer bytes than requested when the data wraps
 * around in the ring buffer; in this case only a contiguous part of the data
 * is returned.
 *
 * The number of valid bytes returned is populated in *num; will always return
 * at least 1 byte. max must not be 0 or greater than the number of bytes in
 * the FIFO.
 *
 * Clients are responsible for checking the availability of requested data
 * using fifo8_num_used().
 *
 * Returns: A pointer to popped data.
 */
const uint8_t *fifo8_pop_buf(Fifo8 *fifo, uint32_t max, uint32_t *num);

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

/**
 * fifo8_num_free:
 * @fifo: FIFO to check
 *
 * Return the number of free bytes in the FIFO.
 *
 * Returns: Number of free bytes.
 */

uint32_t fifo8_num_free(Fifo8 *fifo);

/**
 * fifo8_num_used:
 * @fifo: FIFO to check
 *
 * Return the number of used bytes in the FIFO.
 *
 * Returns: Number of used bytes.
 */

uint32_t fifo8_num_used(Fifo8 *fifo);

extern const VMStateDescription vmstate_fifo8;

#define VMSTATE_FIFO8_TEST(_field, _state, _test) {                  \
    .name         = (stringify(_field)),                             \
    .field_exists = (_test),                                         \
    .size         = sizeof(Fifo8),                                   \
    .vmsd         = &vmstate_fifo8,                                  \
    .flags        = VMS_STRUCT,                                      \
    .offset       = vmstate_offset_value(_state, _field, Fifo8),     \
}

#define VMSTATE_FIFO8(_field, _state)                                \
    VMSTATE_FIFO8_TEST(_field, _state, NULL)

#endif /* QEMU_FIFO8_H */
