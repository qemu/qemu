#include "qemu/osdep.h"
#include "exec/ramlist.h"
#include "exec/cpu-common.h"
#include "system/memory.h"

void *qemu_ram_get_host_addr(RAMBlock *rb)
{
    return 0;
}

ram_addr_t qemu_ram_get_offset(RAMBlock *rb)
{
    return 0;
}

ram_addr_t qemu_ram_get_used_length(RAMBlock *rb)
{
    return 0;
}

void ram_block_notifier_add(RAMBlockNotifier *n)
{
}

void ram_block_notifier_remove(RAMBlockNotifier *n)
{
}

int qemu_ram_foreach_block(RAMBlockIterFunc func, void *opaque)
{
    return 0;
}

int ram_block_discard_disable(bool state)
{
    return 0;
}
