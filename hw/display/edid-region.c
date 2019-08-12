#include "qemu/osdep.h"
#include "exec/memory.h"
#include "hw/display/edid.h"

static uint64_t edid_region_read(void *ptr, hwaddr addr, unsigned size)
{
    uint8_t *edid = ptr;

    return edid[addr];
}

static void edid_region_write(void *ptr, hwaddr addr,
                             uint64_t val, unsigned size)
{
    /* read only */
}

static const MemoryRegionOps edid_region_ops = {
    .read = edid_region_read,
    .write = edid_region_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void qemu_edid_region_io(MemoryRegion *region, Object *owner,
                         uint8_t *edid, size_t size)
{
    memory_region_init_io(region, owner, &edid_region_ops,
                          edid, "edid", size);
}
