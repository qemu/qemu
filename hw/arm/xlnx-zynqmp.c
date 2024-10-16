/*
 * Xilinx Zynq MPSoC emulation
 *
 * Copyright (C) 2015 Xilinx Inc
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/xlnx-zynqmp.h"
#include "hw/intc/arm_gic_common.h"
#include "hw/misc/unimp.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "sysemu/sysemu.h"
#include "kvm_arm.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

#define GIC_NUM_SPI_INTR 160

#define ARM_PHYS_TIMER_PPI  30
#define ARM_VIRT_TIMER_PPI  27
#define ARM_HYP_TIMER_PPI   26
#define ARM_SEC_TIMER_PPI   29
#define GIC_MAINTENANCE_PPI 25

#define GEM_REVISION        0x40070106

#define GIC_BASE_ADDR       0xf9000000
#define GIC_DIST_ADDR       0xf9010000
#define GIC_CPU_ADDR        0xf9020000
#define GIC_VIFACE_ADDR     0xf9040000
#define GIC_VCPU_ADDR       0xf9060000

#define SATA_INTR           133
#define SATA_ADDR           0xFD0C0000
#define SATA_NUM_PORTS      2

#define QSPI_ADDR           0xff0f0000
#define LQSPI_ADDR          0xc0000000
#define QSPI_IRQ            15
#define QSPI_DMA_ADDR       0xff0f0800
#define NUM_QSPI_IRQ_LINES  2

#define CRF_ADDR            0xfd1a0000
#define CRF_IRQ             120

/* Serializer/Deserializer.  */
#define SERDES_ADDR         0xfd400000
#define SERDES_SIZE         0x20000

#define DP_ADDR             0xfd4a0000
#define DP_IRQ              0x77

#define DPDMA_ADDR          0xfd4c0000
#define DPDMA_IRQ           0x7a

#define APU_ADDR            0xfd5c0000
#define APU_IRQ             153

#define TTC0_ADDR           0xFF110000
#define TTC0_IRQ            36

#define IPI_ADDR            0xFF300000
#define IPI_IRQ             64

#define RTC_ADDR            0xffa60000
#define RTC_IRQ             26

#define BBRAM_ADDR          0xffcd0000
#define BBRAM_IRQ           11

#define EFUSE_ADDR          0xffcc0000
#define EFUSE_IRQ           87

#define SDHCI_CAPABILITIES  0x280737ec6481 /* Datasheet: UG1085 (v1.7) */

static const uint64_t gem_addr[XLNX_ZYNQMP_NUM_GEMS] = {
    0xFF0B0000, 0xFF0C0000, 0xFF0D0000, 0xFF0E0000,
};

static const int gem_intr[XLNX_ZYNQMP_NUM_GEMS] = {
    57, 59, 61, 63,
};

static const uint64_t uart_addr[XLNX_ZYNQMP_NUM_UARTS] = {
    0xFF000000, 0xFF010000,
};

static const int uart_intr[XLNX_ZYNQMP_NUM_UARTS] = {
    21, 22,
};

static const uint64_t can_addr[XLNX_ZYNQMP_NUM_CAN] = {
    0xFF060000, 0xFF070000,
};

static const int can_intr[XLNX_ZYNQMP_NUM_CAN] = {
    23, 24,
};

static const uint64_t sdhci_addr[XLNX_ZYNQMP_NUM_SDHCI] = {
    0xFF160000, 0xFF170000,
};

static const int sdhci_intr[XLNX_ZYNQMP_NUM_SDHCI] = {
    48, 49,
};

static const uint64_t spi_addr[XLNX_ZYNQMP_NUM_SPIS] = {
    0xFF040000, 0xFF050000,
};

static const int spi_intr[XLNX_ZYNQMP_NUM_SPIS] = {
    19, 20,
};

