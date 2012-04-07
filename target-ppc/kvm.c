/*
 * PowerPC implementation of KVM hooks
 *
 * Copyright IBM Corp. 2007
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 *
 * Authors:
 *  Jerone Young <jyoung5@us.ibm.com>
 *  Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *  Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <dirent.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "cpu.h"
#include "device_tree.h"
#include "hw/sysbus.h"
#include "hw/spapr.h"

#include "hw/sysbus.h"
#include "hw/spapr.h"
#include "hw/spapr_vio.h"

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

#define PROC_DEVTREE_CPU      "/proc/device-tree/cpus/"

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int cap_interrupt_unset = false;
static int cap_interrupt_level = false;
static int cap_segstate;
static int cap_booke_sregs;
static int cap_ppc_smt;
static int cap_ppc_rma;
static int cap_spapr_tce;

/* XXX We have a race condition where we actually have a level triggered
 *     interrupt, but the infrastructure can't expose that yet, so the guest
 *     takes but ignores it, goes to sleep and never gets notified that there's
 *     still an interrupt pending.
 *
 *     As a quick workaround, let's just wake up again 20 ms after we injected
 *     an interrupt. That way we can assure that we're always reinjecting
 *     interrupts in case the guest swallowed them.
 */
static QEMUTimer *idle_timer;

static void kvm_kick_env(void *env)
{
    qemu_cpu_kick(env);
}

int kvm_arch_init(KVMState *s)
{
    cap_interrupt_unset = kvm_check_extension(s, KVM_CAP_PPC_UNSET_IRQ);
    cap_interrupt_level = kvm_check_extension(s, KVM_CAP_PPC_IRQ_LEVEL);
    cap_segstate = kvm_check_extension(s, KVM_CAP_PPC_SEGSTATE);
    cap_booke_sregs = kvm_check_extension(s, KVM_CAP_PPC_BOOKE_SREGS);
    cap_ppc_smt = kvm_check_extension(s, KVM_CAP_PPC_SMT);
    cap_ppc_rma = kvm_check_extension(s, KVM_CAP_PPC_RMA);
    cap_spapr_tce = kvm_check_extension(s, KVM_CAP_SPAPR_TCE);

    if (!cap_interrupt_level) {
        fprintf(stderr, "KVM: Couldn't find level irq capability. Expect the "
                        "VM to stall at times!\n");
    }

    return 0;
}

static int kvm_arch_sync_sregs(CPUPPCState *cenv)
{
    struct kvm_sregs sregs;
    int ret;

    if (cenv->excp_model == POWERPC_EXCP_BOOKE) {
        /* What we're really trying to say is "if we're on BookE, we use
           the native PVR for now". This is the only sane way to check
           it though, so we potentially confuse users that they can run
           BookE guests on BookS. Let's hope nobody dares enough :) */
        return 0;
    } else {
        if (!cap_segstate) {
            fprintf(stderr, "kvm error: missing PVR setting capability\n");
            return -ENOSYS;
        }
    }

    ret = kvm_vcpu_ioctl(cenv, KVM_GET_SREGS, &sregs);
    if (ret) {
        return ret;
    }

    sregs.pvr = cenv->spr[SPR_PVR];
    return kvm_vcpu_ioctl(cenv, KVM_SET_SREGS, &sregs);
}

