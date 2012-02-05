/* hw/s3c2440.c
 *
 * Samsung S3C2440 emulation
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2012 Stefan Weil
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "sysbus.h"
#include "sysemu.h"
#include "exec-memory.h"        /* get_system_memory */

#include "s3c2440.h"

#define logout(fmt, ...) \
    fprintf(stderr, "S3C24xx\t%-24s" fmt, __func__, ##__VA_ARGS__)

#define TODO() logout("%s:%u: missing\n", __FILE__, __LINE__)

/* S3C2440 SoC ID */
#define CPU_S3C2440_IDENT_S3C2440A 0x32440001

/* Integrated peripherals */

/* SRAM */
#define CPU_S3C2440_SRAM_BASE (CPU_S3C2440_PERIPHERAL + 0x00000000)
#define CPU_S3C2440_SRAM_SIZE 4096

/* Memory control */
#define CPU_S3C2440_MEMC_BASE (CPU_S3C2440_PERIPHERAL + 0x8000000)

/* USB controller */
#define CPU_S3C2440_OHCI_BASE (CPU_S3C2440_PERIPHERAL + 0x9000000)

/* Interrupt controller */
#define CPU_S3C2440_IRQ_BASE (CPU_S3C2440_PERIPHERAL + 0xA000000)

/* Clock control */
#define CPU_S3C2440_CLKCON_BASE (CPU_S3C2440_PERIPHERAL + 0x0c000000)

/* LCD controller */
#define CPU_S3C2440_LCD_BASE (CPU_S3C2440_PERIPHERAL + 0xD000000)

/* NAND */
#define CPU_S3C2440_NAND_BASE (CPU_S3C2440_PERIPHERAL + 0xE000000)

/* serial port bases */
#define CPU_S3C2440_SERIAL0_BASE (CPU_S3C2440_PERIPHERAL + 0x10000000)
#define CPU_S3C2440_SERIAL1_BASE (CPU_S3C2440_PERIPHERAL + 0x10004000)
#define CPU_S3C2440_SERIAL2_BASE (CPU_S3C2440_PERIPHERAL + 0x10008000)

/* Timer controller */
#define CPU_S3C2440_TIMERS_BASE (CPU_S3C2440_PERIPHERAL + 0x11000000)

/* IIC */
#define CPU_S3C2440_IIC_BASE (CPU_S3C2440_PERIPHERAL + 0x14000000)

/* GPIO */
#define CPU_S3C2440_GPIO_BASE (CPU_S3C2440_PERIPHERAL + 0x16000000)

/* Real time clock */
#define CPU_S3C2440_RTC_BASE (CPU_S3C2440_PERIPHERAL + 0x17000000)
#define CPU_S3C2440_ADC_BASE (CPU_S3C2440_PERIPHERAL + 0x18000000)

/*----------------------------------------------------------------------------*/

/* Camera interface. */

typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
} S3C24xxCamState;

static uint64_t s3c24xx_cam_read(void *opaque, target_phys_addr_t offset,
                                 unsigned size)
{
    //~ S3C24xxCamState *s = opaque;
    logout("0x" TARGET_FMT_plx "\n", offset);

    switch (offset) {
    default:
        return 0;
    }
}

static void s3c24xx_cam_write(void *opaque, target_phys_addr_t offset,
                                uint64_t value, unsigned size)
{
    //~ S3C24xxCamState *s = opaque;

    logout("0x" TARGET_FMT_plx " 0x%08" PRIx64 "\n", offset, value);

    switch (offset) {
    }
}

static void s3c24xx_cam_reset(DeviceState *d)
{
    //~ S3C24xxCamState *s = FROM_SYSBUS(S3C24xxCamState, sysbus_from_qdev(d));
}

static const MemoryRegionOps s3c24xx_cam_ops = {
    .read = s3c24xx_cam_read,
    .write = s3c24xx_cam_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int s3c24xx_cam_init(SysBusDevice *dev)
{
    S3C24xxCamState *s = FROM_SYSBUS(S3C24xxCamState, dev);

    logout("\n");
    memory_region_init_io(&s->mmio, &s3c24xx_cam_ops, s, "s3c24xx-cam", 3 * 4);
    sysbus_init_mmio(dev, &s->mmio);

    //~ qdev_init_gpio_in(&dev->qdev, mv88w8618_pic_set_irq, 32);
    //~ sysbus_init_irq(dev, &s->parent_irq);
    return 0;
}

static const VMStateDescription s3c24xx_cam_vmsd = {
    .name = "s3c24xx_cam",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void s3c24xx_cam_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->reset = s3c24xx_cam_reset;
    dc->vmsd = &s3c24xx_cam_vmsd;
    k->init = s3c24xx_cam_init;
}

static TypeInfo s3c24xx_cam_info = {
    .name = "s3c24xx_cam",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S3C24xxCamState),
    .class_init = s3c24xx_cam_class_init
};

static void s3c24xx_cam_register(void)
{
    type_register_static(&s3c24xx_cam_info);
}

device_init(s3c24xx_cam_register)

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
    memory_region_init_io(&s->mmio, &s3c24xx_wdg_ops, s, "s3c24xx-wdg", 3 * 4);
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

