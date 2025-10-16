/*
 *  S/390 misc helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
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
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "qemu/timer.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/target_page.h"
#include "qapi/error.h"
#include "tcg_s390x.h"
#include "s390-tod.h"

#if !defined(CONFIG_USER_ONLY)
#include "system/cpus.h"
#include "system/system.h"
#include "hw/s390x/ebcdic.h"
#include "hw/s390x/s390-hypercall.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/ioinst.h"
#include "hw/s390x/s390-pci-inst.h"
#include "hw/boards.h"
#include "hw/s390x/tod.h"
#include CONFIG_DEVICES
#endif

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

/* Raise an exception statically from a TB.  */
void HELPER(exception)(CPUS390XState *env, uint32_t excp)
{
    CPUState *cs = env_cpu(env);

    HELPER_LOG("%s: exception %d\n", __func__, excp);
    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

/* Store CPU Timer (also used for EXTRACT CPU TIME) */
uint64_t HELPER(stpt)(CPUS390XState *env)
{
#if defined(CONFIG_USER_ONLY)
    /*
     * Fake a descending CPU timer. We could get negative values here,
     * but we don't care as it is up to the OS when to process that
     * interrupt and reset to > 0.
     */
    return UINT64_MAX - (uint64_t)cpu_get_host_ticks();
#else
    return time2tod(env->cputm - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
#endif
}

/* Store Clock */
uint64_t HELPER(stck)(CPUS390XState *env)
{
#ifdef CONFIG_USER_ONLY
    struct timespec ts;
    uint64_t ns;

    clock_gettime(CLOCK_REALTIME, &ts);
    ns = ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;

    return TOD_UNIX_EPOCH + time2tod(ns);
#else
    S390TODState *td = s390_get_todstate();
    S390TODClass *tdc = S390_TOD_GET_CLASS(td);
    S390TOD tod;

    tdc->get(td, &tod, &error_abort);
    return tod.low;
#endif
}

#ifndef CONFIG_USER_ONLY
/* SCLP service call */
uint32_t HELPER(servc)(CPUS390XState *env, uint64_t r1, uint64_t r2)
{
    bql_lock();
    int r = sclp_service_call(env_archcpu(env), r1, r2);
    bql_unlock();
    if (r < 0) {
        tcg_s390_program_interrupt(env, -r, GETPC());
    }
    return r;
}

void HELPER(diag)(CPUS390XState *env, uint32_t r1, uint32_t r3, uint32_t num)
{
    uint64_t r;

    switch (num) {
#ifdef CONFIG_S390_CCW_VIRTIO
    case 0x500:
        /* QEMU/KVM hypercall */
        bql_lock();
        handle_diag_500(env_archcpu(env), GETPC());
        bql_unlock();
        r = 0;
        break;
#endif /* CONFIG_S390_CCW_VIRTIO */
    case 0x44:
        /* yield */
        r = 0;
        break;
    case 0x308:
        /* ipl */
        bql_lock();
        handle_diag_308(env, r1, r3, GETPC());
        bql_unlock();
        r = 0;
        break;
    case 0x288:
        /* time bomb (watchdog) */
        r = handle_diag_288(env, r1, r3);
        break;
    default:
        r = -1;
        break;
    }

    if (r) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }
}

/* Set Prefix */
void HELPER(spx)(CPUS390XState *env, uint64_t a1)
{
    const uint32_t prefix = a1 & 0x7fffe000;
    const uint32_t old_prefix = env->psa;
    CPUState *cs = env_cpu(env);

    if (prefix == old_prefix) {
        return;
    }
    /*
     * Since prefix got aligned to 8k and memory increments are a multiple of
     * 8k checking the first page is sufficient
     */
    if (!mmu_absolute_addr_valid(prefix, true)) {
        tcg_s390_program_interrupt(env, PGM_ADDRESSING, GETPC());
    }

    env->psa = prefix;
    HELPER_LOG("prefix: %#x\n", prefix);
    tlb_flush_page(cs, 0);
    tlb_flush_page(cs, TARGET_PAGE_SIZE);
    if (prefix != 0) {
        tlb_flush_page(cs, prefix);
        tlb_flush_page(cs, prefix + TARGET_PAGE_SIZE);
    }
    if (old_prefix != 0) {
        tlb_flush_page(cs, old_prefix);
        tlb_flush_page(cs, old_prefix + TARGET_PAGE_SIZE);
    }
}

static void update_ckc_timer(CPUS390XState *env)
{
    S390TODState *td = s390_get_todstate();
    uint64_t time;

    /* stop the timer and remove pending CKC IRQs */
    timer_del(env->tod_timer);
    g_assert(bql_locked());
    env->pending_int &= ~INTERRUPT_EXT_CLOCK_COMPARATOR;

    /* the tod has to exceed the ckc, this can never happen if ckc is all 1's */
    if (env->ckc == -1ULL) {
        return;
    }

    if (env->ckc < td->base.low) {
        time = 0;
    } else {
        /* difference between origins */
        time = env->ckc - td->base.low;

        /* nanoseconds */
        time = tod2time(time);
    }

    timer_mod(env->tod_timer, time);
}

/* Set Clock Comparator */
void HELPER(sckc)(CPUS390XState *env, uint64_t ckc)
{
    env->ckc = ckc;

    bql_lock();
    update_ckc_timer(env);
    bql_unlock();
}

void tcg_s390_tod_updated(CPUState *cs, run_on_cpu_data opaque)
{
    update_ckc_timer(cpu_env(cs));
}

/* Set Clock */
uint32_t HELPER(sck)(CPUS390XState *env, uint64_t tod_low)
{
    S390TODState *td = s390_get_todstate();
    S390TODClass *tdc = S390_TOD_GET_CLASS(td);
    S390TOD tod = {
        .high = 0,
        .low = tod_low,
    };

    bql_lock();
    tdc->set(td, &tod, &error_abort);
    bql_unlock();
    return 0;
}

/* Set Tod Programmable Field */
void HELPER(sckpf)(CPUS390XState *env, uint64_t r0)
{
    uint32_t val = r0;

    if (val & 0xffff0000) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }
    env->todpr = val;
}

