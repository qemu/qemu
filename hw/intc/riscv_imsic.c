/*
 * RISC-V IMSIC (Incoming Message Signaled Interrupt Controller)
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
#include "system/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/pci/msi.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/irq.h"
#include "target/riscv/cpu.h"
#include "target/riscv/cpu_bits.h"
#include "system/system.h"
#include "system/kvm.h"
#include "migration/vmstate.h"

#define IMSIC_MMIO_PAGE_LE             0x00
#define IMSIC_MMIO_PAGE_BE             0x04

#define IMSIC_MIN_ID                   ((IMSIC_EIPx_BITS * 2) - 1)
#define IMSIC_MAX_ID                   (IMSIC_TOPEI_IID_MASK)

#define IMSIC_EISTATE_PENDING          (1U << 0)
#define IMSIC_EISTATE_ENABLED          (1U << 1)
#define IMSIC_EISTATE_ENPEND           (IMSIC_EISTATE_ENABLED | \
                                        IMSIC_EISTATE_PENDING)

static uint32_t riscv_imsic_topei(RISCVIMSICState *imsic, uint32_t page)
{
    uint32_t i, max_irq, base;

    base = page * imsic->num_irqs;
    max_irq = (imsic->eithreshold[page] &&
               (imsic->eithreshold[page] <= imsic->num_irqs)) ?
               imsic->eithreshold[page] : imsic->num_irqs;
    for (i = 1; i < max_irq; i++) {
        if ((qatomic_read(&imsic->eistate[base + i]) & IMSIC_EISTATE_ENPEND) ==
                IMSIC_EISTATE_ENPEND) {
            return (i << IMSIC_TOPEI_IID_SHIFT) | i;
        }
    }

    return 0;
}

static void riscv_imsic_update(RISCVIMSICState *imsic, uint32_t page)
{
    uint32_t base = page * imsic->num_irqs;

    /*
     * Lower the interrupt line if necessary, then evaluate the current
     * IMSIC state.
     * This sequence ensures that any race between evaluating the eistate and
     * updating the interrupt line will not result in an incorrectly
     * deactivated connected CPU IRQ line.
     * If multiple interrupts are pending, this sequence functions identically
     * to qemu_irq_pulse.
     */

    if (qatomic_fetch_and(&imsic->eistate[base], ~IMSIC_EISTATE_ENPEND)) {
        qemu_irq_lower(imsic->external_irqs[page]);
    }
    if (imsic->eidelivery[page] && riscv_imsic_topei(imsic, page)) {
        qemu_irq_raise(imsic->external_irqs[page]);
        qatomic_or(&imsic->eistate[base], IMSIC_EISTATE_ENPEND);
    }
}

static int riscv_imsic_eidelivery_rmw(RISCVIMSICState *imsic, uint32_t page,
                                      target_ulong *val,
                                      target_ulong new_val,
                                      target_ulong wr_mask)
{
    target_ulong old_val = imsic->eidelivery[page];

    if (val) {
        *val = old_val;
    }

    wr_mask &= 0x1;
    imsic->eidelivery[page] = (old_val & ~wr_mask) | (new_val & wr_mask);

    riscv_imsic_update(imsic, page);
    return 0;
}

static int riscv_imsic_eithreshold_rmw(RISCVIMSICState *imsic, uint32_t page,
                                      target_ulong *val,
                                      target_ulong new_val,
                                      target_ulong wr_mask)
{
    target_ulong old_val = imsic->eithreshold[page];

    if (val) {
        *val = old_val;
    }

    wr_mask &= IMSIC_MAX_ID;
    imsic->eithreshold[page] = (old_val & ~wr_mask) | (new_val & wr_mask);

    riscv_imsic_update(imsic, page);
    return 0;
}

