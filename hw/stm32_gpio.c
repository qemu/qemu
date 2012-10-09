/*
 * STM32 Microcontroller GPIO (General Purpose I/O) module
 *
 * Copyright (C) 2010 Andre Beckus
 *
 * Source code based on pl061.c
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
/* use PL061 for inspiration. */

#include "sysbus.h"
#include "stm32.h"




/* DEFINITIONS*/

#define GPIOx_CRL_OFFSET 0x00
#define GPIOx_CRH_OFFSET 0x04
#define GPIOx_IDR_OFFSET 0x08
#define GPIOx_ODR_OFFSET 0x0c
#define GPIOx_BSRR_OFFSET 0x10
#define GPIOx_BRR_OFFSET 0x14
#define GPIOx_LCKR_OFFSET 0x18

#define GPIOx_CRL_INDEX 0
#define GPIOx_CRH_INDEX 1

struct Stm32Gpio {
    /* Inherited */
    SysBusDevice busdev;

    /* Properties */
    stm32_periph_t periph;
    void *stm32_rcc_prop;

    /* Private */
    MemoryRegion iomem;

    Stm32Rcc *stm32_rcc;

    /* CRL = 0
     * CRH = 1
     */
    uint32_t GPIOx_CRy[2];

    /* 0 = input
     * 1 = output
     */
    uint16_t dir_mask;

    uint32_t GPIOx_ODR;

    /* IRQs used to communicate with the machine implementation.
     * There is one IRQ for each pin.  Note that for pins configured
     * as inputs, the output IRQ state has no meaning.  Perhaps
     * the output should be updated to match the input in this case....
     */
    qemu_irq out_irq[STM32_GPIO_PIN_COUNT];

    uint16_t in;

    /* EXTI IRQ to notify on input change - there is one EXTI IRQ per pin. */
    qemu_irq exti_irq[STM32_GPIO_PIN_COUNT];
};



/* CALLBACKs */

/* Trigger fired when a GPIO input pin changes state (based
 * on an external stimulus from the machine).
 */
static void stm32_gpio_in_trigger(void *opaque, int irq, int level)
{
    Stm32Gpio *s = opaque;
    unsigned pin = irq;

    assert(pin < STM32_GPIO_PIN_COUNT);

    /* Only proceed if the pin has actually changed value (the trigger
     * will fire when the IRQ is set, even if it set to the same level). */
    if(GET_BIT_VALUE(s->in, pin) != level) {
        /* Update internal pin state. */
        CHANGE_BIT(s->in, pin, level);

        /* Propagate the trigger to the EXTI module. */
        qemu_set_irq(s->exti_irq[pin], level);
    }
}



/* HELPER FUNCTIONS */

/* Gets the four configuration bits for the pin from the CRL or CRH
 * register.
 */
static uint8_t stm32_gpio_get_pin_config(Stm32Gpio *s, unsigned pin) {
    unsigned reg_index, reg_pin;
    unsigned reg_start_bit;

    assert(pin < STM32_GPIO_PIN_COUNT);

    /* Determine the register (CRL or CRH). */
    reg_index = pin / 8;
    assert((reg_index == GPIOx_CRL_INDEX) || (reg_index == GPIOx_CRH_INDEX));

    /* Get the pin within the register. */
    reg_pin = pin % 8;

    /* Get the start position of the config bits in the register (each
     * pin config takes 4 bits). */
    reg_start_bit = reg_pin * 4;

    return (s->GPIOx_CRy[reg_index] >> reg_start_bit) & 0xf;
}





/* REGISTER IMPLEMENTATION */

/* Update the CRL or CRH Configuration Register */
static void stm32_gpio_GPIOx_CRy_write(Stm32Gpio *s, int cr_index,
                                        uint32_t new_value, bool init)
{
    unsigned pin, pin_dir;

    assert((cr_index == GPIOx_CRL_INDEX) || (cr_index == GPIOx_CRH_INDEX));

    s->GPIOx_CRy[cr_index] = new_value;

    /* Update the direction mask */
    for(pin=0; pin < STM32_GPIO_PIN_COUNT; pin++) {
        pin_dir = stm32_gpio_get_mode_bits(s, pin);
        /* If the mode is 0, the pin is input.  Otherwise, it
         * is output.
         */
        CHANGE_BIT(s->dir_mask, pin, pin_dir);
    }
}

