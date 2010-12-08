#include "rwhandler.h"
#include "ioport.h"
#include "cpu-all.h"

#define RWHANDLER_WRITE(name, len, type) \
static void name(void *opaque, type addr, uint32_t value) \
{\
    struct ReadWriteHandler *handler = opaque;\
    handler->write(handler, addr, value, len);\
}

#define RWHANDLER_READ(name, len, type) \
static uint32_t name(void *opaque, type addr) \
{ \
    struct ReadWriteHandler *handler = opaque; \
    return handler->read(handler, addr, len); \
}

RWHANDLER_WRITE(cpu_io_memory_simple_writeb, 1, target_phys_addr_t);
RWHANDLER_READ(cpu_io_memory_simple_readb, 1, target_phys_addr_t);
RWHANDLER_WRITE(cpu_io_memory_simple_writew, 2, target_phys_addr_t);
RWHANDLER_READ(cpu_io_memory_simple_readw, 2, target_phys_addr_t);
RWHANDLER_WRITE(cpu_io_memory_simple_writel, 4, target_phys_addr_t);
RWHANDLER_READ(cpu_io_memory_simple_readl, 4, target_phys_addr_t);

static CPUWriteMemoryFunc * const cpu_io_memory_simple_write[] = {
    &cpu_io_memory_simple_writeb,
    &cpu_io_memory_simple_writew,
    &cpu_io_memory_simple_writel,
};

static CPUReadMemoryFunc * const cpu_io_memory_simple_read[] = {
    &cpu_io_memory_simple_readb,
    &cpu_io_memory_simple_readw,
    &cpu_io_memory_simple_readl,
};

int cpu_register_io_memory_simple(struct ReadWriteHandler *handler, int endian)
{
    if (!handler->read || !handler->write) {
        return -1;
    }
    return cpu_register_io_memory(cpu_io_memory_simple_read,
                                  cpu_io_memory_simple_write,
                                  handler, endian);
}

RWHANDLER_WRITE(ioport_simple_writeb, 1, uint32_t);
RWHANDLER_READ(ioport_simple_readb, 1, uint32_t);
RWHANDLER_WRITE(ioport_simple_writew, 2, uint32_t);
RWHANDLER_READ(ioport_simple_readw, 2, uint32_t);
RWHANDLER_WRITE(ioport_simple_writel, 4, uint32_t);
RWHANDLER_READ(ioport_simple_readl, 4, uint32_t);

int register_ioport_simple(ReadWriteHandler* handler,
                           pio_addr_t start, int length, int size)
{
    IOPortWriteFunc *write;
    IOPortReadFunc *read;
    int r;
    switch (size) {
    case 1:
        write = ioport_simple_writeb;
        read = ioport_simple_readb;
        break;
    case 2:
        write = ioport_simple_writew;
        read = ioport_simple_readw;
        break;
    default:
        write = ioport_simple_writel;
        read = ioport_simple_readl;
    }
    if (handler->write) {
        r = register_ioport_write(start, length, size, write, handler);
        if (r < 0) {
            return r;
        }
    }
    if (handler->read) {
        r = register_ioport_read(start, length, size, read, handler);
        if (r < 0) {
            return r;
        }
    }
    return 0;
}
