/*
 *  PowerPC exception emulation helpers for QEMU (TCG specific)
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "system/runstate.h"

#include "helper_regs.h"
#include "hw/ppc/ppc.h"
#include "internal.h"
#include "cpu.h"
#include "trace.h"

/*****************************************************************************/
/* Exceptions processing helpers */

void raise_exception_err_ra(CPUPPCState *env, uint32_t exception,
                            uint32_t error_code, uintptr_t raddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = exception;
    env->error_code = error_code;
    cpu_loop_exit_restore(cs, raddr);
}

void helper_raise_exception_err(CPUPPCState *env, uint32_t exception,
                                uint32_t error_code)
{
    raise_exception_err_ra(env, exception, error_code, 0);
}

void helper_raise_exception(CPUPPCState *env, uint32_t exception)
{
    raise_exception_err_ra(env, exception, 0, 0);
}

#ifndef CONFIG_USER_ONLY

void raise_exception_err(CPUPPCState *env, uint32_t exception,
                                           uint32_t error_code)
{
    raise_exception_err_ra(env, exception, error_code, 0);
}

void raise_exception(CPUPPCState *env, uint32_t exception)
{
    raise_exception_err_ra(env, exception, 0, 0);
}

#endif /* !CONFIG_USER_ONLY */

void helper_TW(CPUPPCState *env, target_ulong arg1, target_ulong arg2,
               uint32_t flags)
{
    if (!likely(!(((int32_t)arg1 < (int32_t)arg2 && (flags & 0x10)) ||
                  ((int32_t)arg1 > (int32_t)arg2 && (flags & 0x08)) ||
                  ((int32_t)arg1 == (int32_t)arg2 && (flags & 0x04)) ||
                  ((uint32_t)arg1 < (uint32_t)arg2 && (flags & 0x02)) ||
                  ((uint32_t)arg1 > (uint32_t)arg2 && (flags & 0x01))))) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_TRAP, GETPC());
    }
}

#ifdef TARGET_PPC64
void helper_TD(CPUPPCState *env, target_ulong arg1, target_ulong arg2,
               uint32_t flags)
{
    if (!likely(!(((int64_t)arg1 < (int64_t)arg2 && (flags & 0x10)) ||
                  ((int64_t)arg1 > (int64_t)arg2 && (flags & 0x08)) ||
                  ((int64_t)arg1 == (int64_t)arg2 && (flags & 0x04)) ||
                  ((uint64_t)arg1 < (uint64_t)arg2 && (flags & 0x02)) ||
                  ((uint64_t)arg1 > (uint64_t)arg2 && (flags & 0x01))))) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_TRAP, GETPC());
    }
}
#endif /* TARGET_PPC64 */

static uint32_t helper_SIMON_LIKE_32_64(uint32_t x, uint64_t key, uint32_t lane)
{
    const uint16_t c = 0xfffc;
    const uint64_t z0 = 0xfa2561cdf44ac398ULL;
    uint16_t z = 0, temp;
    uint16_t k[32], eff_k[32], xleft[33], xright[33], fxleft[32];

    for (int i = 3; i >= 0; i--) {
        k[i] = key & 0xffff;
        key >>= 16;
    }
    xleft[0] = x & 0xffff;
    xright[0] = (x >> 16) & 0xffff;

    for (int i = 0; i < 28; i++) {
        z = (z0 >> (63 - i)) & 1;
        temp = ror16(k[i + 3], 3) ^ k[i + 1];
        k[i + 4] = c ^ z ^ k[i] ^ temp ^ ror16(temp, 1);
    }

    for (int i = 0; i < 8; i++) {
        eff_k[4 * i + 0] = k[4 * i + ((0 + lane) % 4)];
        eff_k[4 * i + 1] = k[4 * i + ((1 + lane) % 4)];
        eff_k[4 * i + 2] = k[4 * i + ((2 + lane) % 4)];
        eff_k[4 * i + 3] = k[4 * i + ((3 + lane) % 4)];
    }

    for (int i = 0; i < 32; i++) {
        fxleft[i] = (rol16(xleft[i], 1) &
            rol16(xleft[i], 8)) ^ rol16(xleft[i], 2);
        xleft[i + 1] = xright[i] ^ fxleft[i] ^ eff_k[i];
        xright[i + 1] = xleft[i];
    }

    return (((uint32_t)xright[32]) << 16) | xleft[32];
}

static uint64_t hash_digest(uint64_t ra, uint64_t rb, uint64_t key)
{
    uint64_t stage0_h = 0ULL, stage0_l = 0ULL;
    uint64_t stage1_h, stage1_l;

    for (int i = 0; i < 4; i++) {
        stage0_h |= ror64(rb & 0xff, 8 * (2 * i + 1));
        stage0_h |= ((ra >> 32) & 0xff) << (8 * 2 * i);
        stage0_l |= ror64((rb >> 32) & 0xff, 8 * (2 * i + 1));
        stage0_l |= (ra & 0xff) << (8 * 2 * i);
        rb >>= 8;
        ra >>= 8;
    }

    stage1_h = (uint64_t)helper_SIMON_LIKE_32_64(stage0_h >> 32, key, 0) << 32;
    stage1_h |= helper_SIMON_LIKE_32_64(stage0_h, key, 1);
    stage1_l = (uint64_t)helper_SIMON_LIKE_32_64(stage0_l >> 32, key, 2) << 32;
    stage1_l |= helper_SIMON_LIKE_32_64(stage0_l, key, 3);

    return stage1_h ^ stage1_l;
}