/* Store Clock Comparator */
uint64_t HELPER(stckc)(CPUS390XState *env)
{
    return env->ckc;
}

/* Set CPU Timer */
void HELPER(spt)(CPUS390XState *env, uint64_t time)
{
    if (time == -1ULL) {
        return;
    }

    /* nanoseconds */
    time = tod2time(time);

    env->cputm = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + time;

    timer_mod(env->cpu_timer, env->cputm);
}

/* Store System Information */
uint32_t HELPER(stsi)(CPUS390XState *env, uint64_t a0, uint64_t r0, uint64_t r1)
{
    const uintptr_t ra = GETPC();
    const uint32_t sel1 = r0 & STSI_R0_SEL1_MASK;
    const uint32_t sel2 = r1 & STSI_R1_SEL2_MASK;
    const MachineState *ms = MACHINE(qdev_get_machine());
    uint16_t total_cpus = 0, conf_cpus = 0, reserved_cpus = 0;
    S390CPU *cpu = env_archcpu(env);
    SysIB sysib = { };
    int i, cc = 0;

    if ((r0 & STSI_R0_FC_MASK) > STSI_R0_FC_LEVEL_3) {
        /* invalid function code: no other checks are performed */
        return 3;
    }

    if ((r0 & STSI_R0_RESERVED_MASK) || (r1 & STSI_R1_RESERVED_MASK)) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    if ((r0 & STSI_R0_FC_MASK) == STSI_R0_FC_CURRENT) {
        /* query the current level: no further checks are performed */
        env->regs[0] = STSI_R0_FC_LEVEL_3;
        return 0;
    }

    if (a0 & ~TARGET_PAGE_MASK) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    /* count the cpus and split them into configured and reserved ones */
    for (i = 0; i < ms->possible_cpus->len; i++) {
        total_cpus++;
        if (ms->possible_cpus->cpus[i].cpu) {
            conf_cpus++;
        } else {
            reserved_cpus++;
        }
    }

    /*
     * In theory, we could report Level 1 / Level 2 as current. However,
     * the Linux kernel will detect this as running under LPAR and assume
     * that we have a sclp linemode console (which is always present on
     * LPAR, but not the default for QEMU), therefore not displaying boot
     * messages and making booting a Linux kernel under TCG harder.
     *
     * For now we fake the same SMP configuration on all levels.
     *
     * TODO: We could later make the level configurable via the machine
     *       and change defaults (linemode console) based on machine type
     *       and accelerator.
     */
    switch (r0 & STSI_R0_FC_MASK) {
    case STSI_R0_FC_LEVEL_1:
        if ((sel1 == 1) && (sel2 == 1)) {
            /* Basic Machine Configuration */
            char type[5] = {};

            ebcdic_put(sysib.sysib_111.manuf, "QEMU            ", 16);
            /* same as machine type number in STORE CPU ID, but in EBCDIC */
            snprintf(type, ARRAY_SIZE(type), "%X", cpu->model->def->type);
            ebcdic_put(sysib.sysib_111.type, type, 4);
            /* model number (not stored in STORE CPU ID for z/Architecture) */
            ebcdic_put(sysib.sysib_111.model, "QEMU            ", 16);
            ebcdic_put(sysib.sysib_111.sequence, "QEMU            ", 16);
            ebcdic_put(sysib.sysib_111.plant, "QEMU", 4);
        } else if ((sel1 == 2) && (sel2 == 1)) {
            /* Basic Machine CPU */
            ebcdic_put(sysib.sysib_121.sequence, "QEMUQEMUQEMUQEMU", 16);
            ebcdic_put(sysib.sysib_121.plant, "QEMU", 4);
            sysib.sysib_121.cpu_addr = cpu_to_be16(env->core_id);
        } else if ((sel1 == 2) && (sel2 == 2)) {
            /* Basic Machine CPUs */
            sysib.sysib_122.capability = cpu_to_be32(0x443afc29);
            sysib.sysib_122.total_cpus = cpu_to_be16(total_cpus);
            sysib.sysib_122.conf_cpus = cpu_to_be16(conf_cpus);
            sysib.sysib_122.reserved_cpus = cpu_to_be16(reserved_cpus);
        } else {
            cc = 3;
        }
        break;
    case STSI_R0_FC_LEVEL_2:
        if ((sel1 == 2) && (sel2 == 1)) {
            /* LPAR CPU */
            ebcdic_put(sysib.sysib_221.sequence, "QEMUQEMUQEMUQEMU", 16);
            ebcdic_put(sysib.sysib_221.plant, "QEMU", 4);
            sysib.sysib_221.cpu_addr = cpu_to_be16(env->core_id);
        } else if ((sel1 == 2) && (sel2 == 2)) {
            /* LPAR CPUs */
            sysib.sysib_222.lcpuc = 0x80; /* dedicated */
            sysib.sysib_222.total_cpus = cpu_to_be16(total_cpus);
            sysib.sysib_222.conf_cpus = cpu_to_be16(conf_cpus);
            sysib.sysib_222.reserved_cpus = cpu_to_be16(reserved_cpus);
            ebcdic_put(sysib.sysib_222.name, "QEMU    ", 8);
            sysib.sysib_222.caf = cpu_to_be32(1000);
            sysib.sysib_222.dedicated_cpus = cpu_to_be16(conf_cpus);
        } else {
            cc = 3;
        }
        break;
    case STSI_R0_FC_LEVEL_3:
        if ((sel1 == 2) && (sel2 == 2)) {
            /* VM CPUs */
            sysib.sysib_322.count = 1;
            sysib.sysib_322.vm[0].total_cpus = cpu_to_be16(total_cpus);
            sysib.sysib_322.vm[0].conf_cpus = cpu_to_be16(conf_cpus);
            sysib.sysib_322.vm[0].reserved_cpus = cpu_to_be16(reserved_cpus);
            sysib.sysib_322.vm[0].caf = cpu_to_be32(1000);
            /* Linux kernel uses this to distinguish us from z/VM */
            ebcdic_put(sysib.sysib_322.vm[0].cpi, "KVM/Linux       ", 16);
            sysib.sysib_322.vm[0].ext_name_encoding = 2; /* UTF-8 */

            /* If our VM has a name, use the real name */
            if (qemu_name) {
                memset(sysib.sysib_322.vm[0].name, 0x40,
                       sizeof(sysib.sysib_322.vm[0].name));
                ebcdic_put(sysib.sysib_322.vm[0].name, qemu_name,
                           MIN(sizeof(sysib.sysib_322.vm[0].name),
                               strlen(qemu_name)));
                strpadcpy((char *)sysib.sysib_322.ext_names[0],
                          sizeof(sysib.sysib_322.ext_names[0]),
                          qemu_name, '\0');

            } else {
                ebcdic_put(sysib.sysib_322.vm[0].name, "TCGguest", 8);
                strcpy((char *)sysib.sysib_322.ext_names[0], "TCGguest");
            }

            /* add the uuid */
            memcpy(sysib.sysib_322.vm[0].uuid, &qemu_uuid,
                   sizeof(sysib.sysib_322.vm[0].uuid));
        } else {
            cc = 3;
        }
        break;
    }

    if (cc == 0) {
        if (s390_cpu_virt_mem_write(cpu, a0, 0, &sysib, sizeof(sysib))) {
            s390_cpu_virt_mem_handle_exc(cpu, ra);
        }
    }

    return cc;
}

