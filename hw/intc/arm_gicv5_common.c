/*
 * Common base class for GICv5 IRS
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/intc/arm_gicv5_common.h"
#include "hw/intc/arm_gicv5_stream.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "trace.h"

OBJECT_DEFINE_ABSTRACT_TYPE(GICv5Common, gicv5_common, ARM_GICV5_COMMON, SYS_BUS_DEVICE)

/* Any value > 2^24 is out of the valid range for this property */
#define GICV5_SPI_IRS_RANGE_NOT_SET 0xffffffff

static bool bad_frame_accepts(void *opaque, hwaddr addr, unsigned size,
                              bool is_write, MemTxAttrs attrs)
{
    return false;
}

/*
 * Used for the sysbus MMIO regions corresponding to IRS frames where
 * this IRS does not implement the interrupt domain.  It's probably a
 * board/SoC error to create an IRS and try to wire up this MMIO
 * region, but if it does then the region will behave as unassigned
 * memory (generating a decode error).  These frames are just here so
 * that changing which domains are implemented doesn't reorder which
 * sysbus MMIO region is which.
 */
static const MemoryRegionOps bad_frame_ops = {
    .valid.accepts = bad_frame_accepts,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void gicv5_common_init_irqs_and_mmio(GICv5Common *cs,
                                     qemu_irq_handler handler,
                                     const MemoryRegionOps config_ops[NUM_GICV5_DOMAINS])
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(cs);

    if (cs->spi_irs_range) {
        qdev_init_gpio_in(DEVICE(cs), handler, cs->spi_irs_range);
    }

    for (int i = 0; i < NUM_GICV5_DOMAINS; i++) {
        g_autofree char *memname = g_strdup_printf("gicv5-irs-%d", i);
        const MemoryRegionOps *ops = gicv5_domain_implemented(cs, i) ?
            &config_ops[i] : &bad_frame_ops;
        memory_region_init_io(&cs->iomem[i], OBJECT(cs), ops, cs,
                              memname, IRS_CONFIG_FRAME_SIZE);
        sysbus_init_mmio(sbd, &cs->iomem[i]);
    }
}

static void gicv5_common_reset_hold(Object *obj, ResetType type)
{
    GICv5Common *cs = ARM_GICV5_COMMON(obj);

    memset(cs->irs_ist_baser, 0, sizeof(cs->irs_ist_baser));
    memset(cs->irs_ist_cfgr, 0, sizeof(cs->irs_ist_cfgr));
    memset(cs->irs_cr0, 0, sizeof(cs->irs_cr0));
    memset(cs->irs_cr1, 0, sizeof(cs->irs_cr1));
    memset(cs->irs_pe_selr, 0, sizeof(cs->irs_pe_selr));

    if (cs->spi) {
        GICv5Domain mp_domain;

        /*
         * D_YGLYC, D_TVVRZ: SPIs reset to edge-triggered, inactive,
         * idle, disabled, targeted routing mode, not assigned to a
         * VM, and assigned to the most-privileged interrupt domain.
         * Other state is UNKNOWN: we choose to zero it.
         */
        memset(cs->spi, 0, cs->spi_irs_range * sizeof(*cs->spi));

        /*
         * The most-privileged interrupt domain is effectively the
         * first in the list (EL3, S, NS) that we implement.
         */
        if (gicv5_domain_implemented(cs, GICV5_ID_EL3)) {
            mp_domain = GICV5_ID_EL3;
        } else if (gicv5_domain_implemented(cs, GICV5_ID_S)) {
            mp_domain = GICV5_ID_S;
        } else {
            mp_domain = GICV5_ID_NS;
        }

        for (int i = 0; i < cs->spi_irs_range; i++) {
            cs->spi[i].domain = mp_domain;
        }
    }

    for (int i = 0; i < NUM_GICV5_DOMAINS; i++) {
        /*
         * We reset irs_spi_selr to an invalid value so that our reset
         * value for IRS_SPI_STATUSR.V is correctly 0. The guest can
         * never read IRS_SPI_SELR directly.
         */
        cs->irs_spi_selr[i] = cs->spi_base + cs->spi_irs_range;
    }
}

static void gicv5_common_init(Object *obj)
{
}

