/*
 * QEMU RISC-V Board Compatible with Microchip PolarFire SoC Icicle Kit
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * Provides a board compatible with the Microchip PolarFire SoC Icicle Kit
 *
 * 0) CLINT (Core Level Interruptor)
 * 1) PLIC (Platform Level Interrupt Controller)
 * 2) eNVM (Embedded Non-Volatile Memory)
 * 3) MMUARTs (Multi-Mode UART)
 * 4) Cadence eMMC/SDHC controller and an SD card connected to it
 * 5) SiFive Platform DMA (Direct Memory Access Controller)
 * 6) GEM (Gigabit Ethernet MAC Controller)
 * 7) DMC (DDR Memory Controller)
 * 8) IOSCB modules
 *
 * This board currently generates devicetree dynamically that indicates at least
 * two harts and up to five harts.
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
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "hw/cpu/cluster.h"
#include "target/riscv/cpu.h"
#include "hw/misc/unimp.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/microchip_pfsoc.h"
#include "hw/intc/sifive_clint.h"
#include "hw/intc/sifive_plic.h"
#include "sysemu/sysemu.h"

/*
 * The BIOS image used by this machine is called Hart Software Services (HSS).
 * See https://github.com/polarfire-soc/hart-software-services
 */
#define BIOS_FILENAME   "hss.bin"
#define RESET_VECTOR    0x20220000

/* CLINT timebase frequency */
#define CLINT_TIMEBASE_FREQ 1000000

/* GEM version */
#define GEM_REVISION    0x0107010c

/*
 * The complete description of the whole PolarFire SoC memory map is scattered
 * in different documents. There are several places to look at for memory maps:
 *
 * 1 Chapter 11 "MSS Memory Map", in the doc "UG0880: PolarFire SoC FPGA
 *   Microprocessor Subsystem (MSS) User Guide", which can be downloaded from
 *   https://www.microsemi.com/document-portal/doc_download/
 *   1244570-ug0880-polarfire-soc-fpga-microprocessor-subsystem-mss-user-guide,
 *   describes the whole picture of the PolarFire SoC memory map.
 *
 * 2 A zip file for PolarFire soC memory map, which can be downloaded from
 *   https://www.microsemi.com/document-portal/doc_download/
 *   1244581-polarfire-soc-register-map, contains the following 2 major parts:
 *   - Register Map/PF_SoC_RegMap_V1_1/pfsoc_regmap.htm
 *     describes the complete integrated peripherals memory map
 *   - Register Map/PF_SoC_RegMap_V1_1/MPFS250T/mpfs250t_ioscb_memmap_dri.htm
 *     describes the complete IOSCB modules memory maps
 */
