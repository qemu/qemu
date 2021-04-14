#include "hypercall.h"

/*
 * intercept_hypercall
 * Intercepts a HVC instruction.
 */
void intercept_hypercall(DisasContext *s, uint32_t insn, CPUARMState *cpu_env) {
    qemu_log("Intercepted a hypercall\n");
}
