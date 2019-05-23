#include "qemu/osdep.h"
#include "sysemu/sysemu.h"

bool machine_init_done = true;

void qemu_add_machine_init_done_notifier(Notifier *notify)
{
}