/* Set up a shared TLB array with KVM */
static int kvm_booke206_tlb_init(CPUPPCState *env)
{
    struct kvm_book3e_206_tlb_params params = {};
    struct kvm_config_tlb cfg = {};
    struct kvm_enable_cap encap = {};
    unsigned int entries = 0;
    int ret, i;

    if (!kvm_enabled() ||
        !kvm_check_extension(env->kvm_state, KVM_CAP_SW_TLB)) {
        return 0;
    }

    assert(ARRAY_SIZE(params.tlb_sizes) == BOOKE206_MAX_TLBN);

    for (i = 0; i < BOOKE206_MAX_TLBN; i++) {
        params.tlb_sizes[i] = booke206_tlb_size(env, i);
        params.tlb_ways[i] = booke206_tlb_ways(env, i);
        entries += params.tlb_sizes[i];
    }

    assert(entries == env->nb_tlb);
    assert(sizeof(struct kvm_book3e_206_tlb_entry) == sizeof(ppcmas_tlb_t));

    env->tlb_dirty = true;

    cfg.array = (uintptr_t)env->tlb.tlbm;
    cfg.array_len = sizeof(ppcmas_tlb_t) * entries;
    cfg.params = (uintptr_t)&params;
    cfg.mmu_type = KVM_MMU_FSL_BOOKE_NOHV;

    encap.cap = KVM_CAP_SW_TLB;
    encap.args[0] = (uintptr_t)&cfg;

    ret = kvm_vcpu_ioctl(env, KVM_ENABLE_CAP, &encap);
    if (ret < 0) {
        fprintf(stderr, "%s: couldn't enable KVM_CAP_SW_TLB: %s\n",
                __func__, strerror(-ret));
        return ret;
    }

    env->kvm_sw_tlb = true;
    return 0;
}

int kvm_arch_init_vcpu(CPUPPCState *cenv)
{
    int ret;

    ret = kvm_arch_sync_sregs(cenv);
    if (ret) {
        return ret;
    }

    idle_timer = qemu_new_timer_ns(vm_clock, kvm_kick_env, cenv);

    /* Some targets support access to KVM's guest TLB. */
    switch (cenv->mmu_model) {
    case POWERPC_MMU_BOOKE206:
        ret = kvm_booke206_tlb_init(cenv);
        break;
    default:
        break;
    }

    return ret;
}

void kvm_arch_reset_vcpu(CPUPPCState *env)
{
}

static void kvm_sw_tlb_put(CPUPPCState *env)
{
    struct kvm_dirty_tlb dirty_tlb;
    unsigned char *bitmap;
    int ret;

    if (!env->kvm_sw_tlb) {
        return;
    }

    bitmap = g_malloc((env->nb_tlb + 7) / 8);
    memset(bitmap, 0xFF, (env->nb_tlb + 7) / 8);

    dirty_tlb.bitmap = (uintptr_t)bitmap;
    dirty_tlb.num_dirty = env->nb_tlb;

    ret = kvm_vcpu_ioctl(env, KVM_DIRTY_TLB, &dirty_tlb);
    if (ret) {
        fprintf(stderr, "%s: KVM_DIRTY_TLB: %s\n",
                __func__, strerror(-ret));
    }

    g_free(bitmap);
}

int kvm_arch_put_registers(CPUPPCState *env, int level)
{
    struct kvm_regs regs;
    int ret;
    int i;

    ret = kvm_vcpu_ioctl(env, KVM_GET_REGS, &regs);
    if (ret < 0)
        return ret;

    regs.ctr = env->ctr;
    regs.lr  = env->lr;
    regs.xer = env->xer;
    regs.msr = env->msr;
    regs.pc = env->nip;

    regs.srr0 = env->spr[SPR_SRR0];
    regs.srr1 = env->spr[SPR_SRR1];

    regs.sprg0 = env->spr[SPR_SPRG0];
    regs.sprg1 = env->spr[SPR_SPRG1];
    regs.sprg2 = env->spr[SPR_SPRG2];
    regs.sprg3 = env->spr[SPR_SPRG3];
    regs.sprg4 = env->spr[SPR_SPRG4];
    regs.sprg5 = env->spr[SPR_SPRG5];
    regs.sprg6 = env->spr[SPR_SPRG6];
    regs.sprg7 = env->spr[SPR_SPRG7];

    regs.pid = env->spr[SPR_BOOKE_PID];

    for (i = 0;i < 32; i++)
        regs.gpr[i] = env->gpr[i];

    ret = kvm_vcpu_ioctl(env, KVM_SET_REGS, &regs);
    if (ret < 0)
        return ret;

    if (env->tlb_dirty) {
        kvm_sw_tlb_put(env);
        env->tlb_dirty = false;
    }

    return ret;
}

