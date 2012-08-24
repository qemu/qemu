/*
 *  S/390 misc helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
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

#include "cpu.h"
#include "exec/memory.h"
#include "qemu/host-utils.h"
#include "helper.h"
#include <string.h>
#include "sysemu/kvm.h"
#include "qemu/timer.h"
#ifdef CONFIG_KVM
#include <linux/kvm.h>
#endif

#if !defined(CONFIG_USER_ONLY)
#include "exec/softmmu_exec.h"
#include "sysemu/sysemu.h"
#endif

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

/* Raise an exception dynamically from a helper function.  */
void QEMU_NORETURN runtime_exception(CPUS390XState *env, int excp,
                                     uintptr_t retaddr)
{
    int t;

    env->exception_index = EXCP_PGM;
    env->int_pgm_code = excp;

    /* Use the (ultimate) callers address to find the insn that trapped.  */
    cpu_restore_state(env, retaddr);

    /* Advance past the insn.  */
    t = cpu_ldub_code(env, env->psw.addr);
    env->int_pgm_ilen = t = get_ilen(t);
    env->psw.addr += 2 * t;

    cpu_loop_exit(env);
}

/* Raise an exception statically from a TB.  */
void HELPER(exception)(CPUS390XState *env, uint32_t excp)
{
    HELPER_LOG("%s: exception %d\n", __func__, excp);
    env->exception_index = excp;
    cpu_loop_exit(env);
}

#ifndef CONFIG_USER_ONLY
void program_interrupt(CPUS390XState *env, uint32_t code, int ilen)
{
    qemu_log_mask(CPU_LOG_INT, "program interrupt at %#" PRIx64 "\n",
                  env->psw.addr);

    if (kvm_enabled()) {
#ifdef CONFIG_KVM
        kvm_s390_interrupt(s390_env_get_cpu(env), KVM_S390_PROGRAM_INT, code);
#endif
    } else {
        env->int_pgm_code = code;
        env->int_pgm_ilen = ilen;
        env->exception_index = EXCP_PGM;
        cpu_loop_exit(env);
    }
}

/* SCLP service call */
uint32_t HELPER(servc)(CPUS390XState *env, uint32_t r1, uint64_t r2)
{
    int r;

    r = sclp_service_call(r1, r2);
    if (r < 0) {
        program_interrupt(env, -r, 4);
        return 0;
    }
    return r;
}

/* DIAG */
uint64_t HELPER(diag)(CPUS390XState *env, uint32_t num, uint64_t mem,
                      uint64_t code)
{
    uint64_t r;

    switch (num) {
    case 0x500:
        /* KVM hypercall */
        r = s390_virtio_hypercall(env, mem, code);
        break;
    case 0x44:
        /* yield */
        r = 0;
        break;
    case 0x308:
        /* ipl */
        r = 0;
        break;
    default:
        r = -1;
        break;
    }

    if (r) {
        program_interrupt(env, PGM_OPERATION, ILEN_LATER_INC);
    }

    return r;
}

/* Set Prefix */
void HELPER(spx)(CPUS390XState *env, uint64_t a1)
{
    uint32_t prefix;

    prefix = cpu_ldl_data(env, a1);
    env->psa = prefix & 0xfffff000;
    qemu_log("prefix: %#x\n", prefix);
    tlb_flush_page(env, 0);
    tlb_flush_page(env, TARGET_PAGE_SIZE);
}

static inline uint64_t clock_value(CPUS390XState *env)
{
    uint64_t time;

    time = env->tod_offset +
        time2tod(qemu_get_clock_ns(vm_clock) - env->tod_basetime);

    return time;
}

/* Store Clock */
uint64_t HELPER(stck)(CPUS390XState *env)
{
    return clock_value(env);
}

