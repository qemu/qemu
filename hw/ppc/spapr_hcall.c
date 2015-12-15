#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "cpu.h"
#include "helper_regs.h"
#include "hw/ppc/spapr.h"
#include "mmu-hash64.h"
#include "cpu-models.h"
#include "trace.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"

struct SPRSyncState {
    CPUState *cs;
    int spr;
    target_ulong value;
    target_ulong mask;
};

static void do_spr_sync(void *arg)
{
    struct SPRSyncState *s = arg;
    PowerPCCPU *cpu = POWERPC_CPU(s->cs);
    CPUPPCState *env = &cpu->env;

    cpu_synchronize_state(s->cs);
    env->spr[s->spr] &= ~s->mask;
    env->spr[s->spr] |= s->value;
}

static void set_spr(CPUState *cs, int spr, target_ulong value,
                    target_ulong mask)
{
    struct SPRSyncState s = {
        .cs = cs,
        .spr = spr,
        .value = value,
        .mask = mask
    };
    run_on_cpu(cs, do_spr_sync, &s);
}

static bool has_spr(PowerPCCPU *cpu, int spr)
{
    /* We can test whether the SPR is defined by checking for a valid name */
    return cpu->env.spr_cb[spr].name != NULL;
}

static inline bool valid_pte_index(CPUPPCState *env, target_ulong pte_index)
{
    /*
     * hash value/pteg group index is normalized by htab_mask
     */
    if (((pte_index & ~7ULL) / HPTES_PER_GROUP) & ~env->htab_mask) {
        return false;
    }
    return true;
}

static bool is_ram_address(sPAPRMachineState *spapr, hwaddr addr)
{
    MachineState *machine = MACHINE(spapr);
    MemoryHotplugState *hpms = &spapr->hotplug_memory;

    if (addr < machine->ram_size) {
        return true;
    }
    if ((addr >= hpms->base)
        && ((addr - hpms->base) < memory_region_size(&hpms->mr))) {
        return true;
    }

    return false;
}

static target_ulong h_enter(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                            target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong pte_index = args[1];
    target_ulong pteh = args[2];
    target_ulong ptel = args[3];
    unsigned apshift, spshift;
    target_ulong raddr;
    target_ulong index;
    uint64_t token;

    apshift = ppc_hash64_hpte_page_shift_noslb(cpu, pteh, ptel, &spshift);
    if (!apshift) {
        /* Bad page size encoding */
        return H_PARAMETER;
    }

    raddr = (ptel & HPTE64_R_RPN) & ~((1ULL << apshift) - 1);

    if (is_ram_address(spapr, raddr)) {
        /* Regular RAM - should have WIMG=0010 */
        if ((ptel & HPTE64_R_WIMG) != HPTE64_R_M) {
            return H_PARAMETER;
        }
    } else {
        /* Looks like an IO address */
        /* FIXME: What WIMG combinations could be sensible for IO?
         * For now we allow WIMG=010x, but are there others? */
        /* FIXME: Should we check against registered IO addresses? */
        if ((ptel & (HPTE64_R_W | HPTE64_R_I | HPTE64_R_M)) != HPTE64_R_I) {
            return H_PARAMETER;
        }
    }

    pteh &= ~0x60ULL;

    if (!valid_pte_index(env, pte_index)) {
        return H_PARAMETER;
    }

    index = 0;
    if (likely((flags & H_EXACT) == 0)) {
        pte_index &= ~7ULL;
        token = ppc_hash64_start_access(cpu, pte_index);
        for (; index < 8; index++) {
            if (!(ppc_hash64_load_hpte0(cpu, token, index) & HPTE64_V_VALID)) {
                break;
            }
        }
        ppc_hash64_stop_access(cpu, token);
        if (index == 8) {
            return H_PTEG_FULL;
        }
    } else {
        token = ppc_hash64_start_access(cpu, pte_index);
        if (ppc_hash64_load_hpte0(cpu, token, 0) & HPTE64_V_VALID) {
            ppc_hash64_stop_access(cpu, token);
            return H_PTEG_FULL;
        }
        ppc_hash64_stop_access(cpu, token);
    }

    ppc_hash64_store_hpte(cpu, pte_index + index,
                          pteh | HPTE64_V_HPTE_DIRTY, ptel);

    args[0] = pte_index + index;
    return H_SUCCESS;
}

typedef enum {
    REMOVE_SUCCESS = 0,
    REMOVE_NOT_FOUND = 1,
    REMOVE_PARM = 2,
    REMOVE_HW = 3,
} RemoveResult;