static void do_hash(CPUPPCState *env, target_ulong ea, target_ulong ra,
                    target_ulong rb, uint64_t key, bool store)
{
    uint64_t calculated_hash = hash_digest(ra, rb, key), loaded_hash;

    if (store) {
        cpu_stq_data_ra(env, ea, calculated_hash, GETPC());
    } else {
        loaded_hash = cpu_ldq_data_ra(env, ea, GETPC());
        if (loaded_hash != calculated_hash) {
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                POWERPC_EXCP_TRAP, GETPC());
        }
    }
}

#include "qemu/guest-random.h"

#ifdef TARGET_PPC64
#define HELPER_HASH(op, key, store, dexcr_aspect)                             \
void helper_##op(CPUPPCState *env, target_ulong ea, target_ulong ra,          \
                 target_ulong rb)                                             \
{                                                                             \
    if (env->msr & R_MSR_PR_MASK) {                                           \
        if (!(env->spr[SPR_DEXCR] & R_DEXCR_PRO_##dexcr_aspect##_MASK ||      \
            env->spr[SPR_HDEXCR] & R_HDEXCR_ENF_##dexcr_aspect##_MASK))       \
            return;                                                           \
    } else if (!(env->msr & R_MSR_HV_MASK)) {                                 \
        if (!(env->spr[SPR_DEXCR] & R_DEXCR_PNH_##dexcr_aspect##_MASK ||      \
            env->spr[SPR_HDEXCR] & R_HDEXCR_ENF_##dexcr_aspect##_MASK))       \
            return;                                                           \
    } else if (!(env->msr & R_MSR_S_MASK)) {                                  \
        if (!(env->spr[SPR_HDEXCR] & R_HDEXCR_HNU_##dexcr_aspect##_MASK))     \
            return;                                                           \
    }                                                                         \
                                                                              \
    do_hash(env, ea, ra, rb, key, store);                                     \
}
#else
#define HELPER_HASH(op, key, store, dexcr_aspect)                             \
void helper_##op(CPUPPCState *env, target_ulong ea, target_ulong ra,          \
                 target_ulong rb)                                             \
{                                                                             \
    do_hash(env, ea, ra, rb, key, store);                                     \
}
#endif /* TARGET_PPC64 */

HELPER_HASH(HASHST, env->spr[SPR_HASHKEYR], true, NPHIE)
HELPER_HASH(HASHCHK, env->spr[SPR_HASHKEYR], false, NPHIE)
HELPER_HASH(HASHSTP, env->spr[SPR_HASHPKEYR], true, PHIE)
HELPER_HASH(HASHCHKP, env->spr[SPR_HASHPKEYR], false, PHIE)

#ifndef CONFIG_USER_ONLY

void ppc_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                 MMUAccessType access_type,
                                 int mmu_idx, uintptr_t retaddr)
{
    CPUPPCState *env = cpu_env(cs);
    uint32_t insn;

    /* Restore state and reload the insn we executed, for filling in DSISR.  */
    cpu_restore_state(cs, retaddr);
    insn = ppc_ldl_code(env, env->nip);

    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_4xx:
        env->spr[SPR_40x_DEAR] = vaddr;
        break;
    case POWERPC_MMU_BOOKE:
    case POWERPC_MMU_BOOKE206:
        env->spr[SPR_BOOKE_DEAR] = vaddr;
        break;
    default:
        env->spr[SPR_DAR] = vaddr;
        break;
    }

    cs->exception_index = POWERPC_EXCP_ALIGN;
    env->error_code = insn & 0x03FF0000;
    cpu_loop_exit(cs);
}

void ppc_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                   vaddr vaddr, unsigned size,
                                   MMUAccessType access_type,
                                   int mmu_idx, MemTxAttrs attrs,
                                   MemTxResult response, uintptr_t retaddr)
{
    CPUPPCState *env = cpu_env(cs);

    switch (env->excp_model) {
#if defined(TARGET_PPC64)
    case POWERPC_EXCP_POWER8:
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        /*
         * Machine check codes can be found in processor User Manual or
         * Linux or skiboot source.
         */
        if (access_type == MMU_DATA_LOAD) {
            env->spr[SPR_DAR] = vaddr;
            env->spr[SPR_DSISR] = PPC_BIT(57);
            env->error_code = PPC_BIT(42);

        } else if (access_type == MMU_DATA_STORE) {
            /*
             * MCE for stores in POWER is asynchronous so hardware does
             * not set DAR, but QEMU can do better.
             */
            env->spr[SPR_DAR] = vaddr;
            env->error_code = PPC_BIT(36) | PPC_BIT(43) | PPC_BIT(45);
            env->error_code |= PPC_BIT(42);

        } else { /* Fetch */
            /*
             * is_prefix_insn_excp() tests !PPC_BIT(42) to avoid fetching
             * the instruction, so that must always be clear for fetches.
             */
            env->error_code = PPC_BIT(36) | PPC_BIT(44) | PPC_BIT(45);
        }
        break;
#endif
    default:
        /*
         * TODO: Check behaviour for other CPUs, for now do nothing.
         * Could add a basic MCE even if real hardware ignores.
         */
        return;
    }

    cs->exception_index = POWERPC_EXCP_MCHECK;
    cpu_loop_exit_restore(cs, retaddr);
}

void ppc_cpu_debug_excp_handler(CPUState *cs)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        if (cs->watchpoint_hit) {
            if (cs->watchpoint_hit->flags & BP_CPU) {
                env->spr[SPR_DAR] = cs->watchpoint_hit->hitaddr;
                env->spr[SPR_DSISR] = PPC_BIT(41);
                cs->watchpoint_hit = NULL;
                raise_exception(env, POWERPC_EXCP_DSI);
            }
            cs->watchpoint_hit = NULL;
        } else if (cpu_breakpoint_test(cs, env->nip, BP_CPU)) {
            raise_exception_err(env, POWERPC_EXCP_TRACE,
                                PPC_BIT(33) | PPC_BIT(43));
        }
    }
#endif
}

