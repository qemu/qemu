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

#include "qemu/osdep.h"
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>

#include <linux/kvm.h>

#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "cpu-models.h"
#include "qemu/timer.h"
#include "sysemu/hw_accel.h"
#include "kvm_ppc.h"
#include "sysemu/cpus.h"
#include "sysemu/device_tree.h"
#include "mmu-hash64.h"

#include "hw/sysbus.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/hw.h"
#include "hw/ppc/ppc.h"
#include "migration/qemu-file-types.h"
#include "sysemu/watchdog.h"
#include "trace.h"
#include "exec/gdbstub.h"
#include "exec/memattrs.h"
#include "exec/ram_addr.h"
#include "sysemu/hostmem.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/mmap-alloc.h"
#include "elf.h"
#include "sysemu/kvm_int.h"

#define PROC_DEVTREE_CPU      "/proc/device-tree/cpus/"

#define DEBUG_RETURN_GUEST 0
#define DEBUG_RETURN_GDB   1

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int cap_interrupt_unset;
static int cap_segstate;
static int cap_booke_sregs;
static int cap_ppc_smt;
static int cap_ppc_smt_possible;
static int cap_spapr_tce;
static int cap_spapr_tce_64;
static int cap_spapr_multitce;
static int cap_spapr_vfio;
static int cap_hior;
static int cap_one_reg;
static int cap_epr;
static int cap_ppc_watchdog;
static int cap_papr;
static int cap_htab_fd;
static int cap_fixup_hcalls;
static int cap_htm;             /* Hardware transactional memory support */
static int cap_mmu_radix;
static int cap_mmu_hash_v3;
static int cap_xive;
static int cap_resize_hpt;
static int cap_ppc_pvr_compat;
static int cap_ppc_safe_cache;
static int cap_ppc_safe_bounds_check;
static int cap_ppc_safe_indirect_branch;
static int cap_ppc_count_cache_flush_assist;
static int cap_ppc_nested_kvm_hv;
static int cap_large_decr;
static int cap_fwnmi;

static uint32_t debug_inst_opcode;

/*
 * Check whether we are running with KVM-PR (instead of KVM-HV).  This
 * should only be used for fallback tests - generally we should use
 * explicit capabilities for the features we want, rather than
 * assuming what is/isn't available depending on the KVM variant.
 */
static bool kvmppc_is_pr(KVMState *ks)
{
    /* Assume KVM-PR if the GET_PVINFO capability is available */
    return kvm_vm_check_extension(ks, KVM_CAP_PPC_GET_PVINFO) != 0;
}

static int kvm_ppc_register_host_cpu_type(void);
static void kvmppc_get_cpu_characteristics(KVMState *s);
static int kvmppc_get_dec_bits(void);

int kvm_arch_init(MachineState *ms, KVMState *s)
{
    cap_interrupt_unset = kvm_check_extension(s, KVM_CAP_PPC_UNSET_IRQ);
    cap_segstate = kvm_check_extension(s, KVM_CAP_PPC_SEGSTATE);
    cap_booke_sregs = kvm_check_extension(s, KVM_CAP_PPC_BOOKE_SREGS);
    cap_ppc_smt_possible = kvm_vm_check_extension(s, KVM_CAP_PPC_SMT_POSSIBLE);
    cap_spapr_tce = kvm_check_extension(s, KVM_CAP_SPAPR_TCE);
    cap_spapr_tce_64 = kvm_check_extension(s, KVM_CAP_SPAPR_TCE_64);
    cap_spapr_multitce = kvm_check_extension(s, KVM_CAP_SPAPR_MULTITCE);
    cap_spapr_vfio = kvm_vm_check_extension(s, KVM_CAP_SPAPR_TCE_VFIO);
    cap_one_reg = kvm_check_extension(s, KVM_CAP_ONE_REG);
    cap_hior = kvm_check_extension(s, KVM_CAP_PPC_HIOR);
    cap_epr = kvm_check_extension(s, KVM_CAP_PPC_EPR);
    cap_ppc_watchdog = kvm_check_extension(s, KVM_CAP_PPC_BOOKE_WATCHDOG);
    /*
     * Note: we don't set cap_papr here, because this capability is
     * only activated after this by kvmppc_set_papr()
     */
    cap_htab_fd = kvm_vm_check_extension(s, KVM_CAP_PPC_HTAB_FD);
    cap_fixup_hcalls = kvm_check_extension(s, KVM_CAP_PPC_FIXUP_HCALL);
    cap_ppc_smt = kvm_vm_check_extension(s, KVM_CAP_PPC_SMT);
    cap_htm = kvm_vm_check_extension(s, KVM_CAP_PPC_HTM);
    cap_mmu_radix = kvm_vm_check_extension(s, KVM_CAP_PPC_MMU_RADIX);
    cap_mmu_hash_v3 = kvm_vm_check_extension(s, KVM_CAP_PPC_MMU_HASH_V3);
    cap_xive = kvm_vm_check_extension(s, KVM_CAP_PPC_IRQ_XIVE);
    cap_resize_hpt = kvm_vm_check_extension(s, KVM_CAP_SPAPR_RESIZE_HPT);
    kvmppc_get_cpu_characteristics(s);
    cap_ppc_nested_kvm_hv = kvm_vm_check_extension(s, KVM_CAP_PPC_NESTED_HV);
    cap_large_decr = kvmppc_get_dec_bits();
    cap_fwnmi = kvm_vm_check_extension(s, KVM_CAP_PPC_FWNMI);
    /*
     * Note: setting it to false because there is not such capability
     * in KVM at this moment.
     *
     * TODO: call kvm_vm_check_extension() with the right capability
     * after the kernel starts implementing it.
     */
    cap_ppc_pvr_compat = false;

    if (!kvm_check_extension(s, KVM_CAP_PPC_IRQ_LEVEL)) {
        error_report("KVM: Host kernel doesn't have level irq capability");
        exit(1);
    }

    kvm_ppc_register_host_cpu_type();

    return 0;
}

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}

static int kvm_arch_sync_sregs(PowerPCCPU *cpu)
{
    CPUPPCState *cenv = &cpu->env;
    CPUState *cs = CPU(cpu);
    struct kvm_sregs sregs;
    int ret;

    if (cenv->excp_model == POWERPC_EXCP_BOOKE) {
        /*
         * What we're really trying to say is "if we're on BookE, we
         * use the native PVR for now". This is the only sane way to
         * check it though, so we potentially confuse users that they
         * can run BookE guests on BookS. Let's hope nobody dares
         * enough :)
         */
        return 0;
    } else {
        if (!cap_segstate) {
            fprintf(stderr, "kvm error: missing PVR setting capability\n");
            return -ENOSYS;
        }
    }

    ret = kvm_vcpu_ioctl(cs, KVM_GET_SREGS, &sregs);
    if (ret) {
        return ret;
    }

    sregs.pvr = cenv->spr[SPR_PVR];
    return kvm_vcpu_ioctl(cs, KVM_SET_SREGS, &sregs);
}

/* Set up a shared TLB array with KVM */
static int kvm_booke206_tlb_init(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    struct kvm_book3e_206_tlb_params params = {};
    struct kvm_config_tlb cfg = {};
    unsigned int entries = 0;
    int ret, i;

    if (!kvm_enabled() ||
        !kvm_check_extension(cs->kvm_state, KVM_CAP_SW_TLB)) {
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

    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_SW_TLB, 0, (uintptr_t)&cfg);
    if (ret < 0) {
        fprintf(stderr, "%s: couldn't enable KVM_CAP_SW_TLB: %s\n",
                __func__, strerror(-ret));
        return ret;
    }

    env->kvm_sw_tlb = true;
    return 0;
}


#if defined(TARGET_PPC64)
static void kvm_get_smmu_info(struct kvm_ppc_smmu_info *info, Error **errp)
{
    int ret;

    assert(kvm_state != NULL);

    if (!kvm_check_extension(kvm_state, KVM_CAP_PPC_GET_SMMU_INFO)) {
        error_setg(errp, "KVM doesn't expose the MMU features it supports");
        error_append_hint(errp, "Consider switching to a newer KVM\n");
        return;
    }

    ret = kvm_vm_ioctl(kvm_state, KVM_PPC_GET_SMMU_INFO, info);
    if (ret == 0) {
        return;
    }

    error_setg_errno(errp, -ret,
                     "KVM failed to provide the MMU features it supports");
}

struct ppc_radix_page_info *kvm_get_radix_page_info(void)
{
    KVMState *s = KVM_STATE(current_accel());
    struct ppc_radix_page_info *radix_page_info;
    struct kvm_ppc_rmmu_info rmmu_info;
    int i;

    if (!kvm_check_extension(s, KVM_CAP_PPC_MMU_RADIX)) {
        return NULL;
    }
    if (kvm_vm_ioctl(s, KVM_PPC_GET_RMMU_INFO, &rmmu_info)) {
        return NULL;
    }
    radix_page_info = g_malloc0(sizeof(*radix_page_info));
    radix_page_info->count = 0;
    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        if (rmmu_info.ap_encodings[i]) {
            radix_page_info->entries[i] = rmmu_info.ap_encodings[i];
            radix_page_info->count++;
        }
    }
    return radix_page_info;
}

target_ulong kvmppc_configure_v3_mmu(PowerPCCPU *cpu,
                                     bool radix, bool gtse,
                                     uint64_t proc_tbl)
{
    CPUState *cs = CPU(cpu);
    int ret;
    uint64_t flags = 0;
    struct kvm_ppc_mmuv3_cfg cfg = {
        .process_table = proc_tbl,
    };

    if (radix) {
        flags |= KVM_PPC_MMUV3_RADIX;
    }
    if (gtse) {
        flags |= KVM_PPC_MMUV3_GTSE;
    }
    cfg.flags = flags;
    ret = kvm_vm_ioctl(cs->kvm_state, KVM_PPC_CONFIGURE_V3_MMU, &cfg);
    switch (ret) {
    case 0:
        return H_SUCCESS;
    case -EINVAL:
        return H_PARAMETER;
    case -ENODEV:
        return H_NOT_AVAILABLE;
    default:
        return H_HARDWARE;
    }
}

bool kvmppc_hpt_needs_host_contiguous_pages(void)
{
    static struct kvm_ppc_smmu_info smmu_info;

    if (!kvm_enabled()) {
        return false;
    }

    kvm_get_smmu_info(&smmu_info, &error_fatal);
    return !!(smmu_info.flags & KVM_PPC_PAGE_SIZES_REAL);
}

