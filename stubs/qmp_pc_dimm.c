#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/mem/pc-dimm.h"

MemoryDeviceInfoList *qmp_pc_dimm_device_list(void)
{
   return NULL;
}

uint64_t get_plugged_memory_size(void)
{
    return (uint64_t)-1;
}
