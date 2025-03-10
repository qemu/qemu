/*
 * ASPEED SoC 27x0 family
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * Implementation extracted from the AST2600 and adapted for AST27x0.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/arm/bsa.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/i2c/aspeed_i2c.h"
#include "net/net.h"
#include "system/system.h"
#include "hw/intc/arm_gicv3.h"
#include "qobject/qlist.h"
#include "qemu/log.h"

static const hwaddr aspeed_soc_ast2700_memmap[] = {
    [ASPEED_DEV_SRAM]      =  0x10000000,
    [ASPEED_DEV_HACE]      =  0x12070000,
    [ASPEED_DEV_EMMC]      =  0x12090000,
    [ASPEED_DEV_INTC]      =  0x12100000,
    [ASPEED_GIC_DIST]      =  0x12200000,
    [ASPEED_GIC_REDIST]    =  0x12280000,
    [ASPEED_DEV_SDMC]      =  0x12C00000,
    [ASPEED_DEV_SCU]       =  0x12C02000,
    [ASPEED_DEV_RTC]       =  0x12C0F000,
    [ASPEED_DEV_TIMER1]    =  0x12C10000,
    [ASPEED_DEV_SLI]       =  0x12C17000,
    [ASPEED_DEV_UART4]     =  0X12C1A000,
    [ASPEED_DEV_FMC]       =  0x14000000,
    [ASPEED_DEV_SPI0]      =  0x14010000,
    [ASPEED_DEV_SPI1]      =  0x14020000,
    [ASPEED_DEV_SPI2]      =  0x14030000,
    [ASPEED_DEV_MII1]      =  0x14040000,
    [ASPEED_DEV_MII2]      =  0x14040008,
    [ASPEED_DEV_MII3]      =  0x14040010,
    [ASPEED_DEV_ETH1]      =  0x14050000,
    [ASPEED_DEV_ETH2]      =  0x14060000,
    [ASPEED_DEV_ETH3]      =  0x14070000,
    [ASPEED_DEV_SDHCI]     =  0x14080000,
    [ASPEED_DEV_ADC]       =  0x14C00000,
    [ASPEED_DEV_SCUIO]     =  0x14C02000,
    [ASPEED_DEV_GPIO]      =  0x14C0B000,
    [ASPEED_DEV_I2C]       =  0x14C0F000,
    [ASPEED_DEV_INTCIO]    =  0x14C18000,
    [ASPEED_DEV_SLIIO]     =  0x14C1E000,
    [ASPEED_DEV_VUART]     =  0X14C30000,
    [ASPEED_DEV_UART0]     =  0X14C33000,
    [ASPEED_DEV_UART1]     =  0X14C33100,
    [ASPEED_DEV_UART2]     =  0X14C33200,
    [ASPEED_DEV_UART3]     =  0X14C33300,
    [ASPEED_DEV_UART5]     =  0X14C33400,
    [ASPEED_DEV_UART6]     =  0X14C33500,
    [ASPEED_DEV_UART7]     =  0X14C33600,
    [ASPEED_DEV_UART8]     =  0X14C33700,
    [ASPEED_DEV_UART9]     =  0X14C33800,
    [ASPEED_DEV_UART10]    =  0X14C33900,
    [ASPEED_DEV_UART11]    =  0X14C33A00,
    [ASPEED_DEV_UART12]    =  0X14C33B00,
    [ASPEED_DEV_WDT]       =  0x14C37000,
    [ASPEED_DEV_SPI_BOOT]  =  0x100000000,
    [ASPEED_DEV_SDRAM]     =  0x400000000,
};

#define AST2700_MAX_IRQ 256

/* Shared Peripheral Interrupt values below are offset by -32 from datasheet */
static const int aspeed_soc_ast2700a0_irqmap[] = {
    [ASPEED_DEV_SDMC]      = 0,
    [ASPEED_DEV_HACE]      = 4,
    [ASPEED_DEV_XDMA]      = 5,
    [ASPEED_DEV_UART4]     = 8,
    [ASPEED_DEV_SCU]       = 12,
    [ASPEED_DEV_RTC]       = 13,
    [ASPEED_DEV_EMMC]      = 15,
    [ASPEED_DEV_TIMER1]    = 16,
    [ASPEED_DEV_TIMER2]    = 17,
    [ASPEED_DEV_TIMER3]    = 18,
    [ASPEED_DEV_TIMER4]    = 19,
    [ASPEED_DEV_TIMER5]    = 20,
    [ASPEED_DEV_TIMER6]    = 21,
    [ASPEED_DEV_TIMER7]    = 22,
    [ASPEED_DEV_TIMER8]    = 23,
    [ASPEED_DEV_DP]        = 28,
    [ASPEED_DEV_LPC]       = 128,
    [ASPEED_DEV_IBT]       = 128,
    [ASPEED_DEV_KCS]       = 128,
    [ASPEED_DEV_ADC]       = 130,
    [ASPEED_DEV_GPIO]      = 130,
    [ASPEED_DEV_I2C]       = 130,
    [ASPEED_DEV_FMC]       = 131,
    [ASPEED_DEV_WDT]       = 131,
    [ASPEED_DEV_PWM]       = 131,
    [ASPEED_DEV_I3C]       = 131,
    [ASPEED_DEV_UART0]     = 132,
    [ASPEED_DEV_UART1]     = 132,
    [ASPEED_DEV_UART2]     = 132,
    [ASPEED_DEV_UART3]     = 132,
    [ASPEED_DEV_UART5]     = 132,
    [ASPEED_DEV_UART6]     = 132,
    [ASPEED_DEV_UART7]     = 132,
    [ASPEED_DEV_UART8]     = 132,
    [ASPEED_DEV_UART9]     = 132,
    [ASPEED_DEV_UART10]    = 132,
    [ASPEED_DEV_UART11]    = 132,
    [ASPEED_DEV_UART12]    = 132,
    [ASPEED_DEV_ETH1]      = 132,
    [ASPEED_DEV_ETH2]      = 132,
    [ASPEED_DEV_ETH3]      = 132,
    [ASPEED_DEV_PECI]      = 133,
    [ASPEED_DEV_SDHCI]     = 133,
};

