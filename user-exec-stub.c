#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qom/cpu.h"
#include "sysemu/replay.h"

void cpu_resume(CPUState *cpu)
{
}

void qemu_init_vcpu(CPUState *cpu)
{
}

/* User mode emulation does not support record/replay yet.  */

bool replay_exception(void)
{
    return true;
}

bool replay_has_exception(void)
{
    return false;
}

bool replay_interrupt(void)
{
    return true;
}

bool replay_has_interrupt(void)
{
    return false;
}
