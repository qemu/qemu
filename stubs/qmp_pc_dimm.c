#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/mem/pc-dimm.h"

int qmp_pc_dimm_device_list(Object *obj, void *opaque)
{
   return 0;
}

uint64_t get_plugged_memory_size(void)
{
    return (uint64_t)-1;
}
