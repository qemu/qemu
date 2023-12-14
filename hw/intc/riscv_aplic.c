/*
 * RISC-V APLIC (Advanced Platform Level Interrupt Controller)
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/pci/msi.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/irq.h"
#include "target/riscv/cpu.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm/kvm_riscv.h"
#include "migration/vmstate.h"

#define APLIC_MAX_IDC                  (1UL << 14)
#define APLIC_MAX_SOURCE               1024
#define APLIC_MIN_IPRIO_BITS           1
#define APLIC_MAX_IPRIO_BITS           8
#define APLIC_MAX_CHILDREN             1024

#define APLIC_DOMAINCFG                0x0000
#define APLIC_DOMAINCFG_RDONLY         0x80000000
#define APLIC_DOMAINCFG_IE             (1 << 8)
#define APLIC_DOMAINCFG_DM             (1 << 2)
#define APLIC_DOMAINCFG_BE             (1 << 0)

#define APLIC_SOURCECFG_BASE           0x0004
#define APLIC_SOURCECFG_D              (1 << 10)
#define APLIC_SOURCECFG_CHILDIDX_MASK  0x000003ff
#define APLIC_SOURCECFG_SM_MASK        0x00000007
#define APLIC_SOURCECFG_SM_INACTIVE    0x0
#define APLIC_SOURCECFG_SM_DETACH      0x1
#define APLIC_SOURCECFG_SM_EDGE_RISE   0x4
#define APLIC_SOURCECFG_SM_EDGE_FALL   0x5
#define APLIC_SOURCECFG_SM_LEVEL_HIGH  0x6
#define APLIC_SOURCECFG_SM_LEVEL_LOW   0x7

#define APLIC_MMSICFGADDR              0x1bc0
#define APLIC_MMSICFGADDRH             0x1bc4
#define APLIC_SMSICFGADDR              0x1bc8
#define APLIC_SMSICFGADDRH             0x1bcc

#define APLIC_xMSICFGADDRH_L           (1UL << 31)
#define APLIC_xMSICFGADDRH_HHXS_MASK   0x1f
#define APLIC_xMSICFGADDRH_HHXS_SHIFT  24
#define APLIC_xMSICFGADDRH_LHXS_MASK   0x7
#define APLIC_xMSICFGADDRH_LHXS_SHIFT  20
#define APLIC_xMSICFGADDRH_HHXW_MASK   0x7
#define APLIC_xMSICFGADDRH_HHXW_SHIFT  16
#define APLIC_xMSICFGADDRH_LHXW_MASK   0xf
#define APLIC_xMSICFGADDRH_LHXW_SHIFT  12
#define APLIC_xMSICFGADDRH_BAPPN_MASK  0xfff

#define APLIC_xMSICFGADDR_PPN_SHIFT    12

#define APLIC_xMSICFGADDR_PPN_HART(__lhxs) \
    ((1UL << (__lhxs)) - 1)

#define APLIC_xMSICFGADDR_PPN_LHX_MASK(__lhxw) \
    ((1UL << (__lhxw)) - 1)
#define APLIC_xMSICFGADDR_PPN_LHX_SHIFT(__lhxs) \
    ((__lhxs))
#define APLIC_xMSICFGADDR_PPN_LHX(__lhxw, __lhxs) \
    (APLIC_xMSICFGADDR_PPN_LHX_MASK(__lhxw) << \
     APLIC_xMSICFGADDR_PPN_LHX_SHIFT(__lhxs))

#define APLIC_xMSICFGADDR_PPN_HHX_MASK(__hhxw) \
    ((1UL << (__hhxw)) - 1)
#define APLIC_xMSICFGADDR_PPN_HHX_SHIFT(__hhxs) \
    ((__hhxs) + APLIC_xMSICFGADDR_PPN_SHIFT)
#define APLIC_xMSICFGADDR_PPN_HHX(__hhxw, __hhxs) \
    (APLIC_xMSICFGADDR_PPN_HHX_MASK(__hhxw) << \
     APLIC_xMSICFGADDR_PPN_HHX_SHIFT(__hhxs))

#define APLIC_xMSICFGADDRH_VALID_MASK   \
    (APLIC_xMSICFGADDRH_L | \
     (APLIC_xMSICFGADDRH_HHXS_MASK << APLIC_xMSICFGADDRH_HHXS_SHIFT) | \
     (APLIC_xMSICFGADDRH_LHXS_MASK << APLIC_xMSICFGADDRH_LHXS_SHIFT) | \
     (APLIC_xMSICFGADDRH_HHXW_MASK << APLIC_xMSICFGADDRH_HHXW_SHIFT) | \
     (APLIC_xMSICFGADDRH_LHXW_MASK << APLIC_xMSICFGADDRH_LHXW_SHIFT) | \
     APLIC_xMSICFGADDRH_BAPPN_MASK)

#define APLIC_SETIP_BASE               0x1c00
#define APLIC_SETIPNUM                 0x1cdc

#define APLIC_CLRIP_BASE               0x1d00
#define APLIC_CLRIPNUM                 0x1ddc

#define APLIC_SETIE_BASE               0x1e00
#define APLIC_SETIENUM                 0x1edc

#define APLIC_CLRIE_BASE               0x1f00
#define APLIC_CLRIENUM                 0x1fdc

#define APLIC_SETIPNUM_LE              0x2000
#define APLIC_SETIPNUM_BE              0x2004

#define APLIC_ISTATE_PENDING           (1U << 0)
#define APLIC_ISTATE_ENABLED           (1U << 1)
#define APLIC_ISTATE_ENPEND            (APLIC_ISTATE_ENABLED | \
                                        APLIC_ISTATE_PENDING)
#define APLIC_ISTATE_INPUT             (1U << 8)

#define APLIC_GENMSI                   0x3000

#define APLIC_TARGET_BASE              0x3004
#define APLIC_TARGET_HART_IDX_SHIFT    18
#define APLIC_TARGET_HART_IDX_MASK     0x3fff
#define APLIC_TARGET_GUEST_IDX_SHIFT   12
#define APLIC_TARGET_GUEST_IDX_MASK    0x3f
#define APLIC_TARGET_IPRIO_MASK        0xff
#define APLIC_TARGET_EIID_MASK         0x7ff

#define APLIC_IDC_BASE                 0x4000
#define APLIC_IDC_SIZE                 32

#define APLIC_IDC_IDELIVERY            0x00

#define APLIC_IDC_IFORCE               0x04

#define APLIC_IDC_ITHRESHOLD           0x08

#define APLIC_IDC_TOPI                 0x18
#define APLIC_IDC_TOPI_ID_SHIFT        16
#define APLIC_IDC_TOPI_ID_MASK         0x3ff
#define APLIC_IDC_TOPI_PRIO_MASK       0xff

#define APLIC_IDC_CLAIMI               0x1c

/*
 * KVM AIA only supports APLIC MSI, fallback to QEMU emulation if we want to use
 * APLIC Wired.
 */
