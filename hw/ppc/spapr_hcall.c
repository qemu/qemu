#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "helper_regs.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "mmu-hash64.h"
#include "cpu-models.h"
#include "trace.h"
#include "kvm_ppc.h"
#include "hw/ppc/spapr_ovec.h"
#include "mmu-book3s-v3.h"
#include "hw/mem/memory-device.h"

static bool has_spr(PowerPCCPU *cpu, int spr)
{
    /* We can test whether the SPR is defined by checking for a valid name */
    return cpu->env.spr_cb[spr].name != NULL;
}

static inline bool valid_ptex(PowerPCCPU *cpu, target_ulong ptex)
{
    /*
     * hash value/pteg group index is normalized by HPT mask
     */
    if (((ptex & ~7ULL) / HPTES_PER_GROUP) & ~ppc_hash64_hpt_mask(cpu)) {
        return false;
    }
    return true;
}

static bool is_ram_address(SpaprMachineState *spapr, hwaddr addr)
{
    MachineState *machine = MACHINE(spapr);
    DeviceMemoryState *dms = machine->device_memory;

    if (addr < machine->ram_size) {
        return true;
    }
    if ((addr >= dms->base)
        && ((addr - dms->base) < memory_region_size(&dms->mr))) {
        return true;
    }

    return false;
}

static target_ulong h_enter(PowerPCCPU *cpu, SpaprMachineState *spapr,
                            target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    target_ulong pteh = args[2];
    target_ulong ptel = args[3];
    unsigned apshift;
    target_ulong raddr;
    target_ulong slot;
    const ppc_hash_pte64_t *hptes;

    apshift = ppc_hash64_hpte_page_shift_noslb(cpu, pteh, ptel);
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
        target_ulong wimg_flags;
        /* Looks like an IO address */
        /* FIXME: What WIMG combinations could be sensible for IO?
         * For now we allow WIMG=010x, but are there others? */
        /* FIXME: Should we check against registered IO addresses? */
        wimg_flags = (ptel & (HPTE64_R_W | HPTE64_R_I | HPTE64_R_M));

        if (wimg_flags != HPTE64_R_I &&
            wimg_flags != (HPTE64_R_I | HPTE64_R_M)) {
            return H_PARAMETER;
        }
    }

    pteh &= ~0x60ULL;

    if (!valid_ptex(cpu, ptex)) {
        return H_PARAMETER;
    }

    slot = ptex & 7ULL;
    ptex = ptex & ~7ULL;

    if (likely((flags & H_EXACT) == 0)) {
        hptes = ppc_hash64_map_hptes(cpu, ptex, HPTES_PER_GROUP);
        for (slot = 0; slot < 8; slot++) {
            if (!(ppc_hash64_hpte0(cpu, hptes, slot) & HPTE64_V_VALID)) {
                break;
            }
        }
        ppc_hash64_unmap_hptes(cpu, hptes, ptex, HPTES_PER_GROUP);
        if (slot == 8) {
            return H_PTEG_FULL;
        }
    } else {
        hptes = ppc_hash64_map_hptes(cpu, ptex + slot, 1);
        if (ppc_hash64_hpte0(cpu, hptes, 0) & HPTE64_V_VALID) {
            ppc_hash64_unmap_hptes(cpu, hptes, ptex + slot, 1);
            return H_PTEG_FULL;
        }
        ppc_hash64_unmap_hptes(cpu, hptes, ptex, 1);
    }

    spapr_store_hpte(cpu, ptex + slot, pteh | HPTE64_V_HPTE_DIRTY, ptel);

    args[0] = ptex + slot;
    return H_SUCCESS;
}

typedef enum {
    REMOVE_SUCCESS = 0,
    REMOVE_NOT_FOUND = 1,
    REMOVE_PARM = 2,
    REMOVE_HW = 3,
} RemoveResult;

static RemoveResult remove_hpte(PowerPCCPU *cpu
                                , target_ulong ptex,
                                target_ulong avpn,
                                target_ulong flags,
                                target_ulong *vp, target_ulong *rp)
{
    const ppc_hash_pte64_t *hptes;
    target_ulong v, r;

    if (!valid_ptex(cpu, ptex)) {
        return REMOVE_PARM;
    }

    hptes = ppc_hash64_map_hptes(cpu, ptex, 1);
    v = ppc_hash64_hpte0(cpu, hptes, 0);
    r = ppc_hash64_hpte1(cpu, hptes, 0);
    ppc_hash64_unmap_hptes(cpu, hptes, ptex, 1);

    if ((v & HPTE64_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn) ||
        ((flags & H_ANDCOND) && (v & avpn) != 0)) {
        return REMOVE_NOT_FOUND;
    }
    *vp = v;
    *rp = r;
    spapr_store_hpte(cpu, ptex, HPTE64_V_HPTE_DIRTY, 0);
    ppc_hash64_tlb_flush_hpte(cpu, ptex, v, r);
    return REMOVE_SUCCESS;
}

static target_ulong h_remove(PowerPCCPU *cpu, SpaprMachineState *spapr,
                             target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    target_ulong avpn = args[2];
    RemoveResult ret;

    ret = remove_hpte(cpu, ptex, avpn, flags,
                      &args[0], &args[1]);

    switch (ret) {
    case REMOVE_SUCCESS:
        check_tlb_flush(env, true);
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

static target_ulong h_bulk_remove(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                  target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    int i;
    target_ulong rc = H_SUCCESS;

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
            rc = H_PARAMETER;
            goto exit;

        case REMOVE_HW:
            rc = H_HARDWARE;
            goto exit;
        }
    }
 exit:
    check_tlb_flush(env, true);

    return rc;
}

static target_ulong h_protect(PowerPCCPU *cpu, SpaprMachineState *spapr,
                              target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    target_ulong avpn = args[2];
    const ppc_hash_pte64_t *hptes;
    target_ulong v, r;

    if (!valid_ptex(cpu, ptex)) {
        return H_PARAMETER;
    }

    hptes = ppc_hash64_map_hptes(cpu, ptex, 1);
    v = ppc_hash64_hpte0(cpu, hptes, 0);
    r = ppc_hash64_hpte1(cpu, hptes, 0);
    ppc_hash64_unmap_hptes(cpu, hptes, ptex, 1);

    if ((v & HPTE64_V_VALID) == 0 ||
        ((flags & H_AVPN) && (v & ~0x7fULL) != avpn)) {
        return H_NOT_FOUND;
    }

    r &= ~(HPTE64_R_PP0 | HPTE64_R_PP | HPTE64_R_N |
           HPTE64_R_KEY_HI | HPTE64_R_KEY_LO);
    r |= (flags << 55) & HPTE64_R_PP0;
    r |= (flags << 48) & HPTE64_R_KEY_HI;
    r |= flags & (HPTE64_R_PP | HPTE64_R_N | HPTE64_R_KEY_LO);
    spapr_store_hpte(cpu, ptex,
                     (v & ~HPTE64_V_VALID) | HPTE64_V_HPTE_DIRTY, 0);
    ppc_hash64_tlb_flush_hpte(cpu, ptex, v, r);
    /* Flush the tlb */
    check_tlb_flush(env, true);
    /* Don't need a memory barrier, due to qemu's global lock */
    spapr_store_hpte(cpu, ptex, v | HPTE64_V_HPTE_DIRTY, r);
    return H_SUCCESS;
}