static const uint64_t gdma_ch_addr[XLNX_ZYNQMP_NUM_GDMA_CH] = {
    0xFD500000, 0xFD510000, 0xFD520000, 0xFD530000,
    0xFD540000, 0xFD550000, 0xFD560000, 0xFD570000
};

static const int gdma_ch_intr[XLNX_ZYNQMP_NUM_GDMA_CH] = {
    124, 125, 126, 127, 128, 129, 130, 131
};

static const uint64_t adma_ch_addr[XLNX_ZYNQMP_NUM_ADMA_CH] = {
    0xFFA80000, 0xFFA90000, 0xFFAA0000, 0xFFAB0000,
    0xFFAC0000, 0xFFAD0000, 0xFFAE0000, 0xFFAF0000
};

static const int adma_ch_intr[XLNX_ZYNQMP_NUM_ADMA_CH] = {
    77, 78, 79, 80, 81, 82, 83, 84
};

static const uint64_t usb_addr[XLNX_ZYNQMP_NUM_USB] = {
    0xFE200000, 0xFE300000
};

static const int usb_intr[XLNX_ZYNQMP_NUM_USB] = {
    65, 70
};

typedef struct XlnxZynqMPGICRegion {
    int region_index;
    uint32_t address;
    uint32_t offset;
    bool virt;
} XlnxZynqMPGICRegion;

static const XlnxZynqMPGICRegion xlnx_zynqmp_gic_regions[] = {
    /* Distributor */
    {
        .region_index = 0,
        .address = GIC_DIST_ADDR,
        .offset = 0,
        .virt = false
    },

    /* CPU interface */
    {
        .region_index = 1,
        .address = GIC_CPU_ADDR,
        .offset = 0,
        .virt = false
    },
    {
        .region_index = 1,
        .address = GIC_CPU_ADDR + 0x10000,
        .offset = 0x1000,
        .virt = false
    },

    /* Virtual interface */
    {
        .region_index = 2,
        .address = GIC_VIFACE_ADDR,
        .offset = 0,
        .virt = true
    },

    /* Virtual CPU interface */
    {
        .region_index = 3,
        .address = GIC_VCPU_ADDR,
        .offset = 0,
        .virt = true
    },
    {
        .region_index = 3,
        .address = GIC_VCPU_ADDR + 0x10000,
        .offset = 0x1000,
        .virt = true
    },
};

static inline int arm_gic_ppi_index(int cpu_nr, int ppi_index)
{
    return GIC_NUM_SPI_INTR + cpu_nr * GIC_INTERNAL + ppi_index;
}

static void xlnx_zynqmp_create_rpu(MachineState *ms, XlnxZynqMPState *s,
                                   const char *boot_cpu, Error **errp)
{
    int i;
    int num_rpus = MIN((int)(ms->smp.cpus - XLNX_ZYNQMP_NUM_APU_CPUS),
                       XLNX_ZYNQMP_NUM_RPU_CPUS);

    if (num_rpus <= 0) {
        /* Don't create rpu-cluster object if there's nothing to put in it */
        return;
    }

    object_initialize_child(OBJECT(s), "rpu-cluster", &s->rpu_cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->rpu_cluster), "cluster-id", 1);

    for (i = 0; i < num_rpus; i++) {
        const char *name;

        object_initialize_child(OBJECT(&s->rpu_cluster), "rpu-cpu[*]",
                                &s->rpu_cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-r5f"));

        name = object_get_canonical_path_component(OBJECT(&s->rpu_cpu[i]));
        if (strcmp(name, boot_cpu)) {
            /*
             * Secondary CPUs start in powered-down state.
             */
            object_property_set_bool(OBJECT(&s->rpu_cpu[i]),
                                     "start-powered-off", true, &error_abort);
        } else {
            s->boot_cpu_ptr = &s->rpu_cpu[i];
        }

        object_property_set_bool(OBJECT(&s->rpu_cpu[i]), "reset-hivecs", true,
                                 &error_abort);
        if (!qdev_realize(DEVICE(&s->rpu_cpu[i]), NULL, errp)) {
            return;
        }
    }

    qdev_realize(DEVICE(&s->rpu_cluster), NULL, &error_fatal);
}