static bool is_kvm_aia(bool msimode)
{
    return kvm_irqchip_in_kernel() && msimode;
}

static uint32_t riscv_aplic_read_input_word(RISCVAPLICState *aplic,
                                            uint32_t word)
{
    uint32_t i, irq, ret = 0;

    for (i = 0; i < 32; i++) {
        irq = word * 32 + i;
        if (!irq || aplic->num_irqs <= irq) {
            continue;
        }

        ret |= ((aplic->state[irq] & APLIC_ISTATE_INPUT) ? 1 : 0) << i;
    }

    return ret;
}

static uint32_t riscv_aplic_read_pending_word(RISCVAPLICState *aplic,
                                              uint32_t word)
{
    uint32_t i, irq, ret = 0;

    for (i = 0; i < 32; i++) {
        irq = word * 32 + i;
        if (!irq || aplic->num_irqs <= irq) {
            continue;
        }

        ret |= ((aplic->state[irq] & APLIC_ISTATE_PENDING) ? 1 : 0) << i;
    }

    return ret;
}

static void riscv_aplic_set_pending_raw(RISCVAPLICState *aplic,
                                        uint32_t irq, bool pending)
{
    if (pending) {
        aplic->state[irq] |= APLIC_ISTATE_PENDING;
    } else {
        aplic->state[irq] &= ~APLIC_ISTATE_PENDING;
    }
}

static void riscv_aplic_set_pending(RISCVAPLICState *aplic,
                                    uint32_t irq, bool pending)
{
    uint32_t sourcecfg, sm;

    if ((irq <= 0) || (aplic->num_irqs <= irq)) {
        return;
    }

    sourcecfg = aplic->sourcecfg[irq];
    if (sourcecfg & APLIC_SOURCECFG_D) {
        return;
    }

    sm = sourcecfg & APLIC_SOURCECFG_SM_MASK;
    if ((sm == APLIC_SOURCECFG_SM_INACTIVE) ||
        ((!aplic->msimode || (aplic->msimode && !pending)) &&
         ((sm == APLIC_SOURCECFG_SM_LEVEL_HIGH) ||
          (sm == APLIC_SOURCECFG_SM_LEVEL_LOW)))) {
        return;
    }

    riscv_aplic_set_pending_raw(aplic, irq, pending);
}

static void riscv_aplic_set_pending_word(RISCVAPLICState *aplic,
                                         uint32_t word, uint32_t value,
                                         bool pending)
{
    uint32_t i, irq;

    for (i = 0; i < 32; i++) {
        irq = word * 32 + i;
        if (!irq || aplic->num_irqs <= irq) {
            continue;
        }

        if (value & (1U << i)) {
            riscv_aplic_set_pending(aplic, irq, pending);
        }
    }
}