uint32_t HELPER(sigp)(CPUS390XState *env, uint64_t order_code, uint32_t r1,
                      uint32_t r3)
{
    int cc;

    /* TODO: needed to inject interrupts  - push further down */
    bql_lock();
    cc = handle_sigp(env, order_code & SIGP_ORDER_MASK, r1, r3);
    bql_unlock();

    return cc;
}
#endif

#ifndef CONFIG_USER_ONLY
void HELPER(xsch)(CPUS390XState *env, uint64_t r1)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_xsch(cpu, r1, GETPC());
    bql_unlock();
}

void HELPER(csch)(CPUS390XState *env, uint64_t r1)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_csch(cpu, r1, GETPC());
    bql_unlock();
}

void HELPER(hsch)(CPUS390XState *env, uint64_t r1)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_hsch(cpu, r1, GETPC());
    bql_unlock();
}

void HELPER(msch)(CPUS390XState *env, uint64_t r1, uint64_t inst)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_msch(cpu, r1, inst >> 16, GETPC());
    bql_unlock();
}

void HELPER(rchp)(CPUS390XState *env, uint64_t r1)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_rchp(cpu, r1, GETPC());
    bql_unlock();
}

void HELPER(rsch)(CPUS390XState *env, uint64_t r1)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_rsch(cpu, r1, GETPC());
    bql_unlock();
}