static int riscv_imsic_topei_rmw(RISCVIMSICState *imsic, uint32_t page,
                                 target_ulong *val, target_ulong new_val,
                                 target_ulong wr_mask)
{
    uint32_t base, topei = riscv_imsic_topei(imsic, page);

    /* Read pending and enabled interrupt with highest priority */
    if (val) {
        *val = topei;
    }

    /* Writes ignore value and clear top pending interrupt */
    if (topei && wr_mask) {
        topei >>= IMSIC_TOPEI_IID_SHIFT;
        base = page * imsic->num_irqs;
        if (topei) {
            qatomic_and(&imsic->eistate[base + topei], ~IMSIC_EISTATE_PENDING);
        }
    }

    riscv_imsic_update(imsic, page);
    return 0;
}

static int riscv_imsic_eix_rmw(RISCVIMSICState *imsic,
                               uint32_t xlen, uint32_t page,
                               uint32_t num, bool pend, target_ulong *val,
                               target_ulong new_val, target_ulong wr_mask)
{
    uint32_t i, base, prev;
    target_ulong mask;
    uint32_t state = (pend) ? IMSIC_EISTATE_PENDING : IMSIC_EISTATE_ENABLED;

    if (xlen != 32) {
        if (num & 0x1) {
            return -EINVAL;
        }
        num >>= 1;
    }
    if (num >= (imsic->num_irqs / xlen)) {
        return -EINVAL;
    }

    base = (page * imsic->num_irqs) + (num * xlen);

    if (val) {
        *val = 0;
    }

    for (i = 0; i < xlen; i++) {
        /* Bit0 of eip0 and eie0 are read-only zero */
        if (!num && !i) {
            continue;
        }

        mask = (target_ulong)1 << i;
        if (wr_mask & mask) {
            if (new_val & mask) {
                prev = qatomic_fetch_or(&imsic->eistate[base + i], state);
            } else {
                prev = qatomic_fetch_and(&imsic->eistate[base + i], ~state);
            }
        } else {
            prev = qatomic_read(&imsic->eistate[base + i]);
        }
        if (val && (prev & state)) {
            *val |= mask;
        }
    }

    riscv_imsic_update(imsic, page);
    return 0;
}

static int riscv_imsic_rmw(void *arg, target_ulong reg, target_ulong *val,
                           target_ulong new_val, target_ulong wr_mask)
{
    RISCVIMSICState *imsic = arg;
    uint32_t isel, priv, virt, vgein, xlen, page;

    priv = AIA_IREG_PRIV(reg);
    virt = AIA_IREG_VIRT(reg);
    isel = AIA_IREG_ISEL(reg);
    vgein = AIA_IREG_VGEIN(reg);
    xlen = AIA_IREG_XLEN(reg);

    if (imsic->mmode) {
        if (priv == PRV_M && !virt) {
            page = 0;
        } else {
            goto err;
        }
    } else {
        if (priv == PRV_S) {
            if (virt) {
                if (vgein && vgein < imsic->num_pages) {
                    page = vgein;
                } else {
                    goto err;
                }
            } else {
                page = 0;
            }
        } else {
            goto err;
        }
    }

    switch (isel) {
    case ISELECT_IMSIC_EIDELIVERY:
        return riscv_imsic_eidelivery_rmw(imsic, page, val,
                                          new_val, wr_mask);
    case ISELECT_IMSIC_EITHRESHOLD:
        return riscv_imsic_eithreshold_rmw(imsic, page, val,
                                           new_val, wr_mask);
    case ISELECT_IMSIC_TOPEI:
        return riscv_imsic_topei_rmw(imsic, page, val, new_val, wr_mask);
    case ISELECT_IMSIC_EIP0 ... ISELECT_IMSIC_EIP63:
        return riscv_imsic_eix_rmw(imsic, xlen, page,
                                   isel - ISELECT_IMSIC_EIP0,
                                   true, val, new_val, wr_mask);
    case ISELECT_IMSIC_EIE0 ... ISELECT_IMSIC_EIE63:
        return riscv_imsic_eix_rmw(imsic, xlen, page,
                                   isel - ISELECT_IMSIC_EIE0,
                                   false, val, new_val, wr_mask);
    default:
        break;
    };

err:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register priv=%d virt=%d isel=%d vgein=%d\n",
                  __func__, priv, virt, isel, vgein);
    return -EINVAL;
}

