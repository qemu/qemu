/*
 * MAX78000 Global Control Registers
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "system/runstate.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/char/max78000_uart.h"
#include "hw/misc/max78000_trng.h"
#include "hw/misc/max78000_aes.h"
#include "hw/misc/max78000_gcr.h"


static void max78000_gcr_reset_hold(Object *obj, ResetType type)
{
    DeviceState *dev = DEVICE(obj);
    Max78000GcrState *s = MAX78000_GCR(dev);
    s->sysctrl = 0x21002;
    s->rst0 = 0;
    /* All clocks are always ready */
    s->clkctrl = 0x3e140008;
    s->pm = 0x3f000;
    s->pclkdiv = 0;
    s->pclkdis0 = 0xffffffff;
    s->memctrl = 0x5;
    s->memz = 0;
    s->sysst = 0;
    s->rst1 = 0;
    s->pckdis1 = 0xffffffff;
    s->eventen = 0;
    s->revision = 0xa1;
    s->sysie = 0;
    s->eccerr = 0;
    s->ecced = 0;
    s->eccie = 0;
    s->eccaddr = 0;
}

static uint64_t max78000_gcr_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Max78000GcrState *s = opaque;

    switch (addr) {
    case SYSCTRL:
        return s->sysctrl;

    case RST0:
        return s->rst0;

    case CLKCTRL:
        return s->clkctrl;

    case PM:
        return s->pm;

    case PCLKDIV:
        return s->pclkdiv;

    case PCLKDIS0:
        return s->pclkdis0;

    case MEMCTRL:
        return s->memctrl;

    case MEMZ:
        return s->memz;

    case SYSST:
        return s->sysst;

    case RST1:
        return s->rst1;

    case PCKDIS1:
        return s->pckdis1;

    case EVENTEN:
        return s->eventen;

    case REVISION:
        return s->revision;

    case SYSIE:
        return s->sysie;

    case ECCERR:
        return s->eccerr;

    case ECCED:
        return s->ecced;

    case ECCIE:
        return s->eccie;

    case ECCADDR:
        return s->eccaddr;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
        return 0;

    }
}

static void max78000_gcr_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    Max78000GcrState *s = opaque;
    uint32_t val = val64;
    uint8_t zero[0xc000] = {0};
    switch (addr) {
    case SYSCTRL:
        /* Checksum calculations always pass immediately */
        s->sysctrl = (val & 0x30000) | 0x1002;
        break;

    case RST0:
        if (val & SYSTEM_RESET) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        if (val & PERIPHERAL_RESET) {
            /*
             * Peripheral reset resets all peripherals. The CPU
             * retains its state. The GPIO, watchdog timers, AoD,
             * RAM retention, and general control registers (GCR),
             * including the clock configuration, are unaffected.
             */
            val = UART2_RESET | UART1_RESET | UART0_RESET |
                    ADC_RESET | CNN_RESET | TRNG_RESET |
                    RTC_RESET | I2C0_RESET | SPI1_RESET |
                    TMR3_RESET | TMR2_RESET | TMR1_RESET |
                    TMR0_RESET | WDT0_RESET | DMA_RESET;
        }
        if (val & SOFT_RESET) {
            /* Soft reset also resets GPIO */
            val = UART2_RESET | UART1_RESET | UART0_RESET |
                    ADC_RESET | CNN_RESET | TRNG_RESET |
                    RTC_RESET | I2C0_RESET | SPI1_RESET |
                    TMR3_RESET | TMR2_RESET | TMR1_RESET |
                    TMR0_RESET | GPIO1_RESET | GPIO0_RESET |
                    DMA_RESET;
        }
        if (val & UART2_RESET) {
            device_cold_reset(s->uart2);
        }
        if (val & UART1_RESET) {
            device_cold_reset(s->uart1);
        }
        if (val & UART0_RESET) {
            device_cold_reset(s->uart0);
        }
        if (val & TRNG_RESET) {
            device_cold_reset(s->trng);
        }
        if (val & AES_RESET) {
            device_cold_reset(s->aes);
        }
        /* TODO: As other devices are implemented, add them here */
        break;

    case CLKCTRL:
        s->clkctrl = val | SYSCLK_RDY;
        break;

    case PM:
        s->pm = val;
        break;

    case PCLKDIV:
        s->pclkdiv = val;
        break;

    case PCLKDIS0:
        s->pclkdis0 = val;
        break;

    case MEMCTRL:
        s->memctrl = val;
        break;

    case MEMZ:
        if (val & ram0) {
            address_space_write(&s->sram_as, SYSRAM0_START,
                                MEMTXATTRS_UNSPECIFIED, zero, 0x8000);
        }
        if (val & ram1) {
            address_space_write(&s->sram_as, SYSRAM1_START,
                                MEMTXATTRS_UNSPECIFIED, zero, 0x8000);
        }
        if (val & ram2) {
            address_space_write(&s->sram_as, SYSRAM2_START,
                                MEMTXATTRS_UNSPECIFIED, zero, 0xC000);
        }
        if (val & ram3) {
            address_space_write(&s->sram_as, SYSRAM3_START,
                                MEMTXATTRS_UNSPECIFIED, zero, 0x4000);
        }
        break;

    case SYSST:
        s->sysst = val;
        break;

    case RST1:
        /* TODO: As other devices are implemented, add them here */
        s->rst1 = val;
        break;

    case PCKDIS1:
        s->pckdis1 = val;
        break;

    case EVENTEN:
        s->eventen = val;
        break;

    case REVISION:
        s->revision = val;
        break;

    case SYSIE:
        s->sysie = val;
        break;

    case ECCERR:
        s->eccerr = val;
        break;

    case ECCED:
        s->ecced = val;
        break;

    case ECCIE:
        s->eccie = val;
        break;

    case ECCADDR:
        s->eccaddr = val;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;

    }
}