int kvm_arch_get_registers(CPUPPCState *env)
{
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    uint32_t cr;
    int i, ret;

    ret = kvm_vcpu_ioctl(env, KVM_GET_REGS, &regs);
    if (ret < 0)
        return ret;

    cr = regs.cr;
    for (i = 7; i >= 0; i--) {
        env->crf[i] = cr & 15;
        cr >>= 4;
    }

    env->ctr = regs.ctr;
    env->lr = regs.lr;
    env->xer = regs.xer;
    env->msr = regs.msr;
    env->nip = regs.pc;

    env->spr[SPR_SRR0] = regs.srr0;
    env->spr[SPR_SRR1] = regs.srr1;

    env->spr[SPR_SPRG0] = regs.sprg0;
    env->spr[SPR_SPRG1] = regs.sprg1;
    env->spr[SPR_SPRG2] = regs.sprg2;
    env->spr[SPR_SPRG3] = regs.sprg3;
    env->spr[SPR_SPRG4] = regs.sprg4;
    env->spr[SPR_SPRG5] = regs.sprg5;
    env->spr[SPR_SPRG6] = regs.sprg6;
    env->spr[SPR_SPRG7] = regs.sprg7;

    env->spr[SPR_BOOKE_PID] = regs.pid;

    for (i = 0;i < 32; i++)
        env->gpr[i] = regs.gpr[i];

    if (cap_booke_sregs) {
        ret = kvm_vcpu_ioctl(env, KVM_GET_SREGS, &sregs);
        if (ret < 0) {
            return ret;
        }

        if (sregs.u.e.features & KVM_SREGS_E_BASE) {
            env->spr[SPR_BOOKE_CSRR0] = sregs.u.e.csrr0;
            env->spr[SPR_BOOKE_CSRR1] = sregs.u.e.csrr1;
            env->spr[SPR_BOOKE_ESR] = sregs.u.e.esr;
            env->spr[SPR_BOOKE_DEAR] = sregs.u.e.dear;
            env->spr[SPR_BOOKE_MCSR] = sregs.u.e.mcsr;
            env->spr[SPR_BOOKE_TSR] = sregs.u.e.tsr;
            env->spr[SPR_BOOKE_TCR] = sregs.u.e.tcr;
            env->spr[SPR_DECR] = sregs.u.e.dec;
            env->spr[SPR_TBL] = sregs.u.e.tb & 0xffffffff;
            env->spr[SPR_TBU] = sregs.u.e.tb >> 32;
            env->spr[SPR_VRSAVE] = sregs.u.e.vrsave;
        }

        if (sregs.u.e.features & KVM_SREGS_E_ARCH206) {
            env->spr[SPR_BOOKE_PIR] = sregs.u.e.pir;
            env->spr[SPR_BOOKE_MCSRR0] = sregs.u.e.mcsrr0;
            env->spr[SPR_BOOKE_MCSRR1] = sregs.u.e.mcsrr1;
            env->spr[SPR_BOOKE_DECAR] = sregs.u.e.decar;
            env->spr[SPR_BOOKE_IVPR] = sregs.u.e.ivpr;
        }

        if (sregs.u.e.features & KVM_SREGS_E_64) {
            env->spr[SPR_BOOKE_EPCR] = sregs.u.e.epcr;
        }

        if (sregs.u.e.features & KVM_SREGS_E_SPRG8) {
            env->spr[SPR_BOOKE_SPRG8] = sregs.u.e.sprg8;
        }

        if (sregs.u.e.features & KVM_SREGS_E_IVOR) {
            env->spr[SPR_BOOKE_IVOR0] = sregs.u.e.ivor_low[0];
            env->spr[SPR_BOOKE_IVOR1] = sregs.u.e.ivor_low[1];
            env->spr[SPR_BOOKE_IVOR2] = sregs.u.e.ivor_low[2];
            env->spr[SPR_BOOKE_IVOR3] = sregs.u.e.ivor_low[3];
            env->spr[SPR_BOOKE_IVOR4] = sregs.u.e.ivor_low[4];
            env->spr[SPR_BOOKE_IVOR5] = sregs.u.e.ivor_low[5];
            env->spr[SPR_BOOKE_IVOR6] = sregs.u.e.ivor_low[6];
            env->spr[SPR_BOOKE_IVOR7] = sregs.u.e.ivor_low[7];
            env->spr[SPR_BOOKE_IVOR8] = sregs.u.e.ivor_low[8];
            env->spr[SPR_BOOKE_IVOR9] = sregs.u.e.ivor_low[9];
            env->spr[SPR_BOOKE_IVOR10] = sregs.u.e.ivor_low[10];
            env->spr[SPR_BOOKE_IVOR11] = sregs.u.e.ivor_low[11];
            env->spr[SPR_BOOKE_IVOR12] = sregs.u.e.ivor_low[12];
            env->spr[SPR_BOOKE_IVOR13] = sregs.u.e.ivor_low[13];
            env->spr[SPR_BOOKE_IVOR14] = sregs.u.e.ivor_low[14];
            env->spr[SPR_BOOKE_IVOR15] = sregs.u.e.ivor_low[15];

            if (sregs.u.e.features & KVM_SREGS_E_SPE) {
                env->spr[SPR_BOOKE_IVOR32] = sregs.u.e.ivor_high[0];
                env->spr[SPR_BOOKE_IVOR33] = sregs.u.e.ivor_high[1];
                env->spr[SPR_BOOKE_IVOR34] = sregs.u.e.ivor_high[2];
            }

            if (sregs.u.e.features & KVM_SREGS_E_PM) {
                env->spr[SPR_BOOKE_IVOR35] = sregs.u.e.ivor_high[3];
            }

            if (sregs.u.e.features & KVM_SREGS_E_PC) {
                env->spr[SPR_BOOKE_IVOR36] = sregs.u.e.ivor_high[4];
                env->spr[SPR_BOOKE_IVOR37] = sregs.u.e.ivor_high[5];
            }
        }

        if (sregs.u.e.features & KVM_SREGS_E_ARCH206_MMU) {
            env->spr[SPR_BOOKE_MAS0] = sregs.u.e.mas0;
            env->spr[SPR_BOOKE_MAS1] = sregs.u.e.mas1;
            env->spr[SPR_BOOKE_MAS2] = sregs.u.e.mas2;
            env->spr[SPR_BOOKE_MAS3] = sregs.u.e.mas7_3 & 0xffffffff;
            env->spr[SPR_BOOKE_MAS4] = sregs.u.e.mas4;
            env->spr[SPR_BOOKE_MAS6] = sregs.u.e.mas6;
            env->spr[SPR_BOOKE_MAS7] = sregs.u.e.mas7_3 >> 32;
            env->spr[SPR_MMUCFG] = sregs.u.e.mmucfg;
            env->spr[SPR_BOOKE_TLB0CFG] = sregs.u.e.tlbcfg[0];
            env->spr[SPR_BOOKE_TLB1CFG] = sregs.u.e.tlbcfg[1];
        }

        if (sregs.u.e.features & KVM_SREGS_EXP) {
            env->spr[SPR_BOOKE_EPR] = sregs.u.e.epr;
        }

        if (sregs.u.e.features & KVM_SREGS_E_PD) {
            env->spr[SPR_BOOKE_EPLC] = sregs.u.e.eplc;
            env->spr[SPR_BOOKE_EPSC] = sregs.u.e.epsc;
        }

        if (sregs.u.e.impl_id == KVM_SREGS_E_IMPL_FSL) {
            env->spr[SPR_E500_SVR] = sregs.u.e.impl.fsl.svr;
            env->spr[SPR_Exxx_MCAR] = sregs.u.e.impl.fsl.mcar;
            env->spr[SPR_HID0] = sregs.u.e.impl.fsl.hid0;

            if (sregs.u.e.impl.fsl.features & KVM_SREGS_E_FSL_PIDn) {
                env->spr[SPR_BOOKE_PID1] = sregs.u.e.impl.fsl.pid1;
                env->spr[SPR_BOOKE_PID2] = sregs.u.e.impl.fsl.pid2;
            }
        }
    }

    if (cap_segstate) {
        ret = kvm_vcpu_ioctl(env, KVM_GET_SREGS, &sregs);
        if (ret < 0) {
            return ret;
        }

        ppc_store_sdr1(env, sregs.u.s.sdr1);

        /* Sync SLB */
#ifdef TARGET_PPC64
        for (i = 0; i < 64; i++) {
            ppc_store_slb(env, sregs.u.s.ppc64.slb[i].slbe,
                               sregs.u.s.ppc64.slb[i].slbv);
        }
#endif

        /* Sync SRs */
        for (i = 0; i < 16; i++) {
            env->sr[i] = sregs.u.s.ppc32.sr[i];
        }

        /* Sync BATs */
        for (i = 0; i < 8; i++) {
            env->DBAT[0][i] = sregs.u.s.ppc32.dbat[i] & 0xffffffff;
            env->DBAT[1][i] = sregs.u.s.ppc32.dbat[i] >> 32;
            env->IBAT[0][i] = sregs.u.s.ppc32.ibat[i] & 0xffffffff;
            env->IBAT[1][i] = sregs.u.s.ppc32.ibat[i] >> 32;
        }
    }

    return 0;
}

