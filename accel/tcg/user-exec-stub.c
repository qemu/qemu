#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "exec/replay-core.h"
#include "exec/watchpoint.h"
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

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint)
{
    return -ENOSYS;
}

int cpu_watchpoint_remove(CPUState *cpu, vaddr addr,
                          vaddr len, int flags)
{
    return -ENOSYS;
}

void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *wp)
{
}

void cpu_watchpoint_remove_all(CPUState *cpu, int mask)
{
}

int cpu_watchpoint_address_matches(CPUState *cpu, vaddr addr, vaddr len)
{
    return 0;
}

void cpu_check_watchpoint(CPUState *cpu, vaddr addr, vaddr len,
                          MemTxAttrs atr, int fl, uintptr_t ra)
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