static uint32_t riscv_aplic_read_enabled_word(RISCVAPLICState *aplic,
                                              int word)
{
    uint32_t i, irq, ret = 0;

    for (i = 0; i < 32; i++) {
        irq = word * 32 + i;
        if (!irq || aplic->num_irqs <= irq) {
            continue;
        }

        ret |= ((aplic->state[irq] & APLIC_ISTATE_ENABLED) ? 1 : 0) << i;
    }

    return ret;
}

static void riscv_aplic_set_enabled_raw(RISCVAPLICState *aplic,
                                        uint32_t irq, bool enabled)
{
    if (enabled) {
        aplic->state[irq] |= APLIC_ISTATE_ENABLED;
    } else {
        aplic->state[irq] &= ~APLIC_ISTATE_ENABLED;
    }
}

static void riscv_aplic_set_enabled(RISCVAPLICState *aplic,
                                    uint32_t irq, bool enabled)
{
    uint32_t sourcecfg, sm;

    if ((irq <= 0) || (aplic->num_irqs <= irq)) {
        return;
    }

    sourcecfg = aplic->sourcecfg[irq];
    if (sourcecfg & APLIC_SOURCECFG_D) {
        return;
    }

    sm = sourcecfg & APLIC_SOURCECFG_SM_MASK;
    if (sm == APLIC_SOURCECFG_SM_INACTIVE) {
        return;
    }

    riscv_aplic_set_enabled_raw(aplic, irq, enabled);
}

static void riscv_aplic_set_enabled_word(RISCVAPLICState *aplic,
                                         uint32_t word, uint32_t value,
                                         bool enabled)
{
    uint32_t i, irq;

    for (i = 0; i < 32; i++) {
        irq = word * 32 + i;
        if (!irq || aplic->num_irqs <= irq) {
            continue;
        }

        if (value & (1U << i)) {
            riscv_aplic_set_enabled(aplic, irq, enabled);
        }
    }
}

static void riscv_aplic_msi_send(RISCVAPLICState *aplic,
                                 uint32_t hart_idx, uint32_t guest_idx,
                                 uint32_t eiid)
{
    uint64_t addr;
    MemTxResult result;
    RISCVAPLICState *aplic_m;
    uint32_t lhxs, lhxw, hhxs, hhxw, group_idx, msicfgaddr, msicfgaddrH;

    aplic_m = aplic;
    while (aplic_m && !aplic_m->mmode) {
        aplic_m = aplic_m->parent;
    }
    if (!aplic_m) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: m-level APLIC not found\n",
                      __func__);
        return;
    }

    if (aplic->mmode) {
        msicfgaddr = aplic_m->mmsicfgaddr;
        msicfgaddrH = aplic_m->mmsicfgaddrH;
    } else {
        msicfgaddr = aplic_m->smsicfgaddr;
        msicfgaddrH = aplic_m->smsicfgaddrH;
    }

    lhxs = (msicfgaddrH >> APLIC_xMSICFGADDRH_LHXS_SHIFT) &
            APLIC_xMSICFGADDRH_LHXS_MASK;
    lhxw = (msicfgaddrH >> APLIC_xMSICFGADDRH_LHXW_SHIFT) &
            APLIC_xMSICFGADDRH_LHXW_MASK;
    hhxs = (msicfgaddrH >> APLIC_xMSICFGADDRH_HHXS_SHIFT) &
            APLIC_xMSICFGADDRH_HHXS_MASK;
    hhxw = (msicfgaddrH >> APLIC_xMSICFGADDRH_HHXW_SHIFT) &
            APLIC_xMSICFGADDRH_HHXW_MASK;

    group_idx = hart_idx >> lhxw;
    hart_idx &= APLIC_xMSICFGADDR_PPN_LHX_MASK(lhxw);

    addr = msicfgaddr;
    addr |= ((uint64_t)(msicfgaddrH & APLIC_xMSICFGADDRH_BAPPN_MASK)) << 32;
    addr |= ((uint64_t)(group_idx & APLIC_xMSICFGADDR_PPN_HHX_MASK(hhxw))) <<
             APLIC_xMSICFGADDR_PPN_HHX_SHIFT(hhxs);
    addr |= ((uint64_t)(hart_idx & APLIC_xMSICFGADDR_PPN_LHX_MASK(lhxw))) <<
             APLIC_xMSICFGADDR_PPN_LHX_SHIFT(lhxs);
    addr |= (uint64_t)(guest_idx & APLIC_xMSICFGADDR_PPN_HART(lhxs));
    addr <<= APLIC_xMSICFGADDR_PPN_SHIFT;

    address_space_stl_le(&address_space_memory, addr,
                         eiid, MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: MSI write failed for "
                      "hart_index=%d guest_index=%d eiid=%d\n",
                      __func__, hart_idx, guest_idx, eiid);
    }
}