static uint64_t riscv_imsic_read(void *opaque, hwaddr addr, unsigned size)
{
    RISCVIMSICState *imsic = opaque;

    /* Reads must be 4 byte words */
    if ((addr & 0x3) != 0) {
        goto err;
    }

    /* Reads cannot be out of range */
    if (addr > IMSIC_MMIO_SIZE(imsic->num_pages)) {
        goto err;
    }

    return 0;

err:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register read 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void riscv_imsic_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    RISCVIMSICState *imsic = opaque;
    uint32_t page;

    /* Writes must be 4 byte words */
    if ((addr & 0x3) != 0) {
        goto err;
    }

    /* Writes cannot be out of range */
    if (addr > IMSIC_MMIO_SIZE(imsic->num_pages)) {
        goto err;
    }

#if defined(CONFIG_KVM)
    if (kvm_irqchip_in_kernel()) {
        struct kvm_msi msi;

        msi.address_lo = extract64(imsic->mmio.addr + addr, 0, 32);
        msi.address_hi = extract64(imsic->mmio.addr + addr, 32, 32);
        msi.data = le32_to_cpu(value);

        kvm_vm_ioctl(kvm_state, KVM_SIGNAL_MSI, &msi);

        return;
    }
#endif

    /* Writes only supported for MSI little-endian registers */
    page = addr >> IMSIC_MMIO_PAGE_SHIFT;
    if ((addr & (IMSIC_MMIO_PAGE_SZ - 1)) == IMSIC_MMIO_PAGE_LE) {
        if (value && (value < imsic->num_irqs)) {
            qatomic_or(&imsic->eistate[(page * imsic->num_irqs) + value],
                       IMSIC_EISTATE_PENDING);

            /* Update CPU external interrupt status */
            riscv_imsic_update(imsic, page);
        }
    }

    return;

err:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register write 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
}

static const MemoryRegionOps riscv_imsic_ops = {
    .read = riscv_imsic_read,
    .write = riscv_imsic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void riscv_imsic_realize(DeviceState *dev, Error **errp)
{
    RISCVIMSICState *imsic = RISCV_IMSIC(dev);
    RISCVCPU *rcpu = RISCV_CPU(cpu_by_arch_id(imsic->hartid));
    CPUState *cpu = cpu_by_arch_id(imsic->hartid);
    CPURISCVState *env = cpu ? cpu_env(cpu) : NULL;

    /* Claim the CPU interrupt to be triggered by this IMSIC */
    if (riscv_cpu_claim_interrupts(rcpu,
            (imsic->mmode) ? MIP_MEIP : MIP_SEIP) < 0) {
        error_setg(errp, "%s already claimed",
                   (imsic->mmode) ? "MEIP" : "SEIP");
        return;
    }

    if (!kvm_irqchip_in_kernel()) {
        /* Create output IRQ lines */
        imsic->external_irqs = g_malloc(sizeof(qemu_irq) * imsic->num_pages);
        qdev_init_gpio_out(dev, imsic->external_irqs, imsic->num_pages);

        imsic->num_eistate = imsic->num_pages * imsic->num_irqs;
        imsic->eidelivery = g_new0(uint32_t, imsic->num_pages);
        imsic->eithreshold = g_new0(uint32_t, imsic->num_pages);
        imsic->eistate = g_new0(uint32_t, imsic->num_eistate);
    }

    memory_region_init_io(&imsic->mmio, OBJECT(dev), &riscv_imsic_ops,
                          imsic, TYPE_RISCV_IMSIC,
                          IMSIC_MMIO_SIZE(imsic->num_pages));
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &imsic->mmio);

    /* Force select AIA feature and setup CSR read-modify-write callback */
    if (env) {
        if (!imsic->mmode) {
            rcpu->cfg.ext_ssaia = true;
            riscv_cpu_set_geilen(env, imsic->num_pages - 1);
        } else {
            rcpu->cfg.ext_smaia = true;
        }

        if (!kvm_irqchip_in_kernel()) {
            riscv_cpu_set_aia_ireg_rmw_fn(env, (imsic->mmode) ? PRV_M : PRV_S,
                                          riscv_imsic_rmw, imsic);
        }
    }

    msi_nonbroken = true;
}

