#ifndef TRICORE_MISC_H
#define TRICORE_MISC_H 1

#include "exec/memory.h"
#include "hw/irq.h"

struct tricore_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
};
#endif