static void riscv_aplic_msi_irq_update(RISCVAPLICState *aplic, uint32_t irq)
{
    uint32_t hart_idx, guest_idx, eiid;

    if (!aplic->msimode || (aplic->num_irqs <= irq) ||
        !(aplic->domaincfg & APLIC_DOMAINCFG_IE)) {
        return;
    }

    if ((aplic->state[irq] & APLIC_ISTATE_ENPEND) != APLIC_ISTATE_ENPEND) {
        return;
    }

    riscv_aplic_set_pending_raw(aplic, irq, false);

    hart_idx = aplic->target[irq] >> APLIC_TARGET_HART_IDX_SHIFT;
    hart_idx &= APLIC_TARGET_HART_IDX_MASK;
    if (aplic->mmode) {
        /* M-level APLIC ignores guest_index */
        guest_idx = 0;
    } else {
        guest_idx = aplic->target[irq] >> APLIC_TARGET_GUEST_IDX_SHIFT;
        guest_idx &= APLIC_TARGET_GUEST_IDX_MASK;
    }
    eiid = aplic->target[irq] & APLIC_TARGET_EIID_MASK;
    riscv_aplic_msi_send(aplic, hart_idx, guest_idx, eiid);
}

static uint32_t riscv_aplic_idc_topi(RISCVAPLICState *aplic, uint32_t idc)
{
    uint32_t best_irq, best_iprio;
    uint32_t irq, iprio, ihartidx, ithres;

    if (aplic->num_harts <= idc) {
        return 0;
    }

    ithres = aplic->ithreshold[idc];
    best_irq = best_iprio = UINT32_MAX;
    for (irq = 1; irq < aplic->num_irqs; irq++) {
        if ((aplic->state[irq] & APLIC_ISTATE_ENPEND) !=
            APLIC_ISTATE_ENPEND) {
            continue;
        }

        ihartidx = aplic->target[irq] >> APLIC_TARGET_HART_IDX_SHIFT;
        ihartidx &= APLIC_TARGET_HART_IDX_MASK;
        if (ihartidx != idc) {
            continue;
        }

        iprio = aplic->target[irq] & aplic->iprio_mask;
        if (ithres && iprio >= ithres) {
            continue;
        }

        if (iprio < best_iprio) {
            best_irq = irq;
            best_iprio = iprio;
        }
    }

    if (best_irq < aplic->num_irqs && best_iprio <= aplic->iprio_mask) {
        return (best_irq << APLIC_IDC_TOPI_ID_SHIFT) | best_iprio;
    }

    return 0;
}

static void riscv_aplic_idc_update(RISCVAPLICState *aplic, uint32_t idc)
{
    uint32_t topi;

    if (aplic->msimode || aplic->num_harts <= idc) {
        return;
    }

    topi = riscv_aplic_idc_topi(aplic, idc);
    if ((aplic->domaincfg & APLIC_DOMAINCFG_IE) &&
        aplic->idelivery[idc] &&
        (aplic->iforce[idc] || topi)) {
        qemu_irq_raise(aplic->external_irqs[idc]);
    } else {
        qemu_irq_lower(aplic->external_irqs[idc]);
    }
}

static uint32_t riscv_aplic_idc_claimi(RISCVAPLICState *aplic, uint32_t idc)
{
    uint32_t irq, state, sm, topi = riscv_aplic_idc_topi(aplic, idc);

    if (!topi) {
        aplic->iforce[idc] = 0;
        return 0;
    }

    irq = (topi >> APLIC_IDC_TOPI_ID_SHIFT) & APLIC_IDC_TOPI_ID_MASK;
    sm = aplic->sourcecfg[irq] & APLIC_SOURCECFG_SM_MASK;
    state = aplic->state[irq];
    riscv_aplic_set_pending_raw(aplic, irq, false);
    if ((sm == APLIC_SOURCECFG_SM_LEVEL_HIGH) &&
        (state & APLIC_ISTATE_INPUT)) {
        riscv_aplic_set_pending_raw(aplic, irq, true);
    } else if ((sm == APLIC_SOURCECFG_SM_LEVEL_LOW) &&
               !(state & APLIC_ISTATE_INPUT)) {
        riscv_aplic_set_pending_raw(aplic, irq, true);
    }
    riscv_aplic_idc_update(aplic, idc);

    return topi;
}

