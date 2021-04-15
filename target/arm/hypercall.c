#include "hypercall.h"

/*
 * intercept_hypercall
 * Intercepts a HVC instruction.
 */
void intercept_hypercall(CPUARMState *cpu_env) {
    qemu_log("Intercepted a hypercall\n");
    for (int i = 0; i < 32; i++) {
        qemu_log("R%d: 0x%lX\n", i, cpu_env->xregs[i]);
    }

    cpu_env->xregs[0] = 0x1337;
}
