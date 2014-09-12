/*
 * STM32 Microcontroller EXTI (External Interrupt/Event Controller) module
 *
 * Copyright (C) 2010 Andre Beckus
 *
 * Implementation based on ST Microelectronics "RM0008 Reference Manual Rev 10"
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/arm/stm32.h"
#include "qemu/bitops.h"




/* DEFINITIONS*/

#define EXTI_IMR_OFFSET 0x00

#define EXTI_EMR_OFFSET 0x04

#define EXTI_RTSR_OFFSET 0x08

#define EXTI_FTSR_OFFSET 0x0c

#define EXTI_SWIER_OFFSET 0x10

#define EXTI_PR_OFFSET 0x14

/* There are 20 lines for CL devices.  Non-CL devices have only 19, but it
 * doesn't hurt to handle the maximum possible. */
#define EXTI_LINE_COUNT 20

/* The number of IRQ connections to the NVIC */
#define EXTI_IRQ_COUNT 10


struct Stm32Exti {
    /* Inherited */
    SysBusDevice busdev;

    /* Private */
    MemoryRegion iomem;

    uint32_t
        EXTI_IMR,
        EXTI_RTSR,
        EXTI_FTSR,
        EXTI_SWIER,
        EXTI_PR;

    qemu_irq irq[EXTI_IRQ_COUNT];
};

static void stm32_exti_change_EXTI_PR_bit(Stm32Exti *s, unsigned pos,
                                            unsigned new_bit_value);



/* HELPER FUNCTIONS */

/* Call when the EXTI shouldbe tri */
static void stm32_exti_trigger(Stm32Exti *s, int line)
{
    /* Make sure the interrupt for this EXTI line has been enabled. */
    if(s->EXTI_IMR & BIT(line)) {
        /* Set the Pending flag for this line, which will trigger the interrupt
         * (if the flag isn't already set). */
        stm32_exti_change_EXTI_PR_bit(s, line, 1);
    }
}

/* We will assume that this handler will only be called if the pin actually
 * changed state. */
static void stm32_exti_gpio_in_handler(void *opaque, int n, int level)
{
    Stm32Exti *s = (Stm32Exti *)opaque;
    unsigned pin = n;

    assert(pin < STM32_GPIO_PIN_COUNT);

    /* Check the level - if it is rising, then trigger an interrupt if the
     * corresponding Rising Trigger Selection Register flag is set.  Otherwise,
     * trigger if the Falling Trigger Selection Register flag is set.
     */
    if((level  && (s->EXTI_RTSR & BIT(pin))) ||
       (!level && (s->EXTI_FTSR & BIT(pin)))) {
        stm32_exti_trigger(s, pin);
    }
}




/* REGISTER IMPLEMENTATION */

/* Update a Trigger Selection Register (both the Rising and Falling TSR
 * registers are handled by this routine).
 */
static void update_TSR_bit(Stm32Exti *s, uint32_t *tsr_register, unsigned pos,
                                    unsigned new_bit_value)
{
    assert((new_bit_value == 0) || (new_bit_value == 1));
    assert(pos < EXTI_LINE_COUNT);

    if(new_bit_value != extract32(*tsr_register, pos, 1)) {
        /* According to the documentation, the Pending register is cleared when
         * the "sensitivity of the edge detector changes.  Is this right??? */
        stm32_exti_change_EXTI_PR_bit(s, pos, 0);
    }
    *tsr_register = deposit32(*tsr_register, pos, 1, new_bit_value);
}

/* Update the Pending Register.  This will trigger an interrupt if a bit is
 * set.
 */
static void stm32_exti_change_EXTI_PR_bit(Stm32Exti *s, unsigned pos,
                                            unsigned new_bit_value)
{
    unsigned old_bit_value;

    assert((new_bit_value == 0) || (new_bit_value == 1));
    assert(pos < EXTI_LINE_COUNT);

    old_bit_value = extract32(s->EXTI_PR, pos, 1);

    /* Only continue if the PR bit is actually changing value. */
    if(new_bit_value != old_bit_value) {
        /* If the bit is being reset, the corresponding Software Interrupt Event
         * Register bit is automatically reset.
         */
        if(!new_bit_value) {
            s->EXTI_SWIER &= ~BIT(pos);
        }

        /* Update the IRQ for this EXTI line.  Some lines share the same
         * NVIC IRQ.
         */
        if(pos <= 4) {
            /* EXTI0 - EXTI4 each have their own NVIC IRQ */
            qemu_set_irq(s->irq[pos], new_bit_value);
        } else if(pos <= 9) {
            /* EXTI5 - EXTI9 share an NVIC IRQ */
            qemu_set_irq(s->irq[5], new_bit_value);
        } else if(pos <= 15) {
            /* EXTI10 - EXTI15 share an NVIC IRQ */
            qemu_set_irq(s->irq[6], new_bit_value);
        } else if(pos == 16) {
            /* PVD IRQ */
            qemu_set_irq(s->irq[7], new_bit_value);
        } else if(pos == 17) {
            /* RTCAlarm IRQ */
            qemu_set_irq(s->irq[8], new_bit_value);
        } else if(pos == 18) {
            /* OTG_FS_WKUP IRQ */
            qemu_set_irq(s->irq[9], new_bit_value);
        } else {
            assert(false);
        }

        /* Update the register. */
        s->EXTI_PR = deposit32(s->EXTI_PR, pos, 1, new_bit_value);
    }
}