static target_ulong h_read(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong ptex = args[1];
    int i, ridx, n_entries = 1;
    const ppc_hash_pte64_t *hptes;

    if (!valid_ptex(cpu, ptex)) {
        return H_PARAMETER;
    }

    if (flags & H_READ_4) {
        /* Clear the two low order bits */
        ptex &= ~(3ULL);
        n_entries = 4;
    }

    hptes = ppc_hash64_map_hptes(cpu, ptex, n_entries);
    for (i = 0, ridx = 0; i < n_entries; i++) {
        args[ridx++] = ppc_hash64_hpte0(cpu, hptes, i);
        args[ridx++] = ppc_hash64_hpte1(cpu, hptes, i);
    }
    ppc_hash64_unmap_hptes(cpu, hptes, ptex, n_entries);

    return H_SUCCESS;
}

struct SpaprPendingHpt {
    /* These fields are read-only after initialization */
    int shift;
    QemuThread thread;

    /* These fields are protected by the BQL */
    bool complete;

    /* These fields are private to the preparation thread if
     * !complete, otherwise protected by the BQL */
    int ret;
    void *hpt;
};

static void free_pending_hpt(SpaprPendingHpt *pending)
{
    if (pending->hpt) {
        qemu_vfree(pending->hpt);
    }

    g_free(pending);
}

static void *hpt_prepare_thread(void *opaque)
{
    SpaprPendingHpt *pending = opaque;
    size_t size = 1ULL << pending->shift;

    pending->hpt = qemu_memalign(size, size);
    if (pending->hpt) {
        memset(pending->hpt, 0, size);
        pending->ret = H_SUCCESS;
    } else {
        pending->ret = H_NO_MEM;
    }

    qemu_mutex_lock_iothread();

    if (SPAPR_MACHINE(qdev_get_machine())->pending_hpt == pending) {
        /* Ready to go */
        pending->complete = true;
    } else {
        /* We've been cancelled, clean ourselves up */
        free_pending_hpt(pending);
    }

    qemu_mutex_unlock_iothread();
    return NULL;
}

/* Must be called with BQL held */
static void cancel_hpt_prepare(SpaprMachineState *spapr)
{
    SpaprPendingHpt *pending = spapr->pending_hpt;

    /* Let the thread know it's cancelled */
    spapr->pending_hpt = NULL;

    if (!pending) {
        /* Nothing to do */
        return;
    }

    if (!pending->complete) {
        /* thread will clean itself up */
        return;
    }

    free_pending_hpt(pending);
}

/* Convert a return code from the KVM ioctl()s implementing resize HPT
 * into a PAPR hypercall return code */
static target_ulong resize_hpt_convert_rc(int ret)
{
    if (ret >= 100000) {
        return H_LONG_BUSY_ORDER_100_SEC;
    } else if (ret >= 10000) {
        return H_LONG_BUSY_ORDER_10_SEC;
    } else if (ret >= 1000) {
        return H_LONG_BUSY_ORDER_1_SEC;
    } else if (ret >= 100) {
        return H_LONG_BUSY_ORDER_100_MSEC;
    } else if (ret >= 10) {
        return H_LONG_BUSY_ORDER_10_MSEC;
    } else if (ret > 0) {
        return H_LONG_BUSY_ORDER_1_MSEC;
    }

    switch (ret) {
    case 0:
        return H_SUCCESS;
    case -EPERM:
        return H_AUTHORITY;
    case -EINVAL:
        return H_PARAMETER;
    case -ENXIO:
        return H_CLOSED;
    case -ENOSPC:
        return H_PTEG_FULL;
    case -EBUSY:
        return H_BUSY;
    case -ENOMEM:
        return H_NO_MEM;
    default:
        return H_HARDWARE;
    }
}

static target_ulong h_resize_hpt_prepare(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         target_ulong opcode,
                                         target_ulong *args)
{
    target_ulong flags = args[0];
    int shift = args[1];
    SpaprPendingHpt *pending = spapr->pending_hpt;
    uint64_t current_ram_size;
    int rc;

    if (spapr->resize_hpt == SPAPR_RESIZE_HPT_DISABLED) {
        return H_AUTHORITY;
    }

    if (!spapr->htab_shift) {
        /* Radix guest, no HPT */
        return H_NOT_AVAILABLE;
    }

    trace_spapr_h_resize_hpt_prepare(flags, shift);

    if (flags != 0) {
        return H_PARAMETER;
    }

    if (shift && ((shift < 18) || (shift > 46))) {
        return H_PARAMETER;
    }

    current_ram_size = MACHINE(spapr)->ram_size + get_plugged_memory_size();

    /* We only allow the guest to allocate an HPT one order above what
     * we'd normally give them (to stop a small guest claiming a huge
     * chunk of resources in the HPT */
    if (shift > (spapr_hpt_shift_for_ramsize(current_ram_size) + 1)) {
        return H_RESOURCE;
    }

    rc = kvmppc_resize_hpt_prepare(cpu, flags, shift);
    if (rc != -ENOSYS) {
        return resize_hpt_convert_rc(rc);
    }

    if (pending) {
        /* something already in progress */
        if (pending->shift == shift) {
            /* and it's suitable */
            if (pending->complete) {
                return pending->ret;
            } else {
                return H_LONG_BUSY_ORDER_100_MSEC;
            }
        }

        /* not suitable, cancel and replace */
        cancel_hpt_prepare(spapr);
    }

    if (!shift) {
        /* nothing to do */
        return H_SUCCESS;
    }

    /* start new prepare */

    pending = g_new0(SpaprPendingHpt, 1);
    pending->shift = shift;
    pending->ret = H_HARDWARE;

    qemu_thread_create(&pending->thread, "sPAPR HPT prepare",
                       hpt_prepare_thread, pending, QEMU_THREAD_DETACHED);

    spapr->pending_hpt = pending;

    /* In theory we could estimate the time more accurately based on
     * the new size, but there's not much point */
    return H_LONG_BUSY_ORDER_100_MSEC;
}

static uint64_t new_hpte_load0(void *htab, uint64_t pteg, int slot)
{
    uint8_t *addr = htab;

    addr += pteg * HASH_PTEG_SIZE_64;
    addr += slot * HASH_PTE_SIZE_64;
    return  ldq_p(addr);
}

static void new_hpte_store(void *htab, uint64_t pteg, int slot,
                           uint64_t pte0, uint64_t pte1)
{
    uint8_t *addr = htab;

    addr += pteg * HASH_PTEG_SIZE_64;
    addr += slot * HASH_PTE_SIZE_64;

    stq_p(addr, pte0);
    stq_p(addr + HASH_PTE_SIZE_64 / 2, pte1);
}