void HELPER(sal)(CPUS390XState *env, uint64_t r1)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    ioinst_handle_sal(cpu, r1, GETPC());
    bql_unlock();
}

void HELPER(schm)(CPUS390XState *env, uint64_t r1, uint64_t r2, uint64_t inst)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    ioinst_handle_schm(cpu, r1, r2, inst >> 16, GETPC());
    bql_unlock();
}

void HELPER(ssch)(CPUS390XState *env, uint64_t r1, uint64_t inst)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_ssch(cpu, r1, inst >> 16, GETPC());
    bql_unlock();
}

void HELPER(stcrw)(CPUS390XState *env, uint64_t inst)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    ioinst_handle_stcrw(cpu, inst >> 16, GETPC());
    bql_unlock();
}

void HELPER(stsch)(CPUS390XState *env, uint64_t r1, uint64_t inst)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_stsch(cpu, r1, inst >> 16, GETPC());
    bql_unlock();
}

uint32_t HELPER(tpi)(CPUS390XState *env, uint64_t addr)
{
    const uintptr_t ra = GETPC();
    S390CPU *cpu = env_archcpu(env);
    QEMUS390FLICState *flic = s390_get_qemu_flic(s390_get_flic());
    QEMUS390FlicIO *io = NULL;
    LowCore *lowcore;

    if (addr & 0x3) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    bql_lock();
    io = qemu_s390_flic_dequeue_io(flic, env->cregs[6]);
    if (!io) {
        bql_unlock();
        return 0;
    }

    if (addr) {
        struct {
            uint16_t id;
            uint16_t nr;
            uint32_t parm;
        } intc = {
            .id = cpu_to_be16(io->id),
            .nr = cpu_to_be16(io->nr),
            .parm = cpu_to_be32(io->parm),
        };

        if (s390_cpu_virt_mem_write(cpu, addr, 0, &intc, sizeof(intc))) {
            /* writing failed, reinject and properly clean up */
            s390_io_interrupt(io->id, io->nr, io->parm, io->word);
            bql_unlock();
            g_free(io);
            s390_cpu_virt_mem_handle_exc(cpu, ra);
            return 0;
        }
    } else {
        /* no protection applies */
        lowcore = cpu_map_lowcore(env);
        lowcore->subchannel_id = cpu_to_be16(io->id);
        lowcore->subchannel_nr = cpu_to_be16(io->nr);
        lowcore->io_int_parm = cpu_to_be32(io->parm);
        lowcore->io_int_word = cpu_to_be32(io->word);
        cpu_unmap_lowcore(env, lowcore);
    }

    g_free(io);
    bql_unlock();
    return 1;
}