void kvm_check_mmu(PowerPCCPU *cpu, Error **errp)
{
    struct kvm_ppc_smmu_info smmu_info;
    int iq, ik, jq, jk;
    Error *local_err = NULL;

    /* For now, we only have anything to check on hash64 MMUs */
    if (!cpu->hash64_opts || !kvm_enabled()) {
        return;
    }

    kvm_get_smmu_info(&smmu_info, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (ppc_hash64_has(cpu, PPC_HASH64_1TSEG)
        && !(smmu_info.flags & KVM_PPC_1T_SEGMENTS)) {
        error_setg(errp,
                   "KVM does not support 1TiB segments which guest expects");
        return;
    }

    if (smmu_info.slb_size < cpu->hash64_opts->slb_size) {
        error_setg(errp, "KVM only supports %u SLB entries, but guest needs %u",
                   smmu_info.slb_size, cpu->hash64_opts->slb_size);
        return;
    }

    /*
     * Verify that every pagesize supported by the cpu model is
     * supported by KVM with the same encodings
     */
    for (iq = 0; iq < ARRAY_SIZE(cpu->hash64_opts->sps); iq++) {
        PPCHash64SegmentPageSizes *qsps = &cpu->hash64_opts->sps[iq];
        struct kvm_ppc_one_seg_page_size *ksps;

        for (ik = 0; ik < ARRAY_SIZE(smmu_info.sps); ik++) {
            if (qsps->page_shift == smmu_info.sps[ik].page_shift) {
                break;
            }
        }
        if (ik >= ARRAY_SIZE(smmu_info.sps)) {
            error_setg(errp, "KVM doesn't support for base page shift %u",
                       qsps->page_shift);
            return;
        }

        ksps = &smmu_info.sps[ik];
        if (ksps->slb_enc != qsps->slb_enc) {
            error_setg(errp,
"KVM uses SLB encoding 0x%x for page shift %u, but guest expects 0x%x",
                       ksps->slb_enc, ksps->page_shift, qsps->slb_enc);
            return;
        }

        for (jq = 0; jq < ARRAY_SIZE(qsps->enc); jq++) {
            for (jk = 0; jk < ARRAY_SIZE(ksps->enc); jk++) {
                if (qsps->enc[jq].page_shift == ksps->enc[jk].page_shift) {
                    break;
                }
            }

            if (jk >= ARRAY_SIZE(ksps->enc)) {
                error_setg(errp, "KVM doesn't support page shift %u/%u",
                           qsps->enc[jq].page_shift, qsps->page_shift);
                return;
            }
            if (qsps->enc[jq].pte_enc != ksps->enc[jk].pte_enc) {
                error_setg(errp,
"KVM uses PTE encoding 0x%x for page shift %u/%u, but guest expects 0x%x",
                           ksps->enc[jk].pte_enc, qsps->enc[jq].page_shift,
                           qsps->page_shift, qsps->enc[jq].pte_enc);
                return;
            }
        }
    }

    if (ppc_hash64_has(cpu, PPC_HASH64_CI_LARGEPAGE)) {
        /*
         * Mostly what guest pagesizes we can use are related to the
         * host pages used to map guest RAM, which is handled in the
         * platform code. Cache-Inhibited largepages (64k) however are
         * used for I/O, so if they're mapped to the host at all it
         * will be a normal mapping, not a special hugepage one used
         * for RAM.
         */
        if (qemu_real_host_page_size < 0x10000) {
            error_setg(errp,
                       "KVM can't supply 64kiB CI pages, which guest expects");
        }
    }
}
#endif /* !defined (TARGET_PPC64) */

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return POWERPC_CPU(cpu)->vcpu_id;
}

/*
 * e500 supports 2 h/w breakpoint and 2 watchpoint.  book3s supports
 * only 1 watchpoint, so array size of 4 is sufficient for now.
 */
#define MAX_HW_BKPTS 4

static struct HWBreakpoint {
    target_ulong addr;
    int type;
} hw_debug_points[MAX_HW_BKPTS];

static CPUWatchpoint hw_watchpoint;

/* Default there is no breakpoint and watchpoint supported */
static int max_hw_breakpoint;
static int max_hw_watchpoint;
static int nb_hw_breakpoint;
static int nb_hw_watchpoint;

static void kvmppc_hw_debug_points_init(CPUPPCState *cenv)
{
    if (cenv->excp_model == POWERPC_EXCP_BOOKE) {
        max_hw_breakpoint = 2;
        max_hw_watchpoint = 2;
    }

    if ((max_hw_breakpoint + max_hw_watchpoint) > MAX_HW_BKPTS) {
        fprintf(stderr, "Error initializing h/w breakpoints\n");
        return;
    }
}

int kvm_arch_init_vcpu(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *cenv = &cpu->env;
    int ret;

    /* Synchronize sregs with kvm */
    ret = kvm_arch_sync_sregs(cpu);
    if (ret) {
        if (ret == -EINVAL) {
            error_report("Register sync failed... If you're using kvm-hv.ko,"
                         " only \"-cpu host\" is possible");
        }
        return ret;
    }

    switch (cenv->mmu_model) {
    case POWERPC_MMU_BOOKE206:
        /* This target supports access to KVM's guest TLB */
        ret = kvm_booke206_tlb_init(cpu);
        break;
    case POWERPC_MMU_2_07:
        if (!cap_htm && !kvmppc_is_pr(cs->kvm_state)) {
            /*
             * KVM-HV has transactional memory on POWER8 also without
             * the KVM_CAP_PPC_HTM extension, so enable it here
             * instead as long as it's available to userspace on the
             * host.
             */
            if (qemu_getauxval(AT_HWCAP2) & PPC_FEATURE2_HAS_HTM) {
                cap_htm = true;
            }
        }
        break;
    default:
        break;
    }

    kvm_get_one_reg(cs, KVM_REG_PPC_DEBUG_INST, &debug_inst_opcode);
    kvmppc_hw_debug_points_init(cenv);

    return ret;
}

int kvm_arch_destroy_vcpu(CPUState *cs)
{
    return 0;
}

static void kvm_sw_tlb_put(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
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

    ret = kvm_vcpu_ioctl(cs, KVM_DIRTY_TLB, &dirty_tlb);
    if (ret) {
        fprintf(stderr, "%s: KVM_DIRTY_TLB: %s\n",
                __func__, strerror(-ret));
    }

    g_free(bitmap);
}

static void kvm_get_one_spr(CPUState *cs, uint64_t id, int spr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    union {
        uint32_t u32;
        uint64_t u64;
    } val;
    struct kvm_one_reg reg = {
        .id = id,
        .addr = (uintptr_t) &val,
    };
    int ret;

    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret != 0) {
        trace_kvm_failed_spr_get(spr, strerror(errno));
    } else {
        switch (id & KVM_REG_SIZE_MASK) {
        case KVM_REG_SIZE_U32:
            env->spr[spr] = val.u32;
            break;

        case KVM_REG_SIZE_U64:
            env->spr[spr] = val.u64;
            break;

        default:
            /* Don't handle this size yet */
            abort();
        }
    }
}

static void kvm_put_one_spr(CPUState *cs, uint64_t id, int spr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    union {
        uint32_t u32;
        uint64_t u64;
    } val;
    struct kvm_one_reg reg = {
        .id = id,
        .addr = (uintptr_t) &val,
    };
    int ret;

    switch (id & KVM_REG_SIZE_MASK) {
    case KVM_REG_SIZE_U32:
        val.u32 = env->spr[spr];
        break;

    case KVM_REG_SIZE_U64:
        val.u64 = env->spr[spr];
        break;

    default:
        /* Don't handle this size yet */
        abort();
    }

    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret != 0) {
        trace_kvm_failed_spr_set(spr, strerror(errno));
    }
}

static int kvm_put_fp(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    struct kvm_one_reg reg;
    int i;
    int ret;

    if (env->insns_flags & PPC_FLOAT) {
        uint64_t fpscr = env->fpscr;
        bool vsx = !!(env->insns_flags2 & PPC2_VSX);

        reg.id = KVM_REG_PPC_FPSCR;
        reg.addr = (uintptr_t)&fpscr;
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret < 0) {
            trace_kvm_failed_fpscr_set(strerror(errno));
            return ret;
        }

        for (i = 0; i < 32; i++) {
            uint64_t vsr[2];
            uint64_t *fpr = cpu_fpr_ptr(&cpu->env, i);
            uint64_t *vsrl = cpu_vsrl_ptr(&cpu->env, i);

#ifdef HOST_WORDS_BIGENDIAN
            vsr[0] = float64_val(*fpr);
            vsr[1] = *vsrl;
#else
            vsr[0] = *vsrl;
            vsr[1] = float64_val(*fpr);
#endif
            reg.addr = (uintptr_t) &vsr;
            reg.id = vsx ? KVM_REG_PPC_VSR(i) : KVM_REG_PPC_FPR(i);

            ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
            if (ret < 0) {
                trace_kvm_failed_fp_set(vsx ? "VSR" : "FPR", i,
                                        strerror(errno));
                return ret;
            }
        }
    }

    if (env->insns_flags & PPC_ALTIVEC) {
        reg.id = KVM_REG_PPC_VSCR;
        reg.addr = (uintptr_t)&env->vscr;
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret < 0) {
            trace_kvm_failed_vscr_set(strerror(errno));
            return ret;
        }

        for (i = 0; i < 32; i++) {
            reg.id = KVM_REG_PPC_VR(i);
            reg.addr = (uintptr_t)cpu_avr_ptr(env, i);
            ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
            if (ret < 0) {
                trace_kvm_failed_vr_set(i, strerror(errno));
                return ret;
            }
        }
    }

    return 0;
}

static int kvm_get_fp(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    struct kvm_one_reg reg;
    int i;
    int ret;

    if (env->insns_flags & PPC_FLOAT) {
        uint64_t fpscr;
        bool vsx = !!(env->insns_flags2 & PPC2_VSX);

        reg.id = KVM_REG_PPC_FPSCR;
        reg.addr = (uintptr_t)&fpscr;
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret < 0) {
            trace_kvm_failed_fpscr_get(strerror(errno));
            return ret;
        } else {
            env->fpscr = fpscr;
        }

        for (i = 0; i < 32; i++) {
            uint64_t vsr[2];
            uint64_t *fpr = cpu_fpr_ptr(&cpu->env, i);
            uint64_t *vsrl = cpu_vsrl_ptr(&cpu->env, i);

            reg.addr = (uintptr_t) &vsr;
            reg.id = vsx ? KVM_REG_PPC_VSR(i) : KVM_REG_PPC_FPR(i);

            ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
            if (ret < 0) {
                trace_kvm_failed_fp_get(vsx ? "VSR" : "FPR", i,
                                        strerror(errno));
                return ret;
            } else {
#ifdef HOST_WORDS_BIGENDIAN
                *fpr = vsr[0];
                if (vsx) {
                    *vsrl = vsr[1];
                }
#else
                *fpr = vsr[1];
                if (vsx) {
                    *vsrl = vsr[0];
                }
#endif
            }
        }
    }

    if (env->insns_flags & PPC_ALTIVEC) {
        reg.id = KVM_REG_PPC_VSCR;
        reg.addr = (uintptr_t)&env->vscr;
        ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
        if (ret < 0) {
            trace_kvm_failed_vscr_get(strerror(errno));
            return ret;
        }

        for (i = 0; i < 32; i++) {
            reg.id = KVM_REG_PPC_VR(i);
            reg.addr = (uintptr_t)cpu_avr_ptr(env, i);
            ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
            if (ret < 0) {
                trace_kvm_failed_vr_get(i, strerror(errno));
                return ret;
            }
        }
    }

    return 0;
}

