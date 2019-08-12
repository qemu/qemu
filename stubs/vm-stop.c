#include "qemu/osdep.h"

#include "sysemu/runstate.h"
void qemu_system_vmstop_request_prepare(void)
{
    abort();
}

void qemu_system_vmstop_request(RunState state)
{
    abort();
}