int kvmppc_set_interrupt(CPUPPCState *env, int irq, int level)
{
    unsigned virq = level ? KVM_INTERRUPT_SET_LEVEL : KVM_INTERRUPT_UNSET;

    if (irq != PPC_INTERRUPT_EXT) {
        return 0;
    }

    if (!kvm_enabled() || !cap_interrupt_unset || !cap_interrupt_level) {
        return 0;
    }

    kvm_vcpu_ioctl(env, KVM_INTERRUPT, &virq);

    return 0;
}

#if defined(TARGET_PPCEMB)
#define PPC_INPUT_INT PPC40x_INPUT_INT
#elif defined(TARGET_PPC64)
#define PPC_INPUT_INT PPC970_INPUT_INT
#else
#define PPC_INPUT_INT PPC6xx_INPUT_INT
#endif

void kvm_arch_pre_run(CPUPPCState *env, struct kvm_run *run)
{
    int r;
    unsigned irq;

    /* PowerPC QEMU tracks the various core input pins (interrupt, critical
     * interrupt, reset, etc) in PPC-specific env->irq_input_state. */
    if (!cap_interrupt_level &&
        run->ready_for_interrupt_injection &&
        (env->interrupt_request & CPU_INTERRUPT_HARD) &&
        (env->irq_input_state & (1<<PPC_INPUT_INT)))
    {
        /* For now KVM disregards the 'irq' argument. However, in the
         * future KVM could cache it in-kernel to avoid a heavyweight exit
         * when reading the UIC.
         */
        irq = KVM_INTERRUPT_SET;

        dprintf("injected interrupt %d\n", irq);
        r = kvm_vcpu_ioctl(env, KVM_INTERRUPT, &irq);
        if (r < 0)
            printf("cpu %d fail inject %x\n", env->cpu_index, irq);

        /* Always wake up soon in case the interrupt was level based */
        qemu_mod_timer(idle_timer, qemu_get_clock_ns(vm_clock) +
                       (get_ticks_per_sec() / 50));
    }

    /* We don't know if there are more interrupts pending after this. However,
     * the guest will return to userspace in the course of handling this one
     * anyways, so we will get a chance to deliver the rest. */
}