static const int aspeed_soc_ast2700a1_irqmap[] = {
    [ASPEED_DEV_SDMC]      = 0,
    [ASPEED_DEV_HACE]      = 4,
    [ASPEED_DEV_XDMA]      = 5,
    [ASPEED_DEV_UART4]     = 8,
    [ASPEED_DEV_SCU]       = 12,
    [ASPEED_DEV_RTC]       = 13,
    [ASPEED_DEV_EMMC]      = 15,
    [ASPEED_DEV_TIMER1]    = 16,
    [ASPEED_DEV_TIMER2]    = 17,
    [ASPEED_DEV_TIMER3]    = 18,
    [ASPEED_DEV_TIMER4]    = 19,
    [ASPEED_DEV_TIMER5]    = 20,
    [ASPEED_DEV_TIMER6]    = 21,
    [ASPEED_DEV_TIMER7]    = 22,
    [ASPEED_DEV_TIMER8]    = 23,
    [ASPEED_DEV_DP]        = 28,
    [ASPEED_DEV_LPC]       = 192,
    [ASPEED_DEV_IBT]       = 192,
    [ASPEED_DEV_KCS]       = 192,
    [ASPEED_DEV_I2C]       = 194,
    [ASPEED_DEV_ADC]       = 194,
    [ASPEED_DEV_GPIO]      = 194,
    [ASPEED_DEV_FMC]       = 195,
    [ASPEED_DEV_WDT]       = 195,
    [ASPEED_DEV_PWM]       = 195,
    [ASPEED_DEV_I3C]       = 195,
    [ASPEED_DEV_UART0]     = 196,
    [ASPEED_DEV_UART1]     = 196,
    [ASPEED_DEV_UART2]     = 196,
    [ASPEED_DEV_UART3]     = 196,
    [ASPEED_DEV_UART5]     = 196,
    [ASPEED_DEV_UART6]     = 196,
    [ASPEED_DEV_UART7]     = 196,
    [ASPEED_DEV_UART8]     = 196,
    [ASPEED_DEV_UART9]     = 196,
    [ASPEED_DEV_UART10]    = 196,
    [ASPEED_DEV_UART11]    = 196,
    [ASPEED_DEV_UART12]    = 196,
    [ASPEED_DEV_ETH1]      = 196,
    [ASPEED_DEV_ETH2]      = 196,
    [ASPEED_DEV_ETH3]      = 196,
    [ASPEED_DEV_PECI]      = 197,
    [ASPEED_DEV_SDHCI]     = 197,
};

/* GICINT 128 */
/* GICINT 192 */
static const int ast2700_gic128_gic192_intcmap[] = {
    [ASPEED_DEV_LPC]       = 0,
    [ASPEED_DEV_IBT]       = 2,
    [ASPEED_DEV_KCS]       = 4,
};

/* GICINT 129 */
/* GICINT 193 */

/* GICINT 130 */
/* GICINT 194 */
static const int ast2700_gic130_gic194_intcmap[] = {
    [ASPEED_DEV_I2C]        = 0,
    [ASPEED_DEV_ADC]        = 16,
    [ASPEED_DEV_GPIO]       = 18,
};