static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} microchip_pfsoc_memmap[] = {
    [MICROCHIP_PFSOC_RSVD0] =           {        0x0,      0x100 },
    [MICROCHIP_PFSOC_DEBUG] =           {      0x100,      0xf00 },
    [MICROCHIP_PFSOC_E51_DTIM] =        {  0x1000000,     0x2000 },
    [MICROCHIP_PFSOC_BUSERR_UNIT0] =    {  0x1700000,     0x1000 },
    [MICROCHIP_PFSOC_BUSERR_UNIT1] =    {  0x1701000,     0x1000 },
    [MICROCHIP_PFSOC_BUSERR_UNIT2] =    {  0x1702000,     0x1000 },
    [MICROCHIP_PFSOC_BUSERR_UNIT3] =    {  0x1703000,     0x1000 },
    [MICROCHIP_PFSOC_BUSERR_UNIT4] =    {  0x1704000,     0x1000 },
    [MICROCHIP_PFSOC_CLINT] =           {  0x2000000,    0x10000 },
    [MICROCHIP_PFSOC_L2CC] =            {  0x2010000,     0x1000 },
    [MICROCHIP_PFSOC_DMA] =             {  0x3000000,   0x100000 },
    [MICROCHIP_PFSOC_L2LIM] =           {  0x8000000,  0x2000000 },
    [MICROCHIP_PFSOC_PLIC] =            {  0xc000000,  0x4000000 },
    [MICROCHIP_PFSOC_MMUART0] =         { 0x20000000,     0x1000 },
    [MICROCHIP_PFSOC_SYSREG] =          { 0x20002000,     0x2000 },
    [MICROCHIP_PFSOC_MPUCFG] =          { 0x20005000,     0x1000 },
    [MICROCHIP_PFSOC_DDR_SGMII_PHY] =   { 0x20007000,     0x1000 },
    [MICROCHIP_PFSOC_EMMC_SD] =         { 0x20008000,     0x1000 },
    [MICROCHIP_PFSOC_DDR_CFG] =         { 0x20080000,    0x40000 },
    [MICROCHIP_PFSOC_MMUART1] =         { 0x20100000,     0x1000 },
    [MICROCHIP_PFSOC_MMUART2] =         { 0x20102000,     0x1000 },
    [MICROCHIP_PFSOC_MMUART3] =         { 0x20104000,     0x1000 },
    [MICROCHIP_PFSOC_MMUART4] =         { 0x20106000,     0x1000 },
    [MICROCHIP_PFSOC_SPI0] =            { 0x20108000,     0x1000 },
    [MICROCHIP_PFSOC_SPI1] =            { 0x20109000,     0x1000 },
    [MICROCHIP_PFSOC_I2C1] =            { 0x2010b000,     0x1000 },
    [MICROCHIP_PFSOC_GEM0] =            { 0x20110000,     0x2000 },
    [MICROCHIP_PFSOC_GEM1] =            { 0x20112000,     0x2000 },
    [MICROCHIP_PFSOC_GPIO0] =           { 0x20120000,     0x1000 },
    [MICROCHIP_PFSOC_GPIO1] =           { 0x20121000,     0x1000 },
    [MICROCHIP_PFSOC_GPIO2] =           { 0x20122000,     0x1000 },
    [MICROCHIP_PFSOC_ENVM_CFG] =        { 0x20200000,     0x1000 },
    [MICROCHIP_PFSOC_ENVM_DATA] =       { 0x20220000,    0x20000 },
    [MICROCHIP_PFSOC_QSPI_XIP] =        { 0x21000000,  0x1000000 },
    [MICROCHIP_PFSOC_IOSCB] =           { 0x30000000, 0x10000000 },
    [MICROCHIP_PFSOC_DRAM_LO] =         { 0x80000000, 0x40000000 },
    [MICROCHIP_PFSOC_DRAM_LO_ALIAS] =   { 0xc0000000, 0x40000000 },
    [MICROCHIP_PFSOC_DRAM_HI] =       { 0x1000000000,        0x0 },
    [MICROCHIP_PFSOC_DRAM_HI_ALIAS] = { 0x1400000000,        0x0 },
};

static void microchip_pfsoc_soc_instance_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MicrochipPFSoCState *s = MICROCHIP_PFSOC(obj);

    object_initialize_child(obj, "e-cluster", &s->e_cluster, TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->e_cluster), "cluster-id", 0);

    object_initialize_child(OBJECT(&s->e_cluster), "e-cpus", &s->e_cpus,
                            TYPE_RISCV_HART_ARRAY);
    qdev_prop_set_uint32(DEVICE(&s->e_cpus), "num-harts", 1);
    qdev_prop_set_uint32(DEVICE(&s->e_cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->e_cpus), "cpu-type",
                         TYPE_RISCV_CPU_SIFIVE_E51);
    qdev_prop_set_uint64(DEVICE(&s->e_cpus), "resetvec", RESET_VECTOR);

    object_initialize_child(obj, "u-cluster", &s->u_cluster, TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->u_cluster), "cluster-id", 1);

    object_initialize_child(OBJECT(&s->u_cluster), "u-cpus", &s->u_cpus,
                            TYPE_RISCV_HART_ARRAY);
    qdev_prop_set_uint32(DEVICE(&s->u_cpus), "num-harts", ms->smp.cpus - 1);
    qdev_prop_set_uint32(DEVICE(&s->u_cpus), "hartid-base", 1);
    qdev_prop_set_string(DEVICE(&s->u_cpus), "cpu-type",
                         TYPE_RISCV_CPU_SIFIVE_U54);
    qdev_prop_set_uint64(DEVICE(&s->u_cpus), "resetvec", RESET_VECTOR);

    object_initialize_child(obj, "dma-controller", &s->dma,
                            TYPE_SIFIVE_PDMA);

    object_initialize_child(obj, "sysreg", &s->sysreg,
                            TYPE_MCHP_PFSOC_SYSREG);

    object_initialize_child(obj, "ddr-sgmii-phy", &s->ddr_sgmii_phy,
                            TYPE_MCHP_PFSOC_DDR_SGMII_PHY);
    object_initialize_child(obj, "ddr-cfg", &s->ddr_cfg,
                            TYPE_MCHP_PFSOC_DDR_CFG);

    object_initialize_child(obj, "gem0", &s->gem0, TYPE_CADENCE_GEM);
    object_initialize_child(obj, "gem1", &s->gem1, TYPE_CADENCE_GEM);

    object_initialize_child(obj, "sd-controller", &s->sdhci,
                            TYPE_CADENCE_SDHCI);

    object_initialize_child(obj, "ioscb", &s->ioscb, TYPE_MCHP_PFSOC_IOSCB);
}

