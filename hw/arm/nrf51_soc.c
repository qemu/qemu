/*
 * Nordic Semiconductor nRF51 SoC
 * http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.1.pdf
 *
 * Copyright 2018 Joel Stanley <joel@jms.id.au>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/sysbus.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "qemu/log.h"

#include "hw/arm/nrf51.h"
#include "hw/arm/nrf51_soc.h"

/*
 * The size and base is for the NRF51822 part. If other parts
 * are supported in the future, add a sub-class of NRF51SoC for
 * the specific variants
 */
#define NRF51822_FLASH_PAGES    256
#define NRF51822_SRAM_PAGES     16
#define NRF51822_FLASH_SIZE     (NRF51822_FLASH_PAGES * NRF51_PAGE_SIZE)
#define NRF51822_SRAM_SIZE      (NRF51822_SRAM_PAGES * NRF51_PAGE_SIZE)

#define BASE_TO_IRQ(base) ((base >> 12) & 0x1F)

/* HCLK (the main CPU clock) on this SoC is always 16MHz */
#define HCLK_FRQ 16000000

static uint64_t clock_read(void *opaque, hwaddr addr, unsigned int size)
{
    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " [%u]\n",
                  __func__, addr, size);
    return 1;
}

static void clock_write(void *opaque, hwaddr addr, uint64_t data,
                        unsigned int size)
{
    qemu_log_mask(LOG_UNIMP, "%s: 0x%" HWADDR_PRIx " <- 0x%" PRIx64 " [%u]\n",
                  __func__, addr, data, size);
}

static const MemoryRegionOps clock_ops = {
    .read = clock_read,
    .write = clock_write
};


static void nrf51_soc_realize(DeviceState *dev_soc, Error **errp)
{
    NRF51State *s = NRF51_SOC(dev_soc);
    MemoryRegion *mr;
    uint8_t i = 0;
    hwaddr base_addr = 0;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    /*
     * HCLK on this SoC is fixed, so we set up sysclk ourselves and
     * the board shouldn't connect it.
     */
    if (clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must not be wired up by the board code");
        return;
    }
    /* This clock doesn't need migration because it is fixed-frequency */
    clock_set_hz(s->sysclk, HCLK_FRQ);
    qdev_connect_clock_in(DEVICE(&s->armv7m), "cpuclk", s->sysclk);
    /*
     * This SoC has no systick device, so don't connect refclk.
     * TODO: model the lack of systick (currently the armv7m object
     * will always provide one).
     */

    object_property_set_link(OBJECT(&s->armv7m), "memory", OBJECT(&s->container),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    if (!memory_region_init_ram(&s->sram, OBJECT(s), "nrf51.sram", s->sram_size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(&s->container, NRF51_SRAM_BASE, &s->sram);

    /* UART */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0);
    memory_region_add_subregion_overlap(&s->container, NRF51_UART_BASE, mr, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m),
                       BASE_TO_IRQ(NRF51_UART_BASE)));

    /* RNG */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rng), errp)) {
        return;
    }

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rng), 0);
    memory_region_add_subregion_overlap(&s->container, NRF51_RNG_BASE, mr, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rng), 0,
                       qdev_get_gpio_in(DEVICE(&s->armv7m),
                       BASE_TO_IRQ(NRF51_RNG_BASE)));

    /* UICR, FICR, NVMC, FLASH */
    if (!object_property_set_uint(OBJECT(&s->nvm), "flash-size",
                                  s->flash_size, errp)) {
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->nvm), errp)) {
        return;
    }

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 0);
    memory_region_add_subregion_overlap(&s->container, NRF51_NVMC_BASE, mr, 0);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 1);
    memory_region_add_subregion_overlap(&s->container, NRF51_FICR_BASE, mr, 0);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 2);
    memory_region_add_subregion_overlap(&s->container, NRF51_UICR_BASE, mr, 0);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 3);
    memory_region_add_subregion_overlap(&s->container, NRF51_FLASH_BASE, mr, 0);

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0);
    memory_region_add_subregion_overlap(&s->container, NRF51_GPIO_BASE, mr, 0);

    /* Pass all GPIOs to the SOC layer so they are available to the board */
    qdev_pass_gpios(DEVICE(&s->gpio), dev_soc, NULL);

    /* TIMER */
    for (i = 0; i < NRF51_NUM_TIMERS; i++) {
        if (!object_property_set_uint(OBJECT(&s->timer[i]), "id", i, errp)) {
            return;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer[i]), errp)) {
            return;
        }

        base_addr = NRF51_TIMER_BASE + i * NRF51_PERIPHERAL_SIZE;

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer[i]), 0, base_addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->armv7m),
                                            BASE_TO_IRQ(base_addr)));
    }

    /* STUB Peripherals */
    memory_region_init_io(&s->clock, OBJECT(dev_soc), &clock_ops, NULL,
                          "nrf51_soc.clock", NRF51_PERIPHERAL_SIZE);
    memory_region_add_subregion_overlap(&s->container,
                                        NRF51_IOMEM_BASE, &s->clock, -1);

    create_unimplemented_device("nrf51_soc.io", NRF51_IOMEM_BASE,
                                NRF51_IOMEM_SIZE);
    create_unimplemented_device("nrf51_soc.private",
                                NRF51_PRIVATE_BASE, NRF51_PRIVATE_SIZE);
}

static void nrf51_soc_init(Object *obj)
{
    uint8_t i = 0;

    NRF51State *s = NRF51_SOC(obj);

    memory_region_init(&s->container, obj, "nrf51-container", UINT64_MAX);

    object_initialize_child(OBJECT(s), "armv6m", &s->armv7m, TYPE_ARMV7M);
    qdev_prop_set_string(DEVICE(&s->armv7m), "cpu-type",
                         ARM_CPU_TYPE_NAME("cortex-m0"));
    qdev_prop_set_uint32(DEVICE(&s->armv7m), "num-irq", 32);

    object_initialize_child(obj, "uart", &s->uart, TYPE_NRF51_UART);
    object_property_add_alias(obj, "serial0", OBJECT(&s->uart), "chardev");

    object_initialize_child(obj, "rng", &s->rng, TYPE_NRF51_RNG);

    object_initialize_child(obj, "nvm", &s->nvm, TYPE_NRF51_NVM);

    object_initialize_child(obj, "gpio", &s->gpio, TYPE_NRF51_GPIO);

    for (i = 0; i < NRF51_NUM_TIMERS; i++) {
        object_initialize_child(obj, "timer[*]", &s->timer[i],
                                TYPE_NRF51_TIMER);

    }

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
}

static const Property nrf51_soc_properties[] = {
    DEFINE_PROP_LINK("memory", NRF51State, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("sram-size", NRF51State, sram_size, NRF51822_SRAM_SIZE),
    DEFINE_PROP_UINT32("flash-size", NRF51State, flash_size,
                       NRF51822_FLASH_SIZE),
};

static void nrf51_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nrf51_soc_realize;
    device_class_set_props(dc, nrf51_soc_properties);
}

static const TypeInfo nrf51_soc_info = {
    .name          = TYPE_NRF51_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF51State),
    .instance_init = nrf51_soc_init,
    .class_init    = nrf51_soc_class_init,
};

static void nrf51_soc_types(void)
{
    type_register_static(&nrf51_soc_info);
}
type_init(nrf51_soc_types)