/* GICINT 131 */
/* GICINT 195 */
static const int ast2700_gic131_gic195_intcmap[] = {
    [ASPEED_DEV_I3C]       = 0,
    [ASPEED_DEV_WDT]       = 16,
    [ASPEED_DEV_FMC]       = 25,
    [ASPEED_DEV_PWM]       = 29,
};

/* GICINT 132 */
/* GICINT 196 */
static const int ast2700_gic132_gic196_intcmap[] = {
    [ASPEED_DEV_ETH1]      = 0,
    [ASPEED_DEV_ETH2]      = 1,
    [ASPEED_DEV_ETH3]      = 2,
    [ASPEED_DEV_UART0]     = 7,
    [ASPEED_DEV_UART1]     = 8,
    [ASPEED_DEV_UART2]     = 9,
    [ASPEED_DEV_UART3]     = 10,
    [ASPEED_DEV_UART5]     = 11,
    [ASPEED_DEV_UART6]     = 12,
    [ASPEED_DEV_UART7]     = 13,
    [ASPEED_DEV_UART8]     = 14,
    [ASPEED_DEV_UART9]     = 15,
    [ASPEED_DEV_UART10]    = 16,
    [ASPEED_DEV_UART11]    = 17,
    [ASPEED_DEV_UART12]    = 18,
};

/* GICINT 133 */
/* GICINT 197 */
static const int ast2700_gic133_gic197_intcmap[] = {
    [ASPEED_DEV_SDHCI]     = 1,
    [ASPEED_DEV_PECI]      = 4,
};

/* GICINT 128 ~ 136 */
/* GICINT 192 ~ 201 */
struct gic_intc_irq_info {
    int irq;
    int intc_idx;
    int orgate_idx;
    const int *ptr;
};

static const struct gic_intc_irq_info ast2700_gic_intcmap[] = {
    {192, 1, 0, ast2700_gic128_gic192_intcmap},
    {193, 1, 1, NULL},
    {194, 1, 2, ast2700_gic130_gic194_intcmap},
    {195, 1, 3, ast2700_gic131_gic195_intcmap},
    {196, 1, 4, ast2700_gic132_gic196_intcmap},
    {197, 1, 5, ast2700_gic133_gic197_intcmap},
    {198, 1, 6, NULL},
    {199, 1, 7, NULL},
    {200, 1, 8, NULL},
    {201, 1, 9, NULL},
    {128, 0, 1, ast2700_gic128_gic192_intcmap},
    {129, 0, 2, NULL},
    {130, 0, 3, ast2700_gic130_gic194_intcmap},
    {131, 0, 4, ast2700_gic131_gic195_intcmap},
    {132, 0, 5, ast2700_gic132_gic196_intcmap},
    {133, 0, 6, ast2700_gic133_gic197_intcmap},
    {134, 0, 7, NULL},
    {135, 0, 8, NULL},
    {136, 0, 9, NULL},
};

static qemu_irq aspeed_soc_ast2700_get_irq(AspeedSoCState *s, int dev)
{
    Aspeed27x0SoCState *a = ASPEED27X0_SOC(s);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int or_idx;
    int idx;
    int i;

    for (i = 0; i < ARRAY_SIZE(ast2700_gic_intcmap); i++) {
        if (sc->irqmap[dev] == ast2700_gic_intcmap[i].irq) {
            assert(ast2700_gic_intcmap[i].ptr);
            or_idx = ast2700_gic_intcmap[i].orgate_idx;
            idx = ast2700_gic_intcmap[i].intc_idx;
            return qdev_get_gpio_in(DEVICE(&a->intc[idx].orgates[or_idx]),
                                    ast2700_gic_intcmap[i].ptr[dev]);
        }
    }

    return qdev_get_gpio_in(DEVICE(&a->gic), sc->irqmap[dev]);
}

static qemu_irq aspeed_soc_ast2700_get_irq_index(AspeedSoCState *s, int dev,
                                                 int index)
{
    Aspeed27x0SoCState *a = ASPEED27X0_SOC(s);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int or_idx;
    int idx;
    int i;

    for (i = 0; i < ARRAY_SIZE(ast2700_gic_intcmap); i++) {
        if (sc->irqmap[dev] == ast2700_gic_intcmap[i].irq) {
            assert(ast2700_gic_intcmap[i].ptr);
            or_idx = ast2700_gic_intcmap[i].orgate_idx;
            idx = ast2700_gic_intcmap[i].intc_idx;
            return qdev_get_gpio_in(DEVICE(&a->intc[idx].orgates[or_idx]),
                                    ast2700_gic_intcmap[i].ptr[dev] + index);
        }
    }

    /*
     * Invalid OR gate index, device IRQ should be between 128 to 136
     * and 192 to 201.
     */
    g_assert_not_reached();
}