static void microchip_pfsoc_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    MicrochipPFSoCState *s = MICROCHIP_PFSOC(dev);
    const struct MemmapEntry *memmap = microchip_pfsoc_memmap;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *rsvd0_mem = g_new(MemoryRegion, 1);
    MemoryRegion *e51_dtim_mem = g_new(MemoryRegion, 1);
    MemoryRegion *l2lim_mem = g_new(MemoryRegion, 1);
    MemoryRegion *envm_data = g_new(MemoryRegion, 1);
    MemoryRegion *qspi_xip_mem = g_new(MemoryRegion, 1);
    char *plic_hart_config;
    size_t plic_hart_config_len;
    NICInfo *nd;
    int i;

    sysbus_realize(SYS_BUS_DEVICE(&s->e_cpus), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->u_cpus), &error_abort);
    /*
     * The cluster must be realized after the RISC-V hart array container,
     * as the container's CPU object is only created on realize, and the
     * CPU must exist and have been parented into the cluster before the
     * cluster is realized.
     */
    qdev_realize(DEVICE(&s->e_cluster), NULL, &error_abort);
    qdev_realize(DEVICE(&s->u_cluster), NULL, &error_abort);

    /* Reserved Memory at address 0 */
    memory_region_init_ram(rsvd0_mem, NULL, "microchip.pfsoc.rsvd0_mem",
                           memmap[MICROCHIP_PFSOC_RSVD0].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_RSVD0].base,
                                rsvd0_mem);

    /* E51 DTIM */
    memory_region_init_ram(e51_dtim_mem, NULL, "microchip.pfsoc.e51_dtim_mem",
                           memmap[MICROCHIP_PFSOC_E51_DTIM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_E51_DTIM].base,
                                e51_dtim_mem);

    /* Bus Error Units */
    create_unimplemented_device("microchip.pfsoc.buserr_unit0_mem",
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT0].base,
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT0].size);
    create_unimplemented_device("microchip.pfsoc.buserr_unit1_mem",
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT1].base,
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT1].size);
    create_unimplemented_device("microchip.pfsoc.buserr_unit2_mem",
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT2].base,
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT2].size);
    create_unimplemented_device("microchip.pfsoc.buserr_unit3_mem",
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT3].base,
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT3].size);
    create_unimplemented_device("microchip.pfsoc.buserr_unit4_mem",
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT4].base,
        memmap[MICROCHIP_PFSOC_BUSERR_UNIT4].size);

    /* CLINT */
    sifive_clint_create(memmap[MICROCHIP_PFSOC_CLINT].base,
        memmap[MICROCHIP_PFSOC_CLINT].size, 0, ms->smp.cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE,
        CLINT_TIMEBASE_FREQ, false);

    /* L2 cache controller */
    create_unimplemented_device("microchip.pfsoc.l2cc",
        memmap[MICROCHIP_PFSOC_L2CC].base, memmap[MICROCHIP_PFSOC_L2CC].size);

    /*
     * Add L2-LIM at reset size.
     * This should be reduced in size as the L2 Cache Controller WayEnable
     * register is incremented. Unfortunately I don't see a nice (or any) way
     * to handle reducing or blocking out the L2 LIM while still allowing it
     * be re returned to all enabled after a reset. For the time being, just
     * leave it enabled all the time. This won't break anything, but will be
     * too generous to misbehaving guests.
     */
    memory_region_init_ram(l2lim_mem, NULL, "microchip.pfsoc.l2lim",
                           memmap[MICROCHIP_PFSOC_L2LIM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_L2LIM].base,
                                l2lim_mem);

    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(MICROCHIP_PFSOC_PLIC_HART_CONFIG) + 1) *
                           ms->smp.cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < ms->smp.cpus; i++) {
        if (i != 0) {
            strncat(plic_hart_config, "," MICROCHIP_PFSOC_PLIC_HART_CONFIG,
                    plic_hart_config_len);
        } else {
            strncat(plic_hart_config, "M", plic_hart_config_len);
        }
        plic_hart_config_len -= (strlen(MICROCHIP_PFSOC_PLIC_HART_CONFIG) + 1);
    }

    /* PLIC */
    s->plic = sifive_plic_create(memmap[MICROCHIP_PFSOC_PLIC].base,
        plic_hart_config, 0,
        MICROCHIP_PFSOC_PLIC_NUM_SOURCES,
        MICROCHIP_PFSOC_PLIC_NUM_PRIORITIES,
        MICROCHIP_PFSOC_PLIC_PRIORITY_BASE,
        MICROCHIP_PFSOC_PLIC_PENDING_BASE,
        MICROCHIP_PFSOC_PLIC_ENABLE_BASE,
        MICROCHIP_PFSOC_PLIC_ENABLE_STRIDE,
        MICROCHIP_PFSOC_PLIC_CONTEXT_BASE,
        MICROCHIP_PFSOC_PLIC_CONTEXT_STRIDE,
        memmap[MICROCHIP_PFSOC_PLIC].size);
    g_free(plic_hart_config);

    /* DMA */
    sysbus_realize(SYS_BUS_DEVICE(&s->dma), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dma), 0,
                    memmap[MICROCHIP_PFSOC_DMA].base);
    for (i = 0; i < SIFIVE_PDMA_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->dma), i,
                           qdev_get_gpio_in(DEVICE(s->plic),
                                            MICROCHIP_PFSOC_DMA_IRQ0 + i));
    }

    /* SYSREG */
    sysbus_realize(SYS_BUS_DEVICE(&s->sysreg), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sysreg), 0,
                    memmap[MICROCHIP_PFSOC_SYSREG].base);

    /* MPUCFG */
    create_unimplemented_device("microchip.pfsoc.mpucfg",
        memmap[MICROCHIP_PFSOC_MPUCFG].base,
        memmap[MICROCHIP_PFSOC_MPUCFG].size);

    /* DDR SGMII PHY */
    sysbus_realize(SYS_BUS_DEVICE(&s->ddr_sgmii_phy), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_sgmii_phy), 0,
                    memmap[MICROCHIP_PFSOC_DDR_SGMII_PHY].base);

    /* DDR CFG */
    sysbus_realize(SYS_BUS_DEVICE(&s->ddr_cfg), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ddr_cfg), 0,
                    memmap[MICROCHIP_PFSOC_DDR_CFG].base);

    /* SDHCI */
    sysbus_realize(SYS_BUS_DEVICE(&s->sdhci), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdhci), 0,
                    memmap[MICROCHIP_PFSOC_EMMC_SD].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci), 0,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_EMMC_SD_IRQ));

    /* MMUARTs */
    s->serial0 = mchp_pfsoc_mmuart_create(system_memory,
        memmap[MICROCHIP_PFSOC_MMUART0].base,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_MMUART0_IRQ),
        serial_hd(0));
    s->serial1 = mchp_pfsoc_mmuart_create(system_memory,
        memmap[MICROCHIP_PFSOC_MMUART1].base,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_MMUART1_IRQ),
        serial_hd(1));
    s->serial2 = mchp_pfsoc_mmuart_create(system_memory,
        memmap[MICROCHIP_PFSOC_MMUART2].base,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_MMUART2_IRQ),
        serial_hd(2));
    s->serial3 = mchp_pfsoc_mmuart_create(system_memory,
        memmap[MICROCHIP_PFSOC_MMUART3].base,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_MMUART3_IRQ),
        serial_hd(3));
    s->serial4 = mchp_pfsoc_mmuart_create(system_memory,
        memmap[MICROCHIP_PFSOC_MMUART4].base,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_MMUART4_IRQ),
        serial_hd(4));

    /* SPI */
    create_unimplemented_device("microchip.pfsoc.spi0",
        memmap[MICROCHIP_PFSOC_SPI0].base,
        memmap[MICROCHIP_PFSOC_SPI0].size);
    create_unimplemented_device("microchip.pfsoc.spi1",
        memmap[MICROCHIP_PFSOC_SPI1].base,
        memmap[MICROCHIP_PFSOC_SPI1].size);

    /* I2C1 */
    create_unimplemented_device("microchip.pfsoc.i2c1",
        memmap[MICROCHIP_PFSOC_I2C1].base,
        memmap[MICROCHIP_PFSOC_I2C1].size);

    /* GEMs */

    nd = &nd_table[0];
    if (nd->used) {
        qemu_check_nic_model(nd, TYPE_CADENCE_GEM);
        qdev_set_nic_properties(DEVICE(&s->gem0), nd);
    }
    nd = &nd_table[1];
    if (nd->used) {
        qemu_check_nic_model(nd, TYPE_CADENCE_GEM);
        qdev_set_nic_properties(DEVICE(&s->gem1), nd);
    }

    object_property_set_int(OBJECT(&s->gem0), "revision", GEM_REVISION, errp);
    object_property_set_int(OBJECT(&s->gem0), "phy-addr", 8, errp);
    sysbus_realize(SYS_BUS_DEVICE(&s->gem0), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gem0), 0,
                    memmap[MICROCHIP_PFSOC_GEM0].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gem0), 0,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_GEM0_IRQ));

    object_property_set_int(OBJECT(&s->gem1), "revision", GEM_REVISION, errp);
    object_property_set_int(OBJECT(&s->gem1), "phy-addr", 9, errp);
    sysbus_realize(SYS_BUS_DEVICE(&s->gem1), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gem1), 0,
                    memmap[MICROCHIP_PFSOC_GEM1].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gem1), 0,
        qdev_get_gpio_in(DEVICE(s->plic), MICROCHIP_PFSOC_GEM1_IRQ));

    /* GPIOs */
    create_unimplemented_device("microchip.pfsoc.gpio0",
        memmap[MICROCHIP_PFSOC_GPIO0].base,
        memmap[MICROCHIP_PFSOC_GPIO0].size);
    create_unimplemented_device("microchip.pfsoc.gpio1",
        memmap[MICROCHIP_PFSOC_GPIO1].base,
        memmap[MICROCHIP_PFSOC_GPIO1].size);
    create_unimplemented_device("microchip.pfsoc.gpio2",
        memmap[MICROCHIP_PFSOC_GPIO2].base,
        memmap[MICROCHIP_PFSOC_GPIO2].size);

    /* eNVM */
    memory_region_init_rom(envm_data, OBJECT(dev), "microchip.pfsoc.envm.data",
                           memmap[MICROCHIP_PFSOC_ENVM_DATA].size,
                           &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_ENVM_DATA].base,
                                envm_data);

    /* IOSCB */
    sysbus_realize(SYS_BUS_DEVICE(&s->ioscb), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ioscb), 0,
                    memmap[MICROCHIP_PFSOC_IOSCB].base);

    /* QSPI Flash */
    memory_region_init_rom(qspi_xip_mem, OBJECT(dev),
                           "microchip.pfsoc.qspi_xip",
                           memmap[MICROCHIP_PFSOC_QSPI_XIP].size,
                           &error_fatal);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_QSPI_XIP].base,
                                qspi_xip_mem);
}