/* Store Clock Extended */
uint32_t HELPER(stcke)(CPUS390XState *env, uint64_t a1)
{
    cpu_stb_data(env, a1, 0);
    /* basically the same value as stck */
    cpu_stq_data(env, a1 + 1, clock_value(env) | env->cpu_num);
    /* more fine grained than stck */
    cpu_stq_data(env, a1 + 9, 0);
    /* XXX programmable fields */
    cpu_stw_data(env, a1 + 17, 0);

    return 0;
}

/* Set Clock Comparator */
void HELPER(sckc)(CPUS390XState *env, uint64_t time)
{
    if (time == -1ULL) {
        return;
    }

    /* difference between now and then */
    time -= clock_value(env);
    /* nanoseconds */
    time = (time * 125) >> 9;

    qemu_mod_timer(env->tod_timer, qemu_get_clock_ns(vm_clock) + time);
}

/* Store Clock Comparator */
uint64_t HELPER(stckc)(CPUS390XState *env)
{
    /* XXX implement */
    return 0;
}

/* Set CPU Timer */
void HELPER(spt)(CPUS390XState *env, uint64_t a1)
{
    uint64_t time = cpu_ldq_data(env, a1);

    if (time == -1ULL) {
        return;
    }

    /* nanoseconds */
    time = (time * 125) >> 9;

    qemu_mod_timer(env->cpu_timer, qemu_get_clock_ns(vm_clock) + time);
}

/* Store CPU Timer */
void HELPER(stpt)(CPUS390XState *env, uint64_t a1)
{
    /* XXX implement */
    cpu_stq_data(env, a1, 0);
}