static uint64_t aspeed_ram_capacity_read(void *opaque, hwaddr addr,
                                                    unsigned int size)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: DRAM read out of ram size, addr:0x%" PRIx64 "\n",
                   __func__, addr);
    return 0;
}

static void aspeed_ram_capacity_write(void *opaque, hwaddr addr, uint64_t data,
                                                unsigned int size)
{
    AspeedSoCState *s = ASPEED_SOC(opaque);
    ram_addr_t ram_size;
    MemTxResult result;

    ram_size = object_property_get_uint(OBJECT(&s->sdmc), "ram-size",
                                        &error_abort);

    assert(ram_size > 0);

    /*
     * Emulate ddr capacity hardware behavior.
     * If writes the data to the address which is beyond the ram size,
     * it would write the data to the "address % ram_size".
     */
    result = address_space_write(&s->dram_as, addr % ram_size,
                                 MEMTXATTRS_UNSPECIFIED, &data, 4);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: DRAM write failed, addr:0x%" HWADDR_PRIx
                      ", data :0x%" PRIx64  "\n",
                      __func__, addr % ram_size, data);
    }
}

static const MemoryRegionOps aspeed_ram_capacity_ops = {
    .read = aspeed_ram_capacity_read,
    .write = aspeed_ram_capacity_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

/*
 * SDMC should be realized first to get correct RAM size and max size
 * values
 */
static bool aspeed_soc_ast2700_dram_init(DeviceState *dev, Error **errp)
{
    ram_addr_t ram_size, max_ram_size;
    Aspeed27x0SoCState *a = ASPEED27X0_SOC(dev);
    AspeedSoCState *s = ASPEED_SOC(dev);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    ram_size = object_property_get_uint(OBJECT(&s->sdmc), "ram-size",
                                        &error_abort);
    max_ram_size = object_property_get_uint(OBJECT(&s->sdmc), "max-ram-size",
                                            &error_abort);

    memory_region_init(&s->dram_container, OBJECT(s), "ram-container",
                       ram_size);
    memory_region_add_subregion(&s->dram_container, 0, s->dram_mr);
    address_space_init(&s->dram_as, s->dram_mr, "dram");

    /*
     * Add a memory region beyond the RAM region to emulate
     * ddr capacity hardware behavior.
     */
    if (ram_size < max_ram_size) {
        memory_region_init_io(&a->dram_empty, OBJECT(s),
                              &aspeed_ram_capacity_ops, s,
                              "ram-empty", max_ram_size - ram_size);

        memory_region_add_subregion(s->memory,
                                    sc->memmap[ASPEED_DEV_SDRAM] + ram_size,
                                    &a->dram_empty);
    }

    memory_region_add_subregion(s->memory,
                      sc->memmap[ASPEED_DEV_SDRAM], &s->dram_container);
    return true;
}

static void aspeed_soc_ast2700_init(Object *obj)
{
    Aspeed27x0SoCState *a = ASPEED27X0_SOC(obj);
    AspeedSoCState *s = ASPEED_SOC(obj);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int i;
    char socname[8];
    char typename[64];

    if (sscanf(object_get_typename(obj), "%7s", socname) != 1) {
        g_assert_not_reached();
    }

    for (i = 0; i < sc->num_cpus; i++) {
        object_initialize_child(obj, "cpu[*]", &a->cpu[i],
                                aspeed_soc_cpu_type(sc));
    }

    object_initialize_child(obj, "gic", &a->gic, gicv3_class_name());

    object_initialize_child(obj, "scu", &s->scu, TYPE_ASPEED_2700_SCU);
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev",
                         sc->silicon_rev);
    object_property_add_alias(obj, "hw-strap1", OBJECT(&s->scu),
                              "hw-strap1");
    object_property_add_alias(obj, "hw-prot-key", OBJECT(&s->scu),
                              "hw-prot-key");

    object_initialize_child(obj, "scuio", &s->scuio, TYPE_ASPEED_2700_SCUIO);
    qdev_prop_set_uint32(DEVICE(&s->scuio), "silicon-rev",
                         sc->silicon_rev);
    /*
     * There is one hw-strap1 register in the SCU (CPU DIE) and another
     * hw-strap1 register in the SCUIO (IO DIE). To reuse the current design
     * of hw-strap, hw-strap1 is assigned to the SCU and sets the value in the
     * SCU hw-strap1 register, while hw-strap2 is assigned to the SCUIO and
     * sets the value in the SCUIO hw-strap1 register.
     */
    object_property_add_alias(obj, "hw-strap2", OBJECT(&s->scuio),
                                  "hw-strap1");

    snprintf(typename, sizeof(typename), "aspeed.fmc-%s", socname);
    object_initialize_child(obj, "fmc", &s->fmc, typename);

    for (i = 0; i < sc->spis_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.spi%d-%s", i, socname);
        object_initialize_child(obj, "spi[*]", &s->spi[i], typename);
    }

    snprintf(typename, sizeof(typename), "aspeed.sdmc-%s", socname);
    object_initialize_child(obj, "sdmc", &s->sdmc, typename);
    object_property_add_alias(obj, "ram-size", OBJECT(&s->sdmc),
                              "ram-size");

    for (i = 0; i < sc->wdts_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.wdt-%s", socname);
        object_initialize_child(obj, "wdt[*]", &s->wdt[i], typename);
    }

    for (i = 0; i < sc->macs_num; i++) {
        object_initialize_child(obj, "ftgmac100[*]", &s->ftgmac100[i],
                                TYPE_FTGMAC100);

        object_initialize_child(obj, "mii[*]", &s->mii[i], TYPE_ASPEED_MII);
    }

    for (i = 0; i < sc->uarts_num; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i], TYPE_SERIAL_MM);
    }

    object_initialize_child(obj, "sli", &s->sli, TYPE_ASPEED_2700_SLI);
    object_initialize_child(obj, "sliio", &s->sliio, TYPE_ASPEED_2700_SLIIO);
    object_initialize_child(obj, "intc", &a->intc[0], TYPE_ASPEED_2700_INTC);
    object_initialize_child(obj, "intcio", &a->intc[1],
                            TYPE_ASPEED_2700_INTCIO);

    snprintf(typename, sizeof(typename), "aspeed.adc-%s", socname);
    object_initialize_child(obj, "adc", &s->adc, typename);

    snprintf(typename, sizeof(typename), "aspeed.i2c-%s", socname);
    object_initialize_child(obj, "i2c", &s->i2c, typename);

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s", socname);
    object_initialize_child(obj, "gpio", &s->gpio, typename);

    object_initialize_child(obj, "rtc", &s->rtc, TYPE_ASPEED_RTC);

    snprintf(typename, sizeof(typename), "aspeed.sdhci-%s", socname);
    object_initialize_child(obj, "sd-controller", &s->sdhci, typename);
    object_property_set_int(OBJECT(&s->sdhci), "num-slots", 1, &error_abort);

    /* Init sd card slot class here so that they're under the correct parent */
    object_initialize_child(obj, "sd-controller.sdhci",
                            &s->sdhci.slots[0], TYPE_SYSBUS_SDHCI);

    object_initialize_child(obj, "emmc-controller", &s->emmc, typename);
    object_property_set_int(OBJECT(&s->emmc), "num-slots", 1, &error_abort);

    object_initialize_child(obj, "emmc-controller.sdhci", &s->emmc.slots[0],
                            TYPE_SYSBUS_SDHCI);

    snprintf(typename, sizeof(typename), "aspeed.timer-%s", socname);
    object_initialize_child(obj, "timerctrl", &s->timerctrl, typename);

    snprintf(typename, sizeof(typename), "aspeed.hace-%s", socname);
    object_initialize_child(obj, "hace", &s->hace, typename);
}

