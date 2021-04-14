#include "hypercall.h"

/*
 * intercept_hypercall
 * Intercepts a HVC instruction.
 */
void intercept_hypercall(DisasContext *s, uint32_t insn, uint32_t imm16, CPUARMState *cpu_env) {
    qemu_log("Intercepted a hypercall %x\n", imm16);
}