static const Property riscv_imsic_properties[] = {
    DEFINE_PROP_BOOL("mmode", RISCVIMSICState, mmode, 0),
    DEFINE_PROP_UINT32("hartid", RISCVIMSICState, hartid, 0),
    DEFINE_PROP_UINT32("num-pages", RISCVIMSICState, num_pages, 0),
    DEFINE_PROP_UINT32("num-irqs", RISCVIMSICState, num_irqs, 0),
};

static bool riscv_imsic_state_needed(void *opaque)
{
    return !kvm_irqchip_in_kernel();
}

static const VMStateDescription vmstate_riscv_imsic = {
    .name = "riscv_imsic",
    .version_id = 2,
    .minimum_version_id = 2,
    .needed = riscv_imsic_state_needed,
    .fields = (const VMStateField[]) {
            VMSTATE_VARRAY_UINT32(eidelivery, RISCVIMSICState,
                                  num_pages, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(eithreshold, RISCVIMSICState,
                                  num_pages, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(eistate, RISCVIMSICState,
                                  num_eistate, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_END_OF_LIST()
        }
};

static void riscv_imsic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, riscv_imsic_properties);
    dc->realize = riscv_imsic_realize;
    dc->vmsd = &vmstate_riscv_imsic;
}

static const TypeInfo riscv_imsic_info = {
    .name          = TYPE_RISCV_IMSIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVIMSICState),
    .class_init    = riscv_imsic_class_init,
};

static void riscv_imsic_register_types(void)
{
    type_register_static(&riscv_imsic_info);
}

type_init(riscv_imsic_register_types)

/*
 * Create IMSIC device.
 */
DeviceState *riscv_imsic_create(hwaddr addr, uint32_t hartid, bool mmode,
                                uint32_t num_pages, uint32_t num_ids)
{
    DeviceState *dev = qdev_new(TYPE_RISCV_IMSIC);
    CPUState *cpu = cpu_by_arch_id(hartid);
    uint32_t i;

    assert(!(addr & (IMSIC_MMIO_PAGE_SZ - 1)));
    if (mmode) {
        assert(num_pages == 1);
    } else {
        assert(num_pages >= 1 && num_pages <= (IRQ_LOCAL_GUEST_MAX + 1));
    }
    assert(IMSIC_MIN_ID <= num_ids);
    assert(num_ids <= IMSIC_MAX_ID);
    assert((num_ids & IMSIC_MIN_ID) == IMSIC_MIN_ID);

    qdev_prop_set_bit(dev, "mmode", mmode);
    qdev_prop_set_uint32(dev, "hartid", hartid);
    qdev_prop_set_uint32(dev, "num-pages", num_pages);
    qdev_prop_set_uint32(dev, "num-irqs", num_ids + 1);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    if (!kvm_irqchip_in_kernel()) {
        for (i = 0; i < num_pages; i++) {
            if (!i) {
                qdev_connect_gpio_out_named(dev, NULL, i,
                                            qdev_get_gpio_in(DEVICE(cpu),
                                            (mmode) ? IRQ_M_EXT : IRQ_S_EXT));
            } else {
                qdev_connect_gpio_out_named(dev, NULL, i,
                                            qdev_get_gpio_in(DEVICE(cpu),
                                            IRQ_LOCAL_MAX + i - 1));
            }
        }
    }

    return dev;
}