static void riscv_aplic_request(void *opaque, int irq, int level)
{
    bool update = false;
    RISCVAPLICState *aplic = opaque;
    uint32_t sourcecfg, childidx, state, idc;

    assert((0 < irq) && (irq < aplic->num_irqs));

    sourcecfg = aplic->sourcecfg[irq];
    if (sourcecfg & APLIC_SOURCECFG_D) {
        childidx = sourcecfg & APLIC_SOURCECFG_CHILDIDX_MASK;
        if (childidx < aplic->num_children) {
            riscv_aplic_request(aplic->children[childidx], irq, level);
        }
        return;
    }

    state = aplic->state[irq];
    switch (sourcecfg & APLIC_SOURCECFG_SM_MASK) {
    case APLIC_SOURCECFG_SM_EDGE_RISE:
        if ((level > 0) && !(state & APLIC_ISTATE_INPUT) &&
            !(state & APLIC_ISTATE_PENDING)) {
            riscv_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    case APLIC_SOURCECFG_SM_EDGE_FALL:
        if ((level <= 0) && (state & APLIC_ISTATE_INPUT) &&
            !(state & APLIC_ISTATE_PENDING)) {
            riscv_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    case APLIC_SOURCECFG_SM_LEVEL_HIGH:
        if ((level > 0) && !(state & APLIC_ISTATE_PENDING)) {
            riscv_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    case APLIC_SOURCECFG_SM_LEVEL_LOW:
        if ((level <= 0) && !(state & APLIC_ISTATE_PENDING)) {
            riscv_aplic_set_pending_raw(aplic, irq, true);
            update = true;
        }
        break;
    default:
        break;
    }

    if (level <= 0) {
        aplic->state[irq] &= ~APLIC_ISTATE_INPUT;
    } else {
        aplic->state[irq] |= APLIC_ISTATE_INPUT;
    }

    if (update) {
        if (aplic->msimode) {
            riscv_aplic_msi_irq_update(aplic, irq);
        } else {
            idc = aplic->target[irq] >> APLIC_TARGET_HART_IDX_SHIFT;
            idc &= APLIC_TARGET_HART_IDX_MASK;
            riscv_aplic_idc_update(aplic, idc);
        }
    }
}

static uint64_t riscv_aplic_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t irq, word, idc;
    RISCVAPLICState *aplic = opaque;

    /* Reads must be 4 byte words */
    if ((addr & 0x3) != 0) {
        goto err;
    }

    if (addr == APLIC_DOMAINCFG) {
        return APLIC_DOMAINCFG_RDONLY | aplic->domaincfg |
               (aplic->msimode ? APLIC_DOMAINCFG_DM : 0);
    } else if ((APLIC_SOURCECFG_BASE <= addr) &&
            (addr < (APLIC_SOURCECFG_BASE + (aplic->num_irqs - 1) * 4))) {
        irq  = ((addr - APLIC_SOURCECFG_BASE) >> 2) + 1;
        return aplic->sourcecfg[irq];
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_MMSICFGADDR)) {
        return aplic->mmsicfgaddr;
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_MMSICFGADDRH)) {
        return aplic->mmsicfgaddrH;
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_SMSICFGADDR)) {
        /*
         * Registers SMSICFGADDR and SMSICFGADDRH are implemented only if:
         * (a) the interrupt domain is at machine level
         * (b) the domain's harts implement supervisor mode
         * (c) the domain has one or more child supervisor-level domains
         *     that support MSI delivery mode (domaincfg.DM is not read-
         *     only zero in at least one of the supervisor-level child
         * domains).
         */
        return (aplic->num_children) ? aplic->smsicfgaddr : 0;
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_SMSICFGADDRH)) {
        return (aplic->num_children) ? aplic->smsicfgaddrH : 0;
    } else if ((APLIC_SETIP_BASE <= addr) &&
            (addr < (APLIC_SETIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - APLIC_SETIP_BASE) >> 2;
        return riscv_aplic_read_pending_word(aplic, word);
    } else if (addr == APLIC_SETIPNUM) {
        return 0;
    } else if ((APLIC_CLRIP_BASE <= addr) &&
            (addr < (APLIC_CLRIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - APLIC_CLRIP_BASE) >> 2;
        return riscv_aplic_read_input_word(aplic, word);
    } else if (addr == APLIC_CLRIPNUM) {
        return 0;
    } else if ((APLIC_SETIE_BASE <= addr) &&
            (addr < (APLIC_SETIE_BASE + aplic->bitfield_words * 4))) {
        word = (addr - APLIC_SETIE_BASE) >> 2;
        return riscv_aplic_read_enabled_word(aplic, word);
    } else if (addr == APLIC_SETIENUM) {
        return 0;
    } else if ((APLIC_CLRIE_BASE <= addr) &&
            (addr < (APLIC_CLRIE_BASE + aplic->bitfield_words * 4))) {
        return 0;
    } else if (addr == APLIC_CLRIENUM) {
        return 0;
    } else if (addr == APLIC_SETIPNUM_LE) {
        return 0;
    } else if (addr == APLIC_SETIPNUM_BE) {
        return 0;
    } else if (addr == APLIC_GENMSI) {
        return (aplic->msimode) ? aplic->genmsi : 0;
    } else if ((APLIC_TARGET_BASE <= addr) &&
            (addr < (APLIC_TARGET_BASE + (aplic->num_irqs - 1) * 4))) {
        irq = ((addr - APLIC_TARGET_BASE) >> 2) + 1;
        return aplic->target[irq];
    } else if (!aplic->msimode && (APLIC_IDC_BASE <= addr) &&
            (addr < (APLIC_IDC_BASE + aplic->num_harts * APLIC_IDC_SIZE))) {
        idc = (addr - APLIC_IDC_BASE) / APLIC_IDC_SIZE;
        switch (addr - (APLIC_IDC_BASE + idc * APLIC_IDC_SIZE)) {
        case APLIC_IDC_IDELIVERY:
            return aplic->idelivery[idc];
        case APLIC_IDC_IFORCE:
            return aplic->iforce[idc];
        case APLIC_IDC_ITHRESHOLD:
            return aplic->ithreshold[idc];
        case APLIC_IDC_TOPI:
            return riscv_aplic_idc_topi(aplic, idc);
        case APLIC_IDC_CLAIMI:
            return riscv_aplic_idc_claimi(aplic, idc);
        default:
            goto err;
        };
    }

err:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register read 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void riscv_aplic_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    RISCVAPLICState *aplic = opaque;
    uint32_t irq, word, idc = UINT32_MAX;

    /* Writes must be 4 byte words */
    if ((addr & 0x3) != 0) {
        goto err;
    }

    if (addr == APLIC_DOMAINCFG) {
        /* Only IE bit writable at the moment */
        value &= APLIC_DOMAINCFG_IE;
        aplic->domaincfg = value;
    } else if ((APLIC_SOURCECFG_BASE <= addr) &&
            (addr < (APLIC_SOURCECFG_BASE + (aplic->num_irqs - 1) * 4))) {
        irq  = ((addr - APLIC_SOURCECFG_BASE) >> 2) + 1;
        if (!aplic->num_children && (value & APLIC_SOURCECFG_D)) {
            value = 0;
        }
        if (value & APLIC_SOURCECFG_D) {
            value &= (APLIC_SOURCECFG_D | APLIC_SOURCECFG_CHILDIDX_MASK);
        } else {
            value &= (APLIC_SOURCECFG_D | APLIC_SOURCECFG_SM_MASK);
        }
        aplic->sourcecfg[irq] = value;
        if ((aplic->sourcecfg[irq] & APLIC_SOURCECFG_D) ||
            (aplic->sourcecfg[irq] == 0)) {
            riscv_aplic_set_pending_raw(aplic, irq, false);
            riscv_aplic_set_enabled_raw(aplic, irq, false);
        }
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_MMSICFGADDR)) {
        if (!(aplic->mmsicfgaddrH & APLIC_xMSICFGADDRH_L)) {
            aplic->mmsicfgaddr = value;
        }
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_MMSICFGADDRH)) {
        if (!(aplic->mmsicfgaddrH & APLIC_xMSICFGADDRH_L)) {
            aplic->mmsicfgaddrH = value & APLIC_xMSICFGADDRH_VALID_MASK;
        }
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_SMSICFGADDR)) {
        /*
         * Registers SMSICFGADDR and SMSICFGADDRH are implemented only if:
         * (a) the interrupt domain is at machine level
         * (b) the domain's harts implement supervisor mode
         * (c) the domain has one or more child supervisor-level domains
         *     that support MSI delivery mode (domaincfg.DM is not read-
         *     only zero in at least one of the supervisor-level child
         * domains).
         */
        if (aplic->num_children &&
            !(aplic->mmsicfgaddrH & APLIC_xMSICFGADDRH_L)) {
            aplic->smsicfgaddr = value;
        }
    } else if (aplic->mmode && aplic->msimode &&
               (addr == APLIC_SMSICFGADDRH)) {
        if (aplic->num_children &&
            !(aplic->mmsicfgaddrH & APLIC_xMSICFGADDRH_L)) {
            aplic->smsicfgaddrH = value & APLIC_xMSICFGADDRH_VALID_MASK;
        }
    } else if ((APLIC_SETIP_BASE <= addr) &&
            (addr < (APLIC_SETIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - APLIC_SETIP_BASE) >> 2;
        riscv_aplic_set_pending_word(aplic, word, value, true);
    } else if (addr == APLIC_SETIPNUM) {
        riscv_aplic_set_pending(aplic, value, true);
    } else if ((APLIC_CLRIP_BASE <= addr) &&
            (addr < (APLIC_CLRIP_BASE + aplic->bitfield_words * 4))) {
        word = (addr - APLIC_CLRIP_BASE) >> 2;
        riscv_aplic_set_pending_word(aplic, word, value, false);
    } else if (addr == APLIC_CLRIPNUM) {
        riscv_aplic_set_pending(aplic, value, false);
    } else if ((APLIC_SETIE_BASE <= addr) &&
            (addr < (APLIC_SETIE_BASE + aplic->bitfield_words * 4))) {
        word = (addr - APLIC_SETIE_BASE) >> 2;
        riscv_aplic_set_enabled_word(aplic, word, value, true);
    } else if (addr == APLIC_SETIENUM) {
        riscv_aplic_set_enabled(aplic, value, true);
    } else if ((APLIC_CLRIE_BASE <= addr) &&
            (addr < (APLIC_CLRIE_BASE + aplic->bitfield_words * 4))) {
        word = (addr - APLIC_CLRIE_BASE) >> 2;
        riscv_aplic_set_enabled_word(aplic, word, value, false);
    } else if (addr == APLIC_CLRIENUM) {
        riscv_aplic_set_enabled(aplic, value, false);
    } else if (addr == APLIC_SETIPNUM_LE) {
        riscv_aplic_set_pending(aplic, value, true);
    } else if (addr == APLIC_SETIPNUM_BE) {
        riscv_aplic_set_pending(aplic, bswap32(value), true);
    } else if (addr == APLIC_GENMSI) {
        if (aplic->msimode) {
            aplic->genmsi = value & ~(APLIC_TARGET_GUEST_IDX_MASK <<
                                      APLIC_TARGET_GUEST_IDX_SHIFT);
            riscv_aplic_msi_send(aplic,
                                 value >> APLIC_TARGET_HART_IDX_SHIFT,
                                 0,
                                 value & APLIC_TARGET_EIID_MASK);
        }
    } else if ((APLIC_TARGET_BASE <= addr) &&
            (addr < (APLIC_TARGET_BASE + (aplic->num_irqs - 1) * 4))) {
        irq = ((addr - APLIC_TARGET_BASE) >> 2) + 1;
        if (aplic->msimode) {
            aplic->target[irq] = value;
        } else {
            aplic->target[irq] = (value & ~APLIC_TARGET_IPRIO_MASK) |
                                 ((value & aplic->iprio_mask) ?
                                  (value & aplic->iprio_mask) : 1);
        }
    } else if (!aplic->msimode && (APLIC_IDC_BASE <= addr) &&
            (addr < (APLIC_IDC_BASE + aplic->num_harts * APLIC_IDC_SIZE))) {
        idc = (addr - APLIC_IDC_BASE) / APLIC_IDC_SIZE;
        switch (addr - (APLIC_IDC_BASE + idc * APLIC_IDC_SIZE)) {
        case APLIC_IDC_IDELIVERY:
            aplic->idelivery[idc] = value & 0x1;
            break;
        case APLIC_IDC_IFORCE:
            aplic->iforce[idc] = value & 0x1;
            break;
        case APLIC_IDC_ITHRESHOLD:
            aplic->ithreshold[idc] = value & aplic->iprio_mask;
            break;
        default:
            goto err;
        };
    } else {
        goto err;
    }

    if (aplic->msimode) {
        for (irq = 1; irq < aplic->num_irqs; irq++) {
            riscv_aplic_msi_irq_update(aplic, irq);
        }
    } else {
        if (idc == UINT32_MAX) {
            for (idc = 0; idc < aplic->num_harts; idc++) {
                riscv_aplic_idc_update(aplic, idc);
            }
        } else {
            riscv_aplic_idc_update(aplic, idc);
        }
    }

    return;

err:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register write 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
}

