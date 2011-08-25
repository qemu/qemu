#ifndef READ_WRITE_HANDLER_H
#define READ_WRITE_HANDLER_H

#include "qemu-common.h"
#include "ioport.h"

typedef struct ReadWriteHandler ReadWriteHandler;

/* len is guaranteed to be one of 1, 2 or 4, addr is guaranteed to fit in an
 * appropriate type (io/memory/etc). They do not need to be range checked. */
typedef void WriteHandlerFunc(ReadWriteHandler *, pcibus_t addr,
                              uint32_t value, int len);
typedef uint32_t ReadHandlerFunc(ReadWriteHandler *, pcibus_t addr, int len);

struct ReadWriteHandler {
    WriteHandlerFunc *write;
    ReadHandlerFunc *read;
};

/* Helpers for when we want to use a single routine with length. */
/* CPU memory handler: both read and write must be present. */
int cpu_register_io_memory_simple(ReadWriteHandler *, int endian);
/* io port handler: can supply only read or write handlers. */
int register_ioport_simple(ReadWriteHandler *,
                           pio_addr_t start, int length, int size);

#endif
