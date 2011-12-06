/* hw/s3c2410x.c
 *
 * Samsung S3C2410X emulation
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "sysbus.h"
#include "sysemu.h"

#include "s3c2410x.h"

#define logout(fmt, ...) \
    fprintf(stderr, "S3C24xx\t%-24s" fmt, __func__, ##__VA_ARGS__)

#define TODO() logout("%s:%u: missing\n", __FILE__, __LINE__)

/* S3C2410 SoC IDs */
#define CPU_S3C2410X_IDENT_S3C2410X 0x32410000
#define CPU_S3C2410X_IDENT_S3C2410A 0x32410002

/* Integrated peripherals */

/* SRAM */
#define CPU_S3C2410X_SRAM_BASE (CPU_S3C2410X_PERIPHERAL + 0x00000000)
#define CPU_S3C2410X_SRAM_SIZE 4096

/* Memory control */
#define CPU_S3C2410X_MEMC_BASE (CPU_S3C2410X_PERIPHERAL + 0x8000000)

/* USB controller */
#define CPU_S3C2410X_OHCI_BASE (CPU_S3C2410X_PERIPHERAL + 0x9000000)

/* Interrupt controller */
#define CPU_S3C2410X_IRQ_BASE (CPU_S3C2410X_PERIPHERAL + 0xA000000)

/* Clock control */
#define CPU_S3C2410X_CLKCON_BASE (CPU_S3C2410X_PERIPHERAL + 0xC000000)

/* LCD controller */
#define CPU_S3C2410X_LCD_BASE (CPU_S3C2410X_PERIPHERAL + 0xD000000)

/* NAND */
#define CPU_S3C2410X_NAND_BASE (CPU_S3C2410X_PERIPHERAL + 0xE000000)

/* serial port bases */
#define CPU_S3C2410X_SERIAL0_BASE (CPU_S3C2410X_PERIPHERAL + 0x10000000)
#define CPU_S3C2410X_SERIAL1_BASE (CPU_S3C2410X_PERIPHERAL + 0x10004000)
#define CPU_S3C2410X_SERIAL2_BASE (CPU_S3C2410X_PERIPHERAL + 0x10008000)

/* Timer controller */
#define CPU_S3C2410X_TIMERS_BASE        (CPU_S3C2410X_PERIPHERAL + 0x11000000)
#define CPU_S3C24XX_WDG_BASE            (CPU_S3C2410X_PERIPHERAL + 0x13000000)

/* IIC */
#define CPU_S3C2410X_IIC_BASE           (CPU_S3C2410X_PERIPHERAL + 0x14000000)

/* GPIO */
#define CPU_S3C2410X_GPIO_BASE          (CPU_S3C2410X_PERIPHERAL + 0x16000000)

/* Real time clock */
#define CPU_S3C2410X_RTC_BASE           (CPU_S3C2410X_PERIPHERAL + 0x17000000)
#define CPU_S3C24XX_ADC_BASE            (CPU_S3C2410X_PERIPHERAL + 0x18000000)

/*----------------------------------------------------------------------------*/

/* Watchdog timer. */

typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
} S3C24xxWdgState;

static uint64_t s3c24xx_wdg_read(void *opaque, target_phys_addr_t offset,
                                 unsigned size)
{
    //~ S3C24xxWdgState *s = opaque;
    logout("0x" TARGET_FMT_plx "\n", offset);

    switch (offset) {
    default:
        return 0;
    }
}

static void s3c24xx_wdg_write(void *opaque, target_phys_addr_t offset,
                              uint64_t value, unsigned size)
{
    //~ S3C24xxWdgState *s = opaque;

    logout("0x" TARGET_FMT_plx " 0x%08" PRIx64 "\n", offset, value);

    switch (offset) {
    }
}

static void s3c24xx_wdg_reset(DeviceState *d)
{
    //~ S3C24xxWdgState *s = FROM_SYSBUS(S3C24xxWdgState, sysbus_from_qdev(d));
}