static RemoveResult remove_hpte(PowerPCCPU *cpu, target_ulong ptex,
                                target_ulong avpn,
                                target_ulong flags,
                                target_ulong *vp, target_ulong *rp)
{
    CPUPPCState *env = &cpu->env;
    uint64_t token;
    target_ulong v, r;

    if (!valid_pte_index(env, ptex)) {
        return REMOVE_PARM;
    }

    token = ppc_hash64_start_access(cpu, ptex);
    v = ppc_hash64_load_hpte0(cpu, token, 0);
    r = ppc_hash64_load_hpte1(cpu, token, 0);
    ppc_hash64_stop_access(cpu, token);

    if ((v & HPTE64_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn) ||
        ((flags & H_ANDCOND) && (v & avpn) != 0)) {
        return REMOVE_NOT_FOUND;
    }
    *vp = v;
    *rp = r;
    ppc_hash64_store_hpte(cpu, ptex, HPTE64_V_HPTE_DIRTY, 0);
    ppc_hash64_tlb_flush_hpte(cpu, ptex, v, r);
    return REMOVE_SUCCESS;
}

static target_ulong h_remove(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                             target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong pte_index = args[1];
    target_ulong avpn = args[2];
    RemoveResult ret;

    ret = remove_hpte(cpu, pte_index, avpn, flags,
                      &args[0], &args[1]);

    switch (ret) {
    case REMOVE_SUCCESS:
        return H_SUCCESS;

    case REMOVE_NOT_FOUND:
        return H_NOT_FOUND;

    case REMOVE_PARM:
        return H_PARAMETER;

    case REMOVE_HW:
        return H_HARDWARE;
    }

    g_assert_not_reached();
}

#define H_BULK_REMOVE_TYPE             0xc000000000000000ULL
#define   H_BULK_REMOVE_REQUEST        0x4000000000000000ULL
#define   H_BULK_REMOVE_RESPONSE       0x8000000000000000ULL
#define   H_BULK_REMOVE_END            0xc000000000000000ULL
#define H_BULK_REMOVE_CODE             0x3000000000000000ULL
#define   H_BULK_REMOVE_SUCCESS        0x0000000000000000ULL
#define   H_BULK_REMOVE_NOT_FOUND      0x1000000000000000ULL
#define   H_BULK_REMOVE_PARM           0x2000000000000000ULL
#define   H_BULK_REMOVE_HW             0x3000000000000000ULL
#define H_BULK_REMOVE_RC               0x0c00000000000000ULL
#define H_BULK_REMOVE_FLAGS            0x0300000000000000ULL
#define   H_BULK_REMOVE_ABSOLUTE       0x0000000000000000ULL
#define   H_BULK_REMOVE_ANDCOND        0x0100000000000000ULL
#define   H_BULK_REMOVE_AVPN           0x0200000000000000ULL
#define H_BULK_REMOVE_PTEX             0x00ffffffffffffffULL

#define H_BULK_REMOVE_MAX_BATCH        4

static target_ulong h_bulk_remove(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                  target_ulong opcode, target_ulong *args)
{
    int i;

    for (i = 0; i < H_BULK_REMOVE_MAX_BATCH; i++) {
        target_ulong *tsh = &args[i*2];
        target_ulong tsl = args[i*2 + 1];
        target_ulong v, r, ret;

        if ((*tsh & H_BULK_REMOVE_TYPE) == H_BULK_REMOVE_END) {
            break;
        } else if ((*tsh & H_BULK_REMOVE_TYPE) != H_BULK_REMOVE_REQUEST) {
            return H_PARAMETER;
        }

        *tsh &= H_BULK_REMOVE_PTEX | H_BULK_REMOVE_FLAGS;
        *tsh |= H_BULK_REMOVE_RESPONSE;

        if ((*tsh & H_BULK_REMOVE_ANDCOND) && (*tsh & H_BULK_REMOVE_AVPN)) {
            *tsh |= H_BULK_REMOVE_PARM;
            return H_PARAMETER;
        }

        ret = remove_hpte(cpu, *tsh & H_BULK_REMOVE_PTEX, tsl,
                          (*tsh & H_BULK_REMOVE_FLAGS) >> 26,
                          &v, &r);

        *tsh |= ret << 60;

        switch (ret) {
        case REMOVE_SUCCESS:
            *tsh |= (r & (HPTE64_R_C | HPTE64_R_R)) << 43;
            break;

        case REMOVE_PARM:
            return H_PARAMETER;

        case REMOVE_HW:
            return H_HARDWARE;
        }
    }

    return H_SUCCESS;
}

