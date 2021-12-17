/*
 * QEMU ATmega MCU
 *
 * Copyright (c) 2019-2020 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/misc/unimp.h"
#include "atmega.h"

enum AtmegaPeripheral {
    POWER0, POWER1,
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF,
    GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK, GPIOL,
    USART0, USART1, USART2, USART3,
    TIMER0, TIMER1, TIMER2, TIMER3, TIMER4, TIMER5,
    PERIFMAX
};

#define GPIO(n)     (n + GPIOA)
#define USART(n)    (n + USART0)
#define TIMER(n)    (n + TIMER0)
#define POWER(n)    (n + POWER0)

typedef struct {
    uint16_t addr;
    enum AtmegaPeripheral power_index;
    uint8_t power_bit;
    /* timer specific */
    uint16_t intmask_addr;
    uint16_t intflag_addr;
    bool is_timer16;
} peripheral_cfg;

struct AtmegaMcuClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/
    const char *uc_name;
    const char *cpu_type;
    size_t flash_size;
    size_t eeprom_size;
    size_t sram_size;
    size_t io_size;
    size_t gpio_count;
    size_t adc_count;
    const uint8_t *irq;
    const peripheral_cfg *dev;
};
typedef struct AtmegaMcuClass AtmegaMcuClass;

DECLARE_CLASS_CHECKERS(AtmegaMcuClass, ATMEGA_MCU,
                       TYPE_ATMEGA_MCU)

static const peripheral_cfg dev168_328[PERIFMAX] = {
    [USART0]        = {  0xc0, POWER0, 1 },
    [TIMER2]        = {  0xb0, POWER0, 6, 0x70, 0x37, false },
    [TIMER1]        = {  0x80, POWER0, 3, 0x6f, 0x36, true },
    [POWER0]        = {  0x64 },
    [TIMER0]        = {  0x44, POWER0, 5, 0x6e, 0x35, false },
    [GPIOD]         = {  0x29 },
    [GPIOC]         = {  0x26 },
    [GPIOB]         = {  0x23 },
}, dev1280_2560[PERIFMAX] = {
    [USART3]        = { 0x130, POWER1, 2 },
    [TIMER5]        = { 0x120, POWER1, 5, 0x73, 0x3a, true },
    [GPIOL]         = { 0x109 },
    [GPIOK]         = { 0x106 },
    [GPIOJ]         = { 0x103 },
    [GPIOH]         = { 0x100 },
    [USART2]        = {  0xd0, POWER1, 1 },
    [USART1]        = {  0xc8, POWER1, 0 },
    [USART0]        = {  0xc0, POWER0, 1 },
    [TIMER2]        = {  0xb0, POWER0, 6, 0x70, 0x37, false }, /* TODO async */
    [TIMER4]        = {  0xa0, POWER1, 4, 0x72, 0x39, true },
    [TIMER3]        = {  0x90, POWER1, 3, 0x71, 0x38, true },
    [TIMER1]        = {  0x80, POWER0, 3, 0x6f, 0x36, true },
    [POWER1]        = {  0x65 },
    [POWER0]        = {  0x64 },
    [TIMER0]        = {  0x44, POWER0, 5, 0x6e, 0x35, false },
    [GPIOG]         = {  0x32 },
    [GPIOF]         = {  0x2f },
    [GPIOE]         = {  0x2c },
    [GPIOD]         = {  0x29 },
    [GPIOC]         = {  0x26 },
    [GPIOB]         = {  0x23 },
    [GPIOA]         = {  0x20 },
};

enum AtmegaIrq {
    USART0_RXC_IRQ, USART0_DRE_IRQ, USART0_TXC_IRQ,
    USART1_RXC_IRQ, USART1_DRE_IRQ, USART1_TXC_IRQ,
    USART2_RXC_IRQ, USART2_DRE_IRQ, USART2_TXC_IRQ,
    USART3_RXC_IRQ, USART3_DRE_IRQ, USART3_TXC_IRQ,
    TIMER0_CAPT_IRQ, TIMER0_COMPA_IRQ, TIMER0_COMPB_IRQ,
        TIMER0_COMPC_IRQ, TIMER0_OVF_IRQ,
    TIMER1_CAPT_IRQ, TIMER1_COMPA_IRQ, TIMER1_COMPB_IRQ,
        TIMER1_COMPC_IRQ, TIMER1_OVF_IRQ,
    TIMER2_CAPT_IRQ, TIMER2_COMPA_IRQ, TIMER2_COMPB_IRQ,
        TIMER2_COMPC_IRQ, TIMER2_OVF_IRQ,
    TIMER3_CAPT_IRQ, TIMER3_COMPA_IRQ, TIMER3_COMPB_IRQ,
        TIMER3_COMPC_IRQ, TIMER3_OVF_IRQ,
    TIMER4_CAPT_IRQ, TIMER4_COMPA_IRQ, TIMER4_COMPB_IRQ,
        TIMER4_COMPC_IRQ, TIMER4_OVF_IRQ,
    TIMER5_CAPT_IRQ, TIMER5_COMPA_IRQ, TIMER5_COMPB_IRQ,
        TIMER5_COMPC_IRQ, TIMER5_OVF_IRQ,
    IRQ_COUNT
};