#if defined(TARGET_PPC64)
static int kvm_get_vpa(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    struct kvm_one_reg reg;
    int ret;

    reg.id = KVM_REG_PPC_VPA_ADDR;
    reg.addr = (uintptr_t)&spapr_cpu->vpa_addr;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret < 0) {
        trace_kvm_failed_vpa_addr_get(strerror(errno));
        return ret;
    }

    assert((uintptr_t)&spapr_cpu->slb_shadow_size
           == ((uintptr_t)&spapr_cpu->slb_shadow_addr + 8));
    reg.id = KVM_REG_PPC_VPA_SLB;
    reg.addr = (uintptr_t)&spapr_cpu->slb_shadow_addr;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret < 0) {
        trace_kvm_failed_slb_get(strerror(errno));
        return ret;
    }

    assert((uintptr_t)&spapr_cpu->dtl_size
           == ((uintptr_t)&spapr_cpu->dtl_addr + 8));
    reg.id = KVM_REG_PPC_VPA_DTL;
    reg.addr = (uintptr_t)&spapr_cpu->dtl_addr;
    ret = kvm_vcpu_ioctl(cs, KVM_GET_ONE_REG, &reg);
    if (ret < 0) {
        trace_kvm_failed_dtl_get(strerror(errno));
        return ret;
    }

    return 0;
}

static int kvm_put_vpa(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    struct kvm_one_reg reg;
    int ret;

    /*
     * SLB shadow or DTL can't be registered unless a master VPA is
     * registered.  That means when restoring state, if a VPA *is*
     * registered, we need to set that up first.  If not, we need to
     * deregister the others before deregistering the master VPA
     */
    assert(spapr_cpu->vpa_addr
           || !(spapr_cpu->slb_shadow_addr || spapr_cpu->dtl_addr));

    if (spapr_cpu->vpa_addr) {
        reg.id = KVM_REG_PPC_VPA_ADDR;
        reg.addr = (uintptr_t)&spapr_cpu->vpa_addr;
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret < 0) {
            trace_kvm_failed_vpa_addr_set(strerror(errno));
            return ret;
        }
    }

    assert((uintptr_t)&spapr_cpu->slb_shadow_size
           == ((uintptr_t)&spapr_cpu->slb_shadow_addr + 8));
    reg.id = KVM_REG_PPC_VPA_SLB;
    reg.addr = (uintptr_t)&spapr_cpu->slb_shadow_addr;
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret < 0) {
        trace_kvm_failed_slb_set(strerror(errno));
        return ret;
    }

    assert((uintptr_t)&spapr_cpu->dtl_size
           == ((uintptr_t)&spapr_cpu->dtl_addr + 8));
    reg.id = KVM_REG_PPC_VPA_DTL;
    reg.addr = (uintptr_t)&spapr_cpu->dtl_addr;
    ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
    if (ret < 0) {
        trace_kvm_failed_dtl_set(strerror(errno));
        return ret;
    }

    if (!spapr_cpu->vpa_addr) {
        reg.id = KVM_REG_PPC_VPA_ADDR;
        reg.addr = (uintptr_t)&spapr_cpu->vpa_addr;
        ret = kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
        if (ret < 0) {
            trace_kvm_failed_null_vpa_addr_set(strerror(errno));
            return ret;
        }
    }

    return 0;
}
#endif /* TARGET_PPC64 */

int kvmppc_put_books_sregs(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    struct kvm_sregs sregs;
    int i;

    sregs.pvr = env->spr[SPR_PVR];

    if (cpu->vhyp) {
        PPCVirtualHypervisorClass *vhc =
            PPC_VIRTUAL_HYPERVISOR_GET_CLASS(cpu->vhyp);
        sregs.u.s.sdr1 = vhc->encode_hpt_for_kvm_pr(cpu->vhyp);
    } else {
        sregs.u.s.sdr1 = env->spr[SPR_SDR1];
    }

    /* Sync SLB */
#ifdef TARGET_PPC64
    for (i = 0; i < ARRAY_SIZE(env->slb); i++) {
        sregs.u.s.ppc64.slb[i].slbe = env->slb[i].esid;
        if (env->slb[i].esid & SLB_ESID_V) {
            sregs.u.s.ppc64.slb[i].slbe |= i;
        }
        sregs.u.s.ppc64.slb[i].slbv = env->slb[i].vsid;
    }
#endif

    /* Sync SRs */
    for (i = 0; i < 16; i++) {
        sregs.u.s.ppc32.sr[i] = env->sr[i];
    }

    /* Sync BATs */
    for (i = 0; i < 8; i++) {
        /* Beware. We have to swap upper and lower bits here */
        sregs.u.s.ppc32.dbat[i] = ((uint64_t)env->DBAT[0][i] << 32)
            | env->DBAT[1][i];
        sregs.u.s.ppc32.ibat[i] = ((uint64_t)env->IBAT[0][i] << 32)
            | env->IBAT[1][i];
    }

    return kvm_vcpu_ioctl(CPU(cpu), KVM_SET_SREGS, &sregs);
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    struct kvm_regs regs;
    int ret;
    int i;

    ret = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);
    if (ret < 0) {
        return ret;
    }

    regs.ctr = env->ctr;
    regs.lr  = env->lr;
    regs.xer = cpu_read_xer(env);
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

    for (i = 0; i < 32; i++) {
        regs.gpr[i] = env->gpr[i];
    }

    regs.cr = 0;
    for (i = 0; i < 8; i++) {
        regs.cr |= (env->crf[i] & 15) << (4 * (7 - i));
    }

    ret = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);
    if (ret < 0) {
        return ret;
    }

    kvm_put_fp(cs);

    if (env->tlb_dirty) {
        kvm_sw_tlb_put(cpu);
        env->tlb_dirty = false;
    }

    if (cap_segstate && (level >= KVM_PUT_RESET_STATE)) {
        ret = kvmppc_put_books_sregs(cpu);
        if (ret < 0) {
            return ret;
        }
    }

    if (cap_hior && (level >= KVM_PUT_RESET_STATE)) {
        kvm_put_one_spr(cs, KVM_REG_PPC_HIOR, SPR_HIOR);
    }

    if (cap_one_reg) {
        int i;

        /*
         * We deliberately ignore errors here, for kernels which have
         * the ONE_REG calls, but don't support the specific
         * registers, there's a reasonable chance things will still
         * work, at least until we try to migrate.
         */
        for (i = 0; i < 1024; i++) {
            uint64_t id = env->spr_cb[i].one_reg_id;

            if (id != 0) {
                kvm_put_one_spr(cs, id, i);
            }
        }

#ifdef TARGET_PPC64
        if (msr_ts) {
            for (i = 0; i < ARRAY_SIZE(env->tm_gpr); i++) {
                kvm_set_one_reg(cs, KVM_REG_PPC_TM_GPR(i), &env->tm_gpr[i]);
            }
            for (i = 0; i < ARRAY_SIZE(env->tm_vsr); i++) {
                kvm_set_one_reg(cs, KVM_REG_PPC_TM_VSR(i), &env->tm_vsr[i]);
            }
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_CR, &env->tm_cr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_LR, &env->tm_lr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_CTR, &env->tm_ctr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_FPSCR, &env->tm_fpscr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_AMR, &env->tm_amr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_PPR, &env->tm_ppr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_VRSAVE, &env->tm_vrsave);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_VSCR, &env->tm_vscr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_DSCR, &env->tm_dscr);
            kvm_set_one_reg(cs, KVM_REG_PPC_TM_TAR, &env->tm_tar);
        }

        if (cap_papr) {
            if (kvm_put_vpa(cs) < 0) {
                trace_kvm_failed_put_vpa();
            }
        }

        kvm_set_one_reg(cs, KVM_REG_PPC_TB_OFFSET, &env->tb_env->tb_offset);

        if (level > KVM_PUT_RUNTIME_STATE) {
            kvm_put_one_spr(cs, KVM_REG_PPC_DPDES, SPR_DPDES);
        }
#endif /* TARGET_PPC64 */
    }

    return ret;
}

static void kvm_sync_excp(CPUPPCState *env, int vector, int ivor)
{
     env->excp_vectors[vector] = env->spr[ivor] + env->spr[SPR_BOOKE_IVPR];
}