static target_ulong h_protect(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                              target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong pte_index = args[1];
    target_ulong avpn = args[2];
    uint64_t token;
    target_ulong v, r;

    if (!valid_pte_index(env, pte_index)) {
        return H_PARAMETER;
    }

    token = ppc_hash64_start_access(cpu, pte_index);
    v = ppc_hash64_load_hpte0(cpu, token, 0);
    r = ppc_hash64_load_hpte1(cpu, token, 0);
    ppc_hash64_stop_access(cpu, token);

    if ((v & HPTE64_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn)) {
        return H_NOT_FOUND;
    }

    r &= ~(HPTE64_R_PP0 | HPTE64_R_PP | HPTE64_R_N |
           HPTE64_R_KEY_HI | HPTE64_R_KEY_LO);
    r |= (flags << 55) & HPTE64_R_PP0;
    r |= (flags << 48) & HPTE64_R_KEY_HI;
    r |= flags & (HPTE64_R_PP | HPTE64_R_N | HPTE64_R_KEY_LO);
    ppc_hash64_store_hpte(cpu, pte_index,
                          (v & ~HPTE64_V_VALID) | HPTE64_V_HPTE_DIRTY, 0);
    ppc_hash64_tlb_flush_hpte(cpu, pte_index, v, r);
    /* Don't need a memory barrier, due to qemu's global lock */
    ppc_hash64_store_hpte(cpu, pte_index, v | HPTE64_V_HPTE_DIRTY, r);
    return H_SUCCESS;
}

static target_ulong h_read(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong pte_index = args[1];
    uint8_t *hpte;
    int i, ridx, n_entries = 1;

    if (!valid_pte_index(env, pte_index)) {
        return H_PARAMETER;
    }

    if (flags & H_READ_4) {
        /* Clear the two low order bits */
        pte_index &= ~(3ULL);
        n_entries = 4;
    }

    hpte = env->external_htab + (pte_index * HASH_PTE_SIZE_64);

    for (i = 0, ridx = 0; i < n_entries; i++) {
        args[ridx++] = ldq_p(hpte);
        args[ridx++] = ldq_p(hpte + (HASH_PTE_SIZE_64/2));
        hpte += HASH_PTE_SIZE_64;
    }

    return H_SUCCESS;
}

static target_ulong h_set_sprg0(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    cpu_synchronize_state(CPU(cpu));
    cpu->env.spr[SPR_SPRG0] = args[0];

    return H_SUCCESS;
}

static target_ulong h_set_dabr(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                               target_ulong opcode, target_ulong *args)
{
    if (!has_spr(cpu, SPR_DABR)) {
        return H_HARDWARE;              /* DABR register not available */
    }
    cpu_synchronize_state(CPU(cpu));

    if (has_spr(cpu, SPR_DABRX)) {
        cpu->env.spr[SPR_DABRX] = 0x3;  /* Use Problem and Privileged state */
    } else if (!(args[0] & 0x4)) {      /* Breakpoint Translation set? */
        return H_RESERVED_DABR;
    }

    cpu->env.spr[SPR_DABR] = args[0];
    return H_SUCCESS;
}

static target_ulong h_set_xdabr(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    target_ulong dabrx = args[1];

    if (!has_spr(cpu, SPR_DABR) || !has_spr(cpu, SPR_DABRX)) {
        return H_HARDWARE;
    }

    if ((dabrx & ~0xfULL) != 0 || (dabrx & H_DABRX_HYPERVISOR) != 0
        || (dabrx & (H_DABRX_KERNEL | H_DABRX_USER)) == 0) {
        return H_PARAMETER;
    }

    cpu_synchronize_state(CPU(cpu));
    cpu->env.spr[SPR_DABRX] = dabrx;
    cpu->env.spr[SPR_DABR] = args[0];

    return H_SUCCESS;
}

