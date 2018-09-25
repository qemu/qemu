#ifndef EDID_H
#define EDID_H

#include "hw/hw.h"

typedef struct qemu_edid_info {
    const char *vendor;
    const char *name;
    const char *serial;
    uint32_t    dpi;
    uint32_t    prefx;
    uint32_t    prefy;
    uint32_t    maxx;
    uint32_t    maxy;
} qemu_edid_info;

void qemu_edid_generate(uint8_t *edid, size_t size,
                        qemu_edid_info *info);
size_t qemu_edid_size(uint8_t *edid);
void qemu_edid_region_io(MemoryRegion *region, Object *owner,
                         uint8_t *edid, size_t size);

#endif /* EDID_H */
