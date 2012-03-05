#ifndef IORANGE_H
#define IORANGE_H

#include <stdint.h>

typedef struct IORange IORange;
typedef struct IORangeOps IORangeOps;

struct IORangeOps {
    void (*read)(IORange *iorange, uint64_t offset, unsigned width,
                 uint64_t *data);
    void (*write)(IORange *iorange, uint64_t offset, unsigned width,
                  uint64_t data);
    void (*destructor)(IORange *iorange);
};

struct IORange {
    const IORangeOps *ops;
    uint64_t base;
    uint64_t len;
};

static inline void iorange_init(IORange *iorange, const IORangeOps *ops,
                                uint64_t base, uint64_t len)
{
    iorange->ops = ops;
    iorange->base = base;
    iorange->len = len;
}

#endif