/*
 * ASPEED ast2700 has 0x0 as cluster ID
 *
 * https://developer.arm.com/documentation/100236/0100/register-descriptions/aarch64-system-registers/multiprocessor-affinity-register--el1
 */
static uint64_t aspeed_calc_affinity(int cpu)
{
    return (0x0 << ARM_AFF1_SHIFT) | cpu;
}

static bool aspeed_soc_ast2700_gic_realize(DeviceState *dev, Error **errp)
{
    Aspeed27x0SoCState *a = ASPEED27X0_SOC(dev);
    AspeedSoCState *s = ASPEED_SOC(dev);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    SysBusDevice *gicbusdev;
    DeviceState *gicdev;
    QList *redist_region_count;
    int i;

    gicbusdev = SYS_BUS_DEVICE(&a->gic);
    gicdev = DEVICE(&a->gic);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", sc->num_cpus);
    qdev_prop_set_uint32(gicdev, "num-irq", AST2700_MAX_IRQ + GIC_INTERNAL);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, sc->num_cpus);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);

    if (!sysbus_realize(gicbusdev, errp)) {
        return false;
    }
    sysbus_mmio_map(gicbusdev, 0, sc->memmap[ASPEED_GIC_DIST]);
    sysbus_mmio_map(gicbusdev, 1, sc->memmap[ASPEED_GIC_REDIST]);

    for (i = 0; i < sc->num_cpus; i++) {
        DeviceState *cpudev = DEVICE(&a->cpu[i]);
        int intidbase = AST2700_MAX_IRQ + i * GIC_INTERNAL;

        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
        };
        int j;

        for (j = 0; j < ARRAY_SIZE(timer_irq); j++) {
            qdev_connect_gpio_out(cpudev, j,
                    qdev_get_gpio_in(gicdev, intidbase + timer_irq[j]));
        }

        qemu_irq irq = qdev_get_gpio_in(gicdev,
                                        intidbase + ARCH_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                    0, irq);
        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                qdev_get_gpio_in(gicdev, intidbase + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
        sysbus_connect_irq(gicbusdev, i + 4 * sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_NMI));
        sysbus_connect_irq(gicbusdev, i + 5 * sc->num_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VINMI));
    }

    return true;
}

