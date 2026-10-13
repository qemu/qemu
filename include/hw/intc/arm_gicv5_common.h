/*
 * Common base class for GICv5 IRS
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICV5_COMMON_H
#define HW_INTC_ARM_GICV5_COMMON_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/intc/arm_gicv5_types.h"
#include "target/arm/cpu-qom.h"
#include "qemu/error-report.h"
#include "system/kvm.h"

/*
 * QEMU interface:
 *
 * + QOM array property "cpus": CPUState pointers to each CPU
 *   connected to this IRS.
 * + QOM array property "cpu-iaffids": array of uint32_t giving the
 *   IAFFID for each CPU in the "cpus" property array
 * + QOM property "irsid": unique identifier for this IRS in the system
 *   (this is IRS_IDR0.IRSID); default is 0
 * + QOM property "spi-range": total number of SPIs in the system
 *   IRS (this is IRS_IDR5.SPI_RANGE); must be set
 * + QOM property "spi-base": minimum SPI INTID.ID implemented on this
 *   IRS (this is IRS_IDR7.SPI_BASE); default is 0
 * + QOM property "spi-irs-range": number of SPI INTID.ID managed on this
 *   IRS (this is IRS_IDR6.SPI_IRS_RANGE); defaults to value of spi-range
 * + unnamed GPIO inputs: the SPIs handled by this IRS
 *   (so GPIO input 0 is the SPI with INTID SPI_BASE, input 1 is
 *   SPI_BASE + 1, and so on up to SPI_BASE + SPI_IRS_RANGE - 1)
 *
 * sysbus MMIO regions (in order matching IRS_IDR0.INT_DOM encoding):
 * - IRS config frame for the Secure Interrupt Domain
 * - IRS config frame for the Non-secure Interrupt Domain
 * - IRS config frame for the EL3 Interrupt Domain
 * - IRS config frame for the Realm Interrupt Domain
 *
 * Note that even if this particular IRS does not implement all four
 * interrupt domains it will still expose four sysbus MMIO regions.
 * The regions corresponding to unimplemented domains will always fail
 * accesses with a decode error. Generally the SoC/board should
 * probably not map a region for a domain that it configured the IRS
 * to not implement; the regions are only exposed so that changing
 * which domains are implemented doesn't reorder which sysbus MMIO
 * region is which (e.g. NS will always be 1 and EL3 will always be 2).
 */

#define TYPE_ARM_GICV5_COMMON "arm-gicv5-common"

OBJECT_DECLARE_TYPE(GICv5Common, GICv5CommonClass, ARM_GICV5_COMMON)

/*
 * This is where we store the state the IRS handles for an SPI.
 * Generally this corresponds to the spec's list of state in I_JVVTZ
 * and J_BWPPP. level is a QEMU implementation detail and is where we
 * store the actual current state of the incoming qemu_irq line.
 */
typedef struct GICv5SPIState {
    uint32_t iaffid;
    uint8_t priority;
    bool level;
    bool pending;
    bool active;
    bool enabled;
    GICv5HandlingMode hm;
    GICv5RoutingMode irm;
    GICv5TriggerMode tm;
    GICv5Domain domain;
} GICv5SPIState;

/*
 * This class is for common state that will eventually be shared
 * between TCG and KVM implementations of the GICv5.
 */
struct GICv5Common {
    SysBusDevice parent_obj;

    MemoryRegion iomem[NUM_GICV5_DOMAINS];

    uint64_t irs_ist_baser[NUM_GICV5_DOMAINS];
    uint32_t irs_ist_cfgr[NUM_GICV5_DOMAINS];
    uint32_t irs_spi_selr[NUM_GICV5_DOMAINS];
    uint32_t irs_cr0[NUM_GICV5_DOMAINS];
    uint32_t irs_cr1[NUM_GICV5_DOMAINS];
    uint32_t irs_pe_selr[NUM_GICV5_DOMAINS];

    /*
     * Pointer to an array of state information for the SPIs.  Array
     * element 0 is SPI ID s->spi_base, and there are s->spi_irs_range
     * elements in total. SPI state is not per-domain: SPI is
     * configurable to a particular domain via IRS_SPI_DOMAINR.
     */
    GICv5SPIState *spi;

    /* Bits here are set for each physical interrupt domain implemented */
    uint8_t implemented_domains;

    /* ID register values: set at realize, constant thereafter */
    uint32_t irs_idr0;
    uint32_t irs_idr1;
    uint32_t irs_idr2;
    uint32_t irs_idr3;
    uint32_t irs_idr4;
    uint32_t irs_idr5;
    uint32_t irs_idr6;
    uint32_t irs_idr7;
    uint32_t irs_iidr;
    uint32_t irs_aidr;