static void s3c24xx_wdg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->reset = s3c24xx_wdg_reset;
    dc->vmsd = &s3c24xx_wdg_vmsd;
    k->init = s3c24xx_wdg_init;
}

static TypeInfo s3c24xx_wdg_info = {
    .name = "s3c24xx_wdg",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S3C24xxWdgState),
    .class_init = s3c24xx_wdg_class_init
};

static void s3c24xx_wdg_register(void)
{
    type_register_static(&s3c24xx_wdg_info);
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
    memory_region_init_io(&s->mmio, &s3c24xx_adc_ops, s, "s3c24xx-adc", 7 * 4);
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

static void s3c24xx_adc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->reset = s3c24xx_adc_reset;
    dc->vmsd = &s3c24xx_adc_vmsd;
    k->init = s3c24xx_adc_init;
}

static TypeInfo s3c24xx_adc_info = {
    .name = "s3c24xx_adc",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S3C24xxAdcState),
    .class_init = s3c24xx_adc_class_init
};

static void s3c24xx_adc_register(void)
{
    type_register_static(&s3c24xx_adc_info);
}

device_init(s3c24xx_adc_register)

/*----------------------------------------------------------------------------*/

/* Initialise a Samsung S3C2440 SOC ARM core and internal peripherals. */
S3CState *
s3c2440_init(int sdram_size)
{
    DeviceState *dev;
    MemoryRegion *sysmem = get_system_memory();
    S3CState *s = g_new0(S3CState, 1);

    /* Prepare the ARM 920T core. */
    s->cpu_env = cpu_init("arm920t");

    /* S3C2440X SDRAM memory is always at the same physical location. */
    memory_region_init_ram(&s->sdram0, "s3c2440.sdram0", sdram_size);
    memory_region_init_alias(&s->sdram1, "s3c2440.sdram1",
                             &s->sdram0, 0, sdram_size);
    memory_region_init_alias(&s->sdram2, "s3c2440.sdram2",
                             &s->sdram0, 0, sdram_size);
    memory_region_add_subregion(sysmem, CPU_S3C2440_DRAM, &s->sdram0);
    memory_region_add_subregion(sysmem,
                                CPU_S3C2440_DRAM + 0x80000000, &s->sdram1);
    memory_region_add_subregion(sysmem,
                                CPU_S3C2440_DRAM + 0x90000000, &s->sdram2);

    /* S3C2440 SRAM */
    memory_region_init_ram(&s->sram, "s3c2440.sram", CPU_S3C2440_SRAM_SIZE);
    memory_region_add_subregion(sysmem, CPU_S3C2440_SRAM_BASE, &s->sram);

    /* SDRAM memory controller */
    s->memc = s3c24xx_memc_init(CPU_S3C2440_MEMC_BASE);

    /* Interrupt controller */
    s->irq = s3c24xx_irq_init(s, CPU_S3C2440_IRQ_BASE);

    /* Clock and power control */
    s->clkcon = s3c24xx_clkcon_init(s, CPU_S3C2440_CLKCON_BASE, 12000000);

    /* Timer controller */
    s->timers = s3c24xx_timers_init(s, CPU_S3C2440_TIMERS_BASE, 0, 12000000);

    /* Serial port controllers */
    s->uart[0] = s3c24xx_serial_init(s, serial_hds[0], CPU_S3C2440_SERIAL0_BASE, 32);
    s->uart[1] = s3c24xx_serial_init(s, serial_hds[1], CPU_S3C2440_SERIAL1_BASE, 35);
    s->uart[2] = s3c24xx_serial_init(s, serial_hds[2], CPU_S3C2440_SERIAL2_BASE, 38);

    /* Real time clock */
    s->rtc = s3c24xx_rtc_init(CPU_S3C2440_RTC_BASE);

    /* And some GPIO */
    //~ dev = sysbus_create_simple("s3c24xx_gpio", CPU_S3C2440_GPIO_BASE, NULL);
    s->gpio = s3c24xx_gpio_init(s, CPU_S3C2440_GPIO_BASE, CPU_S3C2440_IDENT_S3C2440A);

    /* I2C */
    s->iic = s3c24xx_iic_init(s3c24xx_get_irq(s->irq, 27),
                              CPU_S3C2440_IIC_BASE);

    /* LCD controller */
    dev = sysbus_create_simple("s3c24xx_lcd", CPU_S3C2440_LCD_BASE,
                               s3c24xx_get_irq(s->irq, 16));

    /* NAND controller */
    s->nand = s3c24xx_nand_init(CPU_S3C2440_NAND_BASE);

    /* A two port OHCI controller */
    dev = qdev_create(NULL, "sysbus-ohci");
    qdev_prop_set_uint32(dev, "num-ports", 2);
    //~ qdev_prop_set_taddr(dev, "dma-offset", base);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, CPU_S3C2440_OHCI_BASE);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, s3c24xx_get_irq(s->irq, 26));

    dev = sysbus_create_simple("s3c24xx_adc", CPU_S3C2440_ADC_BASE, NULL);

    return s;
}
