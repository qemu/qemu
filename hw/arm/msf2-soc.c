/*
 * SmartFusion2 SoC emulation.
 *
 * Copyright (c) 2017-2020 Subbaraya Sundeep <sundeep.lkml@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/char/serial.h"
#include "hw/irq.h"
#include "hw/arm/msf2-soc.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"

#define MSF2_TIMER_BASE       0x40004000
#define MSF2_SYSREG_BASE      0x40038000
#define MSF2_EMAC_BASE        0x40041000

#define ENVM_BASE_ADDRESS     0x60000000

#define SRAM_BASE_ADDRESS     0x20000000

#define MSF2_EMAC_IRQ         12

#define MSF2_ENVM_MAX_SIZE    (512 * KiB)

/*
 * eSRAM max size is 80k without SECDED(Single error correction and
 * dual error detection) feature and 64k with SECDED.
 * We do not support SECDED now.
 */
#define MSF2_ESRAM_MAX_SIZE       (80 * KiB)

static const uint32_t spi_addr[MSF2_NUM_SPIS] = { 0x40001000 , 0x40011000 };
static const uint32_t uart_addr[MSF2_NUM_UARTS] = { 0x40000000 , 0x40010000 };

static const int spi_irq[MSF2_NUM_SPIS] = { 2, 3 };
static const int uart_irq[MSF2_NUM_UARTS] = { 10, 11 };
static const int timer_irq[MSF2_NUM_TIMERS] = { 14, 15 };

static void m2sxxx_soc_initfn(Object *obj)
{
    MSF2State *s = MSF2_SOC(obj);
    int i;

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    object_initialize_child(obj, "sysreg", &s->sysreg, TYPE_MSF2_SYSREG);

    object_initialize_child(obj, "timer", &s->timer, TYPE_MSS_TIMER);

    for (i = 0; i < MSF2_NUM_SPIS; i++) {
        object_initialize_child(obj, "spi[*]", &s->spi[i], TYPE_MSS_SPI);
    }

    object_initialize_child(obj, "emac", &s->emac, TYPE_MSS_EMAC);
}