static int kvmppc_get_booke_sregs(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    struct kvm_sregs sregs;
    int ret;

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_SREGS, &sregs);
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
        kvm_sync_excp(env, POWERPC_EXCP_CRITICAL,  SPR_BOOKE_IVOR0);
        env->spr[SPR_BOOKE_IVOR1] = sregs.u.e.ivor_low[1];
        kvm_sync_excp(env, POWERPC_EXCP_MCHECK,  SPR_BOOKE_IVOR1);
        env->spr[SPR_BOOKE_IVOR2] = sregs.u.e.ivor_low[2];
        kvm_sync_excp(env, POWERPC_EXCP_DSI,  SPR_BOOKE_IVOR2);
        env->spr[SPR_BOOKE_IVOR3] = sregs.u.e.ivor_low[3];
        kvm_sync_excp(env, POWERPC_EXCP_ISI,  SPR_BOOKE_IVOR3);
        env->spr[SPR_BOOKE_IVOR4] = sregs.u.e.ivor_low[4];
        kvm_sync_excp(env, POWERPC_EXCP_EXTERNAL,  SPR_BOOKE_IVOR4);
        env->spr[SPR_BOOKE_IVOR5] = sregs.u.e.ivor_low[5];
        kvm_sync_excp(env, POWERPC_EXCP_ALIGN,  SPR_BOOKE_IVOR5);
        env->spr[SPR_BOOKE_IVOR6] = sregs.u.e.ivor_low[6];
        kvm_sync_excp(env, POWERPC_EXCP_PROGRAM,  SPR_BOOKE_IVOR6);
        env->spr[SPR_BOOKE_IVOR7] = sregs.u.e.ivor_low[7];
        kvm_sync_excp(env, POWERPC_EXCP_FPU,  SPR_BOOKE_IVOR7);
        env->spr[SPR_BOOKE_IVOR8] = sregs.u.e.ivor_low[8];
        kvm_sync_excp(env, POWERPC_EXCP_SYSCALL,  SPR_BOOKE_IVOR8);
        env->spr[SPR_BOOKE_IVOR9] = sregs.u.e.ivor_low[9];
        kvm_sync_excp(env, POWERPC_EXCP_APU,  SPR_BOOKE_IVOR9);
        env->spr[SPR_BOOKE_IVOR10] = sregs.u.e.ivor_low[10];
        kvm_sync_excp(env, POWERPC_EXCP_DECR,  SPR_BOOKE_IVOR10);
        env->spr[SPR_BOOKE_IVOR11] = sregs.u.e.ivor_low[11];
        kvm_sync_excp(env, POWERPC_EXCP_FIT,  SPR_BOOKE_IVOR11);
        env->spr[SPR_BOOKE_IVOR12] = sregs.u.e.ivor_low[12];
        kvm_sync_excp(env, POWERPC_EXCP_WDT,  SPR_BOOKE_IVOR12);
        env->spr[SPR_BOOKE_IVOR13] = sregs.u.e.ivor_low[13];
        kvm_sync_excp(env, POWERPC_EXCP_DTLB,  SPR_BOOKE_IVOR13);
        env->spr[SPR_BOOKE_IVOR14] = sregs.u.e.ivor_low[14];
        kvm_sync_excp(env, POWERPC_EXCP_ITLB,  SPR_BOOKE_IVOR14);
        env->spr[SPR_BOOKE_IVOR15] = sregs.u.e.ivor_low[15];
        kvm_sync_excp(env, POWERPC_EXCP_DEBUG,  SPR_BOOKE_IVOR15);

        if (sregs.u.e.features & KVM_SREGS_E_SPE) {
            env->spr[SPR_BOOKE_IVOR32] = sregs.u.e.ivor_high[0];
            kvm_sync_excp(env, POWERPC_EXCP_SPEU,  SPR_BOOKE_IVOR32);
            env->spr[SPR_BOOKE_IVOR33] = sregs.u.e.ivor_high[1];
            kvm_sync_excp(env, POWERPC_EXCP_EFPDI,  SPR_BOOKE_IVOR33);
            env->spr[SPR_BOOKE_IVOR34] = sregs.u.e.ivor_high[2];
            kvm_sync_excp(env, POWERPC_EXCP_EFPRI,  SPR_BOOKE_IVOR34);
        }

        if (sregs.u.e.features & KVM_SREGS_E_PM) {
            env->spr[SPR_BOOKE_IVOR35] = sregs.u.e.ivor_high[3];
            kvm_sync_excp(env, POWERPC_EXCP_EPERFM,  SPR_BOOKE_IVOR35);
        }

        if (sregs.u.e.features & KVM_SREGS_E_PC) {
            env->spr[SPR_BOOKE_IVOR36] = sregs.u.e.ivor_high[4];
            kvm_sync_excp(env, POWERPC_EXCP_DOORI,  SPR_BOOKE_IVOR36);
            env->spr[SPR_BOOKE_IVOR37] = sregs.u.e.ivor_high[5];
            kvm_sync_excp(env, POWERPC_EXCP_DOORCI, SPR_BOOKE_IVOR37);
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

    return 0;
}

static int kvmppc_get_books_sregs(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;
    struct kvm_sregs sregs;
    int ret;
    int i;

    ret = kvm_vcpu_ioctl(CPU(cpu), KVM_GET_SREGS, &sregs);
    if (ret < 0) {
        return ret;
    }

    if (!cpu->vhyp) {
        ppc_store_sdr1(env, sregs.u.s.sdr1);
    }

    /* Sync SLB */
#ifdef TARGET_PPC64
    /*
     * The packed SLB array we get from KVM_GET_SREGS only contains
     * information about valid entries. So we flush our internal copy
     * to get rid of stale ones, then put all valid SLB entries back
     * in.
     */
    memset(env->slb, 0, sizeof(env->slb));
    for (i = 0; i < ARRAY_SIZE(env->slb); i++) {
        target_ulong rb = sregs.u.s.ppc64.slb[i].slbe;
        target_ulong rs = sregs.u.s.ppc64.slb[i].slbv;
        /*
         * Only restore valid entries
         */
        if (rb & SLB_ESID_V) {
            ppc_store_slb(cpu, rb & 0xfff, rb & ~0xfffULL, rs);
        }
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

    return 0;
}

int kvm_arch_get_registers(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    struct kvm_regs regs;
    uint32_t cr;
    int i, ret;

    ret = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);
    if (ret < 0) {
        return ret;
    }

    cr = regs.cr;
    for (i = 7; i >= 0; i--) {
        env->crf[i] = cr & 15;
        cr >>= 4;
    }

    env->ctr = regs.ctr;
    env->lr = regs.lr;
    cpu_write_xer(env, regs.xer);
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

    for (i = 0; i < 32; i++) {
        env->gpr[i] = regs.gpr[i];
    }

    kvm_get_fp(cs);

    if (cap_booke_sregs) {
        ret = kvmppc_get_booke_sregs(cpu);
        if (ret < 0) {
            return ret;
        }
    }

    if (cap_segstate) {
        ret = kvmppc_get_books_sregs(cpu);
        if (ret < 0) {
            return ret;
        }
    }

    if (cap_hior) {
        kvm_get_one_spr(cs, KVM_REG_PPC_HIOR, SPR_HIOR);
    }

    if (cap_one_reg) {
        int i;

        /*
         * We deliberately ignore errors here, for kernels which have
         * the ONE_REG calls, but don't support the specific
         * registers, there's a reasonable chance things will still
         * work, at least until we try to migrate.
         */
        for (i = 0; i < 1024; i++) {
            uint64_t id = env->spr_cb[i].one_reg_id;

            if (id != 0) {
                kvm_get_one_spr(cs, id, i);
            }
        }

#ifdef TARGET_PPC64
        if (msr_ts) {
            for (i = 0; i < ARRAY_SIZE(env->tm_gpr); i++) {
                kvm_get_one_reg(cs, KVM_REG_PPC_TM_GPR(i), &env->tm_gpr[i]);
            }
            for (i = 0; i < ARRAY_SIZE(env->tm_vsr); i++) {
                kvm_get_one_reg(cs, KVM_REG_PPC_TM_VSR(i), &env->tm_vsr[i]);
            }
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_CR, &env->tm_cr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_LR, &env->tm_lr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_CTR, &env->tm_ctr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_FPSCR, &env->tm_fpscr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_AMR, &env->tm_amr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_PPR, &env->tm_ppr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_VRSAVE, &env->tm_vrsave);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_VSCR, &env->tm_vscr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_DSCR, &env->tm_dscr);
            kvm_get_one_reg(cs, KVM_REG_PPC_TM_TAR, &env->tm_tar);
        }

        if (cap_papr) {
            if (kvm_get_vpa(cs) < 0) {
                trace_kvm_failed_get_vpa();
            }
        }

        kvm_get_one_reg(cs, KVM_REG_PPC_TB_OFFSET, &env->tb_env->tb_offset);
        kvm_get_one_spr(cs, KVM_REG_PPC_DPDES, SPR_DPDES);
#endif
    }

    return 0;
}

int kvmppc_set_interrupt(PowerPCCPU *cpu, int irq, int level)
{
    unsigned virq = level ? KVM_INTERRUPT_SET_LEVEL : KVM_INTERRUPT_UNSET;

    if (irq != PPC_INTERRUPT_EXT) {
        return 0;
    }

    if (!kvm_enabled() || !cap_interrupt_unset) {
        return 0;
    }

    kvm_vcpu_ioctl(CPU(cpu), KVM_INTERRUPT, &virq);

    return 0;
}

void kvm_arch_pre_run(CPUState *cs, struct kvm_run *run)
{
    return;
}

MemTxAttrs kvm_arch_post_run(CPUState *cs, struct kvm_run *run)
{
    return MEMTXATTRS_UNSPECIFIED;
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return cs->halted;
}

static int kvmppc_handle_halt(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    if (!(cs->interrupt_request & CPU_INTERRUPT_HARD) && (msr_ee)) {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
    }

    return 0;
}

/* map dcr access to existing qemu dcr emulation */
static int kvmppc_handle_dcr_read(CPUPPCState *env,
                                  uint32_t dcrn, uint32_t *data)
{
    if (ppc_dcr_read(env->dcr_env, dcrn, data) < 0) {
        fprintf(stderr, "Read to unhandled DCR (0x%x)\n", dcrn);
    }

    return 0;
}

static int kvmppc_handle_dcr_write(CPUPPCState *env,
                                   uint32_t dcrn, uint32_t data)
{
    if (ppc_dcr_write(env->dcr_env, dcrn, data) < 0) {
        fprintf(stderr, "Write to unhandled DCR (0x%x)\n", dcrn);
    }

    return 0;
}

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    /* Mixed endian case is not handled */
    uint32_t sc = debug_inst_opcode;

    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn,
                            sizeof(sc), 0) ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&sc, sizeof(sc), 1)) {
        return -EINVAL;
    }

    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    uint32_t sc;

    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&sc, sizeof(sc), 0) ||
        sc != debug_inst_opcode ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn,
                            sizeof(sc), 1)) {
        return -EINVAL;
    }

    return 0;
}

static int find_hw_breakpoint(target_ulong addr, int type)
{
    int n;

    assert((nb_hw_breakpoint + nb_hw_watchpoint)
           <= ARRAY_SIZE(hw_debug_points));

    for (n = 0; n < nb_hw_breakpoint + nb_hw_watchpoint; n++) {
        if (hw_debug_points[n].addr == addr &&
             hw_debug_points[n].type == type) {
            return n;
        }
    }

    return -1;
}

static int find_hw_watchpoint(target_ulong addr, int *flag)
{
    int n;

    n = find_hw_breakpoint(addr, GDB_WATCHPOINT_ACCESS);
    if (n >= 0) {
        *flag = BP_MEM_ACCESS;
        return n;
    }

    n = find_hw_breakpoint(addr, GDB_WATCHPOINT_WRITE);
    if (n >= 0) {
        *flag = BP_MEM_WRITE;
        return n;
    }

    n = find_hw_breakpoint(addr, GDB_WATCHPOINT_READ);
    if (n >= 0) {
        *flag = BP_MEM_READ;
        return n;
    }

    return -1;
}

