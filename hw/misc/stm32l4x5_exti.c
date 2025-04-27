/*
 * STM32L4x5 EXTI (Extended interrupts and events controller)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Samuel Tardieu <samuel.tardieu@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This work is based on the stm32f4xx_exti by Alistair Francis.
 * Original code is licensed under the MIT License:
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 */

/*
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/stm32l4x5_exti.h"

#define EXTI_IMR1   0x00
#define EXTI_EMR1   0x04
#define EXTI_RTSR1  0x08
#define EXTI_FTSR1  0x0C
#define EXTI_SWIER1 0x10
#define EXTI_PR1    0x14
#define EXTI_IMR2   0x20
#define EXTI_EMR2   0x24
#define EXTI_RTSR2  0x28
#define EXTI_FTSR2  0x2C
#define EXTI_SWIER2 0x30
#define EXTI_PR2    0x34

#define EXTI_MAX_IRQ_PER_BANK 32
#define EXTI_IRQS_BANK0  32
#define EXTI_IRQS_BANK1  8

static const unsigned irqs_per_bank[EXTI_NUM_REGISTER] = {
    EXTI_IRQS_BANK0,
    EXTI_IRQS_BANK1,
};

static const uint32_t exti_romask[EXTI_NUM_REGISTER] = {
    0xff820000, /* 0b11111111_10000010_00000000_00000000 */
    0x00000087, /* 0b00000000_00000000_00000000_10000111 */
};

static unsigned regbank_index_by_irq(unsigned irq)
{
    return irq >= EXTI_MAX_IRQ_PER_BANK ? 1 : 0;
}

static unsigned regbank_index_by_addr(hwaddr addr)
{
    return addr >= EXTI_IMR2 ? 1 : 0;
}

static unsigned valid_mask(unsigned bank)
{
    return MAKE_64BIT_MASK(0, irqs_per_bank[bank]);
}

static unsigned configurable_mask(unsigned bank)
{
    return valid_mask(bank) & ~exti_romask[bank];
}

static void stm32l4x5_exti_reset_hold(Object *obj, ResetType type)
{
    Stm32l4x5ExtiState *s = STM32L4X5_EXTI(obj);

    for (unsigned bank = 0; bank < EXTI_NUM_REGISTER; bank++) {
        s->imr[bank] = exti_romask[bank];
        s->emr[bank] = 0x00000000;
        s->rtsr[bank] = 0x00000000;
        s->ftsr[bank] = 0x00000000;
        s->swier[bank] = 0x00000000;
        s->pr[bank] = 0x00000000;
        s->irq_levels[bank] = 0x00000000;
    }
}

static void stm32l4x5_exti_set_irq(void *opaque, int irq, int level)
{
    Stm32l4x5ExtiState *s = opaque;
    const unsigned bank = regbank_index_by_irq(irq);
    const int oirq = irq;

    trace_stm32l4x5_exti_set_irq(irq, level);

    /* Shift the value to enable access in x2 registers. */
    irq %= EXTI_MAX_IRQ_PER_BANK;

    if (level == extract32(s->irq_levels[bank], irq, 1)) {
        /* No change in IRQ line state: do nothing */
        return;
    }
    s->irq_levels[bank] = deposit32(s->irq_levels[bank], irq, 1, level);

    /* If the interrupt is masked, pr won't be raised */
    if (!extract32(s->imr[bank], irq, 1)) {
        return;
    }

    /* In case of a direct line interrupt */
    if (extract32(exti_romask[bank], irq, 1)) {
        qemu_set_irq(s->irq[oirq], level);
        return;
    }

    /* In case of a configurable interrupt */
    if ((level && extract32(s->rtsr[bank], irq, 1)) ||
        (!level && extract32(s->ftsr[bank], irq, 1))) {

        s->pr[bank] |= 1 << irq;
        qemu_irq_pulse(s->irq[oirq]);
    }
}