static target_ulong h_page_init(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    hwaddr dst = args[1];
    hwaddr src = args[2];
    hwaddr len = TARGET_PAGE_SIZE;
    uint8_t *pdst, *psrc;
    target_long ret = H_SUCCESS;

    if (flags & ~(H_ICACHE_SYNCHRONIZE | H_ICACHE_INVALIDATE
                  | H_COPY_PAGE | H_ZERO_PAGE)) {
        qemu_log_mask(LOG_UNIMP, "h_page_init: Bad flags (" TARGET_FMT_lx "\n",
                      flags);
        return H_PARAMETER;
    }

    /* Map-in destination */
    if (!is_ram_address(spapr, dst) || (dst & ~TARGET_PAGE_MASK) != 0) {
        return H_PARAMETER;
    }
    pdst = cpu_physical_memory_map(dst, &len, 1);
    if (!pdst || len != TARGET_PAGE_SIZE) {
        return H_PARAMETER;
    }

    if (flags & H_COPY_PAGE) {
        /* Map-in source, copy to destination, and unmap source again */
        if (!is_ram_address(spapr, src) || (src & ~TARGET_PAGE_MASK) != 0) {
            ret = H_PARAMETER;
            goto unmap_out;
        }
        psrc = cpu_physical_memory_map(src, &len, 0);
        if (!psrc || len != TARGET_PAGE_SIZE) {
            ret = H_PARAMETER;
            goto unmap_out;
        }
        memcpy(pdst, psrc, len);
        cpu_physical_memory_unmap(psrc, len, 0, len);
    } else if (flags & H_ZERO_PAGE) {
        memset(pdst, 0, len);          /* Just clear the destination page */
    }

    if (kvm_enabled() && (flags & H_ICACHE_SYNCHRONIZE) != 0) {
        kvmppc_dcbst_range(cpu, pdst, len);
    }
    if (flags & (H_ICACHE_SYNCHRONIZE | H_ICACHE_INVALIDATE)) {
        if (kvm_enabled()) {
            kvmppc_icbi_range(cpu, pdst, len);
        } else {
            tb_flush(CPU(cpu));
        }
    }

unmap_out:
    cpu_physical_memory_unmap(pdst, TARGET_PAGE_SIZE, 1, len);
    return ret;
}

#define FLAGS_REGISTER_VPA         0x0000200000000000ULL
#define FLAGS_REGISTER_DTL         0x0000400000000000ULL
#define FLAGS_REGISTER_SLBSHADOW   0x0000600000000000ULL
#define FLAGS_DEREGISTER_VPA       0x0000a00000000000ULL
#define FLAGS_DEREGISTER_DTL       0x0000c00000000000ULL
#define FLAGS_DEREGISTER_SLBSHADOW 0x0000e00000000000ULL

#define VPA_MIN_SIZE           640
#define VPA_SIZE_OFFSET        0x4
#define VPA_SHARED_PROC_OFFSET 0x9
#define VPA_SHARED_PROC_VAL    0x2

static target_ulong register_vpa(CPUPPCState *env, target_ulong vpa)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    uint16_t size;
    uint8_t tmp;

    if (vpa == 0) {
        hcall_dprintf("Can't cope with registering a VPA at logical 0\n");
        return H_HARDWARE;
    }

    if (vpa % env->dcache_line_size) {
        return H_PARAMETER;
    }
    /* FIXME: bounds check the address */

    size = lduw_be_phys(cs->as, vpa + 0x4);

    if (size < VPA_MIN_SIZE) {
        return H_PARAMETER;
    }

    /* VPA is not allowed to cross a page boundary */
    if ((vpa / 4096) != ((vpa + size - 1) / 4096)) {
        return H_PARAMETER;
    }

    env->vpa_addr = vpa;

    tmp = ldub_phys(cs->as, env->vpa_addr + VPA_SHARED_PROC_OFFSET);
    tmp |= VPA_SHARED_PROC_VAL;
    stb_phys(cs->as, env->vpa_addr + VPA_SHARED_PROC_OFFSET, tmp);

    return H_SUCCESS;
}

static target_ulong deregister_vpa(CPUPPCState *env, target_ulong vpa)
{
    if (env->slb_shadow_addr) {
        return H_RESOURCE;
    }

    if (env->dtl_addr) {
        return H_RESOURCE;
    }

    env->vpa_addr = 0;
    return H_SUCCESS;
}

static target_ulong register_slb_shadow(CPUPPCState *env, target_ulong addr)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    uint32_t size;

    if (addr == 0) {
        hcall_dprintf("Can't cope with SLB shadow at logical 0\n");
        return H_HARDWARE;
    }

    size = ldl_be_phys(cs->as, addr + 0x4);
    if (size < 0x8) {
        return H_PARAMETER;
    }

    if ((addr / 4096) != ((addr + size - 1) / 4096)) {
        return H_PARAMETER;
    }

    if (!env->vpa_addr) {
        return H_RESOURCE;
    }

    env->slb_shadow_addr = addr;
    env->slb_shadow_size = size;

    return H_SUCCESS;
}

