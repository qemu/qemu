#include "qemu/osdep.h"
#include "hw/mem/memory-device.h"

MemoryDeviceInfoList *qmp_memory_device_list(void)
{
   return NULL;
}

uint64_t get_plugged_memory_size(void)
{
    return (uint64_t)-1;
}

unsigned int memory_devices_get_reserved_memslots(void)
{
    return 0;
}

bool memory_devices_memslot_auto_decision_active(void)
{
    return false;
}
