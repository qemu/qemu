/*
 * STM32L4X5 RCC (Reset and clock control)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 *
 * Inspired by the BCM2835 CPRMAN clock manager implementation by Luc Michel.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/misc/stm32l4x5_rcc.h"
#include "hw/misc/stm32l4x5_rcc_internals.h"
#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "trace.h"

#define HSE_DEFAULT_FRQ 48000000ULL
#define HSI_FRQ 16000000ULL
#define MSI_DEFAULT_FRQ 4000000ULL
#define LSE_FRQ 32768ULL
#define LSI_FRQ 32000ULL

static void rcc_update_irq(Stm32l4x5RccState *s)
{
    if (s->cifr & CIFR_IRQ_MASK) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void stm32l4x5_rcc_reset_hold(Object *obj)
{
    Stm32l4x5RccState *s = STM32L4X5_RCC(obj);
    s->cr = 0x00000063;
    /*
     * Factory-programmed calibration data
     * From the reference manual: 0x10XX 00XX
     * Value taken from a real card.
     */
    s->icscr = 0x106E0082;
    s->cfgr = 0x0;
    s->pllcfgr = 0x00001000;
    s->pllsai1cfgr = 0x00001000;
    s->pllsai2cfgr = 0x00001000;
    s->cier = 0x0;
    s->cifr = 0x0;
    s->ahb1rstr = 0x0;
    s->ahb2rstr = 0x0;
    s->ahb3rstr = 0x0;
    s->apb1rstr1 = 0x0;
    s->apb1rstr2 = 0x0;
    s->apb2rstr = 0x0;
    s->ahb1enr = 0x00000100;
    s->ahb2enr = 0x0;
    s->ahb3enr = 0x0;
    s->apb1enr1 = 0x0;
    s->apb1enr2 = 0x0;
    s->apb2enr = 0x0;
    s->ahb1smenr = 0x00011303;
    s->ahb2smenr = 0x000532FF;
    s->ahb3smenr =  0x00000101;
    s->apb1smenr1 = 0xF2FECA3F;
    s->apb1smenr2 = 0x00000025;
    s->apb2smenr = 0x01677C01;
    s->ccipr = 0x0;
    s->bdcr = 0x0;
    s->csr = 0x0C000600;
}

static uint64_t stm32l4x5_rcc_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32l4x5RccState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case A_CR:
        retvalue = s->cr;
        break;
    case A_ICSCR:
        retvalue = s->icscr;
        break;
    case A_CFGR:
        retvalue = s->cfgr;
        break;
    case A_PLLCFGR:
        retvalue = s->pllcfgr;
        break;
    case A_PLLSAI1CFGR:
        retvalue = s->pllsai1cfgr;
        break;
    case A_PLLSAI2CFGR:
        retvalue = s->pllsai2cfgr;
        break;
    case A_CIER:
        retvalue = s->cier;
        break;
    case A_CIFR:
        retvalue = s->cifr;
        break;
    case A_CICR:
        /* CICR is write only, return the reset value = 0 */
        break;
    case A_AHB1RSTR:
        retvalue = s->ahb1rstr;
        break;
    case A_AHB2RSTR:
        retvalue = s->ahb2rstr;
        break;
    case A_AHB3RSTR:
        retvalue = s->ahb3rstr;
        break;
    case A_APB1RSTR1:
        retvalue = s->apb1rstr1;
        break;
    case A_APB1RSTR2:
        retvalue = s->apb1rstr2;
        break;
    case A_APB2RSTR:
        retvalue = s->apb2rstr;
        break;
    case A_AHB1ENR:
        retvalue = s->ahb1enr;
        break;
    case A_AHB2ENR:
        retvalue = s->ahb2enr;
        break;
    case A_AHB3ENR:
        retvalue = s->ahb3enr;
        break;
    case A_APB1ENR1:
        retvalue = s->apb1enr1;
        break;
    case A_APB1ENR2:
        retvalue = s->apb1enr2;
        break;
    case A_APB2ENR:
        retvalue = s->apb2enr;
        break;
    case A_AHB1SMENR:
        retvalue = s->ahb1smenr;
        break;
    case A_AHB2SMENR:
        retvalue = s->ahb2smenr;
        break;
    case A_AHB3SMENR:
        retvalue = s->ahb3smenr;
        break;
    case A_APB1SMENR1:
        retvalue = s->apb1smenr1;
        break;
    case A_APB1SMENR2:
        retvalue = s->apb1smenr2;
        break;
    case A_APB2SMENR:
        retvalue = s->apb2smenr;
        break;
    case A_CCIPR:
        retvalue = s->ccipr;
        break;
    case A_BDCR:
        retvalue = s->bdcr;
        break;
    case A_CSR:
        retvalue = s->csr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    trace_stm32l4x5_rcc_read(addr, retvalue);

    return retvalue;
}

