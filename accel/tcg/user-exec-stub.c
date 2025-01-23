#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "exec/replay-core.h"
#include "internal-common.h"

void cpu_resume(CPUState *cpu)
{
}

void cpu_remove_sync(CPUState *cpu)
{
}

void qemu_init_vcpu(CPUState *cpu)
{
}

void cpu_exec_reset_hold(CPUState *cpu)
{
}

/* User mode emulation does not support softmmu yet.  */

void tlb_init(CPUState *cpu)
{
}

void tlb_destroy(CPUState *cpu)
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