static const MemoryRegionOps s3c24xx_wdg_ops = {
    .read = s3c24xx_wdg_read,
    .write = s3c24xx_wdg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int s3c24xx_wdg_init(SysBusDevice *dev)
{
    S3C24xxWdgState *s = FROM_SYSBUS(S3C24xxWdgState, dev);

    logout("\n");
    memory_region_init_io(&s->mmio, &s3c24xx_wdg_ops, s,
                          "s3c24xx-wdg", 3 * 4);
    sysbus_init_mmio(dev, &s->mmio);

    //~ qdev_init_gpio_in(&dev->qdev, mv88w8618_pic_set_irq, 32);
    //~ sysbus_init_irq(dev, &s->parent_irq);
    return 0;
}

static const VMStateDescription s3c24xx_wdg_vmsd = {
    .name = "s3c24xx_wdg",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo s3c24xx_wdg_info = {
    .init = s3c24xx_wdg_init,
    .qdev.name = "s3c24xx_wdg",
    .qdev.size = sizeof(S3C24xxWdgState),
    .qdev.reset = s3c24xx_wdg_reset,
    .qdev.vmsd = &s3c24xx_wdg_vmsd,
};

static void s3c24xx_wdg_register(void)
{
    sysbus_register_withprop(&s3c24xx_wdg_info);
}

device_init(s3c24xx_wdg_register)

/*----------------------------------------------------------------------------*/

/* ADC. */

typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
} S3C24xxAdcState;

static uint64_t s3c24xx_adc_read(void *opaque, target_phys_addr_t offset,
                                 unsigned size)
{
    //~ S3C24xxAdcState *s = opaque;
    logout("0x" TARGET_FMT_plx "\n", offset);

    switch (offset) {
    //~ case MP_PIC_STATUS:
        //~ return s->level & s->enabled;

    default:
        return 0;
    }
}

static void s3c24xx_adc_write(void *opaque, target_phys_addr_t offset,
                              uint64_t value, unsigned size)
{
    //~ S3C24xxAdcState *s = opaque;

    logout("0x" TARGET_FMT_plx " 0x%08" PRIx64 "\n", offset, value);

    switch (offset) {
    //~ case MP_PIC_ENABLE_SET:
        //~ s->enabled |= value;
        //~ break;

    //~ case MP_PIC_ENABLE_CLR:
        //~ s->enabled &= ~value;
        //~ s->level &= ~value;
        //~ break;
    }
    //~ mv88w8618_pic_update(s);
}

static void s3c24xx_adc_reset(DeviceState *d)
{
    //~ S3C24xxAdcState *s = FROM_SYSBUS(S3C24xxAdcState, sysbus_from_qdev(d));
}

static const MemoryRegionOps s3c24xx_adc_ops = {
    .read = s3c24xx_adc_read,
    .write = s3c24xx_adc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int s3c24xx_adc_init(SysBusDevice *dev)
{
    S3C24xxAdcState *s = FROM_SYSBUS(S3C24xxAdcState, dev);

    logout("\n");
    memory_region_init_io(&s->mmio, &s3c24xx_adc_ops, s,
                          "s3c24xx-adc", 7 * 4);
    sysbus_init_mmio(dev, &s->mmio);

    //~ qdev_init_gpio_in(&dev->qdev, mv88w8618_pic_set_irq, 32);
    //~ sysbus_init_irq(dev, &s->parent_irq);
    return 0;
}

static const VMStateDescription s3c24xx_adc_vmsd = {
    .name = "s3c24xx_adc",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo s3c24xx_adc_info = {
    .init = s3c24xx_adc_init,
    .qdev.name = "s3c24xx_adc",
    .qdev.size = sizeof(S3C24xxAdcState),
    .qdev.reset = s3c24xx_adc_reset,
    .qdev.vmsd = &s3c24xx_adc_vmsd,
};

static void s3c24xx_adc_register(void)
{
    sysbus_register_withprop(&s3c24xx_adc_info);
}

device_init(s3c24xx_adc_register)

/*----------------------------------------------------------------------------*/

/* Initialise a Samsung S3C2410X SOC ARM core and internal peripherals. */
S3CState *
s3c2410x_init(int sdram_size)
{
    DeviceState *dev;
    ram_addr_t offset;
    S3CState *s = g_malloc0(sizeof(S3CState));

    /* Prepare the ARM 920T core */
    s->cpu_env = cpu_init("arm920t");

    /* S3C2410X SDRAM memory is always at the same physical location */
    offset = qemu_ram_alloc(NULL, "s3c2410x.sdram", sdram_size);
    cpu_register_physical_memory(CPU_S3C2410X_DRAM,
                                 ram_size,
                                 offset | IO_MEM_RAM);
    cpu_register_physical_memory(CPU_S3C2410X_DRAM + 0x80000000,
                                 ram_size,
                                 offset | IO_MEM_RAM);
    cpu_register_physical_memory(CPU_S3C2410X_DRAM + 0x90000000,
                                 ram_size,
                                 offset | IO_MEM_RAM);

    /* S3C2410X SRAM */
    offset = qemu_ram_alloc(NULL, "s3c2410x.sram", CPU_S3C2410X_SRAM_SIZE);
    cpu_register_physical_memory(CPU_S3C2410X_SRAM_BASE,
                                 CPU_S3C2410X_SRAM_SIZE,
                                 offset | IO_MEM_RAM);

    /* SDRAM memory controller */
    s->memc = s3c24xx_memc_init(CPU_S3C2410X_MEMC_BASE);

    /* Interrupt controller */
    s->irq = s3c24xx_irq_init(s, CPU_S3C2410X_IRQ_BASE);

    /* Clock and power control */
    s->clkcon = s3c24xx_clkcon_init(s, CPU_S3C2410X_CLKCON_BASE, 12000000);

    /* Timer controller */
    s->timers = s3c24xx_timers_init(s, CPU_S3C2410X_TIMERS_BASE, 0, 12000000);

    /* Serial port controllers */
    s->uart[0] = s3c24xx_serial_init(s, serial_hds[0], CPU_S3C2410X_SERIAL0_BASE, 32);
    s->uart[1] = s3c24xx_serial_init(s, serial_hds[1], CPU_S3C2410X_SERIAL1_BASE, 35);
    s->uart[2] = s3c24xx_serial_init(s, serial_hds[2], CPU_S3C2410X_SERIAL2_BASE, 38);

    /* Real time clock */
    s->rtc = s3c24xx_rtc_init(CPU_S3C2410X_RTC_BASE);

    /* GPIO */
    dev = sysbus_create_simple("s3c24xx_gpio", CPU_S3C2410X_GPIO_BASE, NULL);
    s->gpio = s3c24xx_gpio_init(s, CPU_S3C2410X_GPIO_BASE, CPU_S3C2410X_IDENT_S3C2410A);

    /* I2C */
    s->iic = s3c24xx_iic_init(s3c24xx_get_irq(s->irq, 27),
                              CPU_S3C2410X_IIC_BASE);

    /* LCD controller */
    dev = sysbus_create_simple("s3c24xx_lcd", CPU_S3C2410X_LCD_BASE,
                               s3c24xx_get_irq(s->irq, 16));

    /* NAND controller */
    s->nand = s3c24xx_nand_init(CPU_S3C2410X_NAND_BASE);

    /* A two port OHCI controller */
    dev = qdev_create(NULL, "sysbus-ohci");
    qdev_prop_set_uint32(dev, "num-ports", 2);
    //~ qdev_prop_set_taddr(dev, "dma-offset", base);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, CPU_S3C2410X_OHCI_BASE);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, s3c24xx_get_irq(s->irq, 26));

    dev = sysbus_create_simple("s3c24xx_wdg", CPU_S3C24XX_WDG_BASE, NULL);
    dev = sysbus_create_simple("s3c24xx_adc", CPU_S3C24XX_ADC_BASE, NULL);

    return s;
}
