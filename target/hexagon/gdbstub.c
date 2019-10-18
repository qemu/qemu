/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * Hexagon GDB server stub
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "exec/gdbstub.h"
#include "cpu.h"

static int gdb_get_vreg(CPUHexagonState *env, uint8_t *mem_buf, int n)
{
    int total = 0;
    int i;
    for (i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        total += gdb_get_regl(mem_buf, env->VRegs[n].uw[i]);
        mem_buf += 4;
    }
    return total;
}

static int gdb_get_qreg(CPUHexagonState *env, uint8_t *mem_buf, int n)
{
    int total = 0;
    int i;
    for (i = 0; i < MAX_VEC_SIZE_BYTES / 4 / 8; i++) {
        total += gdb_get_regl(mem_buf, env->QRegs[n].uw[i]);
        mem_buf += 4;
    }
    return total;
}

int hexagon_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    if (n < TOTAL_PER_THREAD_REGS) {
        return gdb_get_regl(mem_buf, env->gpr[n]);
    }
    n -= TOTAL_PER_THREAD_REGS;

    if (n < NUM_VREGS) {
        return gdb_get_vreg(env, mem_buf, n);
    }
    n -= NUM_VREGS;

    if (n < NUM_QREGS) {
        return gdb_get_qreg(env, mem_buf, n);
    }

    g_assert_not_reached();
    return 0;
}

static int gdb_put_vreg(CPUHexagonState *env, uint8_t *mem_buf, int n)
{
    int i;
    for (i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        env->VRegs[n].uw[i] = ldtul_p(mem_buf);
        mem_buf += 4;
    }
    return MAX_VEC_SIZE_BYTES;
}

static int gdb_put_qreg(CPUHexagonState *env, uint8_t *mem_buf, int n)
{
    int i;
    for (i = 0; i < MAX_VEC_SIZE_BYTES / 4 / 8; i++) {
        env->QRegs[n].uw[i] = ldtul_p(mem_buf);
        mem_buf += 4;
    }
    return MAX_VEC_SIZE_BYTES / 8;
}

int hexagon_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    if (n < TOTAL_PER_THREAD_REGS) {
        env->gpr[n] = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    }
    n -= TOTAL_PER_THREAD_REGS;

    if (n < NUM_VREGS) {
        return gdb_put_vreg(env, mem_buf, n);
    }
    n -= NUM_VREGS;

    if (n < NUM_QREGS) {
        return gdb_put_qreg(env, mem_buf, n);
    }

    g_assert_not_reached();
    return 0;
}