static const MemoryRegionOps riscv_aplic_ops = {
    .read = riscv_aplic_read,
    .write = riscv_aplic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void riscv_aplic_realize(DeviceState *dev, Error **errp)
{
    uint32_t i;
    RISCVAPLICState *aplic = RISCV_APLIC(dev);

    if (!is_kvm_aia(aplic->msimode)) {
        aplic->bitfield_words = (aplic->num_irqs + 31) >> 5;
        aplic->sourcecfg = g_new0(uint32_t, aplic->num_irqs);
        aplic->state = g_new0(uint32_t, aplic->num_irqs);
        aplic->target = g_new0(uint32_t, aplic->num_irqs);
        if (!aplic->msimode) {
            for (i = 0; i < aplic->num_irqs; i++) {
                aplic->target[i] = 1;
            }
        }
        aplic->idelivery = g_new0(uint32_t, aplic->num_harts);
        aplic->iforce = g_new0(uint32_t, aplic->num_harts);
        aplic->ithreshold = g_new0(uint32_t, aplic->num_harts);

        memory_region_init_io(&aplic->mmio, OBJECT(dev), &riscv_aplic_ops,
                              aplic, TYPE_RISCV_APLIC, aplic->aperture_size);
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &aplic->mmio);
    }

    /*
     * Only root APLICs have hardware IRQ lines. All non-root APLICs
     * have IRQ lines delegated by their parent APLIC.
     */
    if (!aplic->parent) {
        if (kvm_enabled() && is_kvm_aia(aplic->msimode)) {
            qdev_init_gpio_in(dev, riscv_kvm_aplic_request, aplic->num_irqs);
        } else {
            qdev_init_gpio_in(dev, riscv_aplic_request, aplic->num_irqs);
        }
    }

    /* Create output IRQ lines for non-MSI mode */
    if (!aplic->msimode) {
        aplic->external_irqs = g_malloc(sizeof(qemu_irq) * aplic->num_harts);
        qdev_init_gpio_out(dev, aplic->external_irqs, aplic->num_harts);

        /* Claim the CPU interrupt to be triggered by this APLIC */
        for (i = 0; i < aplic->num_harts; i++) {
            RISCVCPU *cpu = RISCV_CPU(cpu_by_arch_id(aplic->hartid_base + i));
            if (riscv_cpu_claim_interrupts(cpu,
                (aplic->mmode) ? MIP_MEIP : MIP_SEIP) < 0) {
                error_report("%s already claimed",
                             (aplic->mmode) ? "MEIP" : "SEIP");
                exit(1);
            }
        }
    }

    msi_nonbroken = true;
}

