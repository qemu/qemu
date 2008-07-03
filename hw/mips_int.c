#include "hw.h"
#include "mips.h"
#include "cpu.h"
#include "exec-all.h"

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
            if (loglevel & CPU_LOG_INT) {
                fprintf(logfile, "%s: cpu_interrupt (0x%08x,0x%08x)\n", __func__,
                  env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask,
                  env->interrupt_request);
                cpu_dump_state(env, logfile, fprintf, 0);
            }
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
        } else {
            if (loglevel & CPU_LOG_INT) {
                fprintf(logfile, "%s: no interrupt (0x%08x,0x%08x)\n", __func__,
                  env->CP0_Status & env->CP0_Cause & CP0Ca_IP_mask,
                  env->interrupt_request);
                cpu_dump_state(env, logfile, fprintf, 0);
            }
        }
    } else {
        if (loglevel & CPU_LOG_INT) {
            fprintf(logfile, "%s: cpu_reset_interrupt\n", __func__);
            cpu_dump_state(env, logfile, fprintf, 0);
        }
        cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
    }
}

static void cpu_mips_irq_request(void *opaque, int irq, int level)
{
    CPUState *env = (CPUState *)opaque;

    if (irq < 0 || irq > 7)
        return;

    if (level) {
        env->CP0_Cause |= 1 << (irq + CP0Ca_IP);
    } else {
        env->CP0_Cause &= ~(1 << (irq + CP0Ca_IP));
    }
    cpu_mips_update_irq(env);
}

void cpu_mips_irq_init_cpu(CPUState *env)
{
    qemu_irq *qi;
    int i;

    qi = qemu_allocate_irqs(cpu_mips_irq_request, env, 8);
    for (i = 0; i < 8; i++) {
        env->irq[i] = qi[i];
    }
}