static int rehash_hpte(PowerPCCPU *cpu,
                       const ppc_hash_pte64_t *hptes,
                       void *old_hpt, uint64_t oldsize,
                       void *new_hpt, uint64_t newsize,
                       uint64_t pteg, int slot)
{
    uint64_t old_hash_mask = (oldsize >> 7) - 1;
    uint64_t new_hash_mask = (newsize >> 7) - 1;
    target_ulong pte0 = ppc_hash64_hpte0(cpu, hptes, slot);
    target_ulong pte1;
    uint64_t avpn;
    unsigned base_pg_shift;
    uint64_t hash, new_pteg, replace_pte0;

    if (!(pte0 & HPTE64_V_VALID) || !(pte0 & HPTE64_V_BOLTED)) {
        return H_SUCCESS;
    }

    pte1 = ppc_hash64_hpte1(cpu, hptes, slot);

    base_pg_shift = ppc_hash64_hpte_page_shift_noslb(cpu, pte0, pte1);
    assert(base_pg_shift); /* H_ENTER shouldn't allow a bad encoding */
    avpn = HPTE64_V_AVPN_VAL(pte0) & ~(((1ULL << base_pg_shift) - 1) >> 23);

    if (pte0 & HPTE64_V_SECONDARY) {
        pteg = ~pteg;
    }

    if ((pte0 & HPTE64_V_SSIZE) == HPTE64_V_SSIZE_256M) {
        uint64_t offset, vsid;

        /* We only have 28 - 23 bits of offset in avpn */
        offset = (avpn & 0x1f) << 23;
        vsid = avpn >> 5;
        /* We can find more bits from the pteg value */
        if (base_pg_shift < 23) {
            offset |= ((vsid ^ pteg) & old_hash_mask) << base_pg_shift;
        }

        hash = vsid ^ (offset >> base_pg_shift);
    } else if ((pte0 & HPTE64_V_SSIZE) == HPTE64_V_SSIZE_1T) {
        uint64_t offset, vsid;

        /* We only have 40 - 23 bits of seg_off in avpn */
        offset = (avpn & 0x1ffff) << 23;
        vsid = avpn >> 17;
        if (base_pg_shift < 23) {
            offset |= ((vsid ^ (vsid << 25) ^ pteg) & old_hash_mask)
                << base_pg_shift;
        }

        hash = vsid ^ (vsid << 25) ^ (offset >> base_pg_shift);
    } else {
        error_report("rehash_pte: Bad segment size in HPTE");
        return H_HARDWARE;
    }

    new_pteg = hash & new_hash_mask;
    if (pte0 & HPTE64_V_SECONDARY) {
        assert(~pteg == (hash & old_hash_mask));
        new_pteg = ~new_pteg;
    } else {
        assert(pteg == (hash & old_hash_mask));
    }
    assert((oldsize != newsize) || (pteg == new_pteg));
    replace_pte0 = new_hpte_load0(new_hpt, new_pteg, slot);
    /*
     * Strictly speaking, we don't need all these tests, since we only
     * ever rehash bolted HPTEs.  We might in future handle non-bolted
     * HPTEs, though so make the logic correct for those cases as
     * well.
     */
    if (replace_pte0 & HPTE64_V_VALID) {
        assert(newsize < oldsize);
        if (replace_pte0 & HPTE64_V_BOLTED) {
            if (pte0 & HPTE64_V_BOLTED) {
                /* Bolted collision, nothing we can do */
                return H_PTEG_FULL;
            } else {
                /* Discard this hpte */
                return H_SUCCESS;
            }
        }
    }

    new_hpte_store(new_hpt, new_pteg, slot, pte0, pte1);
    return H_SUCCESS;
}

static int rehash_hpt(PowerPCCPU *cpu,
                      void *old_hpt, uint64_t oldsize,
                      void *new_hpt, uint64_t newsize)
{
    uint64_t n_ptegs = oldsize >> 7;
    uint64_t pteg;
    int slot;
    int rc;

    for (pteg = 0; pteg < n_ptegs; pteg++) {
        hwaddr ptex = pteg * HPTES_PER_GROUP;
        const ppc_hash_pte64_t *hptes
            = ppc_hash64_map_hptes(cpu, ptex, HPTES_PER_GROUP);

        if (!hptes) {
            return H_HARDWARE;
        }

        for (slot = 0; slot < HPTES_PER_GROUP; slot++) {
            rc = rehash_hpte(cpu, hptes, old_hpt, oldsize, new_hpt, newsize,
                             pteg, slot);
            if (rc != H_SUCCESS) {
                ppc_hash64_unmap_hptes(cpu, hptes, ptex, HPTES_PER_GROUP);
                return rc;
            }
        }
        ppc_hash64_unmap_hptes(cpu, hptes, ptex, HPTES_PER_GROUP);
    }

    return H_SUCCESS;
}

static void do_push_sregs_to_kvm_pr(CPUState *cs, run_on_cpu_data data)
{
    int ret;

    cpu_synchronize_state(cs);

    ret = kvmppc_put_books_sregs(POWERPC_CPU(cs));
    if (ret < 0) {
        error_report("failed to push sregs to KVM: %s", strerror(-ret));
        exit(1);
    }
}

static void push_sregs_to_kvm_pr(SpaprMachineState *spapr)
{
    CPUState *cs;

    /*
     * This is a hack for the benefit of KVM PR - it abuses the SDR1
     * slot in kvm_sregs to communicate the userspace address of the
     * HPT
     */
    if (!kvm_enabled() || !spapr->htab) {
        return;
    }

    CPU_FOREACH(cs) {
        run_on_cpu(cs, do_push_sregs_to_kvm_pr, RUN_ON_CPU_NULL);
    }
}

static target_ulong h_resize_hpt_commit(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong opcode,
                                        target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong shift = args[1];
    SpaprPendingHpt *pending = spapr->pending_hpt;
    int rc;
    size_t newsize;

    if (spapr->resize_hpt == SPAPR_RESIZE_HPT_DISABLED) {
        return H_AUTHORITY;
    }

    if (!spapr->htab_shift) {
        /* Radix guest, no HPT */
        return H_NOT_AVAILABLE;
    }

    trace_spapr_h_resize_hpt_commit(flags, shift);

    rc = kvmppc_resize_hpt_commit(cpu, flags, shift);
    if (rc != -ENOSYS) {
        rc = resize_hpt_convert_rc(rc);
        if (rc == H_SUCCESS) {
            /* Need to set the new htab_shift in the machine state */
            spapr->htab_shift = shift;
        }
        return rc;
    }

    if (flags != 0) {
        return H_PARAMETER;
    }

    if (!pending || (pending->shift != shift)) {
        /* no matching prepare */
        return H_CLOSED;
    }

    if (!pending->complete) {
        /* prepare has not completed */
        return H_BUSY;
    }

    /* Shouldn't have got past PREPARE without an HPT */
    g_assert(spapr->htab_shift);

    newsize = 1ULL << pending->shift;
    rc = rehash_hpt(cpu, spapr->htab, HTAB_SIZE(spapr),
                    pending->hpt, newsize);
    if (rc == H_SUCCESS) {
        qemu_vfree(spapr->htab);
        spapr->htab = pending->hpt;
        spapr->htab_shift = pending->shift;

        push_sregs_to_kvm_pr(spapr);

        pending->hpt = NULL; /* so it's not free()d */
    }

    /* Clean up */
    spapr->pending_hpt = NULL;
    free_pending_hpt(pending);

    return rc;
}