bool ppc_cpu_debug_check_breakpoint(CPUState *cs)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        target_ulong priv;

        priv = env->spr[SPR_CIABR] & PPC_BITMASK(62, 63);
        switch (priv) {
        case 0x1: /* problem */
            return env->msr & ((target_ulong)1 << MSR_PR);
        case 0x2: /* supervisor */
            return (!(env->msr & ((target_ulong)1 << MSR_PR)) &&
                    !(env->msr & ((target_ulong)1 << MSR_HV)));
        case 0x3: /* hypervisor */
            return (!(env->msr & ((target_ulong)1 << MSR_PR)) &&
                     (env->msr & ((target_ulong)1 << MSR_HV)));
        default:
            g_assert_not_reached();
        }
    }
#endif

    return false;
}

bool ppc_cpu_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        if (wp == env->dawr0_watchpoint) {
            uint32_t dawrx = env->spr[SPR_DAWRX0];
            bool wt = extract32(dawrx, PPC_BIT_NR(59), 1);
            bool wti = extract32(dawrx, PPC_BIT_NR(60), 1);
            bool hv = extract32(dawrx, PPC_BIT_NR(61), 1);
            bool sv = extract32(dawrx, PPC_BIT_NR(62), 1);
            bool pr = extract32(dawrx, PPC_BIT_NR(62), 1);

            if ((env->msr & ((target_ulong)1 << MSR_PR)) && !pr) {
                return false;
            } else if ((env->msr & ((target_ulong)1 << MSR_HV)) && !hv) {
                return false;
            } else if (!sv) {
                return false;
            }

            if (!wti) {
                if (env->msr & ((target_ulong)1 << MSR_DR)) {
                    if (!wt) {
                        return false;
                    }
                } else {
                    if (wt) {
                        return false;
                    }
                }
            }

            return true;
        }
    }
#endif

    return false;
}

/*
 * This stops the machine and logs CPU state without killing QEMU (like
 * cpu_abort()) because it is often a guest error as opposed to a QEMU error,
 * so the machine can still be debugged.
 */
G_NORETURN void powerpc_checkstop(CPUPPCState *env, const char *reason)
{
    CPUState *cs = env_cpu(env);
    FILE *f;

    f = qemu_log_trylock();
    if (f) {
        fprintf(f, "Entering checkstop state: %s\n", reason);
        cpu_dump_state(cs, f, CPU_DUMP_FPU | CPU_DUMP_CCOP);
        qemu_log_unlock(f);
    }

    /*
     * This stops the machine and logs CPU state without killing QEMU
     * (like cpu_abort()) so the machine can still be debugged (because
     * it is often a guest error).
     */
    qemu_system_guest_panicked(NULL);
    cpu_loop_exit_noexc(cs);
}

/* Return true iff byteswap is needed to load instruction */
static inline bool insn_need_byteswap(CPUArchState *env)
{
    /* SYSTEM builds TARGET_BIG_ENDIAN. Need to swap when MSR[LE] is set */
    return !!(env->msr & ((target_ulong)1 << MSR_LE));
}

uint32_t ppc_ldl_code(CPUArchState *env, target_ulong addr)
{
    uint32_t insn = cpu_ldl_code(env, addr);

    if (insn_need_byteswap(env)) {
        insn = bswap32(insn);
    }

    return insn;
}

#endif /* !CONFIG_USER_ONLY */
