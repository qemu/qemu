/*
 * PA-RISC emulation cpu definitions for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HPPA_CPU_H
#define HPPA_CPU_H

#include "qemu-common.h"
#include "cpu-qom.h"

/* We only support hppa-linux-user at present, so 32-bit only.  */
#define TARGET_LONG_BITS 32
#define TARGET_PHYS_ADDR_SPACE_BITS  32
#define TARGET_VIRT_ADDR_SPACE_BITS  32

#define CPUArchState struct CPUHPPAState

#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"

#define TARGET_PAGE_BITS 12

#define ALIGNED_ONLY
#define NB_MMU_MODES     1
#define MMU_USER_IDX     0
#define TARGET_INSN_START_EXTRA_WORDS 1

#define EXCP_SYSCALL     1
#define EXCP_SYSCALL_LWS 2
#define EXCP_SIGSEGV     3
#define EXCP_SIGILL      4
#define EXCP_SIGFPE      5

typedef struct CPUHPPAState CPUHPPAState;

struct CPUHPPAState {
    target_ulong gr[32];
    uint64_t fr[32];

    target_ulong sar;
    target_ulong cr26;
    target_ulong cr27;

    target_ulong psw_n;      /* boolean */
    target_long  psw_v;      /* in most significant bit */

    /* Splitting the carry-borrow field into the MSB and "the rest", allows
     * for "the rest" to be deleted when it is unused, but the MSB is in use.
     * In addition, it's easier to compute carry-in for bit B+1 than it is to
     * compute carry-out for bit B (3 vs 4 insns for addition, assuming the
     * host has the appropriate add-with-carry insn to compute the msb).
     * Therefore the carry bits are stored as: cb_msb : cb & 0x11111110.
     */
    target_ulong psw_cb;     /* in least significant bit of next nibble */
    target_ulong psw_cb_msb; /* boolean */

    target_ulong iaoq_f;     /* front */
    target_ulong iaoq_b;     /* back, aka next instruction */

    target_ulong ior;        /* interrupt offset register */

    uint32_t fr0_shadow;     /* flags, c, ca/cq, rm, d, enables */
    float_status fp_status;

    /* Those resources are used only in QEMU core */
    CPU_COMMON
};

/**
 * HPPACPU:
 * @env: #CPUHPPAState
 *
 * An HPPA CPU.
 */
struct HPPACPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUHPPAState env;
};

static inline HPPACPU *hppa_env_get_cpu(CPUHPPAState *env)
{
    return container_of(env, HPPACPU, env);
}

#define ENV_GET_CPU(e)  CPU(hppa_env_get_cpu(e))
#define ENV_OFFSET      offsetof(HPPACPU, env)

#include "exec/cpu-all.h"

static inline int cpu_mmu_index(CPUHPPAState *env, bool ifetch)
{
    return 0;
}

void hppa_translate_init(void);

HPPACPU *cpu_hppa_init(const char *cpu_model);

#define cpu_init(cpu_model) CPU(cpu_hppa_init(cpu_model))

void hppa_cpu_list(FILE *f, fprintf_function cpu_fprintf);

static inline void cpu_get_tb_cpu_state(CPUHPPAState *env, target_ulong *pc,
                                        target_ulong *cs_base,
                                        uint32_t *pflags)
{
    *pc = env->iaoq_f;
    *cs_base = env->iaoq_b;
    *pflags = env->psw_n;
}

target_ulong cpu_hppa_get_psw(CPUHPPAState *env);
void cpu_hppa_put_psw(CPUHPPAState *env, target_ulong);
void cpu_hppa_loaded_fr0(CPUHPPAState *env);

#define cpu_signal_handler cpu_hppa_signal_handler

int cpu_hppa_signal_handler(int host_signum, void *pinfo, void *puc);
int hppa_cpu_handle_mmu_fault(CPUState *cpu, vaddr address, int rw, int midx);
int hppa_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int hppa_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void hppa_cpu_do_interrupt(CPUState *cpu);
bool hppa_cpu_exec_interrupt(CPUState *cpu, int int_req);
void hppa_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function, int);

#endif /* HPPA_CPU_H */