static target_ulong h_set_sprg0(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    cpu_synchronize_state(CPU(cpu));
    cpu->env.spr[SPR_SPRG0] = args[0];

    return H_SUCCESS;
}

static target_ulong h_set_dabr(PowerPCCPU *cpu, SpaprMachineState *spapr,
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

static target_ulong h_set_xdabr(PowerPCCPU *cpu, SpaprMachineState *spapr,
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

static target_ulong h_page_init(PowerPCCPU *cpu, SpaprMachineState *spapr,
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

static target_ulong register_vpa(PowerPCCPU *cpu, target_ulong vpa)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
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

    spapr_cpu->vpa_addr = vpa;

    tmp = ldub_phys(cs->as, spapr_cpu->vpa_addr + VPA_SHARED_PROC_OFFSET);
    tmp |= VPA_SHARED_PROC_VAL;
    stb_phys(cs->as, spapr_cpu->vpa_addr + VPA_SHARED_PROC_OFFSET, tmp);

    return H_SUCCESS;
}

static target_ulong deregister_vpa(PowerPCCPU *cpu, target_ulong vpa)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    if (spapr_cpu->slb_shadow_addr) {
        return H_RESOURCE;
    }

    if (spapr_cpu->dtl_addr) {
        return H_RESOURCE;
    }

    spapr_cpu->vpa_addr = 0;
    return H_SUCCESS;
}

static target_ulong register_slb_shadow(PowerPCCPU *cpu, target_ulong addr)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    uint32_t size;

    if (addr == 0) {
        hcall_dprintf("Can't cope with SLB shadow at logical 0\n");
        return H_HARDWARE;
    }

    size = ldl_be_phys(CPU(cpu)->as, addr + 0x4);
    if (size < 0x8) {
        return H_PARAMETER;
    }

    if ((addr / 4096) != ((addr + size - 1) / 4096)) {
        return H_PARAMETER;
    }

    if (!spapr_cpu->vpa_addr) {
        return H_RESOURCE;
    }

    spapr_cpu->slb_shadow_addr = addr;
    spapr_cpu->slb_shadow_size = size;

    return H_SUCCESS;
}

static target_ulong deregister_slb_shadow(PowerPCCPU *cpu, target_ulong addr)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    spapr_cpu->slb_shadow_addr = 0;
    spapr_cpu->slb_shadow_size = 0;
    return H_SUCCESS;
}

static target_ulong register_dtl(PowerPCCPU *cpu, target_ulong addr)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);
    uint32_t size;

    if (addr == 0) {
        hcall_dprintf("Can't cope with DTL at logical 0\n");
        return H_HARDWARE;
    }

    size = ldl_be_phys(CPU(cpu)->as, addr + 0x4);

    if (size < 48) {
        return H_PARAMETER;
    }

    if (!spapr_cpu->vpa_addr) {
        return H_RESOURCE;
    }

    spapr_cpu->dtl_addr = addr;
    spapr_cpu->dtl_size = size;

    return H_SUCCESS;
}

static target_ulong deregister_dtl(PowerPCCPU *cpu, target_ulong addr)
{
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    spapr_cpu->dtl_addr = 0;
    spapr_cpu->dtl_size = 0;

    return H_SUCCESS;
}

static target_ulong h_register_vpa(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong procno = args[1];
    target_ulong vpa = args[2];
    target_ulong ret = H_PARAMETER;
    PowerPCCPU *tcpu;

    tcpu = spapr_find_cpu(procno);
    if (!tcpu) {
        return H_PARAMETER;
    }

    switch (flags) {
    case FLAGS_REGISTER_VPA:
        ret = register_vpa(tcpu, vpa);
        break;

    case FLAGS_DEREGISTER_VPA:
        ret = deregister_vpa(tcpu, vpa);
        break;

    case FLAGS_REGISTER_SLBSHADOW:
        ret = register_slb_shadow(tcpu, vpa);
        break;

    case FLAGS_DEREGISTER_SLBSHADOW:
        ret = deregister_slb_shadow(tcpu, vpa);
        break;

    case FLAGS_REGISTER_DTL:
        ret = register_dtl(tcpu, vpa);
        break;

    case FLAGS_DEREGISTER_DTL:
        ret = deregister_dtl(tcpu, vpa);
        break;
    }

    return ret;
}

static target_ulong h_cede(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    env->msr |= (1ULL << MSR_EE);
    hreg_compute_hflags(env);

    if (spapr_cpu->prod) {
        spapr_cpu->prod = false;
        return H_SUCCESS;
    }

    if (!cpu_has_work(cs)) {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cs->exit_request = 1;
    }

    return H_SUCCESS;
}

/*
 * Confer to self, aka join. Cede could use the same pattern as well, if
 * EXCP_HLT can be changed to ECXP_HALTED.
 */
static target_ulong h_confer_self(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    if (spapr_cpu->prod) {
        spapr_cpu->prod = false;
        return H_SUCCESS;
    }
    cs->halted = 1;
    cs->exception_index = EXCP_HALTED;
    cs->exit_request = 1;

    return H_SUCCESS;
}

static target_ulong h_join(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    CPUPPCState *env = &cpu->env;
    CPUState *cs;
    bool last_unjoined = true;

    if (env->msr & (1ULL << MSR_EE)) {
        return H_BAD_MODE;
    }

    /*
     * Must not join the last CPU running. Interestingly, no such restriction
     * for H_CONFER-to-self, but that is probably not intended to be used
     * when H_JOIN is available.
     */
    CPU_FOREACH(cs) {
        PowerPCCPU *c = POWERPC_CPU(cs);
        CPUPPCState *e = &c->env;
        if (c == cpu) {
            continue;
        }

        /* Don't have a way to indicate joined, so use halted && MSR[EE]=0 */
        if (!cs->halted || (e->msr & (1ULL << MSR_EE))) {
            last_unjoined = false;
            break;
        }
    }
    if (last_unjoined) {
        return H_CONTINUE;
    }

    return h_confer_self(cpu);
}