int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    if ((nb_hw_breakpoint + nb_hw_watchpoint) >= ARRAY_SIZE(hw_debug_points)) {
        return -ENOBUFS;
    }

    hw_debug_points[nb_hw_breakpoint + nb_hw_watchpoint].addr = addr;
    hw_debug_points[nb_hw_breakpoint + nb_hw_watchpoint].type = type;

    switch (type) {
    case GDB_BREAKPOINT_HW:
        if (nb_hw_breakpoint >= max_hw_breakpoint) {
            return -ENOBUFS;
        }

        if (find_hw_breakpoint(addr, type) >= 0) {
            return -EEXIST;
        }

        nb_hw_breakpoint++;
        break;

    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        if (nb_hw_watchpoint >= max_hw_watchpoint) {
            return -ENOBUFS;
        }

        if (find_hw_breakpoint(addr, type) >= 0) {
            return -EEXIST;
        }

        nb_hw_watchpoint++;
        break;

    default:
        return -ENOSYS;
    }

    return 0;
}

int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    int n;

    n = find_hw_breakpoint(addr, type);
    if (n < 0) {
        return -ENOENT;
    }

    switch (type) {
    case GDB_BREAKPOINT_HW:
        nb_hw_breakpoint--;
        break;

    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        nb_hw_watchpoint--;
        break;

    default:
        return -ENOSYS;
    }
    hw_debug_points[n] = hw_debug_points[nb_hw_breakpoint + nb_hw_watchpoint];

    return 0;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
    nb_hw_breakpoint = nb_hw_watchpoint = 0;
}

void kvm_arch_update_guest_debug(CPUState *cs, struct kvm_guest_debug *dbg)
{
    int n;

    /* Software Breakpoint updates */
    if (kvm_sw_breakpoints_active(cs)) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP;
    }

    assert((nb_hw_breakpoint + nb_hw_watchpoint)
           <= ARRAY_SIZE(hw_debug_points));
    assert((nb_hw_breakpoint + nb_hw_watchpoint) <= ARRAY_SIZE(dbg->arch.bp));

    if (nb_hw_breakpoint + nb_hw_watchpoint > 0) {
        dbg->control |= KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_HW_BP;
        memset(dbg->arch.bp, 0, sizeof(dbg->arch.bp));
        for (n = 0; n < nb_hw_breakpoint + nb_hw_watchpoint; n++) {
            switch (hw_debug_points[n].type) {
            case GDB_BREAKPOINT_HW:
                dbg->arch.bp[n].type = KVMPPC_DEBUG_BREAKPOINT;
                break;
            case GDB_WATCHPOINT_WRITE:
                dbg->arch.bp[n].type = KVMPPC_DEBUG_WATCH_WRITE;
                break;
            case GDB_WATCHPOINT_READ:
                dbg->arch.bp[n].type = KVMPPC_DEBUG_WATCH_READ;
                break;
            case GDB_WATCHPOINT_ACCESS:
                dbg->arch.bp[n].type = KVMPPC_DEBUG_WATCH_WRITE |
                                        KVMPPC_DEBUG_WATCH_READ;
                break;
            default:
                cpu_abort(cs, "Unsupported breakpoint type\n");
            }
            dbg->arch.bp[n].addr = hw_debug_points[n].addr;
        }
    }
}

static int kvm_handle_hw_breakpoint(CPUState *cs,
                                    struct kvm_debug_exit_arch *arch_info)
{
    int handle = DEBUG_RETURN_GUEST;
    int n;
    int flag = 0;

    if (nb_hw_breakpoint + nb_hw_watchpoint > 0) {
        if (arch_info->status & KVMPPC_DEBUG_BREAKPOINT) {
            n = find_hw_breakpoint(arch_info->address, GDB_BREAKPOINT_HW);
            if (n >= 0) {
                handle = DEBUG_RETURN_GDB;
            }
        } else if (arch_info->status & (KVMPPC_DEBUG_WATCH_READ |
                                        KVMPPC_DEBUG_WATCH_WRITE)) {
            n = find_hw_watchpoint(arch_info->address,  &flag);
            if (n >= 0) {
                handle = DEBUG_RETURN_GDB;
                cs->watchpoint_hit = &hw_watchpoint;
                hw_watchpoint.vaddr = hw_debug_points[n].addr;
                hw_watchpoint.flags = flag;
            }
        }
    }
    return handle;
}

static int kvm_handle_singlestep(void)
{
    return DEBUG_RETURN_GDB;
}

static int kvm_handle_sw_breakpoint(void)
{
    return DEBUG_RETURN_GDB;
}

static int kvm_handle_debug(PowerPCCPU *cpu, struct kvm_run *run)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    struct kvm_debug_exit_arch *arch_info = &run->debug.arch;

    if (cs->singlestep_enabled) {
        return kvm_handle_singlestep();
    }

    if (arch_info->status) {
        return kvm_handle_hw_breakpoint(cs, arch_info);
    }

    if (kvm_find_sw_breakpoint(cs, arch_info->address)) {
        return kvm_handle_sw_breakpoint();
    }

    /*
     * QEMU is not able to handle debug exception, so inject
     * program exception to guest;
     * Yes program exception NOT debug exception !!
     * When QEMU is using debug resources then debug exception must
     * be always set. To achieve this we set MSR_DE and also set
     * MSRP_DEP so guest cannot change MSR_DE.
     * When emulating debug resource for guest we want guest
     * to control MSR_DE (enable/disable debug interrupt on need).
     * Supporting both configurations are NOT possible.
     * So the result is that we cannot share debug resources
     * between QEMU and Guest on BOOKE architecture.
     * In the current design QEMU gets the priority over guest,
     * this means that if QEMU is using debug resources then guest
     * cannot use them;
     * For software breakpoint QEMU uses a privileged instruction;
     * So there cannot be any reason that we are here for guest
     * set debug exception, only possibility is guest executed a
     * privileged / illegal instruction and that's why we are
     * injecting a program interrupt.
     */
    cpu_synchronize_state(cs);
    /*
     * env->nip is PC, so increment this by 4 to use
     * ppc_cpu_do_interrupt(), which set srr0 = env->nip - 4.
     */
    env->nip += 4;
    cs->exception_index = POWERPC_EXCP_PROGRAM;
    env->error_code = POWERPC_EXCP_INVAL;
    ppc_cpu_do_interrupt(cs);

    return DEBUG_RETURN_GUEST;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int ret;

    qemu_mutex_lock_iothread();

    switch (run->exit_reason) {
    case KVM_EXIT_DCR:
        if (run->dcr.is_write) {
            trace_kvm_handle_dcr_write();
            ret = kvmppc_handle_dcr_write(env, run->dcr.dcrn, run->dcr.data);
        } else {
            trace_kvm_handle_dcr_read();
            ret = kvmppc_handle_dcr_read(env, run->dcr.dcrn, &run->dcr.data);
        }
        break;
    case KVM_EXIT_HLT:
        trace_kvm_handle_halt();
        ret = kvmppc_handle_halt(cpu);
        break;
#if defined(TARGET_PPC64)
    case KVM_EXIT_PAPR_HCALL:
        trace_kvm_handle_papr_hcall();
        run->papr_hcall.ret = spapr_hypercall(cpu,
                                              run->papr_hcall.nr,
                                              run->papr_hcall.args);
        ret = 0;
        break;
#endif
    case KVM_EXIT_EPR:
        trace_kvm_handle_epr();
        run->epr.epr = ldl_phys(cs->as, env->mpic_iack);
        ret = 0;
        break;
    case KVM_EXIT_WATCHDOG:
        trace_kvm_handle_watchdog_expiry();
        watchdog_perform_action();
        ret = 0;
        break;

    case KVM_EXIT_DEBUG:
        trace_kvm_handle_debug_exception();
        if (kvm_handle_debug(cpu, run)) {
            ret = EXCP_DEBUG;
            break;
        }
        /* re-enter, this exception was guest-internal */
        ret = 0;
        break;

#if defined(TARGET_PPC64)
    case KVM_EXIT_NMI:
        trace_kvm_handle_nmi_exception();
        ret = kvm_handle_nmi(cpu, run);
        break;
#endif

    default:
        fprintf(stderr, "KVM: unknown exit reason %d\n", run->exit_reason);
        ret = -1;
        break;
    }

    qemu_mutex_unlock_iothread();
    return ret;
}