static target_ulong deregister_slb_shadow(CPUPPCState *env, target_ulong addr)
{
    env->slb_shadow_addr = 0;
    env->slb_shadow_size = 0;
    return H_SUCCESS;
}

static target_ulong register_dtl(CPUPPCState *env, target_ulong addr)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    uint32_t size;

    if (addr == 0) {
        hcall_dprintf("Can't cope with DTL at logical 0\n");
        return H_HARDWARE;
    }

    size = ldl_be_phys(cs->as, addr + 0x4);

    if (size < 48) {
        return H_PARAMETER;
    }

    if (!env->vpa_addr) {
        return H_RESOURCE;
    }

    env->dtl_addr = addr;
    env->dtl_size = size;

    return H_SUCCESS;
}

static target_ulong deregister_dtl(CPUPPCState *env, target_ulong addr)
{
    env->dtl_addr = 0;
    env->dtl_size = 0;

    return H_SUCCESS;
}

static target_ulong h_register_vpa(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong procno = args[1];
    target_ulong vpa = args[2];
    target_ulong ret = H_PARAMETER;
    CPUPPCState *tenv;
    PowerPCCPU *tcpu;

    tcpu = ppc_get_vcpu_by_dt_id(procno);
    if (!tcpu) {
        return H_PARAMETER;
    }
    tenv = &tcpu->env;

    switch (flags) {
    case FLAGS_REGISTER_VPA:
        ret = register_vpa(tenv, vpa);
        break;

    case FLAGS_DEREGISTER_VPA:
        ret = deregister_vpa(tenv, vpa);
        break;

    case FLAGS_REGISTER_SLBSHADOW:
        ret = register_slb_shadow(tenv, vpa);
        break;

    case FLAGS_DEREGISTER_SLBSHADOW:
        ret = deregister_slb_shadow(tenv, vpa);
        break;

    case FLAGS_REGISTER_DTL:
        ret = register_dtl(tenv, vpa);
        break;

    case FLAGS_DEREGISTER_DTL:
        ret = deregister_dtl(tenv, vpa);
        break;
    }

    return ret;
}

static target_ulong h_cede(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    env->msr |= (1ULL << MSR_EE);
    hreg_compute_hflags(env);
    if (!cpu_has_work(cs)) {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cs->exit_request = 1;
    }
    return H_SUCCESS;
}

static target_ulong h_rtas(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_ulong rtas_r3 = args[0];
    uint32_t token = rtas_ld(rtas_r3, 0);
    uint32_t nargs = rtas_ld(rtas_r3, 1);
    uint32_t nret = rtas_ld(rtas_r3, 2);

    return spapr_rtas_call(cpu, spapr, token, nargs, rtas_r3 + 12,
                           nret, rtas_r3 + 12 + 4*nargs);
}

static target_ulong h_logical_load(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);
    target_ulong size = args[0];
    target_ulong addr = args[1];

    switch (size) {
    case 1:
        args[0] = ldub_phys(cs->as, addr);
        return H_SUCCESS;
    case 2:
        args[0] = lduw_phys(cs->as, addr);
        return H_SUCCESS;
    case 4:
        args[0] = ldl_phys(cs->as, addr);
        return H_SUCCESS;
    case 8:
        args[0] = ldq_phys(cs->as, addr);
        return H_SUCCESS;
    }
    return H_PARAMETER;
}

static target_ulong h_logical_store(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                    target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);

    target_ulong size = args[0];
    target_ulong addr = args[1];
    target_ulong val  = args[2];

    switch (size) {
    case 1:
        stb_phys(cs->as, addr, val);
        return H_SUCCESS;
    case 2:
        stw_phys(cs->as, addr, val);
        return H_SUCCESS;
    case 4:
        stl_phys(cs->as, addr, val);
        return H_SUCCESS;
    case 8:
        stq_phys(cs->as, addr, val);
        return H_SUCCESS;
    }
    return H_PARAMETER;
}