void kvm_arch_post_run(CPUPPCState *env, struct kvm_run *run)
{
}

int kvm_arch_process_async_events(CPUPPCState *env)
{
    return env->halted;
}

static int kvmppc_handle_halt(CPUPPCState *env)
{
    if (!(env->interrupt_request & CPU_INTERRUPT_HARD) && (msr_ee)) {
        env->halted = 1;
        env->exception_index = EXCP_HLT;
    }

    return 0;
}

/* map dcr access to existing qemu dcr emulation */
static int kvmppc_handle_dcr_read(CPUPPCState *env, uint32_t dcrn, uint32_t *data)
{
    if (ppc_dcr_read(env->dcr_env, dcrn, data) < 0)
        fprintf(stderr, "Read to unhandled DCR (0x%x)\n", dcrn);

    return 0;
}

static int kvmppc_handle_dcr_write(CPUPPCState *env, uint32_t dcrn, uint32_t data)
{
    if (ppc_dcr_write(env->dcr_env, dcrn, data) < 0)
        fprintf(stderr, "Write to unhandled DCR (0x%x)\n", dcrn);

    return 0;
}

int kvm_arch_handle_exit(CPUPPCState *env, struct kvm_run *run)
{
    int ret;

    switch (run->exit_reason) {
    case KVM_EXIT_DCR:
        if (run->dcr.is_write) {
            dprintf("handle dcr write\n");
            ret = kvmppc_handle_dcr_write(env, run->dcr.dcrn, run->dcr.data);
        } else {
            dprintf("handle dcr read\n");
            ret = kvmppc_handle_dcr_read(env, run->dcr.dcrn, &run->dcr.data);
        }
        break;
    case KVM_EXIT_HLT:
        dprintf("handle halt\n");
        ret = kvmppc_handle_halt(env);
        break;
#ifdef CONFIG_PSERIES
    case KVM_EXIT_PAPR_HCALL:
        dprintf("handle PAPR hypercall\n");
        run->papr_hcall.ret = spapr_hypercall(env, run->papr_hcall.nr,
                                              run->papr_hcall.args);
        ret = 1;
        break;
#endif
    default:
        fprintf(stderr, "KVM: unknown exit reason %d\n", run->exit_reason);
        ret = -1;
        break;
    }

    return ret;
}

