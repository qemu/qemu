#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"

bool machine_init_done = true;

void qemu_add_machine_init_done_notifier(Notifier *notify)
{
}