void HELPER(tsch)(CPUS390XState *env, uint64_t r1, uint64_t inst)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_tsch(cpu, r1, inst >> 16, GETPC());
    bql_unlock();
}

void HELPER(chsc)(CPUS390XState *env, uint64_t inst)
{
    S390CPU *cpu = env_archcpu(env);
    bql_lock();
    ioinst_handle_chsc(cpu, inst >> 16, GETPC());
    bql_unlock();
}
#endif

#ifndef CONFIG_USER_ONLY
static G_NORETURN void per_raise_exception(CPUS390XState *env)
{
    trigger_pgm_exception(env, PGM_PER);
    cpu_loop_exit(env_cpu(env));
}

static G_NORETURN void per_raise_exception_log(CPUS390XState *env)
{
    qemu_log_mask(CPU_LOG_INT, "PER interrupt after 0x%" PRIx64 "\n",
                  env->per_address);
    per_raise_exception(env);
}

void HELPER(per_check_exception)(CPUS390XState *env)
{
    /* psw_addr, per_address and int_pgm_ilen are already set. */
    if (unlikely(env->per_perc_atmid)) {
        per_raise_exception_log(env);
    }
}

/* Check if an address is within the PER starting address and the PER
   ending address.  The address range might loop.  */
static inline bool get_per_in_range(CPUS390XState *env, uint64_t addr)
{
    if (env->cregs[10] <= env->cregs[11]) {
        return env->cregs[10] <= addr && addr <= env->cregs[11];
    } else {
        return env->cregs[10] <= addr || addr <= env->cregs[11];
    }
}

void HELPER(per_branch)(CPUS390XState *env, uint64_t dest, uint32_t ilen)
{
    if ((env->cregs[9] & PER_CR9_CONTROL_BRANCH_ADDRESS)
        && !get_per_in_range(env, dest)) {
        return;
    }

    env->psw.addr = dest;
    env->int_pgm_ilen = ilen;
    env->per_address = env->gbea;
    env->per_perc_atmid = PER_CODE_EVENT_BRANCH | get_per_atmid(env);
    per_raise_exception_log(env);
}

void HELPER(per_ifetch)(CPUS390XState *env, uint32_t ilen)
{
    if (get_per_in_range(env, env->psw.addr)) {
        env->per_address = env->psw.addr;
        env->int_pgm_ilen = ilen;
        env->per_perc_atmid = PER_CODE_EVENT_IFETCH | get_per_atmid(env);

        /* If the instruction has to be nullified, trigger the
           exception immediately. */
        if (env->cregs[9] & PER_CR9_EVENT_IFETCH_NULLIFICATION) {
            env->per_perc_atmid |= PER_CODE_EVENT_NULLIFICATION;
            qemu_log_mask(CPU_LOG_INT, "PER interrupt before 0x%" PRIx64 "\n",
                          env->per_address);
            per_raise_exception(env);
        }
    }
}

void HELPER(per_store_real)(CPUS390XState *env, uint32_t ilen)
{
    /* PSW is saved just before calling the helper.  */
    env->per_address = env->psw.addr;
    env->int_pgm_ilen = ilen;
    env->per_perc_atmid = PER_CODE_EVENT_STORE_REAL | get_per_atmid(env);
    per_raise_exception_log(env);
}
#endif