#define USART_IRQ_COUNT     3
#define USART_RXC_IRQ(n)    (n * USART_IRQ_COUNT + USART0_RXC_IRQ)
#define USART_DRE_IRQ(n)    (n * USART_IRQ_COUNT + USART0_DRE_IRQ)
#define USART_TXC_IRQ(n)    (n * USART_IRQ_COUNT + USART0_TXC_IRQ)
#define TIMER_IRQ_COUNT     5
#define TIMER_CAPT_IRQ(n)   (n * TIMER_IRQ_COUNT + TIMER0_CAPT_IRQ)
#define TIMER_COMPA_IRQ(n)  (n * TIMER_IRQ_COUNT + TIMER0_COMPA_IRQ)
#define TIMER_COMPB_IRQ(n)  (n * TIMER_IRQ_COUNT + TIMER0_COMPB_IRQ)
#define TIMER_COMPC_IRQ(n)  (n * TIMER_IRQ_COUNT + TIMER0_COMPC_IRQ)
#define TIMER_OVF_IRQ(n)    (n * TIMER_IRQ_COUNT + TIMER0_OVF_IRQ)

static const uint8_t irq168_328[IRQ_COUNT] = {
    [TIMER2_COMPA_IRQ]      = 8,
    [TIMER2_COMPB_IRQ]      = 9,
    [TIMER2_OVF_IRQ]        = 10,
    [TIMER1_CAPT_IRQ]       = 11,
    [TIMER1_COMPA_IRQ]      = 12,
    [TIMER1_COMPB_IRQ]      = 13,
    [TIMER1_OVF_IRQ]        = 14,
    [TIMER0_COMPA_IRQ]      = 15,
    [TIMER0_COMPB_IRQ]      = 16,
    [TIMER0_OVF_IRQ]        = 17,
    [USART0_RXC_IRQ]        = 19,
    [USART0_DRE_IRQ]        = 20,
    [USART0_TXC_IRQ]        = 21,
}, irq1280_2560[IRQ_COUNT] = {
    [TIMER2_COMPA_IRQ]      = 14,
    [TIMER2_COMPB_IRQ]      = 15,
    [TIMER2_OVF_IRQ]        = 16,
    [TIMER1_CAPT_IRQ]       = 17,
    [TIMER1_COMPA_IRQ]      = 18,
    [TIMER1_COMPB_IRQ]      = 19,
    [TIMER1_COMPC_IRQ]      = 20,
    [TIMER1_OVF_IRQ]        = 21,
    [TIMER0_COMPA_IRQ]      = 22,
    [TIMER0_COMPB_IRQ]      = 23,
    [TIMER0_OVF_IRQ]        = 24,
    [USART0_RXC_IRQ]        = 26,
    [USART0_DRE_IRQ]        = 27,
    [USART0_TXC_IRQ]        = 28,
    [TIMER3_CAPT_IRQ]       = 32,
    [TIMER3_COMPA_IRQ]      = 33,
    [TIMER3_COMPB_IRQ]      = 34,
    [TIMER3_COMPC_IRQ]      = 35,
    [TIMER3_OVF_IRQ]        = 36,
    [USART1_RXC_IRQ]        = 37,
    [USART1_DRE_IRQ]        = 38,
    [USART1_TXC_IRQ]        = 39,
    [TIMER4_CAPT_IRQ]       = 42,
    [TIMER4_COMPA_IRQ]      = 43,
    [TIMER4_COMPB_IRQ]      = 44,
    [TIMER4_COMPC_IRQ]      = 45,
    [TIMER4_OVF_IRQ]        = 46,
    [TIMER5_CAPT_IRQ]       = 47,
    [TIMER5_COMPA_IRQ]      = 48,
    [TIMER5_COMPB_IRQ]      = 49,
    [TIMER5_COMPC_IRQ]      = 50,
    [TIMER5_OVF_IRQ]        = 51,
    [USART2_RXC_IRQ]        = 52,
    [USART2_DRE_IRQ]        = 53,
    [USART2_TXC_IRQ]        = 54,
    [USART3_RXC_IRQ]        = 55,
    [USART3_DRE_IRQ]        = 56,
    [USART3_TXC_IRQ]        = 57,
};

