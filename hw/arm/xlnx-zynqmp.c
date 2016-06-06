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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/arm/xlnx-zynqmp.h"
#include "hw/intc/arm_gic_common.h"
#include "exec/address-spaces.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"

#define GIC_NUM_SPI_INTR 160

#define ARM_PHYS_TIMER_PPI  30
#define ARM_VIRT_TIMER_PPI  27

#define GIC_BASE_ADDR       0xf9000000
#define GIC_DIST_ADDR       0xf9010000
#define GIC_CPU_ADDR        0xf9020000

#define SATA_INTR           133
#define SATA_ADDR           0xFD0C0000
#define SATA_NUM_PORTS      2

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

typedef struct XlnxZynqMPGICRegion {
    int region_index;
    uint32_t address;
} XlnxZynqMPGICRegion;

static const XlnxZynqMPGICRegion xlnx_zynqmp_gic_regions[] = {
    { .region_index = 0, .address = GIC_DIST_ADDR, },
    { .region_index = 1, .address = GIC_CPU_ADDR,  },
};

static inline int arm_gic_ppi_index(int cpu_nr, int ppi_index)
{
    return GIC_NUM_SPI_INTR + cpu_nr * GIC_INTERNAL + ppi_index;
}

static void xlnx_zynqmp_create_rpu(XlnxZynqMPState *s, const char *boot_cpu,
                                   Error **errp)
{
    Error *err = NULL;
    int i;

    for (i = 0; i < XLNX_ZYNQMP_NUM_RPU_CPUS; i++) {
        char *name;

        object_initialize(&s->rpu_cpu[i], sizeof(s->rpu_cpu[i]),
                          "cortex-r5-" TYPE_ARM_CPU);
        object_property_add_child(OBJECT(s), "rpu-cpu[*]",
                                  OBJECT(&s->rpu_cpu[i]), &error_abort);

        name = object_get_canonical_path_component(OBJECT(&s->rpu_cpu[i]));
        if (strcmp(name, boot_cpu)) {
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(OBJECT(&s->rpu_cpu[i]), true,
                                     "start-powered-off", &error_abort);
        } else {
            s->boot_cpu_ptr = &s->rpu_cpu[i];
        }
        g_free(name);

        object_property_set_bool(OBJECT(&s->rpu_cpu[i]), true, "reset-hivecs",
                                 &error_abort);
        object_property_set_bool(OBJECT(&s->rpu_cpu[i]), true, "realized",
                                 &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }
}

static void xlnx_zynqmp_init(Object *obj)
{
    XlnxZynqMPState *s = XLNX_ZYNQMP(obj);
    int i;

    for (i = 0; i < XLNX_ZYNQMP_NUM_APU_CPUS; i++) {
        object_initialize(&s->apu_cpu[i], sizeof(s->apu_cpu[i]),
                          "cortex-a53-" TYPE_ARM_CPU);
        object_property_add_child(obj, "apu-cpu[*]", OBJECT(&s->apu_cpu[i]),
                                  &error_abort);
    }

    object_property_add_link(obj, "ddr-ram", TYPE_MEMORY_REGION,
                             (Object **)&s->ddr_ram,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, &error_abort);

    object_initialize(&s->gic, sizeof(s->gic), gic_class_name());
    qdev_set_parent_bus(DEVICE(&s->gic), sysbus_get_default());

    for (i = 0; i < XLNX_ZYNQMP_NUM_GEMS; i++) {
        object_initialize(&s->gem[i], sizeof(s->gem[i]), TYPE_CADENCE_GEM);
        qdev_set_parent_bus(DEVICE(&s->gem[i]), sysbus_get_default());
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_UARTS; i++) {
        object_initialize(&s->uart[i], sizeof(s->uart[i]), TYPE_CADENCE_UART);
        qdev_set_parent_bus(DEVICE(&s->uart[i]), sysbus_get_default());
    }

    object_initialize(&s->sata, sizeof(s->sata), TYPE_SYSBUS_AHCI);
    qdev_set_parent_bus(DEVICE(&s->sata), sysbus_get_default());

    for (i = 0; i < XLNX_ZYNQMP_NUM_SDHCI; i++) {
        object_initialize(&s->sdhci[i], sizeof(s->sdhci[i]),
                          TYPE_SYSBUS_SDHCI);
        qdev_set_parent_bus(DEVICE(&s->sdhci[i]),
                            sysbus_get_default());
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_SPIS; i++) {
        object_initialize(&s->spi[i], sizeof(s->spi[i]),
                          TYPE_XILINX_SPIPS);
        qdev_set_parent_bus(DEVICE(&s->spi[i]), sysbus_get_default());
    }
}

static void xlnx_zynqmp_realize(DeviceState *dev, Error **errp)
{
    XlnxZynqMPState *s = XLNX_ZYNQMP(dev);
    MemoryRegion *system_memory = get_system_memory();
    uint8_t i;
    uint64_t ram_size;
    const char *boot_cpu = s->boot_cpu ? s->boot_cpu : "apu-cpu[0]";
    ram_addr_t ddr_low_size, ddr_high_size;
    qemu_irq gic_spi[GIC_NUM_SPI_INTR];
    Error *err = NULL;

    ram_size = memory_region_size(s->ddr_ram);

    /* Create the DDR Memory Regions. User friendly checks should happen at
     * the board level
     */
    if (ram_size > XLNX_ZYNQMP_MAX_LOW_RAM_SIZE) {
        /* The RAM size is above the maximum available for the low DDR.
         * Create the high DDR memory region as well.
         */
        assert(ram_size <= XLNX_ZYNQMP_MAX_RAM_SIZE);
        ddr_low_size = XLNX_ZYNQMP_MAX_LOW_RAM_SIZE;
        ddr_high_size = ram_size - XLNX_ZYNQMP_MAX_LOW_RAM_SIZE;

        memory_region_init_alias(&s->ddr_ram_high, NULL,
                                 "ddr-ram-high", s->ddr_ram,
                                  ddr_low_size, ddr_high_size);
        memory_region_add_subregion(get_system_memory(),
                                    XLNX_ZYNQMP_HIGH_RAM_START,
                                    &s->ddr_ram_high);
    } else {
        /* RAM must be non-zero */
        assert(ram_size);
        ddr_low_size = ram_size;
    }

    memory_region_init_alias(&s->ddr_ram_low, NULL,
                             "ddr-ram-low", s->ddr_ram,
                              0, ddr_low_size);
    memory_region_add_subregion(get_system_memory(), 0, &s->ddr_ram_low);

    /* Create the four OCM banks */
    for (i = 0; i < XLNX_ZYNQMP_NUM_OCM_BANKS; i++) {
        char *ocm_name = g_strdup_printf("zynqmp.ocm_ram_bank_%d", i);

        memory_region_init_ram(&s->ocm_ram[i], NULL, ocm_name,
                               XLNX_ZYNQMP_OCM_RAM_SIZE, &error_fatal);
        vmstate_register_ram_global(&s->ocm_ram[i]);
        memory_region_add_subregion(get_system_memory(),
                                    XLNX_ZYNQMP_OCM_RAM_0_ADDRESS +
                                        i * XLNX_ZYNQMP_OCM_RAM_SIZE,
                                    &s->ocm_ram[i]);

        g_free(ocm_name);
    }

    qdev_prop_set_uint32(DEVICE(&s->gic), "num-irq", GIC_NUM_SPI_INTR + 32);
    qdev_prop_set_uint32(DEVICE(&s->gic), "revision", 2);
    qdev_prop_set_uint32(DEVICE(&s->gic), "num-cpu", XLNX_ZYNQMP_NUM_APU_CPUS);

    /* Realize APUs before realizing the GIC. KVM requires this.  */
    for (i = 0; i < XLNX_ZYNQMP_NUM_APU_CPUS; i++) {
        char *name;

        object_property_set_int(OBJECT(&s->apu_cpu[i]), QEMU_PSCI_CONDUIT_SMC,
                                "psci-conduit", &error_abort);

        name = object_get_canonical_path_component(OBJECT(&s->apu_cpu[i]));
        if (strcmp(name, boot_cpu)) {
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(OBJECT(&s->apu_cpu[i]), true,
                                     "start-powered-off", &error_abort);
        } else {
            s->boot_cpu_ptr = &s->apu_cpu[i];
        }
        g_free(name);

        object_property_set_bool(OBJECT(&s->apu_cpu[i]),
                                 s->secure, "has_el3", NULL);
        object_property_set_int(OBJECT(&s->apu_cpu[i]), GIC_BASE_ADDR,
                                "reset-cbar", &error_abort);
        object_property_set_bool(OBJECT(&s->apu_cpu[i]), true, "realized",
                                 &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    object_property_set_bool(OBJECT(&s->gic), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    assert(ARRAY_SIZE(xlnx_zynqmp_gic_regions) == XLNX_ZYNQMP_GIC_REGIONS);
    for (i = 0; i < XLNX_ZYNQMP_GIC_REGIONS; i++) {
        SysBusDevice *gic = SYS_BUS_DEVICE(&s->gic);
        const XlnxZynqMPGICRegion *r = &xlnx_zynqmp_gic_regions[i];
        MemoryRegion *mr = sysbus_mmio_get_region(gic, r->region_index);
        uint32_t addr = r->address;
        int j;

        sysbus_mmio_map(gic, r->region_index, addr);

        for (j = 0; j < XLNX_ZYNQMP_GIC_ALIASES; j++) {
            MemoryRegion *alias = &s->gic_mr[i][j];

            addr += XLNX_ZYNQMP_GIC_REGION_SIZE;
            memory_region_init_alias(alias, OBJECT(s), "zynqmp-gic-alias", mr,
                                     0, XLNX_ZYNQMP_GIC_REGION_SIZE);
            memory_region_add_subregion(system_memory, addr, alias);
        }
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_APU_CPUS; i++) {
        qemu_irq irq;

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gic), i,
                           qdev_get_gpio_in(DEVICE(&s->apu_cpu[i]),
                                            ARM_CPU_IRQ));
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_PHYS_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->apu_cpu[i]), 0, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->gic),
                               arm_gic_ppi_index(i, ARM_VIRT_TIMER_PPI));
        qdev_connect_gpio_out(DEVICE(&s->apu_cpu[i]), 1, irq);
    }

    if (s->has_rpu) {
        xlnx_zynqmp_create_rpu(s, boot_cpu, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    if (!s->boot_cpu_ptr) {
        error_setg(errp, "ZynqMP Boot cpu %s not found", boot_cpu);
        return;
    }

    for (i = 0; i < GIC_NUM_SPI_INTR; i++) {
        gic_spi[i] = qdev_get_gpio_in(DEVICE(&s->gic), i);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_GEMS; i++) {
        NICInfo *nd = &nd_table[i];

        if (nd->used) {
            qemu_check_nic_model(nd, TYPE_CADENCE_GEM);
            qdev_set_nic_properties(DEVICE(&s->gem[i]), nd);
        }
        object_property_set_bool(OBJECT(&s->gem[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gem[i]), 0, gem_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gem[i]), 0,
                           gic_spi[gem_intr[i]]);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_UARTS; i++) {
        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hds[i]);
        object_property_set_bool(OBJECT(&s->uart[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, uart_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           gic_spi[uart_intr[i]]);
    }

    object_property_set_int(OBJECT(&s->sata), SATA_NUM_PORTS, "num-ports",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->sata), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sata), 0, SATA_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sata), 0, gic_spi[SATA_INTR]);

    for (i = 0; i < XLNX_ZYNQMP_NUM_SDHCI; i++) {
        char *bus_name;

        object_property_set_bool(OBJECT(&s->sdhci[i]), true,
                                 "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdhci[i]), 0,
                        sdhci_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci[i]), 0,
                           gic_spi[sdhci_intr[i]]);
        /* Alias controller SD bus to the SoC itself */
        bus_name = g_strdup_printf("sd-bus%d", i);
        object_property_add_alias(OBJECT(s), bus_name,
                                  OBJECT(&s->sdhci[i]), "sd-bus",
                                  &error_abort);
        g_free(bus_name);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_SPIS; i++) {
        gchar *bus_name;

        object_property_set_bool(OBJECT(&s->spi[i]), true, "realized", &err);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0, spi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           gic_spi[spi_intr[i]]);

        /* Alias controller SPI bus to the SoC itself */
        bus_name = g_strdup_printf("spi%d", i);
        object_property_add_alias(OBJECT(s), bus_name,
                                  OBJECT(&s->spi[i]), "spi0",
                                  &error_abort);
	g_free(bus_name);
    }
}

static Property xlnx_zynqmp_props[] = {
    DEFINE_PROP_STRING("boot-cpu", XlnxZynqMPState, boot_cpu),
    DEFINE_PROP_BOOL("secure", XlnxZynqMPState, secure, false),
    DEFINE_PROP_BOOL("has_rpu", XlnxZynqMPState, has_rpu, false),
    DEFINE_PROP_END_OF_LIST()
};

static void xlnx_zynqmp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->props = xlnx_zynqmp_props;
    dc->realize = xlnx_zynqmp_realize;

    /*
     * Reason: creates an ARM CPU, thus use after free(), see
     * arm_cpu_class_init()
     */
    dc->cannot_destroy_with_object_finalize_yet = true;
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