static uint64_t stm32l4x5_exti_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Stm32l4x5ExtiState *s = opaque;
    uint32_t r = 0;
    const unsigned bank = regbank_index_by_addr(addr);

    switch (addr) {
    case EXTI_IMR1:
    case EXTI_IMR2:
        r = s->imr[bank];
        break;
    case EXTI_EMR1:
    case EXTI_EMR2:
        r = s->emr[bank];
        break;
    case EXTI_RTSR1:
    case EXTI_RTSR2:
        r = s->rtsr[bank];
        break;
    case EXTI_FTSR1:
    case EXTI_FTSR2:
        r = s->ftsr[bank];
        break;
    case EXTI_SWIER1:
    case EXTI_SWIER2:
        r = s->swier[bank];
        break;
    case EXTI_PR1:
    case EXTI_PR2:
        r = s->pr[bank];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "STM32L4X5_exti_read: Bad offset 0x%" HWADDR_PRIx "\n",
                      addr);
        break;
    }

    trace_stm32l4x5_exti_read(addr, r);

    return r;
}

static void stm32l4x5_exti_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Stm32l4x5ExtiState *s = opaque;
    const unsigned bank = regbank_index_by_addr(addr);

    trace_stm32l4x5_exti_write(addr, val64);

    switch (addr) {
    case EXTI_IMR1:
    case EXTI_IMR2:
        s->imr[bank] = val64 & valid_mask(bank);
        return;
    case EXTI_EMR1:
    case EXTI_EMR2:
        s->emr[bank] = val64 & valid_mask(bank);
        return;
    case EXTI_RTSR1:
    case EXTI_RTSR2:
        s->rtsr[bank] = val64 & configurable_mask(bank);
        return;
    case EXTI_FTSR1:
    case EXTI_FTSR2:
        s->ftsr[bank] = val64 & configurable_mask(bank);
        return;
    case EXTI_SWIER1:
    case EXTI_SWIER2: {
        const uint32_t set = val64 & configurable_mask(bank);
        const uint32_t pend = set & ~s->swier[bank] & s->imr[bank] &
                              ~s->pr[bank];
        s->swier[bank] = set;
        s->pr[bank] |= pend;
        for (unsigned i = 0; i < irqs_per_bank[bank]; i++) {
            if (extract32(pend, i, 1)) {
                qemu_irq_pulse(s->irq[i + 32 * bank]);
            }
        }
        return;
    }
    case EXTI_PR1:
    case EXTI_PR2: {
        const uint32_t cleared = s->pr[bank] & val64 & configurable_mask(bank);
        /* This bit is cleared by writing a 1 to it */
        s->pr[bank] &= ~cleared;
        /* Software triggered interrupts are cleared as well */
        s->swier[bank] &= ~cleared;
        return;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "STM32L4X5_exti_write: Bad offset 0x%" HWADDR_PRIx "\n",
                      addr);
    }
}

static const MemoryRegionOps stm32l4x5_exti_ops = {
    .read = stm32l4x5_exti_read,
    .write = stm32l4x5_exti_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .impl.unaligned = false,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void stm32l4x5_exti_init(Object *obj)
{
    Stm32l4x5ExtiState *s = STM32L4X5_EXTI(obj);

    for (size_t i = 0; i < EXTI_NUM_LINES; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[i]);
    }

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_exti_ops, s,
                          TYPE_STM32L4X5_EXTI, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_in(DEVICE(obj), stm32l4x5_exti_set_irq, EXTI_NUM_LINES);
}

static const VMStateDescription vmstate_stm32l4x5_exti = {
    .name = TYPE_STM32L4X5_EXTI,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(imr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(emr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(rtsr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(ftsr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(swier, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(pr, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_UINT32_ARRAY(irq_levels, Stm32l4x5ExtiState, EXTI_NUM_REGISTER),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32l4x5_exti_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_stm32l4x5_exti;
    rc->phases.hold = stm32l4x5_exti_reset_hold;
}

static const TypeInfo stm32l4x5_exti_types[] = {
    {
        .name          = TYPE_STM32L4X5_EXTI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Stm32l4x5ExtiState),
        .instance_init = stm32l4x5_exti_init,
        .class_init    = stm32l4x5_exti_class_init,
    }
};

DEFINE_TYPES(stm32l4x5_exti_types)