int kvmppc_or_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits)
{
    CPUState *cs = CPU(cpu);
    uint32_t bits = tsr_bits;
    struct kvm_one_reg reg = {
        .id = KVM_REG_PPC_OR_TSR,
        .addr = (uintptr_t) &bits,
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
}

int kvmppc_clear_tsr_bits(PowerPCCPU *cpu, uint32_t tsr_bits)
{

    CPUState *cs = CPU(cpu);
    uint32_t bits = tsr_bits;
    struct kvm_one_reg reg = {
        .id = KVM_REG_PPC_CLEAR_TSR,
        .addr = (uintptr_t) &bits,
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
}

int kvmppc_set_tcr(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    uint32_t tcr = env->spr[SPR_BOOKE_TCR];

    struct kvm_one_reg reg = {
        .id = KVM_REG_PPC_TCR,
        .addr = (uintptr_t) &tcr,
    };

    return kvm_vcpu_ioctl(cs, KVM_SET_ONE_REG, &reg);
}

int kvmppc_booke_watchdog_enable(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int ret;

    if (!kvm_enabled()) {
        return -1;
    }

    if (!cap_ppc_watchdog) {
        printf("warning: KVM does not support watchdog");
        return -1;
    }

    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_PPC_BOOKE_WATCHDOG, 0);
    if (ret < 0) {
        fprintf(stderr, "%s: couldn't enable KVM_CAP_PPC_BOOKE_WATCHDOG: %s\n",
                __func__, strerror(-ret));
        return ret;
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
        if (!fgets(line, sizeof(line), f)) {
            break;
        }
        if (!strncmp(line, field, field_len)) {
            pstrcpy(value, len, line);
            ret = 0;
            break;
        }
    } while (*line);

    fclose(f);

    return ret;
}

uint32_t kvmppc_get_tbfreq(void)
{
    char line[512];
    char *ns;
    uint32_t retval = NANOSECONDS_PER_SECOND;

    if (read_cpuinfo("timebase", line, sizeof(line))) {
        return retval;
    }

    ns = strchr(line, ':');
    if (!ns) {
        return retval;
    }

    ns++;

    return atoi(ns);
}

bool kvmppc_get_host_serial(char **value)
{
    return g_file_get_contents("/proc/device-tree/system-id", value, NULL,
                               NULL);
}

bool kvmppc_get_host_model(char **value)
{
    return g_file_get_contents("/proc/device-tree/model", value, NULL, NULL);
}

/* Try to find a device tree node for a CPU with clock-frequency property */
static int kvmppc_find_cpu_dt(char *buf, int buf_len)
{
    struct dirent *dirp;
    DIR *dp;

    dp = opendir(PROC_DEVTREE_CPU);
    if (!dp) {
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

static uint64_t kvmppc_read_int_dt(const char *filename)
{
    union {
        uint32_t v32;
        uint64_t v64;
    } u;
    FILE *f;
    int len;

    f = fopen(filename, "rb");
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

/*
 * Read a CPU node property from the host device tree that's a single
 * integer (32-bit or 64-bit).  Returns 0 if anything goes wrong
 * (can't find or open the property, or doesn't understand the format)
 */
static uint64_t kvmppc_read_int_cpu_dt(const char *propname)
{
    char buf[PATH_MAX], *tmp;
    uint64_t val;

    if (kvmppc_find_cpu_dt(buf, sizeof(buf))) {
        return -1;
    }

    tmp = g_strdup_printf("%s/%s", buf, propname);
    val = kvmppc_read_int_dt(tmp);
    g_free(tmp);

    return val;
}

uint64_t kvmppc_get_clockfreq(void)
{
    return kvmppc_read_int_cpu_dt("clock-frequency");
}

static int kvmppc_get_dec_bits(void)
{
    int nr_bits = kvmppc_read_int_cpu_dt("ibm,dec-bits");

    if (nr_bits > 0) {
        return nr_bits;
    }
    return 0;
}

static int kvmppc_get_pvinfo(CPUPPCState *env, struct kvm_ppc_pvinfo *pvinfo)
{
    CPUState *cs = env_cpu(env);

    if (kvm_vm_check_extension(cs->kvm_state, KVM_CAP_PPC_GET_PVINFO) &&
        !kvm_vm_ioctl(cs->kvm_state, KVM_PPC_GET_PVINFO, pvinfo)) {
        return 0;
    }

    return 1;
}

int kvmppc_get_hasidle(CPUPPCState *env)
{
    struct kvm_ppc_pvinfo pvinfo;

    if (!kvmppc_get_pvinfo(env, &pvinfo) &&
        (pvinfo.flags & KVM_PPC_PVINFO_FLAGS_EV_IDLE)) {
        return 1;
    }

    return 0;
}

int kvmppc_get_hypercall(CPUPPCState *env, uint8_t *buf, int buf_len)
{
    uint32_t *hc = (uint32_t *)buf;
    struct kvm_ppc_pvinfo pvinfo;

    if (!kvmppc_get_pvinfo(env, &pvinfo)) {
        memcpy(buf, pvinfo.hcall, buf_len);
        return 0;
    }

    /*
     * Fallback to always fail hypercalls regardless of endianness:
     *
     *     tdi 0,r0,72 (becomes b .+8 in wrong endian, nop in good endian)
     *     li r3, -1
     *     b .+8       (becomes nop in wrong endian)
     *     bswap32(li r3, -1)
     */

    hc[0] = cpu_to_be32(0x08000048);
    hc[1] = cpu_to_be32(0x3860ffff);
    hc[2] = cpu_to_be32(0x48000008);
    hc[3] = cpu_to_be32(bswap32(0x3860ffff));

    return 1;
}

static inline int kvmppc_enable_hcall(KVMState *s, target_ulong hcall)
{
    return kvm_vm_enable_cap(s, KVM_CAP_PPC_ENABLE_HCALL, 0, hcall, 1);
}

void kvmppc_enable_logical_ci_hcalls(void)
{
    /*
     * FIXME: it would be nice if we could detect the cases where
     * we're using a device which requires the in kernel
     * implementation of these hcalls, but the kernel lacks them and
     * produce a warning.
     */
    kvmppc_enable_hcall(kvm_state, H_LOGICAL_CI_LOAD);
    kvmppc_enable_hcall(kvm_state, H_LOGICAL_CI_STORE);
}

void kvmppc_enable_set_mode_hcall(void)
{
    kvmppc_enable_hcall(kvm_state, H_SET_MODE);
}

void kvmppc_enable_clear_ref_mod_hcalls(void)
{
    kvmppc_enable_hcall(kvm_state, H_CLEAR_REF);
    kvmppc_enable_hcall(kvm_state, H_CLEAR_MOD);
}

void kvmppc_enable_h_page_init(void)
{
    kvmppc_enable_hcall(kvm_state, H_PAGE_INIT);
}

void kvmppc_set_papr(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int ret;

    if (!kvm_enabled()) {
        return;
    }

    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_PPC_PAPR, 0);
    if (ret) {
        error_report("This vCPU type or KVM version does not support PAPR");
        exit(1);
    }

    /*
     * Update the capability flag so we sync the right information
     * with kvm
     */
    cap_papr = 1;
}

int kvmppc_set_compat(PowerPCCPU *cpu, uint32_t compat_pvr)
{
    return kvm_set_one_reg(CPU(cpu), KVM_REG_PPC_ARCH_COMPAT, &compat_pvr);
}

void kvmppc_set_mpic_proxy(PowerPCCPU *cpu, int mpic_proxy)
{
    CPUState *cs = CPU(cpu);
    int ret;

    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_PPC_EPR, 0, mpic_proxy);
    if (ret && mpic_proxy) {
        error_report("This KVM version does not support EPR");
        exit(1);
    }
}

bool kvmppc_get_fwnmi(void)
{
    return cap_fwnmi;
}

int kvmppc_set_fwnmi(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);

    return kvm_vcpu_enable_cap(cs, KVM_CAP_PPC_FWNMI, 0);
}

int kvmppc_smt_threads(void)
{
    return cap_ppc_smt ? cap_ppc_smt : 1;
}

int kvmppc_set_smt_threads(int smt)
{
    int ret;

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_PPC_SMT, 0, smt, 0);
    if (!ret) {
        cap_ppc_smt = smt;
    }
    return ret;
}

void kvmppc_error_append_smt_possible_hint(Error *const *errp)
{
    int i;
    GString *g;
    char *s;

    assert(kvm_enabled());
    if (cap_ppc_smt_possible) {
        g = g_string_new("Available VSMT modes:");
        for (i = 63; i >= 0; i--) {
            if ((1UL << i) & cap_ppc_smt_possible) {
                g_string_append_printf(g, " %lu", (1UL << i));
            }
        }
        s = g_string_free(g, false);
        error_append_hint(errp, "%s.\n", s);
        g_free(s);
    } else {
        error_append_hint(errp,
                          "This KVM seems to be too old to support VSMT.\n");
    }
}


#ifdef TARGET_PPC64
uint64_t kvmppc_vrma_limit(unsigned int hash_shift)
{
    struct kvm_ppc_smmu_info info;
    long rampagesize, best_page_shift;
    int i;

    /*
     * Find the largest hardware supported page size that's less than
     * or equal to the (logical) backing page size of guest RAM
     */
    kvm_get_smmu_info(&info, &error_fatal);
    rampagesize = qemu_minrampagesize();
    best_page_shift = 0;

    for (i = 0; i < KVM_PPC_PAGE_SIZES_MAX_SZ; i++) {
        struct kvm_ppc_one_seg_page_size *sps = &info.sps[i];

        if (!sps->page_shift) {
            continue;
        }

        if ((sps->page_shift > best_page_shift)
            && ((1UL << sps->page_shift) <= rampagesize)) {
            best_page_shift = sps->page_shift;
        }
    }

    return 1ULL << (best_page_shift + hash_shift - 7);
}
#endif

bool kvmppc_spapr_use_multitce(void)
{
    return cap_spapr_multitce;
}

int kvmppc_spapr_enable_inkernel_multitce(void)
{
    int ret;

    ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_PPC_ENABLE_HCALL, 0,
                            H_PUT_TCE_INDIRECT, 1);
    if (!ret) {
        ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_PPC_ENABLE_HCALL, 0,
                                H_STUFF_TCE, 1);
    }

    return ret;
}

void *kvmppc_create_spapr_tce(uint32_t liobn, uint32_t page_shift,
                              uint64_t bus_offset, uint32_t nb_table,
                              int *pfd, bool need_vfio)
{
    long len;
    int fd;
    void *table;

    /*
     * Must set fd to -1 so we don't try to munmap when called for
     * destroying the table, which the upper layers -will- do
     */
    *pfd = -1;
    if (!cap_spapr_tce || (need_vfio && !cap_spapr_vfio)) {
        return NULL;
    }

    if (cap_spapr_tce_64) {
        struct kvm_create_spapr_tce_64 args = {
            .liobn = liobn,
            .page_shift = page_shift,
            .offset = bus_offset >> page_shift,
            .size = nb_table,
            .flags = 0
        };
        fd = kvm_vm_ioctl(kvm_state, KVM_CREATE_SPAPR_TCE_64, &args);
        if (fd < 0) {
            fprintf(stderr,
                    "KVM: Failed to create TCE64 table for liobn 0x%x\n",
                    liobn);
            return NULL;
        }
    } else if (cap_spapr_tce) {
        uint64_t window_size = (uint64_t) nb_table << page_shift;
        struct kvm_create_spapr_tce args = {
            .liobn = liobn,
            .window_size = window_size,
        };
        if ((window_size != args.window_size) || bus_offset) {
            return NULL;
        }
        fd = kvm_vm_ioctl(kvm_state, KVM_CREATE_SPAPR_TCE, &args);
        if (fd < 0) {
            fprintf(stderr, "KVM: Failed to create TCE table for liobn 0x%x\n",
                    liobn);
            return NULL;
        }
    } else {
        return NULL;
    }

    len = nb_table * sizeof(uint64_t);
    /* FIXME: round this up to page size */

    table = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (table == MAP_FAILED) {
        fprintf(stderr, "KVM: Failed to map TCE table for liobn 0x%x\n",
                liobn);
        close(fd);
        return NULL;
    }

    *pfd = fd;
    return table;
}

int kvmppc_remove_spapr_tce(void *table, int fd, uint32_t nb_table)
{
    long len;

    if (fd < 0) {
        return -1;
    }

    len = nb_table * sizeof(uint64_t);
    if ((munmap(table, len) < 0) ||
        (close(fd) < 0)) {
        fprintf(stderr, "KVM: Unexpected error removing TCE table: %s",
                strerror(errno));
        /* Leak the table */
    }

    return 0;
}