static void stm32l4x5_rcc_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32l4x5RccState *s = opaque;
    const uint32_t value = val64;

    trace_stm32l4x5_rcc_write(addr, value);

    switch (addr) {
    case A_CR:
        s->cr = (s->cr & CR_READ_SET_MASK) |
                (value & (CR_READ_SET_MASK | ~CR_READ_ONLY_MASK));
        break;
    case A_ICSCR:
        s->icscr = value & ~ICSCR_READ_ONLY_MASK;
        break;
    case A_CFGR:
        s->cfgr = value & ~CFGR_READ_ONLY_MASK;
        break;
    case A_PLLCFGR:
        s->pllcfgr = value;
        break;
    case A_PLLSAI1CFGR:
        s->pllsai1cfgr = value;
        break;
    case A_PLLSAI2CFGR:
        s->pllsai2cfgr = value;
        break;
    case A_CIER:
        s->cier = value;
        break;
    case A_CIFR:
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: Write attempt into read-only register (CIFR) 0x%"PRIx32"\n",
            __func__, value);
        break;
    case A_CICR:
        /* Clear interrupt flags by writing a 1 to the CICR register */
        s->cifr &= ~value;
        rcc_update_irq(s);
        break;
    /* Reset behaviors are not implemented */
    case A_AHB1RSTR:
        s->ahb1rstr = value;
        break;
    case A_AHB2RSTR:
        s->ahb2rstr = value;
        break;
    case A_AHB3RSTR:
        s->ahb3rstr = value;
        break;
    case A_APB1RSTR1:
        s->apb1rstr1 = value;
        break;
    case A_APB1RSTR2:
        s->apb1rstr2 = value;
        break;
    case A_APB2RSTR:
        s->apb2rstr = value;
        break;
    case A_AHB1ENR:
        s->ahb1enr = value;
        break;
    case A_AHB2ENR:
        s->ahb2enr = value;
        break;
    case A_AHB3ENR:
        s->ahb3enr = value;
        break;
    case A_APB1ENR1:
        s->apb1enr1 = value;
        break;
    case A_APB1ENR2:
        s->apb1enr2 = value;
        break;
    case A_APB2ENR:
        s->apb2enr = (s->apb2enr & APB2ENR_READ_SET_MASK) | value;
        break;
    /* Behaviors for Sleep and Stop modes are not implemented */
    case A_AHB1SMENR:
        s->ahb1smenr = value;
        break;
    case A_AHB2SMENR:
        s->ahb2smenr = value;
        break;
    case A_AHB3SMENR:
        s->ahb3smenr = value;
        break;
    case A_APB1SMENR1:
        s->apb1smenr1 = value;
        break;
    case A_APB1SMENR2:
        s->apb1smenr2 = value;
        break;
    case A_APB2SMENR:
        s->apb2smenr = value;
        break;
    case A_CCIPR:
        s->ccipr = value;
        break;
    case A_BDCR:
        s->bdcr = value & ~BDCR_READ_ONLY_MASK;
        break;
    case A_CSR:
        s->csr = value & ~CSR_READ_ONLY_MASK;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32l4x5_rcc_ops = {
    .read = stm32l4x5_rcc_read,
    .write = stm32l4x5_rcc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
    .impl = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
};

static const ClockPortInitArray stm32l4x5_rcc_clocks = {
    QDEV_CLOCK_IN(Stm32l4x5RccState, hsi16_rc, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, msi_rc, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, hse, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, lsi_rc, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, lse_crystal, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, sai1_extclk, NULL, 0),
    QDEV_CLOCK_IN(Stm32l4x5RccState, sai2_extclk, NULL, 0),
    QDEV_CLOCK_END
};


