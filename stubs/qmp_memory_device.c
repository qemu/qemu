#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/mem/memory-device.h"

MemoryDeviceInfoList *qmp_memory_device_list(void)
{
   return NULL;
}

uint64_t get_plugged_memory_size(void)
{
    return (uint64_t)-1;
}