static int read_cpuinfo(const char *field, char *value, int len)
{
    FILE *f;
    int ret = -1;
    int field_len = strlen(field);
    char line[512];

    f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        return -1;
    }

    do {
        if(!fgets(line, sizeof(line), f)) {
            break;
        }
        if (!strncmp(line, field, field_len)) {
            strncpy(value, line, len);
            ret = 0;
            break;
        }
    } while(*line);

    fclose(f);

    return ret;
}

uint32_t kvmppc_get_tbfreq(void)
{
    char line[512];
    char *ns;
    uint32_t retval = get_ticks_per_sec();

    if (read_cpuinfo("timebase", line, sizeof(line))) {
        return retval;
    }

    if (!(ns = strchr(line, ':'))) {
        return retval;
    }

    ns++;

    retval = atoi(ns);
    return retval;
}

/* Try to find a device tree node for a CPU with clock-frequency property */
static int kvmppc_find_cpu_dt(char *buf, int buf_len)
{
    struct dirent *dirp;
    DIR *dp;

    if ((dp = opendir(PROC_DEVTREE_CPU)) == NULL) {
        printf("Can't open directory " PROC_DEVTREE_CPU "\n");
        return -1;
    }

    buf[0] = '\0';
    while ((dirp = readdir(dp)) != NULL) {
        FILE *f;
        snprintf(buf, buf_len, "%s%s/clock-frequency", PROC_DEVTREE_CPU,
                 dirp->d_name);
        f = fopen(buf, "r");
        if (f) {
            snprintf(buf, buf_len, "%s%s", PROC_DEVTREE_CPU, dirp->d_name);
            fclose(f);
            break;
        }
        buf[0] = '\0';
    }
    closedir(dp);
    if (buf[0] == '\0') {
        printf("Unknown host!\n");
        return -1;
    }

    return 0;
}

/* Read a CPU node property from the host device tree that's a single
 * integer (32-bit or 64-bit).  Returns 0 if anything goes wrong
 * (can't find or open the property, or doesn't understand the
 * format) */
static uint64_t kvmppc_read_int_cpu_dt(const char *propname)
{
    char buf[PATH_MAX];
    union {
        uint32_t v32;
        uint64_t v64;
    } u;
    FILE *f;
    int len;

    if (kvmppc_find_cpu_dt(buf, sizeof(buf))) {
        return -1;
    }

    strncat(buf, "/", sizeof(buf) - strlen(buf));
    strncat(buf, propname, sizeof(buf) - strlen(buf));

    f = fopen(buf, "rb");
    if (!f) {
        return -1;
    }

    len = fread(&u, 1, sizeof(u), f);
    fclose(f);
    switch (len) {
    case 4:
        /* property is a 32-bit quantity */
        return be32_to_cpu(u.v32);
    case 8:
        return be64_to_cpu(u.v64);
    }

    return 0;
}

