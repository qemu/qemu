#include "vl.h"
#include "cpu.h"

/* Raise IRQ to CPU if necessary. It must be called every time the active
   IRQ may change */
void cpu_mips_update_irq(CPUState *env)
{
    if ((env->CP0_Status & (1 << CP0St_IE)) &&
        !(env->CP0_Status & (1 << CP0St_EXL)) &&
        !(env->CP0_Status & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM)) {
        if ((env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask) &&
            !(env->interrupt_request & CPU_INTERRUPT_HARD)) {
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
	}
    } else
        cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
}

void cpu_mips_irq_request(void *opaque, int irq, int level)
{
    CPUState *env = (CPUState *)opaque;

    if (irq < 0 || irq > 7)
        return;

    if (level) {
        env->CP0_Cause |= 1 << (irq + CP0Ca_IP);
    } else {
        env->CP0_Cause &= ~(1 << (irq +CP0Ca_IP));
    }
    cpu_mips_update_irq(env);
}