static void xlnx_zynqmp_create_bbram(XlnxZynqMPState *s, qemu_irq *gic)
{
    SysBusDevice *sbd;

    object_initialize_child_with_props(OBJECT(s), "bbram", &s->bbram,
                                       sizeof(s->bbram), TYPE_XLNX_BBRAM,
                                       &error_fatal,
                                       "crc-zpads", "1",
                                       NULL);
    sbd = SYS_BUS_DEVICE(&s->bbram);

    sysbus_realize(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, BBRAM_ADDR);
    sysbus_connect_irq(sbd, 0, gic[BBRAM_IRQ]);
}

static void xlnx_zynqmp_create_efuse(XlnxZynqMPState *s, qemu_irq *gic)
{
    Object *bits = OBJECT(&s->efuse);
    Object *ctrl = OBJECT(&s->efuse_ctrl);
    SysBusDevice *sbd;

    object_initialize_child(OBJECT(s), "efuse-ctrl", &s->efuse_ctrl,
                            TYPE_XLNX_ZYNQMP_EFUSE);

    object_initialize_child_with_props(ctrl, "xlnx-efuse@0", bits,
                                       sizeof(s->efuse),
                                       TYPE_XLNX_EFUSE, &error_abort,
                                       "efuse-nr", "3",
                                       "efuse-size", "2048",
                                       NULL);

    qdev_realize(DEVICE(bits), NULL, &error_abort);
    object_property_set_link(ctrl, "efuse", bits, &error_abort);

    sbd = SYS_BUS_DEVICE(ctrl);
    sysbus_realize(sbd, &error_abort);
    sysbus_mmio_map(sbd, 0, EFUSE_ADDR);
    sysbus_connect_irq(sbd, 0, gic[EFUSE_IRQ]);
}

static void xlnx_zynqmp_create_apu_ctrl(XlnxZynqMPState *s, qemu_irq *gic)
{
    SysBusDevice *sbd;
    int i;

    object_initialize_child(OBJECT(s), "apu-ctrl", &s->apu_ctrl,
                            TYPE_XLNX_ZYNQMP_APU_CTRL);
    sbd = SYS_BUS_DEVICE(&s->apu_ctrl);

    for (i = 0; i < XLNX_ZYNQMP_NUM_APU_CPUS; i++) {
        g_autofree gchar *name = g_strdup_printf("cpu%d", i);

        object_property_set_link(OBJECT(&s->apu_ctrl), name,
                                 OBJECT(&s->apu_cpu[i]), &error_abort);
    }

    sysbus_realize(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, APU_ADDR);
    sysbus_connect_irq(sbd, 0, gic[APU_IRQ]);
}

static void xlnx_zynqmp_create_crf(XlnxZynqMPState *s, qemu_irq *gic)
{
    SysBusDevice *sbd;

    object_initialize_child(OBJECT(s), "crf", &s->crf, TYPE_XLNX_ZYNQMP_CRF);
    sbd = SYS_BUS_DEVICE(&s->crf);

    sysbus_realize(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, CRF_ADDR);
    sysbus_connect_irq(sbd, 0, gic[CRF_IRQ]);
}

static void xlnx_zynqmp_create_ttc(XlnxZynqMPState *s, qemu_irq *gic)
{
    SysBusDevice *sbd;
    int i, irq;

    for (i = 0; i < XLNX_ZYNQMP_NUM_TTC; i++) {
        object_initialize_child(OBJECT(s), "ttc[*]", &s->ttc[i],
                                TYPE_CADENCE_TTC);
        sbd = SYS_BUS_DEVICE(&s->ttc[i]);

        sysbus_realize(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, TTC0_ADDR + i * 0x10000);
        for (irq = 0; irq < 3; irq++) {
            sysbus_connect_irq(sbd, irq, gic[TTC0_IRQ + i * 3 + irq]);
        }
    }
}