uint64_t kvmppc_get_clockfreq(void)
{
    return kvmppc_read_int_cpu_dt("clock-frequency");
}

uint32_t kvmppc_get_vmx(void)
{
    return kvmppc_read_int_cpu_dt("ibm,vmx");
}

uint32_t kvmppc_get_dfp(void)
{
    return kvmppc_read_int_cpu_dt("ibm,dfp");
}

int kvmppc_get_hypercall(CPUPPCState *env, uint8_t *buf, int buf_len)
{
    uint32_t *hc = (uint32_t*)buf;

    struct kvm_ppc_pvinfo pvinfo;

    if (kvm_check_extension(env->kvm_state, KVM_CAP_PPC_GET_PVINFO) &&
        !kvm_vm_ioctl(env->kvm_state, KVM_PPC_GET_PVINFO, &pvinfo)) {
        memcpy(buf, pvinfo.hcall, buf_len);

        return 0;
    }

    /*
     * Fallback to always fail hypercalls:
     *
     *     li r3, -1
     *     nop
     *     nop
     *     nop
     */

    hc[0] = 0x3860ffff;
    hc[1] = 0x60000000;
    hc[2] = 0x60000000;
    hc[3] = 0x60000000;

    return 0;
}

void kvmppc_set_papr(CPUPPCState *env)
{
    struct kvm_enable_cap cap = {};
    struct kvm_one_reg reg = {};
    struct kvm_sregs sregs = {};
    int ret;
    uint64_t hior = env->spr[SPR_HIOR];

    cap.cap = KVM_CAP_PPC_PAPR;
    ret = kvm_vcpu_ioctl(env, KVM_ENABLE_CAP, &cap);

    if (ret) {
        goto fail;
    }

    /*
     * XXX We set HIOR here. It really should be a qdev property of
     *     the CPU node, but we don't have CPUs converted to qdev yet.
     *
     *     Once we have qdev CPUs, move HIOR to a qdev property and
     *     remove this chunk.
     */
    reg.id = KVM_REG_PPC_HIOR;
    reg.addr = (uintptr_t)&hior;
    ret = kvm_vcpu_ioctl(env, KVM_SET_ONE_REG, &reg);
    if (ret) {
        fprintf(stderr, "Couldn't set HIOR. Maybe you're running an old \n"
                        "kernel with support for HV KVM but no PAPR PR \n"
                        "KVM in which case things will work. If they don't \n"
                        "please update your host kernel!\n");
    }

    /* Set SDR1 so kernel space finds the HTAB */
    ret = kvm_vcpu_ioctl(env, KVM_GET_SREGS, &sregs);
    if (ret) {
        goto fail;
    }

    sregs.u.s.sdr1 = env->spr[SPR_SDR1];

    ret = kvm_vcpu_ioctl(env, KVM_SET_SREGS, &sregs);
    if (ret) {
        goto fail;
    }

    return;

fail:
    cpu_abort(env, "This KVM version does not support PAPR\n");
}

int kvmppc_smt_threads(void)
{
    return cap_ppc_smt ? cap_ppc_smt : 1;
}