static target_ulong h_logical_memop(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                    target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);

    target_ulong dst   = args[0]; /* Destination address */
    target_ulong src   = args[1]; /* Source address */
    target_ulong esize = args[2]; /* Element size (0=1,1=2,2=4,3=8) */
    target_ulong count = args[3]; /* Element count */
    target_ulong op    = args[4]; /* 0 = copy, 1 = invert */
    uint64_t tmp;
    unsigned int mask = (1 << esize) - 1;
    int step = 1 << esize;

    if (count > 0x80000000) {
        return H_PARAMETER;
    }

    if ((dst & mask) || (src & mask) || (op > 1)) {
        return H_PARAMETER;
    }

    if (dst >= src && dst < (src + (count << esize))) {
            dst = dst + ((count - 1) << esize);
            src = src + ((count - 1) << esize);
            step = -step;
    }

    while (count--) {
        switch (esize) {
        case 0:
            tmp = ldub_phys(cs->as, src);
            break;
        case 1:
            tmp = lduw_phys(cs->as, src);
            break;
        case 2:
            tmp = ldl_phys(cs->as, src);
            break;
        case 3:
            tmp = ldq_phys(cs->as, src);
            break;
        default:
            return H_PARAMETER;
        }
        if (op == 1) {
            tmp = ~tmp;
        }
        switch (esize) {
        case 0:
            stb_phys(cs->as, dst, tmp);
            break;
        case 1:
            stw_phys(cs->as, dst, tmp);
            break;
        case 2:
            stl_phys(cs->as, dst, tmp);
            break;
        case 3:
            stq_phys(cs->as, dst, tmp);
            break;
        }
        dst = dst + step;
        src = src + step;
    }

    return H_SUCCESS;
}

static target_ulong h_logical_icbi(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    /* Nothing to do on emulation, KVM will trap this in the kernel */
    return H_SUCCESS;
}

static target_ulong h_logical_dcbf(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    /* Nothing to do on emulation, KVM will trap this in the kernel */
    return H_SUCCESS;
}

static target_ulong h_set_mode_resource_le(PowerPCCPU *cpu,
                                           target_ulong mflags,
                                           target_ulong value1,
                                           target_ulong value2)
{
    CPUState *cs;

    if (value1) {
        return H_P3;
    }
    if (value2) {
        return H_P4;
    }

    switch (mflags) {
    case H_SET_MODE_ENDIAN_BIG:
        CPU_FOREACH(cs) {
            set_spr(cs, SPR_LPCR, 0, LPCR_ILE);
        }
        spapr_pci_switch_vga(true);
        return H_SUCCESS;

    case H_SET_MODE_ENDIAN_LITTLE:
        CPU_FOREACH(cs) {
            set_spr(cs, SPR_LPCR, LPCR_ILE, LPCR_ILE);
        }
        spapr_pci_switch_vga(false);
        return H_SUCCESS;
    }

    return H_UNSUPPORTED_FLAG;
}

static target_ulong h_set_mode_resource_addr_trans_mode(PowerPCCPU *cpu,
                                                        target_ulong mflags,
                                                        target_ulong value1,
                                                        target_ulong value2)
{
    CPUState *cs;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    if (!(pcc->insns_flags2 & PPC2_ISA207S)) {
        return H_P2;
    }
    if (value1) {
        return H_P3;
    }
    if (value2) {
        return H_P4;
    }

    if (mflags == AIL_RESERVED) {
        return H_UNSUPPORTED_FLAG;
    }

    CPU_FOREACH(cs) {
        set_spr(cs, SPR_LPCR, mflags << LPCR_AIL_SHIFT, LPCR_AIL);
    }

    return H_SUCCESS;
}

static target_ulong h_set_mode(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                               target_ulong opcode, target_ulong *args)
{
    target_ulong resource = args[1];
    target_ulong ret = H_P2;

    switch (resource) {
    case H_SET_MODE_RESOURCE_LE:
        ret = h_set_mode_resource_le(cpu, args[0], args[2], args[3]);
        break;
    case H_SET_MODE_RESOURCE_ADDR_TRANS_MODE:
        ret = h_set_mode_resource_addr_trans_mode(cpu, args[0],
                                                  args[2], args[3]);
        break;
    }

    return ret;
}

/*
 * Return the offset to the requested option vector @vector in the
 * option vector table @table.
 */
static target_ulong cas_get_option_vector(int vector, target_ulong table)
{
    int i;
    char nr_vectors, nr_entries;

    if (!table) {
        return 0;
    }

    nr_vectors = (ldl_phys(&address_space_memory, table) >> 24) + 1;
    if (!vector || vector > nr_vectors) {
        return 0;
    }
    table++; /* skip nr option vectors */

    for (i = 0; i < vector - 1; i++) {
        nr_entries = ldl_phys(&address_space_memory, table) >> 24;
        table += nr_entries + 2;
    }
    return table;
}