static Property riscv_aplic_properties[] = {
    DEFINE_PROP_UINT32("aperture-size", RISCVAPLICState, aperture_size, 0),
    DEFINE_PROP_UINT32("hartid-base", RISCVAPLICState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", RISCVAPLICState, num_harts, 0),
    DEFINE_PROP_UINT32("iprio-mask", RISCVAPLICState, iprio_mask, 0),
    DEFINE_PROP_UINT32("num-irqs", RISCVAPLICState, num_irqs, 0),
    DEFINE_PROP_BOOL("msimode", RISCVAPLICState, msimode, 0),
    DEFINE_PROP_BOOL("mmode", RISCVAPLICState, mmode, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_riscv_aplic = {
    .name = "riscv_aplic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
            VMSTATE_UINT32(domaincfg, RISCVAPLICState),
            VMSTATE_UINT32(mmsicfgaddr, RISCVAPLICState),
            VMSTATE_UINT32(mmsicfgaddrH, RISCVAPLICState),
            VMSTATE_UINT32(smsicfgaddr, RISCVAPLICState),
            VMSTATE_UINT32(smsicfgaddrH, RISCVAPLICState),
            VMSTATE_UINT32(genmsi, RISCVAPLICState),
            VMSTATE_VARRAY_UINT32(sourcecfg, RISCVAPLICState,
                                  num_irqs, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(state, RISCVAPLICState,
                                  num_irqs, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(target, RISCVAPLICState,
                                  num_irqs, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(idelivery, RISCVAPLICState,
                                  num_harts, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(iforce, RISCVAPLICState,
                                  num_harts, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(ithreshold, RISCVAPLICState,
                                  num_harts, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_END_OF_LIST()
        }
};

static void riscv_aplic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, riscv_aplic_properties);
    dc->realize = riscv_aplic_realize;
    dc->vmsd = &vmstate_riscv_aplic;
}

static const TypeInfo riscv_aplic_info = {
    .name          = TYPE_RISCV_APLIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVAPLICState),
    .class_init    = riscv_aplic_class_init,
};

static void riscv_aplic_register_types(void)
{
    type_register_static(&riscv_aplic_info);
}

type_init(riscv_aplic_register_types)

/*
 * Add a APLIC device to another APLIC device as child for
 * interrupt delegation.
 */
void riscv_aplic_add_child(DeviceState *parent, DeviceState *child)
{
    RISCVAPLICState *caplic, *paplic;

    assert(parent && child);
    caplic = RISCV_APLIC(child);
    paplic = RISCV_APLIC(parent);

    assert(paplic->num_irqs == caplic->num_irqs);
    assert(paplic->num_children <= QEMU_APLIC_MAX_CHILDREN);

    caplic->parent = paplic;
    paplic->children[paplic->num_children] = caplic;
    paplic->num_children++;
}

/*
 * Create APLIC device.
 */
DeviceState *riscv_aplic_create(hwaddr addr, hwaddr size,
    uint32_t hartid_base, uint32_t num_harts, uint32_t num_sources,
    uint32_t iprio_bits, bool msimode, bool mmode, DeviceState *parent)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_APLIC);
    uint32_t i;

    assert(num_harts < APLIC_MAX_IDC);
    assert((APLIC_IDC_BASE + (num_harts * APLIC_IDC_SIZE)) <= size);
    assert(num_sources < APLIC_MAX_SOURCE);
    assert(APLIC_MIN_IPRIO_BITS <= iprio_bits);
    assert(iprio_bits <= APLIC_MAX_IPRIO_BITS);

    qdev_prop_set_uint32(dev, "aperture-size", size);
    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "iprio-mask", ((1U << iprio_bits) - 1));
    qdev_prop_set_uint32(dev, "num-irqs", num_sources + 1);
    qdev_prop_set_bit(dev, "msimode", msimode);
    qdev_prop_set_bit(dev, "mmode", mmode);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    if (!is_kvm_aia(msimode)) {
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
    }

    if (parent) {
        riscv_aplic_add_child(parent, dev);
    }

    if (!msimode) {
        for (i = 0; i < num_harts; i++) {
            CPUState *cpu = cpu_by_arch_id(hartid_base + i);

            qdev_connect_gpio_out_named(dev, NULL, i,
                                        qdev_get_gpio_in(DEVICE(cpu),
                                            (mmode) ? IRQ_M_EXT : IRQ_S_EXT));
        }
    }

    return dev;
}
