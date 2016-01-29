#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"

void qemu_system_vmstop_request_prepare(void)
{
    abort();
}

void qemu_system_vmstop_request(RunState state)
{
    abort();
}