typedef struct {
    PowerPCCPU *cpu;
    uint32_t cpu_version;
    Error *err;
} SetCompatState;

static void do_set_compat(void *arg)
{
    SetCompatState *s = arg;

    cpu_synchronize_state(CPU(s->cpu));
    ppc_set_compat(s->cpu, s->cpu_version, &s->err);
}

#define get_compat_level(cpuver) ( \
    ((cpuver) == CPU_POWERPC_LOGICAL_2_05) ? 2050 : \
    ((cpuver) == CPU_POWERPC_LOGICAL_2_06) ? 2060 : \
    ((cpuver) == CPU_POWERPC_LOGICAL_2_06_PLUS) ? 2061 : \
    ((cpuver) == CPU_POWERPC_LOGICAL_2_07) ? 2070 : 0)

#define OV5_DRCONF_MEMORY 0x20

static target_ulong h_client_architecture_support(PowerPCCPU *cpu_,
                                                  sPAPRMachineState *spapr,
                                                  target_ulong opcode,
                                                  target_ulong *args)
{
    target_ulong list = ppc64_phys_to_real(args[0]);
    target_ulong ov_table, ov5;
    PowerPCCPUClass *pcc_ = POWERPC_CPU_GET_CLASS(cpu_);
    CPUState *cs;
    bool cpu_match = false, cpu_update = true, memory_update = false;
    unsigned old_cpu_version = cpu_->cpu_version;
    unsigned compat_lvl = 0, cpu_version = 0;
    unsigned max_lvl = get_compat_level(cpu_->max_compat);
    int counter;
    char ov5_byte2;

    /* Parse PVR list */
    for (counter = 0; counter < 512; ++counter) {
        uint32_t pvr, pvr_mask;

        pvr_mask = ldl_be_phys(&address_space_memory, list);
        list += 4;
        pvr = ldl_be_phys(&address_space_memory, list);
        list += 4;

        trace_spapr_cas_pvr_try(pvr);
        if (!max_lvl &&
            ((cpu_->env.spr[SPR_PVR] & pvr_mask) == (pvr & pvr_mask))) {
            cpu_match = true;
            cpu_version = 0;
        } else if (pvr == cpu_->cpu_version) {
            cpu_match = true;
            cpu_version = cpu_->cpu_version;
        } else if (!cpu_match) {
            /* If it is a logical PVR, try to determine the highest level */
            unsigned lvl = get_compat_level(pvr);
            if (lvl) {
                bool is205 = (pcc_->pcr_mask & PCR_COMPAT_2_05) &&
                     (lvl == get_compat_level(CPU_POWERPC_LOGICAL_2_05));
                bool is206 = (pcc_->pcr_mask & PCR_COMPAT_2_06) &&
                    ((lvl == get_compat_level(CPU_POWERPC_LOGICAL_2_06)) ||
                    (lvl == get_compat_level(CPU_POWERPC_LOGICAL_2_06_PLUS)));

                if (is205 || is206) {
                    if (!max_lvl) {
                        /* User did not set the level, choose the highest */
                        if (compat_lvl <= lvl) {
                            compat_lvl = lvl;
                            cpu_version = pvr;
                        }
                    } else if (max_lvl >= lvl) {
                        /* User chose the level, don't set higher than this */
                        compat_lvl = lvl;
                        cpu_version = pvr;
                    }
                }
            }
        }
        /* Terminator record */
        if (~pvr_mask & pvr) {
            break;
        }
    }

    /* Parsing finished */
    trace_spapr_cas_pvr(cpu_->cpu_version, cpu_match,
                        cpu_version, pcc_->pcr_mask);

    /* Update CPUs */
    if (old_cpu_version != cpu_version) {
        CPU_FOREACH(cs) {
            SetCompatState s = {
                .cpu = POWERPC_CPU(cs),
                .cpu_version = cpu_version,
                .err = NULL,
            };

            run_on_cpu(cs, do_set_compat, &s);

            if (s.err) {
                error_report_err(s.err);
                return H_HARDWARE;
            }
        }
    }

    if (!cpu_version) {
        cpu_update = false;
    }

    /* For the future use: here @ov_table points to the first option vector */
    ov_table = list;

    ov5 = cas_get_option_vector(5, ov_table);
    if (!ov5) {
        return H_SUCCESS;
    }

    /* @list now points to OV 5 */
    ov5_byte2 = ldub_phys(&address_space_memory, ov5 + 2);
    if (ov5_byte2 & OV5_DRCONF_MEMORY) {
        memory_update = true;
    }

    if (spapr_h_cas_compose_response(spapr, args[1], args[2],
                                     cpu_update, memory_update)) {
        qemu_system_reset_request();
    }

    return H_SUCCESS;
}