    /* Properties */
    uint32_t num_cpus;
    ARMCPU **cpus;
    uint32_t num_cpu_iaffids;
    uint32_t *cpu_iaffids;

    /* MemoryRegion and AS to DMA to/from for in-memory data structures */
    MemoryRegion *dma;
    AddressSpace dma_as;

    uint32_t irsid;
    uint32_t spi_base;
    uint32_t spi_irs_range;
    uint32_t spi_range;
};

struct GICv5CommonClass {
    SysBusDeviceClass parent_class;
};


#define IRS_CONFIG_FRAME_SIZE 0x10000

/*
 * The architecture allows a GICv5 to implement less than the full
 * width for various ID fields. QEMU's implementation always supports
 * the full width of these fields. These constants define our
 * implementation's limits.
 */

/* Number of INTID.ID bits we support */
#define QEMU_GICV5_ID_BITS 24
/* Min LPI_ID_BITS supported */
#define QEMU_GICV5_MIN_LPI_ID_BITS 0
/* IAFFID bits supported */
#define QEMU_GICV5_IAFFID_BITS 16
/* Number of priority bits supported in the IRS */
#define QEMU_GICV5_PRI_BITS 5

/*
 * There are no TRMs currently published for hardware implementations
 * of GICv5 that we might identify ourselves as. Instead, we borrow
 * the Arm Implementer code and pick a fake product ID that is
 * unlikely to be used by any real Arm hardware.
 */
#define QEMU_GICV5_IMPLEMENTER 0x43b
#define QEMU_GICV5_PRODUCTID 0xfff
#define QEMU_GICV5_REVISION 0
#define QEMU_GICV5_VARIANT 0

/**
 * gicv5_common_init_irqs_and_mmio: Create IRQs and MMIO regions for the GICv5
 * @s: GIC object
 * @ops: array of MemoryRegionOps that implement the config frames behaviour
 *
 * Subclasses of ARM_GICV5_COMMON should call this to create the sysbus
 * MemoryRegions for the IRS config frames, passing in a four element array
 * of MemoryRegionOps structs.
 */
void gicv5_common_init_irqs_and_mmio(GICv5Common *cs,
                                     qemu_irq_handler handler,
                                     const MemoryRegionOps ops[NUM_GICV5_DOMAINS]);

/**
 * gicv5_domain_implemented: Return true if this IRS implements this domain
 * @s: GIC object
 * @domain: domain to check
 */
static inline bool gicv5_domain_implemented(GICv5Common *cs, GICv5Domain domain)
{
    return cs->implemented_domains & (1 << domain);
}

/**
 * gicv5_class_name
 *
 * Return name of GICv5 class to use depending on whether KVM acceleration is
 * in use. May throw an error if the chosen implementation is not available.
 *
 * Returns: class name to use
 */
static inline const char *gicv5_class_name(void)
{
    /* When we implement KVM GICv5 we might return "kvm-arm-gicv5" here. */
    if (kvm_enabled()) {
        error_report("Userspace GICv5 is not supported with KVM");
        exit(1);
    }
    return "arm-gicv5";
}

/**
 * gicv5_raw_spi_state
 * @cs: GIC object
 * @id: INTID of SPI to look up
 *
 * Return pointer to the GICv5SPIState for this SPI, or NULL if the
 * interrupt ID is out of range. This does not do a check that the SPI
 * is assigned to the right domain: generally you should call it via
 * some other wrapper that performs an appropriate further check.
 */
static inline GICv5SPIState *gicv5_raw_spi_state(GICv5Common *cs, uint32_t id)
{
    if (id < cs->spi_base || id >= cs->spi_base + cs->spi_irs_range) {
        return NULL;
    }

    return cs->spi + (id - cs->spi_base);
}

/**
 * gicv5_spi_state:
 * @cs: GIC object
 * @id: INTID of SPI to look up
 * @domain: domain to check
 *
 * Return pointer to the GICv5SPIState for this SPI, or NULL if the
 * interrupt is unreachable (which can be because the INTID is out of
 * range, or because the SPI is configured for a different domain).
 */
static inline GICv5SPIState *gicv5_spi_state(GICv5Common *cs, uint32_t id,
                                             GICv5Domain domain)
{
    GICv5SPIState *spi = gicv5_raw_spi_state(cs, id);

    if (!spi || spi->domain != domain) {
        return NULL;
    }
    return spi;
}

#endif