off_t kvmppc_alloc_rma(const char *name, MemoryRegion *sysmem)
{
    void *rma;
    off_t size;
    int fd;
    struct kvm_allocate_rma ret;
    MemoryRegion *rma_region;

    /* If cap_ppc_rma == 0, contiguous RMA allocation is not supported
     * if cap_ppc_rma == 1, contiguous RMA allocation is supported, but
     *                      not necessary on this hardware
     * if cap_ppc_rma == 2, contiguous RMA allocation is needed on this hardware
     *
     * FIXME: We should allow the user to force contiguous RMA
     * allocation in the cap_ppc_rma==1 case.
     */
    if (cap_ppc_rma < 2) {
        return 0;
    }

    fd = kvm_vm_ioctl(kvm_state, KVM_ALLOCATE_RMA, &ret);
    if (fd < 0) {
        fprintf(stderr, "KVM: Error on KVM_ALLOCATE_RMA: %s\n",
                strerror(errno));
        return -1;
    }

    size = MIN(ret.rma_size, 256ul << 20);

    rma = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (rma == MAP_FAILED) {
        fprintf(stderr, "KVM: Error mapping RMA: %s\n", strerror(errno));
        return -1;
    };

    rma_region = g_new(MemoryRegion, 1);
    memory_region_init_ram_ptr(rma_region, name, size, rma);
    vmstate_register_ram_global(rma_region);
    memory_region_add_subregion(sysmem, 0, rma_region);

    return size;
}

void *kvmppc_create_spapr_tce(uint32_t liobn, uint32_t window_size, int *pfd)
{
    struct kvm_create_spapr_tce args = {
        .liobn = liobn,
        .window_size = window_size,
    };
    long len;
    int fd;
    void *table;

    /* Must set fd to -1 so we don't try to munmap when called for
     * destroying the table, which the upper layers -will- do
     */
    *pfd = -1;
    if (!cap_spapr_tce) {
        return NULL;
    }

    fd = kvm_vm_ioctl(kvm_state, KVM_CREATE_SPAPR_TCE, &args);
    if (fd < 0) {
        fprintf(stderr, "KVM: Failed to create TCE table for liobn 0x%x\n",
                liobn);
        return NULL;
    }

    len = (window_size / SPAPR_VIO_TCE_PAGE_SIZE) * sizeof(VIOsPAPR_RTCE);
    /* FIXME: round this up to page size */

    table = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (table == MAP_FAILED) {
        fprintf(stderr, "KVM: Failed to map TCE table for liobn 0x%x\n",
                liobn);
        close(fd);
        return NULL;
    }

    *pfd = fd;
    return table;
}

int kvmppc_remove_spapr_tce(void *table, int fd, uint32_t window_size)
{
    long len;

    if (fd < 0) {
        return -1;
    }

    len = (window_size / SPAPR_VIO_TCE_PAGE_SIZE)*sizeof(VIOsPAPR_RTCE);
    if ((munmap(table, len) < 0) ||
        (close(fd) < 0)) {
        fprintf(stderr, "KVM: Unexpected error removing TCE table: %s",
                strerror(errno));
        /* Leak the table */
    }

    return 0;
}

static inline uint32_t mfpvr(void)
{
    uint32_t pvr;

    asm ("mfpvr %0"
         : "=r"(pvr));
    return pvr;
}

static void alter_insns(uint64_t *word, uint64_t flags, bool on)
{
    if (on) {
        *word |= flags;
    } else {
        *word &= ~flags;
    }
}

const ppc_def_t *kvmppc_host_cpu_def(void)
{
    uint32_t host_pvr = mfpvr();
    const ppc_def_t *base_spec;
    ppc_def_t *spec;
    uint32_t vmx = kvmppc_get_vmx();
    uint32_t dfp = kvmppc_get_dfp();

    base_spec = ppc_find_by_pvr(host_pvr);

    spec = g_malloc0(sizeof(*spec));
    memcpy(spec, base_spec, sizeof(*spec));

    /* Now fix up the spec with information we can query from the host */

    if (vmx != -1) {
        /* Only override when we know what the host supports */
        alter_insns(&spec->insns_flags, PPC_ALTIVEC, vmx > 0);
        alter_insns(&spec->insns_flags2, PPC2_VSX, vmx > 1);
    }
    if (dfp != -1) {
        /* Only override when we know what the host supports */
        alter_insns(&spec->insns_flags2, PPC2_DFP, dfp);
    }

    return spec;
}

bool kvm_arch_stop_on_emulation_error(CPUPPCState *env)
{
    return true;
}

int kvm_arch_on_sigbus_vcpu(CPUPPCState *env, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}