static void aspeed_soc_ast2700_realize(DeviceState *dev, Error **errp)
{
    int i;
    Aspeed27x0SoCState *a = ASPEED27X0_SOC(dev);
    AspeedSoCState *s = ASPEED_SOC(dev);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    AspeedINTCClass *ic = ASPEED_INTC_GET_CLASS(&a->intc[0]);
    AspeedINTCClass *icio = ASPEED_INTC_GET_CLASS(&a->intc[1]);
    g_autofree char *sram_name = NULL;
    qemu_irq irq;

    /* Default boot region (SPI memory or ROMs) */
    memory_region_init(&s->spi_boot_container, OBJECT(s),
                       "aspeed.spi_boot_container", 0x400000000);
    memory_region_add_subregion(s->memory, sc->memmap[ASPEED_DEV_SPI_BOOT],
                                &s->spi_boot_container);

    /* CPU */
    for (i = 0; i < sc->num_cpus; i++) {
        object_property_set_int(OBJECT(&a->cpu[i]), "mp-affinity",
                                aspeed_calc_affinity(i), &error_abort);

        object_property_set_int(OBJECT(&a->cpu[i]), "cntfrq", 1125000000,
                                &error_abort);
        object_property_set_link(OBJECT(&a->cpu[i]), "memory",
                                 OBJECT(s->memory), &error_abort);

        if (!qdev_realize(DEVICE(&a->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* GIC */
    if (!aspeed_soc_ast2700_gic_realize(dev, errp)) {
        return;
    }

    /* INTC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&a->intc[0]), errp)) {
        return;
    }

    aspeed_mmio_map(s, SYS_BUS_DEVICE(&a->intc[0]), 0,
                    sc->memmap[ASPEED_DEV_INTC]);

    /* INTCIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&a->intc[1]), errp)) {
        return;
    }

    aspeed_mmio_map(s, SYS_BUS_DEVICE(&a->intc[1]), 0,
                    sc->memmap[ASPEED_DEV_INTCIO]);

    /* irq sources -> orgates -> INTC */
    for (i = 0; i < ic->num_inpins; i++) {
        qdev_connect_gpio_out(DEVICE(&a->intc[0].orgates[i]), 0,
                              qdev_get_gpio_in(DEVICE(&a->intc[0]), i));
    }

    /* INTC -> GIC192 - GIC201 */
    /* INTC -> GIC128 - GIC136 */
    for (i = 0; i < ic->num_outpins; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&a->intc[0]), i,
                           qdev_get_gpio_in(DEVICE(&a->gic),
                                            ast2700_gic_intcmap[i].irq));
    }

    /* irq source -> orgates -> INTCIO */
    for (i = 0; i < icio->num_inpins; i++) {
        qdev_connect_gpio_out(DEVICE(&a->intc[1].orgates[i]), 0,
                              qdev_get_gpio_in(DEVICE(&a->intc[1]), i));
    }

    /* INTCIO -> INTC */
    for (i = 0; i < icio->num_outpins; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&a->intc[1]), i,
                           qdev_get_gpio_in(DEVICE(&a->intc[0].orgates[0]), i));
    }

    /* SRAM */
    sram_name = g_strdup_printf("aspeed.sram.%d", CPU(&a->cpu[0])->cpu_index);
    if (!memory_region_init_ram(&s->sram, OBJECT(s), sram_name, sc->sram_size,
                                 errp)) {
        return;
    }
    memory_region_add_subregion(s->memory,
                                sc->memmap[ASPEED_DEV_SRAM], &s->sram);

    /* SCU */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->scu), 0, sc->memmap[ASPEED_DEV_SCU]);

    /* SCU1 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scuio), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->scuio), 0,
                    sc->memmap[ASPEED_DEV_SCUIO]);

    /* UART */
    if (!aspeed_soc_uart_realize(s, errp)) {
        return;
    }

    /* FMC, The number of CS is set at the board level */
    object_property_set_int(OBJECT(&s->fmc), "dram-base",
                            sc->memmap[ASPEED_DEV_SDRAM],
                            &error_abort);
    object_property_set_link(OBJECT(&s->fmc), "dram", OBJECT(s->dram_mr),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fmc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->fmc), 0, sc->memmap[ASPEED_DEV_FMC]);
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->fmc), 1,
                    ASPEED_SMC_GET_CLASS(&s->fmc)->flash_window_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fmc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_FMC));

    /* Set up an alias on the FMC CE0 region (boot default) */
    MemoryRegion *fmc0_mmio = &s->fmc.flashes[0].mmio;
    memory_region_init_alias(&s->spi_boot, OBJECT(s), "aspeed.spi_boot",
                             fmc0_mmio, 0, memory_region_size(fmc0_mmio));
    memory_region_add_subregion(&s->spi_boot_container, 0x0, &s->spi_boot);

    /* SPI */
    for (i = 0; i < sc->spis_num; i++) {
        object_property_set_link(OBJECT(&s->spi[i]), "dram",
                                 OBJECT(s->dram_mr), &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->spi[i]), 0,
                        sc->memmap[ASPEED_DEV_SPI0 + i]);
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->spi[i]), 1,
                        ASPEED_SMC_GET_CLASS(&s->spi[i])->flash_window_base);
    }

    /*
     * SDMC - SDRAM Memory Controller
     * The SDMC controller is unlocked at SPL stage.
     * At present, only supports to emulate booting
     * start from u-boot stage. Set SDMC controller
     * unlocked by default. It is a temporarily solution.
     */
    object_property_set_bool(OBJECT(&s->sdmc), "unlocked", true,
                                 &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdmc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->sdmc), 0,
                    sc->memmap[ASPEED_DEV_SDMC]);

    /* RAM */
    if (!aspeed_soc_ast2700_dram_init(dev, errp)) {
        return;
    }

    /* Net */
    for (i = 0; i < sc->macs_num; i++) {
        object_property_set_bool(OBJECT(&s->ftgmac100[i]), "aspeed", true,
                                 &error_abort);
        object_property_set_bool(OBJECT(&s->ftgmac100[i]), "dma64", true,
                                 &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->ftgmac100[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                        sc->memmap[ASPEED_DEV_ETH1 + i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                           aspeed_soc_get_irq(s, ASPEED_DEV_ETH1 + i));

        object_property_set_link(OBJECT(&s->mii[i]), "nic",
                                 OBJECT(&s->ftgmac100[i]), &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->mii[i]), errp)) {
            return;
        }

        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->mii[i]), 0,
                        sc->memmap[ASPEED_DEV_MII1 + i]);
    }

    /* Watch dog */
    for (i = 0; i < sc->wdts_num; i++) {
        AspeedWDTClass *awc = ASPEED_WDT_GET_CLASS(&s->wdt[i]);
        hwaddr wdt_offset = sc->memmap[ASPEED_DEV_WDT] + i * awc->iosize;

        object_property_set_link(OBJECT(&s->wdt[i]), "scu", OBJECT(&s->scu),
                                 &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->wdt[i]), 0, wdt_offset);
    }

    /* SLI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sli), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->sli), 0, sc->memmap[ASPEED_DEV_SLI]);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sliio), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->sliio), 0,
                    sc->memmap[ASPEED_DEV_SLIIO]);

    /* ADC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->adc), 0, sc->memmap[ASPEED_DEV_ADC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_ADC));

    /* I2C */
    object_property_set_link(OBJECT(&s->i2c), "dram", OBJECT(s->dram_mr),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->i2c), 0, sc->memmap[ASPEED_DEV_I2C]);
    for (i = 0; i < ASPEED_I2C_GET_CLASS(&s->i2c)->num_busses; i++) {
        /*
         * The AST2700 I2C controller has one source INTC per bus.
         *
         * For AST2700 A0:
         * I2C bus interrupts are connected to the OR gate from bit 0 to bit
         * 15, and the OR gate output pin is connected to the input pin of
         * GICINT130 of INTC (CPU Die). Then, the output pin is connected to
         * the GIC.
         *
         * For AST2700 A1:
         * I2C bus interrupts are connected to the OR gate from bit 0 to bit
         * 15, and the OR gate output pin is connected to the input pin of
         * GICINT194 of INTCIO (IO Die). Then, the output pin is connected
         * to the INTC (CPU Die) input pin, and its output pin is connected
         * to the GIC.
         *
         * I2C bus 0 is connected to the OR gate at bit 0.
         * I2C bus 15 is connected to the OR gate at bit 15.
         */
        irq = aspeed_soc_ast2700_get_irq_index(s, ASPEED_DEV_I2C, i);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c.busses[i]), 0, irq);
    }

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->gpio), 0,
                    sc->memmap[ASPEED_DEV_GPIO]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_GPIO));

    /* RTC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->rtc), 0, sc->memmap[ASPEED_DEV_RTC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_RTC));

    /* SDHCI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdhci), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->sdhci), 0,
                    sc->memmap[ASPEED_DEV_SDHCI]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_SDHCI));

    /* eMMC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->emmc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->emmc), 0,
                    sc->memmap[ASPEED_DEV_EMMC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->emmc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_EMMC));

    /* Timer */
    object_property_set_link(OBJECT(&s->timerctrl), "scu", OBJECT(&s->scu),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timerctrl), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->timerctrl), 0,
                    sc->memmap[ASPEED_DEV_TIMER1]);
    for (i = 0; i < ASPEED_TIMER_NR_TIMERS; i++) {
        irq = aspeed_soc_get_irq(s, ASPEED_DEV_TIMER1 + i);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timerctrl), i, irq);
    }

    /* HACE */
    object_property_set_link(OBJECT(&s->hace), "dram", OBJECT(s->dram_mr),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hace), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->hace), 0,
                    sc->memmap[ASPEED_DEV_HACE]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hace), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_HACE));

    create_unimplemented_device("ast2700.dpmcu", 0x11000000, 0x40000);
    create_unimplemented_device("ast2700.iomem0", 0x12000000, 0x01000000);
    create_unimplemented_device("ast2700.iomem1", 0x14000000, 0x01000000);
    create_unimplemented_device("ast2700.ltpi", 0x30000000, 0x1000000);
    create_unimplemented_device("ast2700.io", 0x0, 0x4000000);
}