static void m2sxxx_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MSF2State *s = MSF2_SOC(dev_soc);
    DeviceState *dev, *armv7m;
    SysBusDevice *busdev;
    int i;

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *nvm = g_new(MemoryRegion, 1);
    MemoryRegion *nvm_alias = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);

    memory_region_init_rom(nvm, OBJECT(dev_soc), "MSF2.eNVM", s->envm_size,
                           &error_fatal);
    /*
     * On power-on, the eNVM region 0x60000000 is automatically
     * remapped to the Cortex-M3 processor executable region
     * start address (0x0). We do not support remapping other eNVM,
     * eSRAM and DDR regions by guest(via Sysreg) currently.
     */
    memory_region_init_alias(nvm_alias, OBJECT(dev_soc), "MSF2.eNVM", nvm, 0,
                             s->envm_size);

    memory_region_add_subregion(system_memory, ENVM_BASE_ADDRESS, nvm);
    memory_region_add_subregion(system_memory, 0, nvm_alias);

    memory_region_init_ram(sram, NULL, "MSF2.eSRAM", s->esram_size,
                           &error_fatal);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, sram);

    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 81);
    qdev_prop_set_string(armv7m, "cpu-type", s->cpu_type);
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    if (!s->m3clk) {
        error_setg(errp, "Invalid m3clk value");
        error_append_hint(errp, "m3clk can not be zero\n");
        return;
    }

    system_clock_scale = NANOSECONDS_PER_SECOND / s->m3clk;

    for (i = 0; i < MSF2_NUM_UARTS; i++) {
        if (serial_hd(i)) {
            serial_mm_init(get_system_memory(), uart_addr[i], 2,
                           qdev_get_gpio_in(armv7m, uart_irq[i]),
                           115200, serial_hd(i), DEVICE_NATIVE_ENDIAN);
        }
    }

    dev = DEVICE(&s->timer);
    /* APB0 clock is the timer input clock */
    qdev_prop_set_uint32(dev, "clock-frequency", s->m3clk / s->apb0div);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer), errp)) {
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, MSF2_TIMER_BASE);
    sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(armv7m, timer_irq[0]));
    sysbus_connect_irq(busdev, 1,
                           qdev_get_gpio_in(armv7m, timer_irq[1]));

    dev = DEVICE(&s->sysreg);
    qdev_prop_set_uint32(dev, "apb0divisor", s->apb0div);
    qdev_prop_set_uint32(dev, "apb1divisor", s->apb1div);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sysreg), errp)) {
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, MSF2_SYSREG_BASE);

    for (i = 0; i < MSF2_NUM_SPIS; i++) {
        gchar *bus_name;

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0, spi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           qdev_get_gpio_in(armv7m, spi_irq[i]));

        /* Alias controller SPI bus to the SoC itself */
        bus_name = g_strdup_printf("spi%d", i);
        object_property_add_alias(OBJECT(s), bus_name,
                                  OBJECT(&s->spi[i]), "spi");
        g_free(bus_name);
    }

    /* FIXME use qdev NIC properties instead of nd_table[] */
    if (nd_table[0].used) {
        qemu_check_nic_model(&nd_table[0], TYPE_MSS_EMAC);
        qdev_set_nic_properties(DEVICE(&s->emac), &nd_table[0]);
    }
    dev = DEVICE(&s->emac);
    object_property_set_link(OBJECT(&s->emac), "ahb-bus",
                             OBJECT(get_system_memory()), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->emac), errp)) {
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, MSF2_EMAC_BASE);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(armv7m, MSF2_EMAC_IRQ));

    /* Below devices are not modelled yet. */
    create_unimplemented_device("i2c_0", 0x40002000, 0x1000);
    create_unimplemented_device("dma", 0x40003000, 0x1000);
    create_unimplemented_device("watchdog", 0x40005000, 0x1000);
    create_unimplemented_device("i2c_1", 0x40012000, 0x1000);
    create_unimplemented_device("gpio", 0x40013000, 0x1000);
    create_unimplemented_device("hs-dma", 0x40014000, 0x1000);
    create_unimplemented_device("can", 0x40015000, 0x1000);
    create_unimplemented_device("rtc", 0x40017000, 0x1000);
    create_unimplemented_device("apb_config", 0x40020000, 0x10000);
    create_unimplemented_device("usb", 0x40043000, 0x1000);
}

static Property m2sxxx_soc_properties[] = {
    /*
     * part name specifies the type of SmartFusion2 device variant(this
     * property is for information purpose only.
     */
    DEFINE_PROP_STRING("cpu-type", MSF2State, cpu_type),
    DEFINE_PROP_STRING("part-name", MSF2State, part_name),
    DEFINE_PROP_UINT64("eNVM-size", MSF2State, envm_size, MSF2_ENVM_MAX_SIZE),
    DEFINE_PROP_UINT64("eSRAM-size", MSF2State, esram_size,
                        MSF2_ESRAM_MAX_SIZE),
    /* Libero GUI shows 100Mhz as default for clocks */
    DEFINE_PROP_UINT32("m3clk", MSF2State, m3clk, 100 * 1000000),
    /* default divisors in Libero GUI */
    DEFINE_PROP_UINT8("apb0div", MSF2State, apb0div, 2),
    DEFINE_PROP_UINT8("apb1div", MSF2State, apb1div, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void m2sxxx_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = m2sxxx_soc_realize;
    device_class_set_props(dc, m2sxxx_soc_properties);
}

static const TypeInfo m2sxxx_soc_info = {
    .name          = TYPE_MSF2_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MSF2State),
    .instance_init = m2sxxx_soc_initfn,
    .class_init    = m2sxxx_soc_class_init,
};

static void m2sxxx_soc_types(void)
{
    type_register_static(&m2sxxx_soc_info);
}

type_init(m2sxxx_soc_types)