static void stm32l4x5_rcc_init(Object *obj)
{
    Stm32l4x5RccState *s = STM32L4X5_RCC(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_rcc_ops, s,
                          TYPE_STM32L4X5_RCC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_clocks(DEVICE(s), stm32l4x5_rcc_clocks);

    s->gnd = clock_new(obj, "gnd");
}

static const VMStateDescription vmstate_stm32l4x5_rcc = {
    .name = TYPE_STM32L4X5_RCC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr, Stm32l4x5RccState),
        VMSTATE_UINT32(icscr, Stm32l4x5RccState),
        VMSTATE_UINT32(cfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(pllcfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(pllsai1cfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(pllsai2cfgr, Stm32l4x5RccState),
        VMSTATE_UINT32(cier, Stm32l4x5RccState),
        VMSTATE_UINT32(cifr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb1rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb2rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb3rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1rstr1, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1rstr2, Stm32l4x5RccState),
        VMSTATE_UINT32(apb2rstr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb1enr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb2enr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb3enr, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1enr1, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1enr2, Stm32l4x5RccState),
        VMSTATE_UINT32(apb2enr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb1smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb2smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(ahb3smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1smenr1, Stm32l4x5RccState),
        VMSTATE_UINT32(apb1smenr2, Stm32l4x5RccState),
        VMSTATE_UINT32(apb2smenr, Stm32l4x5RccState),
        VMSTATE_UINT32(ccipr, Stm32l4x5RccState),
        VMSTATE_UINT32(bdcr, Stm32l4x5RccState),
        VMSTATE_UINT32(csr, Stm32l4x5RccState),
        VMSTATE_CLOCK(hsi16_rc, Stm32l4x5RccState),
        VMSTATE_CLOCK(msi_rc, Stm32l4x5RccState),
        VMSTATE_CLOCK(hse, Stm32l4x5RccState),
        VMSTATE_CLOCK(lsi_rc, Stm32l4x5RccState),
        VMSTATE_CLOCK(lse_crystal, Stm32l4x5RccState),
        VMSTATE_CLOCK(sai1_extclk, Stm32l4x5RccState),
        VMSTATE_CLOCK(sai2_extclk, Stm32l4x5RccState),
        VMSTATE_END_OF_LIST()
    }
};


static void stm32l4x5_rcc_realize(DeviceState *dev, Error **errp)
{
    Stm32l4x5RccState *s = STM32L4X5_RCC(dev);

    if (s->hse_frequency <  4000000ULL ||
        s->hse_frequency > 48000000ULL) {
            error_setg(errp,
                "HSE frequency is outside of the allowed [4-48]Mhz range: %" PRIx64 "",
                s->hse_frequency);
            return;
        }

    clock_update_hz(s->msi_rc, MSI_DEFAULT_FRQ);
    clock_update_hz(s->sai1_extclk, s->sai1_extclk_frequency);
    clock_update_hz(s->sai2_extclk, s->sai2_extclk_frequency);
    clock_update(s->gnd, 0);
}

static Property stm32l4x5_rcc_properties[] = {
    DEFINE_PROP_UINT64("hse_frequency", Stm32l4x5RccState,
        hse_frequency, HSE_DEFAULT_FRQ),
    DEFINE_PROP_UINT64("sai1_extclk_frequency", Stm32l4x5RccState,
        sai1_extclk_frequency, 0),
    DEFINE_PROP_UINT64("sai2_extclk_frequency", Stm32l4x5RccState,
        sai2_extclk_frequency, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32l4x5_rcc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);


    rc->phases.hold = stm32l4x5_rcc_reset_hold;
    device_class_set_props(dc, stm32l4x5_rcc_properties);
    dc->realize = stm32l4x5_rcc_realize;
    dc->vmsd = &vmstate_stm32l4x5_rcc;
}

static const TypeInfo stm32l4x5_rcc_types[] = {
    {
        .name           = TYPE_STM32L4X5_RCC,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32l4x5RccState),
        .instance_init  = stm32l4x5_rcc_init,
        .class_init     = stm32l4x5_rcc_class_init,
    }
};

DEFINE_TYPES(stm32l4x5_rcc_types)