static void connect_peripheral_irq(const AtmegaMcuClass *k,
                                   SysBusDevice *dev, int dev_irqn,
                                   DeviceState *cpu,
                                   unsigned peripheral_index)
{
    int cpu_irq = k->irq[peripheral_index];

    if (!cpu_irq) {
        return;
    }
    /* FIXME move that to avr_cpu_set_int() once 'sample' board is removed */
    assert(cpu_irq >= 2);
    cpu_irq -= 2;

    sysbus_connect_irq(dev, dev_irqn, qdev_get_gpio_in(cpu, cpu_irq));
}

static void connect_power_reduction_gpio(AtmegaMcuState *s,
                                         const AtmegaMcuClass *k,
                                         DeviceState *cpu,
                                         unsigned peripheral_index)
{
    unsigned power_index = k->dev[peripheral_index].power_index;
    assert(k->dev[power_index].addr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pwr[power_index - POWER0]),
                       k->dev[peripheral_index].power_bit,
                       qdev_get_gpio_in(cpu, 0));
}

static void atmega_realize(DeviceState *dev, Error **errp)
{
    AtmegaMcuState *s = ATMEGA_MCU(dev);
    const AtmegaMcuClass *mc = ATMEGA_MCU_GET_CLASS(dev);
    DeviceState *cpudev;
    SysBusDevice *sbd;
    char *devname;
    size_t i;

    assert(mc->io_size <= 0x200);

    if (!s->xtal_freq_hz) {
        error_setg(errp, "\"xtal-frequency-hz\" property must be provided.");
        return;
    }

    /* CPU */
    object_initialize_child(OBJECT(dev), "cpu", &s->cpu, mc->cpu_type);
    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);
    cpudev = DEVICE(&s->cpu);

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "sram", mc->sram_size,
                           &error_abort);
    memory_region_add_subregion(get_system_memory(),
                                OFFSET_DATA + mc->io_size, &s->sram);

    /* Flash */
    memory_region_init_rom(&s->flash, OBJECT(dev),
                           "flash", mc->flash_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), OFFSET_CODE, &s->flash);

    /*
     * I/O
     *
     * 0x00 - 0x1f: Registers
     * 0x20 - 0x5f: I/O memory
     * 0x60 - 0xff: Extended I/O
     */
    s->io = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(s->io, "name", "I/O");
    qdev_prop_set_uint64(s->io, "size", mc->io_size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s->io), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(s->io), 0, OFFSET_DATA, -1234);

    /* Power Reduction */
    for (i = 0; i < POWER_MAX; i++) {
        int idx = POWER(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        devname = g_strdup_printf("power%zu", i);
        object_initialize_child(OBJECT(dev), devname, &s->pwr[i],
                                TYPE_AVR_MASK);
        sysbus_realize(SYS_BUS_DEVICE(&s->pwr[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->pwr[i]), 0,
                        OFFSET_DATA + mc->dev[idx].addr);
        g_free(devname);
    }

    /* GPIO */
    for (i = 0; i < GPIO_MAX; i++) {
        int idx = GPIO(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        devname = g_strdup_printf("atmega-gpio-%c", 'a' + (char)i);
        create_unimplemented_device(devname,
                                    OFFSET_DATA + mc->dev[idx].addr, 3);
        g_free(devname);
    }

    /* USART */
    for (i = 0; i < USART_MAX; i++) {
        int idx = USART(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        devname = g_strdup_printf("usart%zu", i);
        object_initialize_child(OBJECT(dev), devname, &s->usart[i],
                                TYPE_AVR_USART);
        qdev_prop_set_chr(DEVICE(&s->usart[i]), "chardev", serial_hd(i));
        sbd = SYS_BUS_DEVICE(&s->usart[i]);
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, OFFSET_DATA + mc->dev[USART(i)].addr);
        connect_peripheral_irq(mc, sbd, 0, cpudev, USART_RXC_IRQ(i));
        connect_peripheral_irq(mc, sbd, 1, cpudev, USART_DRE_IRQ(i));
        connect_peripheral_irq(mc, sbd, 2, cpudev, USART_TXC_IRQ(i));
        connect_power_reduction_gpio(s, mc, DEVICE(&s->usart[i]), idx);
        g_free(devname);
    }

    /* Timer */
    for (i = 0; i < TIMER_MAX; i++) {
        int idx = TIMER(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        if (!mc->dev[idx].is_timer16) {
            create_unimplemented_device("avr-timer8",
                                        OFFSET_DATA + mc->dev[idx].addr, 5);
            create_unimplemented_device("avr-timer8-intmask",
                                        OFFSET_DATA
                                        + mc->dev[idx].intmask_addr, 1);
            create_unimplemented_device("avr-timer8-intflag",
                                        OFFSET_DATA
                                        + mc->dev[idx].intflag_addr, 1);
            continue;
        }
        devname = g_strdup_printf("timer%zu", i);
        object_initialize_child(OBJECT(dev), devname, &s->timer[i],
                                TYPE_AVR_TIMER16);
        object_property_set_uint(OBJECT(&s->timer[i]), "cpu-frequency-hz",
                                 s->xtal_freq_hz, &error_abort);
        sbd = SYS_BUS_DEVICE(&s->timer[i]);
        sysbus_realize(sbd, &error_abort);
        sysbus_mmio_map(sbd, 0, OFFSET_DATA + mc->dev[idx].addr);
        sysbus_mmio_map(sbd, 1, OFFSET_DATA + mc->dev[idx].intmask_addr);
        sysbus_mmio_map(sbd, 2, OFFSET_DATA + mc->dev[idx].intflag_addr);
        connect_peripheral_irq(mc, sbd, 0, cpudev, TIMER_CAPT_IRQ(i));
        connect_peripheral_irq(mc, sbd, 1, cpudev, TIMER_COMPA_IRQ(i));
        connect_peripheral_irq(mc, sbd, 2, cpudev, TIMER_COMPB_IRQ(i));
        connect_peripheral_irq(mc, sbd, 3, cpudev, TIMER_COMPC_IRQ(i));
        connect_peripheral_irq(mc, sbd, 4, cpudev, TIMER_OVF_IRQ(i));
        connect_power_reduction_gpio(s, mc, DEVICE(&s->timer[i]), idx);
        g_free(devname);
    }

    create_unimplemented_device("avr-twi",          OFFSET_DATA + 0x0b8, 6);
    create_unimplemented_device("avr-adc",          OFFSET_DATA + 0x078, 8);
    create_unimplemented_device("avr-ext-mem-ctrl", OFFSET_DATA + 0x074, 2);
    create_unimplemented_device("avr-watchdog",     OFFSET_DATA + 0x060, 1);
    create_unimplemented_device("avr-spi",          OFFSET_DATA + 0x04c, 3);
    create_unimplemented_device("avr-eeprom",       OFFSET_DATA + 0x03f, 3);
}

static Property atmega_props[] = {
    DEFINE_PROP_UINT64("xtal-frequency-hz", AtmegaMcuState,
                       xtal_freq_hz, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void atmega_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = atmega_realize;
    device_class_set_props(dc, atmega_props);
    /* Reason: Mapped at fixed location on the system bus */
    dc->user_creatable = false;
}

static void atmega168_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr5");
    amc->flash_size = 16 * KiB;
    amc->eeprom_size = 512;
    amc->sram_size = 1 * KiB;
    amc->io_size = 256;
    amc->gpio_count = 23;
    amc->adc_count = 6;
    amc->irq = irq168_328;
    amc->dev = dev168_328;
};

static void atmega328_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr5");
    amc->flash_size = 32 * KiB;
    amc->eeprom_size = 1 * KiB;
    amc->sram_size = 2 * KiB;
    amc->io_size = 256;
    amc->gpio_count = 23;
    amc->adc_count = 6;
    amc->irq = irq168_328;
    amc->dev = dev168_328;
};

static void atmega1280_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr51");
    amc->flash_size = 128 * KiB;
    amc->eeprom_size = 4 * KiB;
    amc->sram_size = 8 * KiB;
    amc->io_size = 512;
    amc->gpio_count = 86;
    amc->adc_count = 16;
    amc->irq = irq1280_2560;
    amc->dev = dev1280_2560;
};

static void atmega2560_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr6");
    amc->flash_size = 256 * KiB;
    amc->eeprom_size = 4 * KiB;
    amc->sram_size = 8 * KiB;
    amc->io_size = 512;
    amc->gpio_count = 54;
    amc->adc_count = 16;
    amc->irq = irq1280_2560;
    amc->dev = dev1280_2560;
};

static const TypeInfo atmega_mcu_types[] = {
    {
        .name           = TYPE_ATMEGA168_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega168_class_init,
    }, {
        .name           = TYPE_ATMEGA328_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega328_class_init,
    }, {
        .name           = TYPE_ATMEGA1280_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega1280_class_init,
    }, {
        .name           = TYPE_ATMEGA2560_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega2560_class_init,
    }, {
        .name           = TYPE_ATMEGA_MCU,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(AtmegaMcuState),
        .class_size     = sizeof(AtmegaMcuClass),
        .class_init     = atmega_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(atmega_mcu_types)