static target_ulong h_confer(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_long target = args[0];
    uint32_t dispatch = args[1];
    CPUState *cs = CPU(cpu);
    SpaprCpuState *spapr_cpu;

    /*
     * -1 means confer to all other CPUs without dispatch counter check,
     *  otherwise it's a targeted confer.
     */
    if (target != -1) {
        PowerPCCPU *target_cpu = spapr_find_cpu(target);
        uint32_t target_dispatch;

        if (!target_cpu) {
            return H_PARAMETER;
        }

        /*
         * target == self is a special case, we wait until prodded, without
         * dispatch counter check.
         */
        if (cpu == target_cpu) {
            return h_confer_self(cpu);
        }

        spapr_cpu = spapr_cpu_state(target_cpu);
        if (!spapr_cpu->vpa_addr || ((dispatch & 1) == 0)) {
            return H_SUCCESS;
        }

        target_dispatch = ldl_be_phys(cs->as,
                                  spapr_cpu->vpa_addr + VPA_DISPATCH_COUNTER);
        if (target_dispatch != dispatch) {
            return H_SUCCESS;
        }

        /*
         * The targeted confer does not do anything special beyond yielding
         * the current vCPU, but even this should be better than nothing.
         * At least for single-threaded tcg, it gives the target a chance to
         * run before we run again. Multi-threaded tcg does not really do
         * anything with EXCP_YIELD yet.
         */
    }

    cs->exception_index = EXCP_YIELD;
    cs->exit_request = 1;
    cpu_loop_exit(cs);

    return H_SUCCESS;
}

static target_ulong h_prod(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_long target = args[0];
    PowerPCCPU *tcpu;
    CPUState *cs;
    SpaprCpuState *spapr_cpu;

    tcpu = spapr_find_cpu(target);
    cs = CPU(tcpu);
    if (!cs) {
        return H_PARAMETER;
    }

    spapr_cpu = spapr_cpu_state(tcpu);
    spapr_cpu->prod = true;
    cs->halted = 0;
    qemu_cpu_kick(cs);

    return H_SUCCESS;
}

static target_ulong h_rtas(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_ulong rtas_r3 = args[0];
    uint32_t token = rtas_ld(rtas_r3, 0);
    uint32_t nargs = rtas_ld(rtas_r3, 1);
    uint32_t nret = rtas_ld(rtas_r3, 2);

    return spapr_rtas_call(cpu, spapr, token, nargs, rtas_r3 + 12,
                           nret, rtas_r3 + 12 + 4*nargs);
}