static void xlnx_zynqmp_create_unimp_mmio(XlnxZynqMPState *s)
{
    static const struct UnimpInfo {
        const char *name;
        hwaddr base;
        hwaddr size;
    } unimp_areas[ARRAY_SIZE(s->mr_unimp)] = {
        { .name = "serdes", SERDES_ADDR, SERDES_SIZE },
    };
    unsigned int nr;

    for (nr = 0; nr < ARRAY_SIZE(unimp_areas); nr++) {
        const struct UnimpInfo *info = &unimp_areas[nr];
        DeviceState *dev = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);
        SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

        assert(info->name && info->base && info->size > 0);
        qdev_prop_set_string(dev, "name", info->name);
        qdev_prop_set_uint64(dev, "size", info->size);
        object_property_add_child(OBJECT(s), info->name, OBJECT(dev));

        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, info->base);
    }
}

static void xlnx_zynqmp_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    XlnxZynqMPState *s = XLNX_ZYNQMP(obj);
    int i;
    int num_apus = MIN(ms->smp.cpus, XLNX_ZYNQMP_NUM_APU_CPUS);

    object_initialize_child(obj, "apu-cluster", &s->apu_cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->apu_cluster), "cluster-id", 0);

    for (i = 0; i < num_apus; i++) {
        object_initialize_child(OBJECT(&s->apu_cluster), "apu-cpu[*]",
                                &s->apu_cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a53"));
    }

    object_initialize_child(obj, "gic", &s->gic, gic_class_name());

    for (i = 0; i < XLNX_ZYNQMP_NUM_GEMS; i++) {
        object_initialize_child(obj, "gem[*]", &s->gem[i], TYPE_CADENCE_GEM);
        object_initialize_child(obj, "gem-irq-orgate[*]",
                                &s->gem_irq_orgate[i], TYPE_OR_IRQ);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_UARTS; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i],
                                TYPE_CADENCE_UART);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_CAN; i++) {
        object_initialize_child(obj, "can[*]", &s->can[i],
                                TYPE_XLNX_ZYNQMP_CAN);
    }

    object_initialize_child(obj, "sata", &s->sata, TYPE_SYSBUS_AHCI);

    for (i = 0; i < XLNX_ZYNQMP_NUM_SDHCI; i++) {
        object_initialize_child(obj, "sdhci[*]", &s->sdhci[i],
                                TYPE_SYSBUS_SDHCI);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_SPIS; i++) {
        object_initialize_child(obj, "spi[*]", &s->spi[i], TYPE_XILINX_SPIPS);
    }

    object_initialize_child(obj, "qspi", &s->qspi, TYPE_XLNX_ZYNQMP_QSPIPS);

    object_initialize_child(obj, "xxxdp", &s->dp, TYPE_XLNX_DP);

    object_initialize_child(obj, "dp-dma", &s->dpdma, TYPE_XLNX_DPDMA);

    object_initialize_child(obj, "ipi", &s->ipi, TYPE_XLNX_ZYNQMP_IPI);

    object_initialize_child(obj, "rtc", &s->rtc, TYPE_XLNX_ZYNQMP_RTC);

    for (i = 0; i < XLNX_ZYNQMP_NUM_GDMA_CH; i++) {
        object_initialize_child(obj, "gdma[*]", &s->gdma[i], TYPE_XLNX_ZDMA);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_ADMA_CH; i++) {
        object_initialize_child(obj, "adma[*]", &s->adma[i], TYPE_XLNX_ZDMA);
    }

    object_initialize_child(obj, "qspi-dma", &s->qspi_dma, TYPE_XLNX_CSU_DMA);
    object_initialize_child(obj, "qspi-irq-orgate",
                            &s->qspi_irq_orgate, TYPE_OR_IRQ);

    for (i = 0; i < XLNX_ZYNQMP_NUM_USB; i++) {
        object_initialize_child(obj, "usb[*]", &s->usb[i], TYPE_USB_DWC3);
    }
}