/* Write the Output Data Register.
 * Propagates the changes to the output IRQs.
 * Perhaps we should also update the input to match the output for
 * pins configured as outputs... */
static void stm32_gpio_GPIOx_ODR_write(Stm32Gpio *s, uint32_t new_value,
                                            bool init)
{
    uint32_t old_value;
    uint16_t changed, changed_out;
    unsigned pin;

    old_value = s->GPIOx_ODR;

    /* Update register value.  Per documentation, the upper 16 bits
     * always read as 0. */
    s->GPIOx_ODR = new_value & 0x0000ffff;

    /* Get pins that changed value */
    changed = old_value ^ new_value;

    /* Get changed pins that are outputs - we will not touch input pins */
    changed_out = changed & s->dir_mask;

    if (changed_out) {
        for (pin = 0; pin < STM32_GPIO_PIN_COUNT; pin++) {
            /* If the value of this pin has changed, then update
             * the output IRQ.
             */
            if (IS_BIT_SET(changed_out, pin)) {
                qemu_set_irq(
                        s->out_irq[pin],
                        IS_BIT_SET(s->GPIOx_ODR, pin) ? 1 : 0);
            }
        }
    }
}

/* Write the Bit Set/Reset Register.
 * Setting a bit sets or resets the corresponding bit in the output
 * register.  The lower 16 bits perform resets, and the upper 16 bits
 * perform sets.  Register is write-only and so does not need to store
 * a value.
 */
static void stm32_gpio_GPIOx_BSRR_write(Stm32Gpio *s, uint32_t new_value)
{
    uint32_t new_ODR;

    new_ODR = s->GPIOx_ODR;

    /* Perform sets with upper halfword. */
    new_ODR |= new_value & 0x0000ffff;

    /* Perform resets. */
    new_ODR &= ~(new_value >> 16) & 0x0000ffff;

    stm32_gpio_GPIOx_ODR_write(s, new_value, false);
}

/* Update the Bit Reset Register.
 * Setting a bit resets the corresponding bit in the output
 * register.  Register is write-only and so does not need to store
 * a value.
 */
static void stm32_gpio_GPIOx_BRR_write(Stm32Gpio *s, uint32_t new_value)
{
    stm32_gpio_GPIOx_ODR_write(
            s,
            s->GPIOx_ODR & (~new_value & 0x0000ffff),
            false);
}


static uint64_t stm32_gpio_readw(Stm32Gpio *s, target_phys_addr_t offset)
{
    switch (offset) {
        case GPIOx_CRL_OFFSET: /* GPIOx_CRL */
            return s->GPIOx_CRy[GPIOx_CRL_INDEX];
        case GPIOx_CRH_OFFSET: /* GPIOx_CRH */
            return s->GPIOx_CRy[GPIOx_CRH_INDEX];
        case GPIOx_IDR_OFFSET:
            return s->in;
        case GPIOx_ODR_OFFSET:
            return s->GPIOx_ODR;
        /* Note that documentation says BSRR and BRR are write-only, but reads
         * work on real hardware.  We follow the documentation.*/
        case GPIOx_BSRR_OFFSET: /* GPIOC_BSRR */
            STM32_WO_REG(offset);
            return 0;
        case GPIOx_BRR_OFFSET: /* GPIOC_BRR */
            STM32_WO_REG(offset);
            return 0;
        case GPIOx_LCKR_OFFSET: /* GPIOx_LCKR */
            /* Locking is not yet implemented */
            return 0;
        default:
            STM32_BAD_REG(offset, WORD_ACCESS_SIZE);
            return 0;
    }
}

static void stm32_gpio_writew(Stm32Gpio *s, target_phys_addr_t offset,
                          uint64_t value)
{


    switch (offset) {
        case GPIOx_CRL_OFFSET: /* GPIOx_CRL */
            stm32_gpio_GPIOx_CRy_write(s, GPIOx_CRL_INDEX, value, false);
            break;
        case GPIOx_CRH_OFFSET: /* GPIOx_CRH */
            stm32_gpio_GPIOx_CRy_write(s, GPIOx_CRH_INDEX, value, false);
            break;
        case GPIOx_IDR_OFFSET:
            STM32_RO_REG(offset);
            break;
        case GPIOx_ODR_OFFSET: /* GPIOx_ODR */
            stm32_gpio_GPIOx_ODR_write(s, value, false);
            break;
        case GPIOx_BSRR_OFFSET: /* GPIOx_BSRR */
            stm32_gpio_GPIOx_BSRR_write(s, value);
            break;
        case GPIOx_BRR_OFFSET: /* GPIOx_BRR */
            stm32_gpio_GPIOx_BRR_write(s, value);
            break;
        case GPIOx_LCKR_OFFSET: /* GPIOx_LCKR */
            /* Locking is not implemented */
            STM32_NOT_IMPL_REG(offset, 4);
            break;
        default:
            STM32_BAD_REG(offset, 4);
            break;
    }
}