static spapr_hcall_fn papr_hypercall_table[(MAX_HCALL_OPCODE / 4) + 1];
static spapr_hcall_fn kvmppc_hypercall_table[KVMPPC_HCALL_MAX - KVMPPC_HCALL_BASE + 1];

void spapr_register_hypercall(target_ulong opcode, spapr_hcall_fn fn)
{
    spapr_hcall_fn *slot;

    if (opcode <= MAX_HCALL_OPCODE) {
        assert((opcode & 0x3) == 0);

        slot = &papr_hypercall_table[opcode / 4];
    } else {
        assert((opcode >= KVMPPC_HCALL_BASE) && (opcode <= KVMPPC_HCALL_MAX));

        slot = &kvmppc_hypercall_table[opcode - KVMPPC_HCALL_BASE];
    }

    assert(!(*slot));
    *slot = fn;
}

target_ulong spapr_hypercall(PowerPCCPU *cpu, target_ulong opcode,
                             target_ulong *args)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    if ((opcode <= MAX_HCALL_OPCODE)
        && ((opcode & 0x3) == 0)) {
        spapr_hcall_fn fn = papr_hypercall_table[opcode / 4];

        if (fn) {
            return fn(cpu, spapr, opcode, args);
        }
    } else if ((opcode >= KVMPPC_HCALL_BASE) &&
               (opcode <= KVMPPC_HCALL_MAX)) {
        spapr_hcall_fn fn = kvmppc_hypercall_table[opcode - KVMPPC_HCALL_BASE];

        if (fn) {
            return fn(cpu, spapr, opcode, args);
        }
    }

    qemu_log_mask(LOG_UNIMP, "Unimplemented SPAPR hcall 0x" TARGET_FMT_lx "\n",
                  opcode);
    return H_FUNCTION;
}

static void hypercall_register_types(void)
{
    /* hcall-pft */
    spapr_register_hypercall(H_ENTER, h_enter);
    spapr_register_hypercall(H_REMOVE, h_remove);
    spapr_register_hypercall(H_PROTECT, h_protect);
    spapr_register_hypercall(H_READ, h_read);

    /* hcall-bulk */
    spapr_register_hypercall(H_BULK_REMOVE, h_bulk_remove);

    /* hcall-splpar */
    spapr_register_hypercall(H_REGISTER_VPA, h_register_vpa);
    spapr_register_hypercall(H_CEDE, h_cede);

    /* processor register resource access h-calls */
    spapr_register_hypercall(H_SET_SPRG0, h_set_sprg0);
    spapr_register_hypercall(H_SET_DABR, h_set_dabr);
    spapr_register_hypercall(H_SET_XDABR, h_set_xdabr);
    spapr_register_hypercall(H_PAGE_INIT, h_page_init);
    spapr_register_hypercall(H_SET_MODE, h_set_mode);

    /* "debugger" hcalls (also used by SLOF). Note: We do -not- differenciate
     * here between the "CI" and the "CACHE" variants, they will use whatever
     * mapping attributes qemu is using. When using KVM, the kernel will
     * enforce the attributes more strongly
     */
    spapr_register_hypercall(H_LOGICAL_CI_LOAD, h_logical_load);
    spapr_register_hypercall(H_LOGICAL_CI_STORE, h_logical_store);
    spapr_register_hypercall(H_LOGICAL_CACHE_LOAD, h_logical_load);
    spapr_register_hypercall(H_LOGICAL_CACHE_STORE, h_logical_store);
    spapr_register_hypercall(H_LOGICAL_ICBI, h_logical_icbi);
    spapr_register_hypercall(H_LOGICAL_DCBF, h_logical_dcbf);
    spapr_register_hypercall(KVMPPC_H_LOGICAL_MEMOP, h_logical_memop);

    /* qemu/KVM-PPC specific hcalls */
    spapr_register_hypercall(KVMPPC_H_RTAS, h_rtas);

    /* ibm,client-architecture-support support */
    spapr_register_hypercall(KVMPPC_H_CAS, h_client_architecture_support);
}

type_init(hypercall_register_types)