static void xlnx_zynqmp_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    XlnxZynqMPState *s = XLNX_ZYNQMP(dev);
    MemoryRegion *system_memory = get_system_memory();
    uint8_t i;
    uint64_t ram_size;
    int num_apus = MIN(ms->smp.cpus, XLNX_ZYNQMP_NUM_APU_CPUS);
    const char *boot_cpu = s->boot_cpu ? s->boot_cpu : "apu-cpu[0]";
    ram_addr_t ddr_low_size, ddr_high_size;
    qemu_irq gic_spi[GIC_NUM_SPI_INTR];
    Error *err = NULL;

    ram_size = memory_region_size(s->ddr_ram);

    /*
     * Create the DDR Memory Regions. User friendly checks should happen at
     * the board level
     */
    if (ram_size > XLNX_ZYNQMP_MAX_LOW_RAM_SIZE) {
        /*
         * The RAM size is above the maximum available for the low DDR.
         * Create the high DDR memory region as well.
         */
        assert(ram_size <= XLNX_ZYNQMP_MAX_RAM_SIZE);
        ddr_low_size = XLNX_ZYNQMP_MAX_LOW_RAM_SIZE;
        ddr_high_size = ram_size - XLNX_ZYNQMP_MAX_LOW_RAM_SIZE;

        memory_region_init_alias(&s->ddr_ram_high, OBJECT(dev),
                                 "ddr-ram-high", s->ddr_ram, ddr_low_size,
                                 ddr_high_size);
        memory_region_add_subregion(get_system_memory(),
                                    XLNX_ZYNQMP_HIGH_RAM_START,
                                    &s->ddr_ram_high);
    } else {
        /* RAM must be non-zero */
        assert(ram_size);
        ddr_low_size = ram_size;
    }

    memory_region_init_alias(&s->ddr_ram_low, OBJECT(dev), "ddr-ram-low",
                             s->ddr_ram, 0, ddr_low_size);
    memory_region_add_subregion(get_system_memory(), 0, &s->ddr_ram_low);

    /* Create the four OCM banks */
    for (i = 0; i < XLNX_ZYNQMP_NUM_OCM_BANKS; i++) {
        char *ocm_name = g_strdup_printf("zynqmp.ocm_ram_bank_%d", i);

        memory_region_init_ram(&s->ocm_ram[i], NULL, ocm_name,
                               XLNX_ZYNQMP_OCM_RAM_SIZE, &error_fatal);
        memory_region_add_subregion(get_system_memory(),
                                    XLNX_ZYNQMP_OCM_RAM_0_ADDRESS +
                                        i * XLNX_ZYNQMP_OCM_RAM_SIZE,
                                    &s->ocm_ram[i]);

        g_free(ocm_name);
    }

    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", GIC_NUM_SPI_INTR + 32);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 2);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", num_apus);
    qdev_prop_set_bit(DEVICE(&s->gic), "has-security-extensions", s->secure);
    qdev_prop_set_bit(DEVICE(&s->gic),
                      "has-virtualization-extensions", s->virt);

    qdev_realize(DEVICE(&s->apu_cluster), NULL, &error_fatal);

    /* Realize APUs before realizing the GIC. KVM requires this.  */
    for (i = 0; i < num_apus; i++) {
        const char *name;

        name = object_get_canonical_path_component(OBJECT(&s->apu_cpu[i]));
        if (strcmp(name, boot_cpu)) {
            /*
             * Secondary CPUs start in powered-down state.
             */
            object_property_set_bool(OBJECT(&s->apu_cpu[i]),
                                     "start-powered-off", true, &error_abort);
        } else {
            s->boot_cpu_ptr = &s->apu_cpu[i];
        }

        object_property_set_bool(OBJECT(&s->apu_cpu[i]), "has_el3", s->secure,
                                 NULL);
        object_property_set_bool(OBJECT(&s->apu_cpu[i]), "has_el2", s->virt,
                                 NULL);
        object_property_set_int(OBJECT(&s->apu_cpu[i]), "reset-cbar",
                                GIC_BASE_ADDR, &error_abort);
        object_property_set_int(OBJECT(&s->apu_cpu[i]), "core-count",
                                num_apus, &error_abort);
        if (!qdev_realize(DEVICE(&s->apu_cpu[i]), NULL, errp)) {
            return;
        }
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gic), errp)) {
        return;
    }

    assert(ARRAY_SIZE(xlnx_zynqmp_gic_regions) == XLNX_ZYNQMP_GIC_REGIONS);
    for (i = 0; i < XLNX_ZYNQMP_GIC_REGIONS; i++) {
        SysBusDevice *gic = SYS_BUS_DEVICE(&s->gic);
        const XlnxZynqMPGICRegion *r = &xlnx_zynqmp_gic_regions[i];
        MemoryRegion *mr;
        uint32_t addr = r->address;
        int j;

        if (r->virt && !s->virt) {
            continue;
        }

        mr = sysbus_mmio_get_region(gic, r->region_index);
        for (j = 0; j < XLNX_ZYNQMP_GIC_ALIASES; j++) {
            MemoryRegion *alias = &s->gic_mr[i][j];

            memory_region_init_alias(alias, OBJECT(s), "zynqmp-gic-alias", mr,
                                     r->offset, XLNX_ZYNQMP_GIC_REGION_SIZE);
            memory_region_add_subregion(system_memory, addr, alias);

            addr += XLNX_ZYNQMP_GIC_REGION_SIZE;
        }
    }

    for (i = 0; i < num_apus; i++) {
        qemu_irq irq;

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_IRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + num_apus,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_FIQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + num_apus * 2,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_VIRQ));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + num_apus * 3,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_VFIQ));
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_PHYS_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->apu_cpu[i]), GTIMER_PHYS, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_VIRT_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->apu_cpu[i]), GTIMER_VIRT, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_HYP_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->apu_cpu[i]), GTIMER_HYP, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_SEC_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->apu_cpu[i]), GTIMER_SEC, irq);

        if (s->virt) {
            irq = qdev_get_gpio_in(DEVICE(&s->gic),
                                   arm_gic_ppi_index(i, GIC_MAINTENANCE_PPI));
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i + num_apus * 4, irq);
        }
    }

    xlnx_zynqmp_create_rpu(ms, s, boot_cpu, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (!s->boot_cpu_ptr) {
        error_setg(errp, "ZynqMP Boot cpu %s not found", boot_cpu);
        return;
    }

    for (i = 0; i < GIC_NUM_SPI_INTR; i++) {
        gic_spi[i] = qdev_get_gpio_in(DEVICE(&s->gic), i);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_GEMS; i++) {
        qemu_configure_nic_device(DEVICE(&s->gem[i]), true, NULL);
        object_property_set_int(OBJECT(&s->gem[i]), "revision", GEM_REVISION,
                                &error_abort);
        object_property_set_int(OBJECT(&s->gem[i]), "phy-addr", 23,
                                &error_abort);
        object_property_set_int(OBJECT(&s->gem[i]), "num-priority-queues", 2,
                                &error_abort);
        object_property_set_int(OBJECT(&s->gem_irq_orgate[i]),
                                "num-lines", 2, &error_fatal);
        qdev_realize(DEVICE(&s->gem_irq_orgate[i]), NULL, &error_fatal);
        qdev_connect_gpio_out(DEVICE(&s->gem_irq_orgate[i]), 0, gic_spi[gem_intr[i]]);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gem[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gem[i]), 0, gem_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gem[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->gem_irq_orgate[i]), 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gem[i]), 1,
                           qdev_get_gpio_in(DEVICE(&s->gem_irq_orgate[i]), 1));
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_UARTS; i++) {
        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, uart_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           gic_spi[uart_intr[i]]);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_CAN; i++) {
        object_property_set_int(OBJECT(&s->can[i]), "ext_clk_freq",
                                XLNX_ZYNQMP_CAN_REF_CLK, &error_abort);

        object_property_set_link(OBJECT(&s->can[i]), "canbus",
                                 OBJECT(s->canbus[i]), &error_fatal);

        sysbus_realize(SYS_BUS_DEVICE(&s->can[i]), &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->can[i]), 0, can_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->can[i]), 0,
                           gic_spi[can_intr[i]]);
    }

    object_property_set_int(OBJECT(&s->sata), "num-ports", SATA_NUM_PORTS,
                            &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sata), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sata), 0, SATA_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sata), 0, gic_spi[SATA_INTR]);

    for (i = 0; i < XLNX_ZYNQMP_NUM_SDHCI; i++) {
        char *bus_name;
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->sdhci[i]);
        Object *sdhci = OBJECT(&s->sdhci[i]);

        /*
         * Compatible with:
         * - SD Host Controller Specification Version 3.00
         * - SDIO Specification Version 3.0
         * - eMMC Specification Version 4.51
         */
        if (!object_property_set_uint(sdhci, "sd-spec-version", 3, errp)) {
            return;
        }
        if (!object_property_set_uint(sdhci, "capareg", SDHCI_CAPABILITIES,
                                      errp)) {
            return;
        }
        if (!object_property_set_uint(sdhci, "uhs", UHS_I, errp)) {
            return;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(sdhci), errp)) {
            return;
        }
        sysbus_mmio_map(sbd, 0, sdhci_addr[i]);
        sysbus_connect_irq(sbd, 0, gic_spi[sdhci_intr[i]]);

        /* Alias controller SD bus to the SoC itself */
        bus_name = g_strdup_printf("sd-bus%d", i);
        object_property_add_alias(OBJECT(s), bus_name, sdhci, "sd-bus");
        g_free(bus_name);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_SPIS; i++) {
        gchar *bus_name;

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0, spi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           gic_spi[spi_intr[i]]);

        /* Alias controller SPI bus to the SoC itself */
        bus_name = g_strdup_printf("spi%d", i);
        object_property_add_alias(OBJECT(s), bus_name,
                                  OBJECT(&s->spi[i]), "spi0");
        g_free(bus_name);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dp), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dp), 0, DP_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dp), 0, gic_spi[DP_IRQ]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dpdma), errp)) {
        return;
    }
    object_property_set_link(OBJECT(&s->dp), "dpdma", OBJECT(&s->dpdma),
                             &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dpdma), 0, DPDMA_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dpdma), 0, gic_spi[DPDMA_IRQ]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ipi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ipi), 0, IPI_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ipi), 0, gic_spi[IPI_IRQ]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rtc), 0, RTC_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0, gic_spi[RTC_IRQ]);

    xlnx_zynqmp_create_bbram(s, gic_spi);
    xlnx_zynqmp_create_efuse(s, gic_spi);
    xlnx_zynqmp_create_apu_ctrl(s, gic_spi);
    xlnx_zynqmp_create_crf(s, gic_spi);
    xlnx_zynqmp_create_ttc(s, gic_spi);
    xlnx_zynqmp_create_unimp_mmio(s);

    for (i = 0; i < XLNX_ZYNQMP_NUM_GDMA_CH; i++) {
        if (!object_property_set_uint(OBJECT(&s->gdma[i]), "bus-width", 128,
                                      errp)) {
            return;
        }
        if (!object_property_set_link(OBJECT(&s->gdma[i]), "dma",
                                      OBJECT(system_memory), errp)) {
            return;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gdma[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gdma[i]), 0, gdma_ch_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gdma[i]), 0,
                           gic_spi[gdma_ch_intr[i]]);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_ADMA_CH; i++) {
        if (!object_property_set_link(OBJECT(&s->adma[i]), "dma",
                                      OBJECT(system_memory), errp)) {
            return;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->adma[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->adma[i]), 0, adma_ch_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->adma[i]), 0,
                           gic_spi[adma_ch_intr[i]]);
    }

    object_property_set_int(OBJECT(&s->qspi_irq_orgate),
                            "num-lines", NUM_QSPI_IRQ_LINES, &error_fatal);
    qdev_realize(DEVICE(&s->qspi_irq_orgate), NULL, &error_fatal);
    qdev_connect_gpio_out(DEVICE(&s->qspi_irq_orgate), 0, gic_spi[QSPI_IRQ]);

    if (!object_property_set_link(OBJECT(&s->qspi_dma), "dma",
                                  OBJECT(system_memory), errp)) {
        return;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->qspi_dma), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->qspi_dma), 0, QSPI_DMA_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->qspi_dma), 0,
                       qdev_get_gpio_in(DEVICE(&s->qspi_irq_orgate), 0));

    if (!object_property_set_link(OBJECT(&s->qspi), "stream-connected-dma",
                                  OBJECT(&s->qspi_dma), errp)) {
         return;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->qspi), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->qspi), 0, QSPI_ADDR);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->qspi), 1, LQSPI_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->qspi), 0,
                       qdev_get_gpio_in(DEVICE(&s->qspi_irq_orgate), 1));

    for (i = 0; i < XLNX_ZYNQMP_NUM_QSPI_BUS; i++) {
        g_autofree gchar *bus_name = g_strdup_printf("qspi%d", i);
        g_autofree gchar *target_bus = g_strdup_printf("spi%d", i);

        /* Alias controller SPI bus to the SoC itself */
        object_property_add_alias(OBJECT(s), bus_name,
                                  OBJECT(&s->qspi), target_bus);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_USB; i++) {
        if (!object_property_set_link(OBJECT(&s->usb[i].sysbus_xhci), "dma",
                                      OBJECT(system_memory), errp)) {
            return;
        }

        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "intrs", 4);
        qdev_prop_set_uint32(DEVICE(&s->usb[i].sysbus_xhci), "slots", 2);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usb[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb[i]), 0, usb_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i].sysbus_xhci), 0,
                           gic_spi[usb_intr[i]]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i].sysbus_xhci), 1,
                           gic_spi[usb_intr[i] + 1]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i].sysbus_xhci), 2,
                           gic_spi[usb_intr[i] + 2]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i].sysbus_xhci), 3,
                           gic_spi[usb_intr[i] + 3]);
    }
}

static Property xlnx_zynqmp_props[] = {
    DEFINE_PROP_STRING("boot-cpu", XlnxZynqMPState, boot_cpu),
    DEFINE_PROP_BOOL("secure", XlnxZynqMPState, secure, false),
    DEFINE_PROP_BOOL("virtualization", XlnxZynqMPState, virt, false),
    DEFINE_PROP_LINK("ddr-ram", XlnxZynqMPState, ddr_ram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("canbus0", XlnxZynqMPState, canbus[0], TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_LINK("canbus1", XlnxZynqMPState, canbus[1], TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_END_OF_LIST()
};

static void xlnx_zynqmp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, xlnx_zynqmp_props);
    dc->realize = xlnx_zynqmp_realize;
    /* Reason: Uses serial_hds in realize function, thus can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo xlnx_zynqmp_type_info = {
    .name = TYPE_XLNX_ZYNQMP,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XlnxZynqMPState),
    .instance_init = xlnx_zynqmp_init,
    .class_init = xlnx_zynqmp_class_init,
};

static void xlnx_zynqmp_register_types(void)
{
    type_register_static(&xlnx_zynqmp_type_info);
}

type_init(xlnx_zynqmp_register_types)