int kvmppc_reset_htab(int shift_hint)
{
    uint32_t shift = shift_hint;

    if (!kvm_enabled()) {
        /* Full emulation, tell caller to allocate htab itself */
        return 0;
    }
    if (kvm_vm_check_extension(kvm_state, KVM_CAP_PPC_ALLOC_HTAB)) {
        int ret;
        ret = kvm_vm_ioctl(kvm_state, KVM_PPC_ALLOCATE_HTAB, &shift);
        if (ret == -ENOTTY) {
            /*
             * At least some versions of PR KVM advertise the
             * capability, but don't implement the ioctl().  Oops.
             * Return 0 so that we allocate the htab in qemu, as is
             * correct for PR.
             */
            return 0;
        } else if (ret < 0) {
            return ret;
        }
        return shift;
    }

    /*
     * We have a kernel that predates the htab reset calls.  For PR
     * KVM, we need to allocate the htab ourselves, for an HV KVM of
     * this era, it has allocated a 16MB fixed size hash table
     * already.
     */
    if (kvmppc_is_pr(kvm_state)) {
        /* PR - tell caller to allocate htab */
        return 0;
    } else {
        /* HV - assume 16MB kernel allocated htab */
        return 24;
    }
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

static void kvmppc_host_cpu_class_init(ObjectClass *oc, void *data)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);
    uint32_t dcache_size = kvmppc_read_int_cpu_dt("d-cache-size");
    uint32_t icache_size = kvmppc_read_int_cpu_dt("i-cache-size");

    /* Now fix up the class with information we can query from the host */
    pcc->pvr = mfpvr();

    alter_insns(&pcc->insns_flags, PPC_ALTIVEC,
                qemu_getauxval(AT_HWCAP) & PPC_FEATURE_HAS_ALTIVEC);
    alter_insns(&pcc->insns_flags2, PPC2_VSX,
                qemu_getauxval(AT_HWCAP) & PPC_FEATURE_HAS_VSX);
    alter_insns(&pcc->insns_flags2, PPC2_DFP,
                qemu_getauxval(AT_HWCAP) & PPC_FEATURE_HAS_DFP);

    if (dcache_size != -1) {
        pcc->l1_dcache_size = dcache_size;
    }

    if (icache_size != -1) {
        pcc->l1_icache_size = icache_size;
    }

#if defined(TARGET_PPC64)
    pcc->radix_page_info = kvm_get_radix_page_info();

    if ((pcc->pvr & 0xffffff00) == CPU_POWERPC_POWER9_DD1) {
        /*
         * POWER9 DD1 has some bugs which make it not really ISA 3.00
         * compliant.  More importantly, advertising ISA 3.00
         * architected mode may prevent guests from activating
         * necessary DD1 workarounds.
         */
        pcc->pcr_supported &= ~(PCR_COMPAT_3_00 | PCR_COMPAT_2_07
                                | PCR_COMPAT_2_06 | PCR_COMPAT_2_05);
    }
#endif /* defined(TARGET_PPC64) */
}

bool kvmppc_has_cap_epr(void)
{
    return cap_epr;
}

bool kvmppc_has_cap_fixup_hcalls(void)
{
    return cap_fixup_hcalls;
}

bool kvmppc_has_cap_htm(void)
{
    return cap_htm;
}

bool kvmppc_has_cap_mmu_radix(void)
{
    return cap_mmu_radix;
}

bool kvmppc_has_cap_mmu_hash_v3(void)
{
    return cap_mmu_hash_v3;
}

static bool kvmppc_power8_host(void)
{
    bool ret = false;
#ifdef TARGET_PPC64
    {
        uint32_t base_pvr = CPU_POWERPC_POWER_SERVER_MASK & mfpvr();
        ret = (base_pvr == CPU_POWERPC_POWER8E_BASE) ||
              (base_pvr == CPU_POWERPC_POWER8NVL_BASE) ||
              (base_pvr == CPU_POWERPC_POWER8_BASE);
    }
#endif /* TARGET_PPC64 */
    return ret;
}

static int parse_cap_ppc_safe_cache(struct kvm_ppc_cpu_char c)
{
    bool l1d_thread_priv_req = !kvmppc_power8_host();

    if (~c.behaviour & c.behaviour_mask & H_CPU_BEHAV_L1D_FLUSH_PR) {
        return 2;
    } else if ((!l1d_thread_priv_req ||
                c.character & c.character_mask & H_CPU_CHAR_L1D_THREAD_PRIV) &&
               (c.character & c.character_mask
                & (H_CPU_CHAR_L1D_FLUSH_ORI30 | H_CPU_CHAR_L1D_FLUSH_TRIG2))) {
        return 1;
    }

    return 0;
}

static int parse_cap_ppc_safe_bounds_check(struct kvm_ppc_cpu_char c)
{
    if (~c.behaviour & c.behaviour_mask & H_CPU_BEHAV_BNDS_CHK_SPEC_BAR) {
        return 2;
    } else if (c.character & c.character_mask & H_CPU_CHAR_SPEC_BAR_ORI31) {
        return 1;
    }

    return 0;
}

static int parse_cap_ppc_safe_indirect_branch(struct kvm_ppc_cpu_char c)
{
    if ((~c.behaviour & c.behaviour_mask & H_CPU_BEHAV_FLUSH_COUNT_CACHE) &&
        (~c.character & c.character_mask & H_CPU_CHAR_CACHE_COUNT_DIS) &&
        (~c.character & c.character_mask & H_CPU_CHAR_BCCTRL_SERIALISED)) {
        return SPAPR_CAP_FIXED_NA;
    } else if (c.behaviour & c.behaviour_mask & H_CPU_BEHAV_FLUSH_COUNT_CACHE) {
        return SPAPR_CAP_WORKAROUND;
    } else if (c.character & c.character_mask & H_CPU_CHAR_CACHE_COUNT_DIS) {
        return  SPAPR_CAP_FIXED_CCD;
    } else if (c.character & c.character_mask & H_CPU_CHAR_BCCTRL_SERIALISED) {
        return SPAPR_CAP_FIXED_IBS;
    }

    return 0;
}

static int parse_cap_ppc_count_cache_flush_assist(struct kvm_ppc_cpu_char c)
{
    if (c.character & c.character_mask & H_CPU_CHAR_BCCTR_FLUSH_ASSIST) {
        return 1;
    }
    return 0;
}

bool kvmppc_has_cap_xive(void)
{
    return cap_xive;
}

static void kvmppc_get_cpu_characteristics(KVMState *s)
{
    struct kvm_ppc_cpu_char c;
    int ret;

    /* Assume broken */
    cap_ppc_safe_cache = 0;
    cap_ppc_safe_bounds_check = 0;
    cap_ppc_safe_indirect_branch = 0;

    ret = kvm_vm_check_extension(s, KVM_CAP_PPC_GET_CPU_CHAR);
    if (!ret) {
        return;
    }
    ret = kvm_vm_ioctl(s, KVM_PPC_GET_CPU_CHAR, &c);
    if (ret < 0) {
        return;
    }

    cap_ppc_safe_cache = parse_cap_ppc_safe_cache(c);
    cap_ppc_safe_bounds_check = parse_cap_ppc_safe_bounds_check(c);
    cap_ppc_safe_indirect_branch = parse_cap_ppc_safe_indirect_branch(c);
    cap_ppc_count_cache_flush_assist =
        parse_cap_ppc_count_cache_flush_assist(c);
}

int kvmppc_get_cap_safe_cache(void)
{
    return cap_ppc_safe_cache;
}

int kvmppc_get_cap_safe_bounds_check(void)
{
    return cap_ppc_safe_bounds_check;
}

int kvmppc_get_cap_safe_indirect_branch(void)
{
    return cap_ppc_safe_indirect_branch;
}

int kvmppc_get_cap_count_cache_flush_assist(void)
{
    return cap_ppc_count_cache_flush_assist;
}

bool kvmppc_has_cap_nested_kvm_hv(void)
{
    return !!cap_ppc_nested_kvm_hv;
}

int kvmppc_set_cap_nested_kvm_hv(int enable)
{
    return kvm_vm_enable_cap(kvm_state, KVM_CAP_PPC_NESTED_HV, 0, enable);
}

bool kvmppc_has_cap_spapr_vfio(void)
{
    return cap_spapr_vfio;
}

int kvmppc_get_cap_large_decr(void)
{
    return cap_large_decr;
}

int kvmppc_enable_cap_large_decr(PowerPCCPU *cpu, int enable)
{
    CPUState *cs = CPU(cpu);
    uint64_t lpcr;

    kvm_get_one_reg(cs, KVM_REG_PPC_LPCR_64, &lpcr);
    /* Do we need to modify the LPCR? */
    if (!!(lpcr & LPCR_LD) != !!enable) {
        if (enable) {
            lpcr |= LPCR_LD;
        } else {
            lpcr &= ~LPCR_LD;
        }
        kvm_set_one_reg(cs, KVM_REG_PPC_LPCR_64, &lpcr);
        kvm_get_one_reg(cs, KVM_REG_PPC_LPCR_64, &lpcr);

        if (!!(lpcr & LPCR_LD) != !!enable) {
            return -1;
        }
    }

    return 0;
}

PowerPCCPUClass *kvm_ppc_get_host_cpu_class(void)
{
    uint32_t host_pvr = mfpvr();
    PowerPCCPUClass *pvr_pcc;

    pvr_pcc = ppc_cpu_class_by_pvr(host_pvr);
    if (pvr_pcc == NULL) {
        pvr_pcc = ppc_cpu_class_by_pvr_mask(host_pvr);
    }

    return pvr_pcc;
}

static void pseries_machine_class_fixup(ObjectClass *oc, void *opaque)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->default_cpu_type = TYPE_HOST_POWERPC_CPU;
}