/* Store System Information */
uint32_t HELPER(stsi)(CPUS390XState *env, uint64_t a0, uint32_t r0,
                      uint32_t r1)
{
    int cc = 0;
    int sel1, sel2;

    if ((r0 & STSI_LEVEL_MASK) <= STSI_LEVEL_3 &&
        ((r0 & STSI_R0_RESERVED_MASK) || (r1 & STSI_R1_RESERVED_MASK))) {
        /* valid function code, invalid reserved bits */
        program_interrupt(env, PGM_SPECIFICATION, 2);
    }

    sel1 = r0 & STSI_R0_SEL1_MASK;
    sel2 = r1 & STSI_R1_SEL2_MASK;

    /* XXX: spec exception if sysib is not 4k-aligned */

    switch (r0 & STSI_LEVEL_MASK) {
    case STSI_LEVEL_1:
        if ((sel1 == 1) && (sel2 == 1)) {
            /* Basic Machine Configuration */
            struct sysib_111 sysib;

            memset(&sysib, 0, sizeof(sysib));
            ebcdic_put(sysib.manuf, "QEMU            ", 16);
            /* same as machine type number in STORE CPU ID */
            ebcdic_put(sysib.type, "QEMU", 4);
            /* same as model number in STORE CPU ID */
            ebcdic_put(sysib.model, "QEMU            ", 16);
            ebcdic_put(sysib.sequence, "QEMU            ", 16);
            ebcdic_put(sysib.plant, "QEMU", 4);
            cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
        } else if ((sel1 == 2) && (sel2 == 1)) {
            /* Basic Machine CPU */
            struct sysib_121 sysib;

            memset(&sysib, 0, sizeof(sysib));
            /* XXX make different for different CPUs? */
            ebcdic_put(sysib.sequence, "QEMUQEMUQEMUQEMU", 16);
            ebcdic_put(sysib.plant, "QEMU", 4);
            stw_p(&sysib.cpu_addr, env->cpu_num);
            cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
        } else if ((sel1 == 2) && (sel2 == 2)) {
            /* Basic Machine CPUs */
            struct sysib_122 sysib;

            memset(&sysib, 0, sizeof(sysib));
            stl_p(&sysib.capability, 0x443afc29);
            /* XXX change when SMP comes */
            stw_p(&sysib.total_cpus, 1);
            stw_p(&sysib.active_cpus, 1);
            stw_p(&sysib.standby_cpus, 0);
            stw_p(&sysib.reserved_cpus, 0);
            cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
        } else {
            cc = 3;
        }
        break;
    case STSI_LEVEL_2:
        {
            if ((sel1 == 2) && (sel2 == 1)) {
                /* LPAR CPU */
                struct sysib_221 sysib;

                memset(&sysib, 0, sizeof(sysib));
                /* XXX make different for different CPUs? */
                ebcdic_put(sysib.sequence, "QEMUQEMUQEMUQEMU", 16);
                ebcdic_put(sysib.plant, "QEMU", 4);
                stw_p(&sysib.cpu_addr, env->cpu_num);
                stw_p(&sysib.cpu_id, 0);
                cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
            } else if ((sel1 == 2) && (sel2 == 2)) {
                /* LPAR CPUs */
                struct sysib_222 sysib;

                memset(&sysib, 0, sizeof(sysib));
                stw_p(&sysib.lpar_num, 0);
                sysib.lcpuc = 0;
                /* XXX change when SMP comes */
                stw_p(&sysib.total_cpus, 1);
                stw_p(&sysib.conf_cpus, 1);
                stw_p(&sysib.standby_cpus, 0);
                stw_p(&sysib.reserved_cpus, 0);
                ebcdic_put(sysib.name, "QEMU    ", 8);
                stl_p(&sysib.caf, 1000);
                stw_p(&sysib.dedicated_cpus, 0);
                stw_p(&sysib.shared_cpus, 0);
                cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
            } else {
                cc = 3;
            }
            break;
        }
    case STSI_LEVEL_3:
        {
            if ((sel1 == 2) && (sel2 == 2)) {
                /* VM CPUs */
                struct sysib_322 sysib;

                memset(&sysib, 0, sizeof(sysib));
                sysib.count = 1;
                /* XXX change when SMP comes */
                stw_p(&sysib.vm[0].total_cpus, 1);
                stw_p(&sysib.vm[0].conf_cpus, 1);
                stw_p(&sysib.vm[0].standby_cpus, 0);
                stw_p(&sysib.vm[0].reserved_cpus, 0);
                ebcdic_put(sysib.vm[0].name, "KVMguest", 8);
                stl_p(&sysib.vm[0].caf, 1000);
                ebcdic_put(sysib.vm[0].cpi, "KVM/Linux       ", 16);
                cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
            } else {
                cc = 3;
            }
            break;
        }
    case STSI_LEVEL_CURRENT:
        env->regs[0] = STSI_LEVEL_3;
        break;
    default:
        cc = 3;
        break;
    }

    return cc;
}

uint32_t HELPER(sigp)(CPUS390XState *env, uint64_t order_code, uint32_t r1,
                      uint64_t cpu_addr)
{
    int cc = 0;

    HELPER_LOG("%s: %016" PRIx64 " %08x %016" PRIx64 "\n",
               __func__, order_code, r1, cpu_addr);

    /* Remember: Use "R1 or R1 + 1, whichever is the odd-numbered register"
       as parameter (input). Status (output) is always R1. */

    switch (order_code) {
    case SIGP_SET_ARCH:
        /* switch arch */
        break;
    case SIGP_SENSE:
        /* enumerate CPU status */
        if (cpu_addr) {
            /* XXX implement when SMP comes */
            return 3;
        }
        env->regs[r1] &= 0xffffffff00000000ULL;
        cc = 1;
        break;
#if !defined(CONFIG_USER_ONLY)
    case SIGP_RESTART:
        qemu_system_reset_request();
        cpu_loop_exit(env);
        break;
    case SIGP_STOP:
        qemu_system_shutdown_request();
        cpu_loop_exit(env);
        break;
#endif
    default:
        /* unknown sigp */
        fprintf(stderr, "XXX unknown sigp: 0x%" PRIx64 "\n", order_code);
        cc = 3;
    }

    return cc;
}
#endif