static uint8_t stfl_bytes[2048];
static unsigned int used_stfl_bytes;

static void prepare_stfl(void)
{
    static bool initialized;
    int i;

    /* racy, but we don't care, the same values are always written */
    if (initialized) {
        return;
    }

    s390_get_feat_block(S390_FEAT_TYPE_STFL, stfl_bytes);
    for (i = 0; i < sizeof(stfl_bytes); i++) {
        if (stfl_bytes[i]) {
            used_stfl_bytes = i + 1;
        }
    }
    initialized = true;
}

#ifndef CONFIG_USER_ONLY
void HELPER(stfl)(CPUS390XState *env)
{
    LowCore *lowcore;

    lowcore = cpu_map_lowcore(env);
    prepare_stfl();
    memcpy(&lowcore->stfl_fac_list, stfl_bytes, sizeof(lowcore->stfl_fac_list));
    cpu_unmap_lowcore(env, lowcore);
}
#endif

uint32_t HELPER(stfle)(CPUS390XState *env, uint64_t addr)
{
    const uintptr_t ra = GETPC();
    const int count_bytes = ((env->regs[0] & 0xff) + 1) * 8;
    int max_bytes;
    int i;

    if (addr & 0x7) {
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, ra);
    }

    prepare_stfl();
    max_bytes = ROUND_UP(used_stfl_bytes, 8);

    /*
     * The PoP says that doublewords beyond the highest-numbered facility
     * bit may or may not be stored.  However, existing hardware appears to
     * not store the words, and existing software depend on that.
     */
    for (i = 0; i < MIN(count_bytes, max_bytes); ++i) {
        cpu_stb_data_ra(env, addr + i, stfl_bytes[i], ra);
    }

    env->regs[0] = deposit64(env->regs[0], 0, 8, (max_bytes / 8) - 1);
    return count_bytes >= max_bytes ? 0 : 3;
}

#ifndef CONFIG_USER_ONLY
/*
 * Note: we ignore any return code of the functions called for the pci
 * instructions, as the only time they return !0 is when the stub is
 * called, and in that case we didn't even offer the zpci facility.
 * The only exception is SIC, where program checks need to be handled
 * by the caller.
 */
void HELPER(clp)(CPUS390XState *env, uint32_t r2)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    clp_service_call(cpu, r2, GETPC());
    bql_unlock();
}

void HELPER(pcilg)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    pcilg_service_call(cpu, r1, r2, GETPC());
    bql_unlock();
}

void HELPER(pcistg)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    pcistg_service_call(cpu, r1, r2, GETPC());
    bql_unlock();
}

void HELPER(stpcifc)(CPUS390XState *env, uint32_t r1, uint64_t fiba,
                     uint32_t ar)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    stpcifc_service_call(cpu, r1, fiba, ar, GETPC());
    bql_unlock();
}

void HELPER(sic)(CPUS390XState *env, uint64_t r1, uint64_t r3)
{
    S390CPU *cpu = env_archcpu(env);
    int r;

    bql_lock();
    r = css_do_sic(cpu, (r3 >> 27) & 0x7, r1 & 0xffff);
    bql_unlock();
    /* css_do_sic() may actually return a PGM_xxx value to inject */
    if (r) {
        tcg_s390_program_interrupt(env, -r, GETPC());
    }
}

void HELPER(rpcit)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    rpcit_service_call(cpu, r1, r2, GETPC());
    bql_unlock();
}

void HELPER(pcistb)(CPUS390XState *env, uint32_t r1, uint32_t r3,
                    uint64_t gaddr, uint32_t ar)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    pcistb_service_call(cpu, r1, r3, gaddr, ar, GETPC());
    bql_unlock();
}

void HELPER(mpcifc)(CPUS390XState *env, uint32_t r1, uint64_t fiba,
                    uint32_t ar)
{
    S390CPU *cpu = env_archcpu(env);

    bql_lock();
    mpcifc_service_call(cpu, r1, fiba, ar, GETPC());
    bql_unlock();
}
#endif
