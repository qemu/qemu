#include "qemu/osdep.h"
#include "exec/ramlist.h"
#include "exec/cpu-common.h"

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