static void microchip_pfsoc_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = microchip_pfsoc_soc_realize;
    /* Reason: Uses serial_hds in realize function, thus can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo microchip_pfsoc_soc_type_info = {
    .name = TYPE_MICROCHIP_PFSOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MicrochipPFSoCState),
    .instance_init = microchip_pfsoc_soc_instance_init,
    .class_init = microchip_pfsoc_soc_class_init,
};

static void microchip_pfsoc_soc_register_types(void)
{
    type_register_static(&microchip_pfsoc_soc_type_info);
}

type_init(microchip_pfsoc_soc_register_types)

static void microchip_icicle_kit_machine_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const struct MemmapEntry *memmap = microchip_pfsoc_memmap;
    MicrochipIcicleKitState *s = MICROCHIP_ICICLE_KIT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mem_low = g_new(MemoryRegion, 1);
    MemoryRegion *mem_low_alias = g_new(MemoryRegion, 1);
    MemoryRegion *mem_high = g_new(MemoryRegion, 1);
    MemoryRegion *mem_high_alias = g_new(MemoryRegion, 1);
    uint64_t mem_high_size;
    DriveInfo *dinfo = drive_get_next(IF_SD);

    /* Sanity check on RAM size */
    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be bigger than %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_MICROCHIP_PFSOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    /* Register RAM */
    memory_region_init_ram(mem_low, NULL, "microchip.icicle.kit.ram_low",
                           memmap[MICROCHIP_PFSOC_DRAM_LO].size,
                           &error_fatal);
    memory_region_init_alias(mem_low_alias, NULL,
                             "microchip.icicle.kit.ram_low.alias",
                             mem_low, 0,
                             memmap[MICROCHIP_PFSOC_DRAM_LO_ALIAS].size);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_DRAM_LO].base,
                                mem_low);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_DRAM_LO_ALIAS].base,
                                mem_low_alias);

    mem_high_size = machine->ram_size - 1 * GiB;

    memory_region_init_ram(mem_high, NULL, "microchip.icicle.kit.ram_high",
                           mem_high_size, &error_fatal);
    memory_region_init_alias(mem_high_alias, NULL,
                             "microchip.icicle.kit.ram_high.alias",
                             mem_high, 0, mem_high_size);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_DRAM_HI].base,
                                mem_high);
    memory_region_add_subregion(system_memory,
                                memmap[MICROCHIP_PFSOC_DRAM_HI_ALIAS].base,
                                mem_high_alias);

    /* Load the firmware */
    riscv_find_and_load_firmware(machine, BIOS_FILENAME, RESET_VECTOR, NULL);

    /* Attach an SD card */
    if (dinfo) {
        CadenceSDHCIState *sdhci = &(s->soc.sdhci);
        DeviceState *card = qdev_new(TYPE_SD_CARD);

        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card, sdhci->bus, &error_fatal);
    }
}

static void microchip_icicle_kit_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Microchip PolarFire SoC Icicle Kit";
    mc->init = microchip_icicle_kit_machine_init;
    mc->max_cpus = MICROCHIP_PFSOC_MANAGEMENT_CPU_COUNT +
                   MICROCHIP_PFSOC_COMPUTE_CPU_COUNT;
    mc->min_cpus = MICROCHIP_PFSOC_MANAGEMENT_CPU_COUNT + 1;
    mc->default_cpus = mc->min_cpus;

    /*
     * Map 513 MiB high memory, the mimimum required high memory size, because
     * HSS will do memory test against the high memory address range regardless
     * of physical memory installed.
     *
     * See memory_tests() in mss_ddr.c in the HSS source code.
     */
    mc->default_ram_size = 1537 * MiB;
}

static const TypeInfo microchip_icicle_kit_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("microchip-icicle-kit"),
    .parent     = TYPE_MACHINE,
    .class_init = microchip_icicle_kit_machine_class_init,
    .instance_size = sizeof(MicrochipIcicleKitState),
};

static void microchip_icicle_kit_machine_init_register_types(void)
{
    type_register_static(&microchip_icicle_kit_machine_typeinfo);
}

type_init(microchip_icicle_kit_machine_init_register_types)