static uint64_t stm32_gpio_read(void *opaque, target_phys_addr_t offset,
                          unsigned size)
{
    Stm32Gpio *s = (Stm32Gpio *)opaque;

    switch(size) {
        case WORD_ACCESS_SIZE:
            return stm32_gpio_readw(s, offset);
        default:
            STM32_BAD_REG(offset, size);
            return 0;
    }
}

static void stm32_gpio_write(void *opaque, target_phys_addr_t offset,
                       uint64_t value, unsigned size)
{
    Stm32Gpio *s = (Stm32Gpio *)opaque;

    stm32_rcc_check_periph_clk((Stm32Rcc *)s->stm32_rcc, s->periph);

    switch(size) {
        case WORD_ACCESS_SIZE:
            stm32_gpio_writew(s, offset, value);
            break;
        default:
            STM32_BAD_REG(offset, size);
            break;
    }
}

static const MemoryRegionOps stm32_gpio_ops = {
    .read = stm32_gpio_read,
    .write = stm32_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void stm32_gpio_reset(DeviceState *dev)
{
    Stm32Gpio *s = FROM_SYSBUS(Stm32Gpio, sysbus_from_qdev(dev));

    stm32_gpio_GPIOx_CRy_write(s, GPIOx_CRL_INDEX, 0x44444444, true);
    stm32_gpio_GPIOx_CRy_write(s, GPIOx_CRH_INDEX, 0x44444444, true);
    stm32_gpio_GPIOx_ODR_write(s, 0x00000000, true);
}






/* PUBLIC FUNCTIONS */

uint8_t stm32_gpio_get_config_bits(Stm32Gpio *s, unsigned pin) {
    return (stm32_gpio_get_pin_config(s, pin) >> 2) & 0x3;
}

uint8_t stm32_gpio_get_mode_bits(Stm32Gpio *s, unsigned pin) {
    return stm32_gpio_get_pin_config(s, pin) & 0x3;
}

void stm32_gpio_set_exti_irq(Stm32Gpio *s, unsigned pin, qemu_irq exti_irq)
{
    assert(pin < STM32_GPIO_PIN_COUNT);

    s->exti_irq[pin] = exti_irq;
}






/* DEVICE INITIALIZATION */

static int stm32_gpio_init(SysBusDevice *dev)
{
    unsigned pin;
    Stm32Gpio *s = FROM_SYSBUS(Stm32Gpio, dev);

    s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;

    memory_region_init_io(&s->iomem, &stm32_gpio_ops, s,
                          "gpio", 0x03ff);
    sysbus_init_mmio(dev, &s->iomem);

    qdev_init_gpio_in(&dev->qdev, stm32_gpio_in_trigger, STM32_GPIO_PIN_COUNT);
    qdev_init_gpio_out(&dev->qdev, s->out_irq, STM32_GPIO_PIN_COUNT);

    for(pin = 0; pin < STM32_GPIO_PIN_COUNT; pin++) {
        stm32_gpio_set_exti_irq(s, pin, NULL);
    }

    return 0;
}

static Property stm32_gpio_properties[] = {
    DEFINE_PROP_PERIPH_T("periph", Stm32Gpio, periph, STM32_PERIPH_UNDEFINED),
    DEFINE_PROP_PTR("stm32_rcc", Stm32Gpio, stm32_rcc_prop),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_gpio_init;
    dc->reset = stm32_gpio_reset;
    dc->props = stm32_gpio_properties;
}

static TypeInfo stm32_gpio_info = {
    .name  = "stm32_gpio",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(Stm32Gpio),
    .class_init = stm32_gpio_class_init
};

static void stm32_gpio_register_types(void)
{
    type_register_static(&stm32_gpio_info);
}

type_init(stm32_gpio_register_types)