static const Property max78000_gcr_properties[] = {
    DEFINE_PROP_LINK("sram", Max78000GcrState, sram,
                     TYPE_MEMORY_REGION, MemoryRegion*),
    DEFINE_PROP_LINK("uart0", Max78000GcrState, uart0,
                     TYPE_MAX78000_UART, DeviceState*),
    DEFINE_PROP_LINK("uart1", Max78000GcrState, uart1,
                     TYPE_MAX78000_UART, DeviceState*),
    DEFINE_PROP_LINK("uart2", Max78000GcrState, uart2,
                     TYPE_MAX78000_UART, DeviceState*),
    DEFINE_PROP_LINK("trng", Max78000GcrState, trng,
                        TYPE_MAX78000_TRNG, DeviceState*),
    DEFINE_PROP_LINK("aes", Max78000GcrState, aes,
                        TYPE_MAX78000_AES, DeviceState*),
};

static const MemoryRegionOps max78000_gcr_ops = {
    .read = max78000_gcr_read,
    .write = max78000_gcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_max78000_gcr = {
    .name = TYPE_MAX78000_GCR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(sysctrl, Max78000GcrState),
        VMSTATE_UINT32(rst0, Max78000GcrState),
        VMSTATE_UINT32(clkctrl, Max78000GcrState),
        VMSTATE_UINT32(pm, Max78000GcrState),
        VMSTATE_UINT32(pclkdiv, Max78000GcrState),
        VMSTATE_UINT32(pclkdis0, Max78000GcrState),
        VMSTATE_UINT32(memctrl, Max78000GcrState),
        VMSTATE_UINT32(memz, Max78000GcrState),
        VMSTATE_UINT32(sysst, Max78000GcrState),
        VMSTATE_UINT32(rst1, Max78000GcrState),
        VMSTATE_UINT32(pckdis1, Max78000GcrState),
        VMSTATE_UINT32(eventen, Max78000GcrState),
        VMSTATE_UINT32(revision, Max78000GcrState),
        VMSTATE_UINT32(sysie, Max78000GcrState),
        VMSTATE_UINT32(eccerr, Max78000GcrState),
        VMSTATE_UINT32(ecced, Max78000GcrState),
        VMSTATE_UINT32(eccie, Max78000GcrState),
        VMSTATE_UINT32(eccaddr, Max78000GcrState),
        VMSTATE_END_OF_LIST()
    }
};

static void max78000_gcr_init(Object *obj)
{
    Max78000GcrState *s = MAX78000_GCR(obj);

    memory_region_init_io(&s->mmio, obj, &max78000_gcr_ops, s,
                          TYPE_MAX78000_GCR, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

}

static void max78000_gcr_realize(DeviceState *dev, Error **errp)
{
    Max78000GcrState *s = MAX78000_GCR(dev);

    address_space_init(&s->sram_as, s->sram, "sram");
}

static void max78000_gcr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, max78000_gcr_properties);

    dc->realize = max78000_gcr_realize;
    dc->vmsd = &vmstate_max78000_gcr;
    rc->phases.hold = max78000_gcr_reset_hold;
}

static const TypeInfo max78000_gcr_info = {
    .name          = TYPE_MAX78000_GCR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max78000GcrState),
    .instance_init = max78000_gcr_init,
    .class_init     = max78000_gcr_class_init,
};

static void max78000_gcr_register_types(void)
{
    type_register_static(&max78000_gcr_info);
}

type_init(max78000_gcr_register_types)
