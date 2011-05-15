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

//#define DEBUG_KVM

#ifdef DEBUG_KVM
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int cap_interrupt_unset = false;
static int cap_interrupt_level = false;
static int cap_segstate;
#ifdef KVM_CAP_PPC_BOOKE_SREGS
static int cap_booke_sregs;
#endif

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
#ifdef KVM_CAP_PPC_UNSET_IRQ
    cap_interrupt_unset = kvm_check_extension(s, KVM_CAP_PPC_UNSET_IRQ);
#endif
#ifdef KVM_CAP_PPC_IRQ_LEVEL
    cap_interrupt_level = kvm_check_extension(s, KVM_CAP_PPC_IRQ_LEVEL);
#endif
#ifdef KVM_CAP_PPC_SEGSTATE
    cap_segstate = kvm_check_extension(s, KVM_CAP_PPC_SEGSTATE);
#endif
#ifdef KVM_CAP_PPC_BOOKE_SREGS
    cap_booke_sregs = kvm_check_extension(s, KVM_CAP_PPC_BOOKE_SREGS);
#endif

    if (!cap_interrupt_level) {
        fprintf(stderr, "KVM: Couldn't find level irq capability. Expect the "
                        "VM to stall at times!\n");
    }

    return 0;
}

static int kvm_arch_sync_sregs(CPUState *cenv)
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

#if !defined(CONFIG_KVM_PPC_PVR)
    if (1) {
        fprintf(stderr, "kvm error: missing PVR setting capability\n");
        return -ENOSYS;
    }
#endif

    ret = kvm_vcpu_ioctl(cenv, KVM_GET_SREGS, &sregs);
    if (ret) {
        return ret;
    }

#ifdef CONFIG_KVM_PPC_PVR
    sregs.pvr = cenv->spr[SPR_PVR];
#endif
    return kvm_vcpu_ioctl(cenv, KVM_SET_SREGS, &sregs);
}

int kvm_arch_init_vcpu(CPUState *cenv)
{
    int ret;

    ret = kvm_arch_sync_sregs(cenv);
    if (ret) {
        return ret;
    }

    idle_timer = qemu_new_timer_ns(vm_clock, kvm_kick_env, cenv);

    return ret;
}

void kvm_arch_reset_vcpu(CPUState *env)
{
}

int kvm_arch_put_registers(CPUState *env, int level)
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

    return ret;
}

int kvm_arch_get_registers(CPUState *env)
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

#ifdef KVM_CAP_PPC_BOOKE_SREGS
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
#endif

#ifdef KVM_CAP_PPC_SEGSTATE
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
#endif

    return 0;
}

int kvmppc_set_interrupt(CPUState *env, int irq, int level)
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

void kvm_arch_pre_run(CPUState *env, struct kvm_run *run)
{
    int r;
    unsigned irq;

    /* PowerPC Qemu tracks the various core input pins (interrupt, critical
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

void kvm_arch_post_run(CPUState *env, struct kvm_run *run)
{
}

int kvm_arch_process_async_events(CPUState *env)
{
    return 0;
}

static int kvmppc_handle_halt(CPUState *env)
{
    if (!(env->interrupt_request & CPU_INTERRUPT_HARD) && (msr_ee)) {
        env->halted = 1;
        env->exception_index = EXCP_HLT;
    }

    return 0;
}

/* map dcr access to existing qemu dcr emulation */
static int kvmppc_handle_dcr_read(CPUState *env, uint32_t dcrn, uint32_t *data)
{
    if (ppc_dcr_read(env->dcr_env, dcrn, data) < 0)
        fprintf(stderr, "Read to unhandled DCR (0x%x)\n", dcrn);

    return 0;
}

static int kvmppc_handle_dcr_write(CPUState *env, uint32_t dcrn, uint32_t data)
{
    if (ppc_dcr_write(env->dcr_env, dcrn, data) < 0)
        fprintf(stderr, "Write to unhandled DCR (0x%x)\n", dcrn);

    return 0;
}

int kvm_arch_handle_exit(CPUState *env, struct kvm_run *run)
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

int kvmppc_get_hypercall(CPUState *env, uint8_t *buf, int buf_len)
{
    uint32_t *hc = (uint32_t*)buf;

#ifdef KVM_CAP_PPC_GET_PVINFO
    struct kvm_ppc_pvinfo pvinfo;

    if (kvm_check_extension(env->kvm_state, KVM_CAP_PPC_GET_PVINFO) &&
        !kvm_vm_ioctl(env->kvm_state, KVM_PPC_GET_PVINFO, &pvinfo)) {
        memcpy(buf, pvinfo.hcall, buf_len);

        return 0;
    }
#endif

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

bool kvm_arch_stop_on_emulation_error(CPUState *env)
{
    return true;
}

int kvm_arch_on_sigbus_vcpu(CPUState *env, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}