static uint64_t stm32_exti_read(void *opaque, hwaddr offset,
                          unsigned size)
{
    Stm32Exti *s = (Stm32Exti *)opaque;

    assert(size == 4);

    switch (offset) {
        case EXTI_IMR_OFFSET:
            return s->EXTI_IMR;
        case EXTI_EMR_OFFSET:
            /* Do nothing, events are not implemented yet. */
            return 0;
        case EXTI_RTSR_OFFSET:
            return s->EXTI_RTSR;
        case EXTI_FTSR_OFFSET:
            return s->EXTI_FTSR;
        case EXTI_SWIER_OFFSET:
            return s->EXTI_SWIER;
        case EXTI_PR_OFFSET:
            return s->EXTI_PR;
        default:
            STM32_BAD_REG(offset, size);
            return 0;
    }
}

static void stm32_exti_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned size)
{
    Stm32Exti *s = (Stm32Exti *)opaque;
    int pos, bit_value;

    assert(size == 4);

    if(offset <= EXTI_EMR_OFFSET) {
        switch (offset) {
            case EXTI_IMR_OFFSET:
                s->EXTI_IMR = value;
                break;
            case EXTI_EMR_OFFSET:
                /* Do nothing, events are not implemented yet.
                 * But we don't want to throw an error. */
                break;
            default:
                STM32_BAD_REG(offset, size);
                break;
        }
    } else {
        /* These registers all contain one bit per EXTI line.  We will loop
         * through each line and then update each bit in the appropriate
         * register.
         */
        for(pos = 0; pos < EXTI_LINE_COUNT; pos++) {
            bit_value = extract32(value, pos, 1);

            switch (offset) {
                case EXTI_RTSR_OFFSET:
                    update_TSR_bit(s, &(s->EXTI_RTSR), pos, bit_value);
                    break;
                case EXTI_FTSR_OFFSET:
                    update_TSR_bit(s, &(s->EXTI_FTSR), pos, bit_value);
                    break;
                case EXTI_SWIER_OFFSET:
                    /* If the Software Interrupt Event Register is changed
                     * from 0 to 1, trigger an interrupt.  Changing the
                     * bit to 0 does nothing. */
                    if(bit_value == 1) {
                        if((s->EXTI_SWIER & BIT(pos)) == 0) {
                            s->EXTI_SWIER |= BIT(pos);
                            stm32_exti_trigger(s, pos);
                        }
                    }
                    break;
                case EXTI_PR_OFFSET:
                    /* When a 1 is written to a PR bit, it actually clears the
                     * PR bit. */
                    if(bit_value == 1) {
                        stm32_exti_change_EXTI_PR_bit(s, pos, 0);
                    }
                    break;
                default:
                    STM32_BAD_REG(offset, size);
                    break;
            }
        }
    }
}

static const MemoryRegionOps stm32_exti_ops = {
    .read = stm32_exti_read,
    .write = stm32_exti_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void stm32_exti_reset(DeviceState *dev)
{
    Stm32Exti *s = STM32_EXTI(dev);

    s->EXTI_IMR = 0x00000000;
    s->EXTI_RTSR = 0x00000000;
    s->EXTI_FTSR = 0x00000000;
    s->EXTI_SWIER = 0x00000000;
    s->EXTI_PR = 0x00000000;
}


/* DEVICE INITIALIZATION */

static int stm32_exti_init(SysBusDevice *dev)
{
    int i;

    Stm32Exti *s = STM32_EXTI(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &stm32_exti_ops, s,
            "exti", 0x03ff);
    sysbus_init_mmio(dev, &s->iomem);

    for(i = 0; i < EXTI_IRQ_COUNT; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }

    /* Create the handlers to handle GPIO input pin changes. */
    qdev_init_gpio_in(DEVICE(dev), stm32_exti_gpio_in_handler, STM32_GPIO_PIN_COUNT);

    return 0;
}

static void stm32_exti_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_exti_init;
    dc->reset = stm32_exti_reset;
}

static TypeInfo stm32_exti_info = {
    .name  = TYPE_STM32_EXTI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(Stm32Exti),
    .class_init = stm32_exti_class_init
};

static void stm32_exti_register_types(void)
{
    type_register_static(&stm32_exti_info);
}

type_init(stm32_exti_register_types)