static int kvm_ppc_register_host_cpu_type(void)
{
    TypeInfo type_info = {
        .name = TYPE_HOST_POWERPC_CPU,
        .class_init = kvmppc_host_cpu_class_init,
    };
    PowerPCCPUClass *pvr_pcc;
    ObjectClass *oc;
    DeviceClass *dc;
    int i;

    pvr_pcc = kvm_ppc_get_host_cpu_class();
    if (pvr_pcc == NULL) {
        return -1;
    }
    type_info.parent = object_class_get_name(OBJECT_CLASS(pvr_pcc));
    type_register(&type_info);
    /* override TCG default cpu type with 'host' cpu model */
    object_class_foreach(pseries_machine_class_fixup, TYPE_SPAPR_MACHINE,
                         false, NULL);

    oc = object_class_by_name(type_info.name);
    g_assert(oc);

    /*
     * Update generic CPU family class alias (e.g. on a POWER8NVL host,
     * we want "POWER8" to be a "family" alias that points to the current
     * host CPU type, too)
     */
    dc = DEVICE_CLASS(ppc_cpu_get_family_class(pvr_pcc));
    for (i = 0; ppc_cpu_aliases[i].alias != NULL; i++) {
        if (strcasecmp(ppc_cpu_aliases[i].alias, dc->desc) == 0) {
            char *suffix;

            ppc_cpu_aliases[i].model = g_strdup(object_class_get_name(oc));
            suffix = strstr(ppc_cpu_aliases[i].model, POWERPC_CPU_TYPE_SUFFIX);
            if (suffix) {
                *suffix = 0;
            }
            break;
        }
    }

    return 0;
}

int kvmppc_define_rtas_kernel_token(uint32_t token, const char *function)
{
    struct kvm_rtas_token_args args = {
        .token = token,
    };

    if (!kvm_check_extension(kvm_state, KVM_CAP_PPC_RTAS)) {
        return -ENOENT;
    }

    strncpy(args.name, function, sizeof(args.name) - 1);

    return kvm_vm_ioctl(kvm_state, KVM_PPC_RTAS_DEFINE_TOKEN, &args);
}

int kvmppc_get_htab_fd(bool write, uint64_t index, Error **errp)
{
    struct kvm_get_htab_fd s = {
        .flags = write ? KVM_GET_HTAB_WRITE : 0,
        .start_index = index,
    };
    int ret;

    if (!cap_htab_fd) {
        error_setg(errp, "KVM version doesn't support %s the HPT",
                   write ? "writing" : "reading");
        return -ENOTSUP;
    }

    ret = kvm_vm_ioctl(kvm_state, KVM_PPC_GET_HTAB_FD, &s);
    if (ret < 0) {
        error_setg(errp, "Unable to open fd for %s HPT %s KVM: %s",
                   write ? "writing" : "reading", write ? "to" : "from",
                   strerror(errno));
        return -errno;
    }

    return ret;
}

int kvmppc_save_htab(QEMUFile *f, int fd, size_t bufsize, int64_t max_ns)
{
    int64_t starttime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    uint8_t buf[bufsize];
    ssize_t rc;

    do {
        rc = read(fd, buf, bufsize);
        if (rc < 0) {
            fprintf(stderr, "Error reading data from KVM HTAB fd: %s\n",
                    strerror(errno));
            return rc;
        } else if (rc) {
            uint8_t *buffer = buf;
            ssize_t n = rc;
            while (n) {
                struct kvm_get_htab_header *head =
                    (struct kvm_get_htab_header *) buffer;
                size_t chunksize = sizeof(*head) +
                     HASH_PTE_SIZE_64 * head->n_valid;

                qemu_put_be32(f, head->index);
                qemu_put_be16(f, head->n_valid);
                qemu_put_be16(f, head->n_invalid);
                qemu_put_buffer(f, (void *)(head + 1),
                                HASH_PTE_SIZE_64 * head->n_valid);

                buffer += chunksize;
                n -= chunksize;
            }
        }
    } while ((rc != 0)
             && ((max_ns < 0) ||
                 ((qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - starttime) < max_ns)));

    return (rc == 0) ? 1 : 0;
}

int kvmppc_load_htab_chunk(QEMUFile *f, int fd, uint32_t index,
                           uint16_t n_valid, uint16_t n_invalid, Error **errp)
{
    struct kvm_get_htab_header *buf;
    size_t chunksize = sizeof(*buf) + n_valid * HASH_PTE_SIZE_64;
    ssize_t rc;

    buf = alloca(chunksize);
    buf->index = index;
    buf->n_valid = n_valid;
    buf->n_invalid = n_invalid;

    qemu_get_buffer(f, (void *)(buf + 1), HASH_PTE_SIZE_64 * n_valid);

    rc = write(fd, buf, chunksize);
    if (rc < 0) {
        error_setg_errno(errp, errno, "Error writing the KVM hash table");
        return -errno;
    }
    if (rc != chunksize) {
        /* We should never get a short write on a single chunk */
        error_setg(errp, "Short write while restoring the KVM hash table");
        return -ENOSPC;
    }
    return 0;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cpu)
{
    return true;
}

void kvm_arch_init_irq_routing(KVMState *s)
{
}

void kvmppc_read_hptes(ppc_hash_pte64_t *hptes, hwaddr ptex, int n)
{
    int fd, rc;
    int i;

    fd = kvmppc_get_htab_fd(false, ptex, &error_abort);

    i = 0;
    while (i < n) {
        struct kvm_get_htab_header *hdr;
        int m = n < HPTES_PER_GROUP ? n : HPTES_PER_GROUP;
        char buf[sizeof(*hdr) + m * HASH_PTE_SIZE_64];

        rc = read(fd, buf, sizeof(buf));
        if (rc < 0) {
            hw_error("kvmppc_read_hptes: Unable to read HPTEs");
        }

        hdr = (struct kvm_get_htab_header *)buf;
        while ((i < n) && ((char *)hdr < (buf + rc))) {
            int invalid = hdr->n_invalid, valid = hdr->n_valid;

            if (hdr->index != (ptex + i)) {
                hw_error("kvmppc_read_hptes: Unexpected HPTE index %"PRIu32
                         " != (%"HWADDR_PRIu" + %d", hdr->index, ptex, i);
            }

            if (n - i < valid) {
                valid = n - i;
            }
            memcpy(hptes + i, hdr + 1, HASH_PTE_SIZE_64 * valid);
            i += valid;

            if ((n - i) < invalid) {
                invalid = n - i;
            }
            memset(hptes + i, 0, invalid * HASH_PTE_SIZE_64);
            i += invalid;

            hdr = (struct kvm_get_htab_header *)
                ((char *)(hdr + 1) + HASH_PTE_SIZE_64 * hdr->n_valid);
        }
    }

    close(fd);
}

void kvmppc_write_hpte(hwaddr ptex, uint64_t pte0, uint64_t pte1)
{
    int fd, rc;
    struct {
        struct kvm_get_htab_header hdr;
        uint64_t pte0;
        uint64_t pte1;
    } buf;

    fd = kvmppc_get_htab_fd(true, 0 /* Ignored */, &error_abort);

    buf.hdr.n_valid = 1;
    buf.hdr.n_invalid = 0;
    buf.hdr.index = ptex;
    buf.pte0 = cpu_to_be64(pte0);
    buf.pte1 = cpu_to_be64(pte1);

    rc = write(fd, &buf, sizeof(buf));
    if (rc != sizeof(buf)) {
        hw_error("kvmppc_write_hpte: Unable to update KVM HPT");
    }
    close(fd);
}

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev)
{
    return 0;
}

int kvm_arch_release_virq_post(int virq)
{
    return 0;
}

int kvm_arch_msi_data_to_gsi(uint32_t data)
{
    return data & 0xffff;
}

#if defined(TARGET_PPC64)
int kvm_handle_nmi(PowerPCCPU *cpu, struct kvm_run *run)
{
    uint16_t flags = run->flags & KVM_RUN_PPC_NMI_DISP_MASK;

    cpu_synchronize_state(CPU(cpu));

    spapr_mce_req_event(cpu, flags == KVM_RUN_PPC_NMI_DISP_FULLY_RECOV);

    return 0;
}
#endif

int kvmppc_enable_hwrng(void)
{
    if (!kvm_enabled() || !kvm_check_extension(kvm_state, KVM_CAP_PPC_HWRNG)) {
        return -1;
    }

    return kvmppc_enable_hcall(kvm_state, H_RANDOM);
}

void kvmppc_check_papr_resize_hpt(Error **errp)
{
    if (!kvm_enabled()) {
        return; /* No KVM, we're good */
    }

    if (cap_resize_hpt) {
        return; /* Kernel has explicit support, we're good */
    }

    /* Otherwise fallback on looking for PR KVM */
    if (kvmppc_is_pr(kvm_state)) {
        return;
    }

    error_setg(errp,
               "Hash page table resizing not available with this KVM version");
}

int kvmppc_resize_hpt_prepare(PowerPCCPU *cpu, target_ulong flags, int shift)
{
    CPUState *cs = CPU(cpu);
    struct kvm_ppc_resize_hpt rhpt = {
        .flags = flags,
        .shift = shift,
    };

    if (!cap_resize_hpt) {
        return -ENOSYS;
    }

    return kvm_vm_ioctl(cs->kvm_state, KVM_PPC_RESIZE_HPT_PREPARE, &rhpt);
}

int kvmppc_resize_hpt_commit(PowerPCCPU *cpu, target_ulong flags, int shift)
{
    CPUState *cs = CPU(cpu);
    struct kvm_ppc_resize_hpt rhpt = {
        .flags = flags,
        .shift = shift,
    };

    if (!cap_resize_hpt) {
        return -ENOSYS;
    }

    return kvm_vm_ioctl(cs->kvm_state, KVM_PPC_RESIZE_HPT_COMMIT, &rhpt);
}

/*
 * This is a helper function to detect a post migration scenario
 * in which a guest, running as KVM-HV, freezes in cpu_post_load because
 * the guest kernel can't handle a PVR value other than the actual host
 * PVR in KVM_SET_SREGS, even if pvr_match() returns true.
 *
 * If we don't have cap_ppc_pvr_compat and we're not running in PR
 * (so, we're HV), return true. The workaround itself is done in
 * cpu_post_load.
 *
 * The order here is important: we'll only check for KVM PR as a
 * fallback if the guest kernel can't handle the situation itself.
 * We need to avoid as much as possible querying the running KVM type
 * in QEMU level.
 */
bool kvmppc_pvr_workaround_required(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);

    if (!kvm_enabled()) {
        return false;
    }

    if (cap_ppc_pvr_compat) {
        return false;
    }

    return !kvmppc_is_pr(cs->kvm_state);
}

void kvmppc_set_reg_ppc_online(PowerPCCPU *cpu, unsigned int online)
{
    CPUState *cs = CPU(cpu);

    if (kvm_enabled()) {
        kvm_set_one_reg(cs, KVM_REG_PPC_ONLINE, &online);
    }
}

void kvmppc_set_reg_tb_offset(PowerPCCPU *cpu, int64_t tb_offset)
{
    CPUState *cs = CPU(cpu);

    if (kvm_enabled()) {
        kvm_set_one_reg(cs, KVM_REG_PPC_TB_OFFSET, &tb_offset);
    }
}

bool kvm_arch_cpu_check_are_resettable(void)
{
    return true;
}
