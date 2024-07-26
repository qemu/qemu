/*
 * MMU hypercalls for the sPAPR (pseries) vHyp hypervisor that is used by TCG
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2010 David Gibson, IBM Corporation.
 *
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "helper_regs.h"
#include "hw/ppc/spapr.h"
#include "mmu-hash64.h"

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

    if (!ppc_hash64_valid_ptex(cpu, ptex)) {
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

    if (!ppc_hash64_valid_ptex(cpu, ptex)) {
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

    if (!ppc_hash64_valid_ptex(cpu, ptex)) {
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

    if (!ppc_hash64_valid_ptex(cpu, ptex)) {
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

    pending->hpt = qemu_try_memalign(size, size);
    if (pending->hpt) {
        memset(pending->hpt, 0, size);
        pending->ret = H_SUCCESS;
    } else {
        pending->ret = H_NO_MEM;
    }

    bql_lock();

    if (SPAPR_MACHINE(qdev_get_machine())->pending_hpt == pending) {
        /* Ready to go */
        pending->complete = true;
    } else {
        /* We've been cancelled, clean ourselves up */
        free_pending_hpt(pending);
    }

    bql_unlock();
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

target_ulong vhyp_mmu_resize_hpt_prepare(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         target_ulong shift)
{
    SpaprPendingHpt *pending = spapr->pending_hpt;

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
    stq_p(addr + HPTE64_DW1, pte1);
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

target_ulong vhyp_mmu_resize_hpt_commit(PowerPCCPU *cpu,
                                        SpaprMachineState *spapr,
                                        target_ulong flags,
                                        target_ulong shift)
{
    SpaprPendingHpt *pending = spapr->pending_hpt;
    int rc;
    size_t newsize;

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

static void hypercall_register_types(void)
{
    /* hcall-pft */
    spapr_register_hypercall(H_ENTER, h_enter);
    spapr_register_hypercall(H_REMOVE, h_remove);
    spapr_register_hypercall(H_PROTECT, h_protect);
    spapr_register_hypercall(H_READ, h_read);

    /* hcall-bulk */
    spapr_register_hypercall(H_BULK_REMOVE, h_bulk_remove);

}

type_init(hypercall_register_types)