static target_ulong h_logical_load(PowerPCCPU *cpu, SpaprMachineState *spapr,
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

static target_ulong h_logical_store(PowerPCCPU *cpu, SpaprMachineState *spapr,
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

static target_ulong h_logical_memop(PowerPCCPU *cpu, SpaprMachineState *spapr,
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

static target_ulong h_logical_icbi(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                   target_ulong opcode, target_ulong *args)
{
    /* Nothing to do on emulation, KVM will trap this in the kernel */
    return H_SUCCESS;
}

static target_ulong h_logical_dcbf(PowerPCCPU *cpu, SpaprMachineState *spapr,
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
    if (value1) {
        return H_P3;
    }
    if (value2) {
        return H_P4;
    }

    switch (mflags) {
    case H_SET_MODE_ENDIAN_BIG:
        spapr_set_all_lpcrs(0, LPCR_ILE);
        spapr_pci_switch_vga(true);
        return H_SUCCESS;

    case H_SET_MODE_ENDIAN_LITTLE:
        spapr_set_all_lpcrs(LPCR_ILE, LPCR_ILE);
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

    spapr_set_all_lpcrs(mflags << LPCR_AIL_SHIFT, LPCR_AIL);

    return H_SUCCESS;
}

static target_ulong h_set_mode(PowerPCCPU *cpu, SpaprMachineState *spapr,
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

static target_ulong h_clean_slb(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    qemu_log_mask(LOG_UNIMP, "Unimplemented SPAPR hcall 0x"TARGET_FMT_lx"%s\n",
                  opcode, " (H_CLEAN_SLB)");
    return H_FUNCTION;
}

static target_ulong h_invalidate_pid(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                     target_ulong opcode, target_ulong *args)
{
    qemu_log_mask(LOG_UNIMP, "Unimplemented SPAPR hcall 0x"TARGET_FMT_lx"%s\n",
                  opcode, " (H_INVALIDATE_PID)");
    return H_FUNCTION;
}

static void spapr_check_setup_free_hpt(SpaprMachineState *spapr,
                                       uint64_t patbe_old, uint64_t patbe_new)
{
    /*
     * We have 4 Options:
     * HASH->HASH || RADIX->RADIX || NOTHING->RADIX : Do Nothing
     * HASH->RADIX                                  : Free HPT
     * RADIX->HASH                                  : Allocate HPT
     * NOTHING->HASH                                : Allocate HPT
     * Note: NOTHING implies the case where we said the guest could choose
     *       later and so assumed radix and now it's called H_REG_PROC_TBL
     */

    if ((patbe_old & PATE1_GR) == (patbe_new & PATE1_GR)) {
        /* We assume RADIX, so this catches all the "Do Nothing" cases */
    } else if (!(patbe_old & PATE1_GR)) {
        /* HASH->RADIX : Free HPT */
        spapr_free_hpt(spapr);
    } else if (!(patbe_new & PATE1_GR)) {
        /* RADIX->HASH || NOTHING->HASH : Allocate HPT */
        spapr_setup_hpt_and_vrma(spapr);
    }
    return;
}

#define FLAGS_MASK              0x01FULL
#define FLAG_MODIFY             0x10
#define FLAG_REGISTER           0x08
#define FLAG_RADIX              0x04
#define FLAG_HASH_PROC_TBL      0x02
#define FLAG_GTSE               0x01

static target_ulong h_register_process_table(PowerPCCPU *cpu,
                                             SpaprMachineState *spapr,
                                             target_ulong opcode,
                                             target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong proc_tbl = args[1];
    target_ulong page_size = args[2];
    target_ulong table_size = args[3];
    target_ulong update_lpcr = 0;
    uint64_t cproc;

    if (flags & ~FLAGS_MASK) { /* Check no reserved bits are set */
        return H_PARAMETER;
    }
    if (flags & FLAG_MODIFY) {
        if (flags & FLAG_REGISTER) {
            if (flags & FLAG_RADIX) { /* Register new RADIX process table */
                if (proc_tbl & 0xfff || proc_tbl >> 60) {
                    return H_P2;
                } else if (page_size) {
                    return H_P3;
                } else if (table_size > 24) {
                    return H_P4;
                }
                cproc = PATE1_GR | proc_tbl | table_size;
            } else { /* Register new HPT process table */
                if (flags & FLAG_HASH_PROC_TBL) { /* Hash with Segment Tables */
                    /* TODO - Not Supported */
                    /* Technically caused by flag bits => H_PARAMETER */
                    return H_PARAMETER;
                } else { /* Hash with SLB */
                    if (proc_tbl >> 38) {
                        return H_P2;
                    } else if (page_size & ~0x7) {
                        return H_P3;
                    } else if (table_size > 24) {
                        return H_P4;
                    }
                }
                cproc = (proc_tbl << 25) | page_size << 5 | table_size;
            }

        } else { /* Deregister current process table */
            /*
             * Set to benign value: (current GR) | 0. This allows
             * deregistration in KVM to succeed even if the radix bit
             * in flags doesn't match the radix bit in the old PATE.
             */
            cproc = spapr->patb_entry & PATE1_GR;
        }
    } else { /* Maintain current registration */
        if (!(flags & FLAG_RADIX) != !(spapr->patb_entry & PATE1_GR)) {
            /* Technically caused by flag bits => H_PARAMETER */
            return H_PARAMETER; /* Existing Process Table Mismatch */
        }
        cproc = spapr->patb_entry;
    }

    /* Check if we need to setup OR free the hpt */
    spapr_check_setup_free_hpt(spapr, spapr->patb_entry, cproc);

    spapr->patb_entry = cproc; /* Save new process table */

    /* Update the UPRT, HR and GTSE bits in the LPCR for all cpus */
    if (flags & FLAG_RADIX)     /* Radix must use process tables, also set HR */
        update_lpcr |= (LPCR_UPRT | LPCR_HR);
    else if (flags & FLAG_HASH_PROC_TBL) /* Hash with process tables */
        update_lpcr |= LPCR_UPRT;
    if (flags & FLAG_GTSE)      /* Guest translation shootdown enable */
        update_lpcr |= LPCR_GTSE;

    spapr_set_all_lpcrs(update_lpcr, LPCR_UPRT | LPCR_HR | LPCR_GTSE);

    if (kvm_enabled()) {
        return kvmppc_configure_v3_mmu(cpu, flags & FLAG_RADIX,
                                       flags & FLAG_GTSE, cproc);
    }
    return H_SUCCESS;
}

#define H_SIGNAL_SYS_RESET_ALL         -1
#define H_SIGNAL_SYS_RESET_ALLBUTSELF  -2

static target_ulong h_signal_sys_reset(PowerPCCPU *cpu,
                                       SpaprMachineState *spapr,
                                       target_ulong opcode, target_ulong *args)
{
    target_long target = args[0];
    CPUState *cs;

    if (target < 0) {
        /* Broadcast */
        if (target < H_SIGNAL_SYS_RESET_ALLBUTSELF) {
            return H_PARAMETER;
        }

        CPU_FOREACH(cs) {
            PowerPCCPU *c = POWERPC_CPU(cs);

            if (target == H_SIGNAL_SYS_RESET_ALLBUTSELF) {
                if (c == cpu) {
                    continue;
                }
            }
            run_on_cpu(cs, spapr_do_system_reset_on_cpu, RUN_ON_CPU_NULL);
        }
        return H_SUCCESS;

    } else {
        /* Unicast */
        cs = CPU(spapr_find_cpu(target));
        if (cs) {
            run_on_cpu(cs, spapr_do_system_reset_on_cpu, RUN_ON_CPU_NULL);
            return H_SUCCESS;
        }
        return H_PARAMETER;
    }
}

static uint32_t cas_check_pvr(SpaprMachineState *spapr, PowerPCCPU *cpu,
                              target_ulong *addr, bool *raw_mode_supported,
                              Error **errp)
{
    bool explicit_match = false; /* Matched the CPU's real PVR */
    uint32_t max_compat = spapr->max_compat_pvr;
    uint32_t best_compat = 0;
    int i;

    /*
     * We scan the supplied table of PVRs looking for two things
     *   1. Is our real CPU PVR in the list?
     *   2. What's the "best" listed logical PVR
     */
    for (i = 0; i < 512; ++i) {
        uint32_t pvr, pvr_mask;

        pvr_mask = ldl_be_phys(&address_space_memory, *addr);
        pvr = ldl_be_phys(&address_space_memory, *addr + 4);
        *addr += 8;

        if (~pvr_mask & pvr) {
            break; /* Terminator record */
        }

        if ((cpu->env.spr[SPR_PVR] & pvr_mask) == (pvr & pvr_mask)) {
            explicit_match = true;
        } else {
            if (ppc_check_compat(cpu, pvr, best_compat, max_compat)) {
                best_compat = pvr;
            }
        }
    }

    if ((best_compat == 0) && (!explicit_match || max_compat)) {
        /* We couldn't find a suitable compatibility mode, and either
         * the guest doesn't support "raw" mode for this CPU, or raw
         * mode is disabled because a maximum compat mode is set */
        error_setg(errp, "Couldn't negotiate a suitable PVR during CAS");
        return 0;
    }

    *raw_mode_supported = explicit_match;

    /* Parsing finished */
    trace_spapr_cas_pvr(cpu->compat_pvr, explicit_match, best_compat);

    return best_compat;
}

static target_ulong h_client_architecture_support(PowerPCCPU *cpu,
                                                  SpaprMachineState *spapr,
                                                  target_ulong opcode,
                                                  target_ulong *args)
{
    /* Working address in data buffer */
    target_ulong addr = ppc64_phys_to_real(args[0]);
    target_ulong ov_table;
    uint32_t cas_pvr;
    SpaprOptionVector *ov1_guest, *ov5_guest, *ov5_cas_old, *ov5_updates;
    bool guest_radix;
    Error *local_err = NULL;
    bool raw_mode_supported = false;
    bool guest_xive;

    cas_pvr = cas_check_pvr(spapr, cpu, &addr, &raw_mode_supported, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return H_HARDWARE;
    }

    /* Update CPUs */
    if (cpu->compat_pvr != cas_pvr) {
        ppc_set_compat_all(cas_pvr, &local_err);
        if (local_err) {
            /* We fail to set compat mode (likely because running with KVM PR),
             * but maybe we can fallback to raw mode if the guest supports it.
             */
            if (!raw_mode_supported) {
                error_report_err(local_err);
                return H_HARDWARE;
            }
            error_free(local_err);
            local_err = NULL;
        }
    }

    /* For the future use: here @ov_table points to the first option vector */
    ov_table = addr;

    ov1_guest = spapr_ovec_parse_vector(ov_table, 1);
    ov5_guest = spapr_ovec_parse_vector(ov_table, 5);
    if (spapr_ovec_test(ov5_guest, OV5_MMU_BOTH)) {
        error_report("guest requested hash and radix MMU, which is invalid.");
        exit(EXIT_FAILURE);
    }
    if (spapr_ovec_test(ov5_guest, OV5_XIVE_BOTH)) {
        error_report("guest requested an invalid interrupt mode");
        exit(EXIT_FAILURE);
    }

    /* The radix/hash bit in byte 24 requires special handling: */
    guest_radix = spapr_ovec_test(ov5_guest, OV5_MMU_RADIX_300);
    spapr_ovec_clear(ov5_guest, OV5_MMU_RADIX_300);

    guest_xive = spapr_ovec_test(ov5_guest, OV5_XIVE_EXPLOIT);

    /*
     * HPT resizing is a bit of a special case, because when enabled
     * we assume an HPT guest will support it until it says it
     * doesn't, instead of assuming it won't support it until it says
     * it does.  Strictly speaking that approach could break for
     * guests which don't make a CAS call, but those are so old we
     * don't care about them.  Without that assumption we'd have to
     * make at least a temporary allocation of an HPT sized for max
     * memory, which could be impossibly difficult under KVM HV if
     * maxram is large.
     */
    if (!guest_radix && !spapr_ovec_test(ov5_guest, OV5_HPT_RESIZE)) {
        int maxshift = spapr_hpt_shift_for_ramsize(MACHINE(spapr)->maxram_size);

        if (spapr->resize_hpt == SPAPR_RESIZE_HPT_REQUIRED) {
            error_report(
                "h_client_architecture_support: Guest doesn't support HPT resizing, but resize-hpt=required");
            exit(1);
        }

        if (spapr->htab_shift < maxshift) {
            /* Guest doesn't know about HPT resizing, so we
             * pre-emptively resize for the maximum permitted RAM.  At
             * the point this is called, nothing should have been
             * entered into the existing HPT */
            spapr_reallocate_hpt(spapr, maxshift, &error_fatal);
            push_sregs_to_kvm_pr(spapr);
        }
    }

    /* NOTE: there are actually a number of ov5 bits where input from the
     * guest is always zero, and the platform/QEMU enables them independently
     * of guest input. To model these properly we'd want some sort of mask,
     * but since they only currently apply to memory migration as defined
     * by LoPAPR 1.1, 14.5.4.8, which QEMU doesn't implement, we don't need
     * to worry about this for now.
     */
    ov5_cas_old = spapr_ovec_clone(spapr->ov5_cas);

    /* also clear the radix/hash bit from the current ov5_cas bits to
     * be in sync with the newly ov5 bits. Else the radix bit will be
     * seen as being removed and this will generate a reset loop
     */
    spapr_ovec_clear(ov5_cas_old, OV5_MMU_RADIX_300);

    /* full range of negotiated ov5 capabilities */
    spapr_ovec_intersect(spapr->ov5_cas, spapr->ov5, ov5_guest);
    spapr_ovec_cleanup(ov5_guest);
    /* capabilities that have been added since CAS-generated guest reset.
     * if capabilities have since been removed, generate another reset
     */
    ov5_updates = spapr_ovec_new();
    spapr->cas_reboot = spapr_ovec_diff(ov5_updates,
                                        ov5_cas_old, spapr->ov5_cas);
    spapr_ovec_cleanup(ov5_cas_old);
    /* Now that processing is finished, set the radix/hash bit for the
     * guest if it requested a valid mode; otherwise terminate the boot. */
    if (guest_radix) {
        if (kvm_enabled() && !kvmppc_has_cap_mmu_radix()) {
            error_report("Guest requested unavailable MMU mode (radix).");
            exit(EXIT_FAILURE);
        }
        spapr_ovec_set(spapr->ov5_cas, OV5_MMU_RADIX_300);
    } else {
        if (kvm_enabled() && kvmppc_has_cap_mmu_radix()
            && !kvmppc_has_cap_mmu_hash_v3()) {
            error_report("Guest requested unavailable MMU mode (hash).");
            exit(EXIT_FAILURE);
        }
    }
    spapr->cas_pre_isa3_guest = !spapr_ovec_test(ov1_guest, OV1_PPC_3_00);
    spapr_ovec_cleanup(ov1_guest);
    if (!spapr->cas_reboot) {
        /* If spapr_machine_reset() did not set up a HPT but one is necessary
         * (because the guest isn't going to use radix) then set it up here. */
        if ((spapr->patb_entry & PATE1_GR) && !guest_radix) {
            /* legacy hash or new hash: */
            spapr_setup_hpt_and_vrma(spapr);
        }
        spapr->cas_reboot =
            (spapr_h_cas_compose_response(spapr, args[1], args[2],
                                          ov5_updates) != 0);
    }

    /*
     * Ensure the guest asks for an interrupt mode we support; otherwise
     * terminate the boot.
     */
    if (guest_xive) {
        if (!spapr->irq->xive) {
            error_report(
"Guest requested unavailable interrupt mode (XIVE), try the ic-mode=xive or ic-mode=dual machine property");
            exit(EXIT_FAILURE);
        }
    } else {
        if (!spapr->irq->xics) {
            error_report(
"Guest requested unavailable interrupt mode (XICS), either don't set the ic-mode machine property or try ic-mode=xics or ic-mode=dual");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Generate a machine reset when we have an update of the
     * interrupt mode. Only required when the machine supports both
     * modes.
     */
    if (!spapr->cas_reboot) {
        spapr->cas_reboot = spapr_ovec_test(ov5_updates, OV5_XIVE_EXPLOIT)
            && spapr->irq->xics && spapr->irq->xive;
    }

    spapr_ovec_cleanup(ov5_updates);

    if (spapr->cas_reboot) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
    }

    return H_SUCCESS;
}

static target_ulong h_home_node_associativity(PowerPCCPU *cpu,
                                              SpaprMachineState *spapr,
                                              target_ulong opcode,
                                              target_ulong *args)
{
    target_ulong flags = args[0];
    target_ulong procno = args[1];
    PowerPCCPU *tcpu;
    int idx;

    /* only support procno from H_REGISTER_VPA */
    if (flags != 0x1) {
        return H_FUNCTION;
    }

    tcpu = spapr_find_cpu(procno);
    if (tcpu == NULL) {
        return H_P2;
    }

    /* sequence is the same as in the "ibm,associativity" property */

    idx = 0;
#define ASSOCIATIVITY(a, b) (((uint64_t)(a) << 32) | \
                             ((uint64_t)(b) & 0xffffffff))
    args[idx++] = ASSOCIATIVITY(0, 0);
    args[idx++] = ASSOCIATIVITY(0, tcpu->node_id);
    args[idx++] = ASSOCIATIVITY(procno, -1);
    for ( ; idx < 6; idx++) {
        args[idx] = -1;
    }
#undef ASSOCIATIVITY

    return H_SUCCESS;
}

static target_ulong h_get_cpu_characteristics(PowerPCCPU *cpu,
                                              SpaprMachineState *spapr,
                                              target_ulong opcode,
                                              target_ulong *args)
{
    uint64_t characteristics = H_CPU_CHAR_HON_BRANCH_HINTS &
                               ~H_CPU_CHAR_THR_RECONF_TRIG;
    uint64_t behaviour = H_CPU_BEHAV_FAVOUR_SECURITY;
    uint8_t safe_cache = spapr_get_cap(spapr, SPAPR_CAP_CFPC);
    uint8_t safe_bounds_check = spapr_get_cap(spapr, SPAPR_CAP_SBBC);
    uint8_t safe_indirect_branch = spapr_get_cap(spapr, SPAPR_CAP_IBS);
    uint8_t count_cache_flush_assist = spapr_get_cap(spapr,
                                                     SPAPR_CAP_CCF_ASSIST);

    switch (safe_cache) {
    case SPAPR_CAP_WORKAROUND:
        characteristics |= H_CPU_CHAR_L1D_FLUSH_ORI30;
        characteristics |= H_CPU_CHAR_L1D_FLUSH_TRIG2;
        characteristics |= H_CPU_CHAR_L1D_THREAD_PRIV;
        behaviour |= H_CPU_BEHAV_L1D_FLUSH_PR;
        break;
    case SPAPR_CAP_FIXED:
        break;
    default: /* broken */
        assert(safe_cache == SPAPR_CAP_BROKEN);
        behaviour |= H_CPU_BEHAV_L1D_FLUSH_PR;
        break;
    }

    switch (safe_bounds_check) {
    case SPAPR_CAP_WORKAROUND:
        characteristics |= H_CPU_CHAR_SPEC_BAR_ORI31;
        behaviour |= H_CPU_BEHAV_BNDS_CHK_SPEC_BAR;
        break;
    case SPAPR_CAP_FIXED:
        break;
    default: /* broken */
        assert(safe_bounds_check == SPAPR_CAP_BROKEN);
        behaviour |= H_CPU_BEHAV_BNDS_CHK_SPEC_BAR;
        break;
    }

    switch (safe_indirect_branch) {
    case SPAPR_CAP_FIXED_NA:
        break;
    case SPAPR_CAP_FIXED_CCD:
        characteristics |= H_CPU_CHAR_CACHE_COUNT_DIS;
        break;
    case SPAPR_CAP_FIXED_IBS:
        characteristics |= H_CPU_CHAR_BCCTRL_SERIALISED;
        break;
    case SPAPR_CAP_WORKAROUND:
        behaviour |= H_CPU_BEHAV_FLUSH_COUNT_CACHE;
        if (count_cache_flush_assist) {
            characteristics |= H_CPU_CHAR_BCCTR_FLUSH_ASSIST;
        }
        break;
    default: /* broken */
        assert(safe_indirect_branch == SPAPR_CAP_BROKEN);
        break;
    }

    args[0] = characteristics;
    args[1] = behaviour;
    return H_SUCCESS;
}

static target_ulong h_update_dt(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                target_ulong opcode, target_ulong *args)
{
    target_ulong dt = ppc64_phys_to_real(args[0]);
    struct fdt_header hdr = { 0 };
    unsigned cb;
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    void *fdt;

    cpu_physical_memory_read(dt, &hdr, sizeof(hdr));
    cb = fdt32_to_cpu(hdr.totalsize);

    if (!smc->update_dt_enabled) {
        return H_SUCCESS;
    }

    /* Check that the fdt did not grow out of proportion */
    if (cb > spapr->fdt_initial_size * 2) {
        trace_spapr_update_dt_failed_size(spapr->fdt_initial_size, cb,
                                          fdt32_to_cpu(hdr.magic));
        return H_PARAMETER;
    }

    fdt = g_malloc0(cb);
    cpu_physical_memory_read(dt, fdt, cb);

    /* Check the fdt consistency */
    if (fdt_check_full(fdt, cb)) {
        trace_spapr_update_dt_failed_check(spapr->fdt_initial_size, cb,
                                           fdt32_to_cpu(hdr.magic));
        return H_PARAMETER;
    }

    g_free(spapr->fdt_blob);
    spapr->fdt_size = cb;
    spapr->fdt_blob = fdt;
    trace_spapr_update_dt(cb);

    return H_SUCCESS;
}

static spapr_hcall_fn papr_hypercall_table[(MAX_HCALL_OPCODE / 4) + 1];
static spapr_hcall_fn kvmppc_hypercall_table[KVMPPC_HCALL_MAX - KVMPPC_HCALL_BASE + 1];
static spapr_hcall_fn svm_hypercall_table[(SVM_HCALL_MAX - SVM_HCALL_BASE) / 4 + 1];

void spapr_register_hypercall(target_ulong opcode, spapr_hcall_fn fn)
{
    spapr_hcall_fn *slot;

    if (opcode <= MAX_HCALL_OPCODE) {
        assert((opcode & 0x3) == 0);

        slot = &papr_hypercall_table[opcode / 4];
    } else if (opcode >= SVM_HCALL_BASE && opcode <= SVM_HCALL_MAX) {
        /* we only have SVM-related hcall numbers assigned in multiples of 4 */
        assert((opcode & 0x3) == 0);

        slot = &svm_hypercall_table[(opcode - SVM_HCALL_BASE) / 4];
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
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());

    if ((opcode <= MAX_HCALL_OPCODE)
        && ((opcode & 0x3) == 0)) {
        spapr_hcall_fn fn = papr_hypercall_table[opcode / 4];

        if (fn) {
            return fn(cpu, spapr, opcode, args);
        }
    } else if ((opcode >= SVM_HCALL_BASE) &&
               (opcode <= SVM_HCALL_MAX)) {
        spapr_hcall_fn fn = svm_hypercall_table[(opcode - SVM_HCALL_BASE) / 4];

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

    /* hcall-hpt-resize */
    spapr_register_hypercall(H_RESIZE_HPT_PREPARE, h_resize_hpt_prepare);
    spapr_register_hypercall(H_RESIZE_HPT_COMMIT, h_resize_hpt_commit);

    /* hcall-splpar */
    spapr_register_hypercall(H_REGISTER_VPA, h_register_vpa);
    spapr_register_hypercall(H_CEDE, h_cede);
    spapr_register_hypercall(H_CONFER, h_confer);
    spapr_register_hypercall(H_PROD, h_prod);

    /* hcall-join */
    spapr_register_hypercall(H_JOIN, h_join);

    spapr_register_hypercall(H_SIGNAL_SYS_RESET, h_signal_sys_reset);

    /* processor register resource access h-calls */
    spapr_register_hypercall(H_SET_SPRG0, h_set_sprg0);
    spapr_register_hypercall(H_SET_DABR, h_set_dabr);
    spapr_register_hypercall(H_SET_XDABR, h_set_xdabr);
    spapr_register_hypercall(H_PAGE_INIT, h_page_init);
    spapr_register_hypercall(H_SET_MODE, h_set_mode);

    /* In Memory Table MMU h-calls */
    spapr_register_hypercall(H_CLEAN_SLB, h_clean_slb);
    spapr_register_hypercall(H_INVALIDATE_PID, h_invalidate_pid);
    spapr_register_hypercall(H_REGISTER_PROC_TBL, h_register_process_table);

    /* hcall-get-cpu-characteristics */
    spapr_register_hypercall(H_GET_CPU_CHARACTERISTICS,
                             h_get_cpu_characteristics);

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

    spapr_register_hypercall(KVMPPC_H_UPDATE_DT, h_update_dt);

    /* Virtual Processor Home Node */
    spapr_register_hypercall(H_HOME_NODE_ASSOCIATIVITY,
                             h_home_node_associativity);
}

type_init(hypercall_register_types)