static void aspeed_soc_ast2700a0_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a35"),
        NULL
    };
    DeviceClass *dc = DEVICE_CLASS(oc);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(oc);

    /* Reason: The Aspeed SoC can only be instantiated from a board */
    dc->user_creatable = false;
    dc->realize      = aspeed_soc_ast2700_realize;

    sc->valid_cpu_types = valid_cpu_types;
    sc->silicon_rev  = AST2700_A0_SILICON_REV;
    sc->sram_size    = 0x20000;
    sc->spis_num     = 3;
    sc->wdts_num     = 8;
    sc->macs_num     = 1;
    sc->uarts_num    = 13;
    sc->num_cpus     = 4;
    sc->uarts_base   = ASPEED_DEV_UART0;
    sc->irqmap       = aspeed_soc_ast2700a0_irqmap;
    sc->memmap       = aspeed_soc_ast2700_memmap;
    sc->get_irq      = aspeed_soc_ast2700_get_irq;
}

static void aspeed_soc_ast2700a1_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a35"),
        NULL
    };
    DeviceClass *dc = DEVICE_CLASS(oc);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(oc);

    /* Reason: The Aspeed SoC can only be instantiated from a board */
    dc->user_creatable = false;
    dc->realize      = aspeed_soc_ast2700_realize;

    sc->valid_cpu_types = valid_cpu_types;
    sc->silicon_rev  = AST2700_A1_SILICON_REV;
    sc->sram_size    = 0x20000;
    sc->spis_num     = 3;
    sc->wdts_num     = 8;
    sc->macs_num     = 3;
    sc->uarts_num    = 13;
    sc->num_cpus     = 4;
    sc->uarts_base   = ASPEED_DEV_UART0;
    sc->irqmap       = aspeed_soc_ast2700a1_irqmap;
    sc->memmap       = aspeed_soc_ast2700_memmap;
    sc->get_irq      = aspeed_soc_ast2700_get_irq;
}

static const TypeInfo aspeed_soc_ast27x0_types[] = {
    {
        .name           = TYPE_ASPEED27X0_SOC,
        .parent         = TYPE_ASPEED_SOC,
        .instance_size  = sizeof(Aspeed27x0SoCState),
        .abstract       = true,
    }, {
        .name           = "ast2700-a0",
        .parent         = TYPE_ASPEED27X0_SOC,
        .instance_init  = aspeed_soc_ast2700_init,
        .class_init     = aspeed_soc_ast2700a0_class_init,
    },
    {
        .name           = "ast2700-a1",
        .parent         = TYPE_ASPEED27X0_SOC,
        .instance_init  = aspeed_soc_ast2700_init,
        .class_init     = aspeed_soc_ast2700a1_class_init,
    },
};

DEFINE_TYPES(aspeed_soc_ast27x0_types)