static void gicv5_common_finalize(Object *obj)
{
    GICv5Common *cs = ARM_GICV5_COMMON(obj);

    g_free(cs->spi);
}

static void gicv5_common_realize(DeviceState *dev, Error **errp)
{
    GICv5Common *cs = ARM_GICV5_COMMON(dev);

    if (cs->num_cpus == 0) {
        error_setg(errp, "The cpus array property must have at least one CPU");
        return;
    }
    if (cs->num_cpus >= (1 << 16)) {
        /* We'll hit other QEMU limits long before this one :-) */
        error_setg(errp, "Number of CPUs exceeds GICv5 architectural maximum");
        return;
    }
    if (cs->num_cpus != cs->num_cpu_iaffids) {
        error_setg(errp, "The cpu-iaffids array property must be the same size "
                   "as the cpus array property");
        return;
    }
    if (cs->irsid >= (1 << 16)) {
        error_setg(errp, "irsid (%u) is more than 2^16-1", cs->irsid);
        return;
    }
    if (cs->spi_range > (1 << 24)) {
        /*
         * Note that IRS_IDR5.SPI_RANGE is a 25 bit field but the largest
         * architecturally permitted value is 2^24 (not 2^25-1), hence
         * use of > in the range check.
         */
        error_setg(errp, "spi-range (%u) is more than 2^24", cs->spi_range);
        return;
    }
    if (cs->spi_irs_range == GICV5_SPI_IRS_RANGE_NOT_SET) {
        /* spi-irs-range defaults to same as spi-range */
        cs->spi_irs_range = cs->spi_range;
    }
    if (cs->spi_irs_range > (1 << 24)) {
        /* Similarly IRS_IDR6.SPI_IRS_RANGE */
        error_setg(errp, "spi-irs-range (%u) is more than 2^24",
                   cs->spi_irs_range);
        return;
    }
    if (cs->spi_base >= (1 << 24)) {
        /* IRS_IDR7.SPI_BASE is a 24-bit field, so range check is >= */
        error_setg(errp, "spi-base (%u) is more than 2^24-1", cs->spi_base);
        return;
    }
    /* range checks above mean we know this addition won't overflow */
    if (cs->spi_base + cs->spi_irs_range > cs->spi_range) {
        error_setg(errp, "spi-base (%u) + spi-irs-range (%u) is "
                   "more than spi-range (%u)",
                   cs->spi_base, cs->spi_irs_range, cs->spi_range);
        return;
    }
    if (!cs->dma) {
        error_setg(errp, "sysmem link property not set");
        return;
    }

    for (int i = 0; i < cs->num_cpus; i++) {
        if (!gicv5_set_gicv5state(cs->cpus[i], cs, cs->cpu_iaffids[i], i)) {
            error_setg(errp,
                       "CPU %d (IAFFID 0x%x) does not implement GICv5 CPU interface",
                       i, cs->cpu_iaffids[i]);
            return;
        }
    }

    address_space_init(&cs->dma_as, cs->dma, "gicv5-sysmem");

    cs->spi = g_new0(GICv5SPIState, cs->spi_irs_range);

    trace_gicv5_common_realize(cs->irsid, cs->num_cpus,
                               cs->spi_base, cs->spi_irs_range, cs->spi_range);
}

static const Property arm_gicv5_common_properties[] = {
    DEFINE_PROP_LINK_ARRAY("cpus", GICv5Common, num_cpus,
                           cpus, TYPE_ARM_CPU, ARMCPU *),
    DEFINE_PROP_ARRAY("cpu-iaffids", GICv5Common, num_cpu_iaffids,
                      cpu_iaffids, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_UINT32("irsid", GICv5Common, irsid, 0),
    DEFINE_PROP_UINT32("spi-range", GICv5Common, spi_range, 0),
    DEFINE_PROP_UINT32("spi-base", GICv5Common, spi_base, 0),
    DEFINE_PROP_UINT32("spi-irs-range", GICv5Common, spi_irs_range,
                       GICV5_SPI_IRS_RANGE_NOT_SET),
    DEFINE_PROP_LINK("sysmem", GICv5Common, dma, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void gicv5_common_class_init(ObjectClass *oc, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    rc->phases.hold = gicv5_common_reset_hold;

    dc->realize = gicv5_common_realize;
    device_class_set_props(dc, arm_gicv5_common_properties);
}
