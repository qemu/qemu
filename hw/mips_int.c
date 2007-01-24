#include "vl.h"
#include "cpu.h"

/* Raise IRQ to CPU if necessary. It must be called every time the active
   IRQ may change */
void cpu_mips_update_irq(CPUState *env)
{
    if ((env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask) &&
        (env->CP0_Status & (1 << CP0St_IE)) &&
        !(env->hflags & MIPS_HFLAG_EXL) &&
	!(env->hflags & MIPS_HFLAG_ERL) &&
	!(env->hflags & MIPS_HFLAG_DM)) {
        if (! (env->interrupt_request & CPU_INTERRUPT_HARD)) {
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
	}
    } else {
        cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }
}

void cpu_mips_irq_request(void *opaque, int irq, int level)
{
    CPUState *env = first_cpu;
   
    uint32_t mask;

    if (irq >= 16)
        return;

    mask = 1 << (irq + CP0Ca_IP);

    if (level) {
        env->CP0_Cause |= mask;
    } else {
        env->CP0_Cause &= ~mask;
    }
    cpu_mips_update_irq(env);
}

