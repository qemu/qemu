/*
 * STM32 Microcontroller AFIO (Alternate Function I/O) module
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

#include "stm32.h"



/* DEFINITIONS */

#define AFIO_EVCR_OFFSET 0x00

#define AFIO_MAPR_OFFSET 0x04
#define AFIO_MAPR_USART3_REMAP_START 4
#define AFIO_MAPR_USART3_REMAP_MASK 0x00000030
#define AFIO_MAPR_USART2_REMAP_BIT 3
#define AFIO_MAPR_USART1_REMAP_BIT 2

#define AFIO_EXTICR_START 0x08
#define AFIO_EXTICR_COUNT 4

#define AFIO_EXTICR1_OFFSET 0x08
#define AFIO_EXTICR2_OFFSET 0x0c
#define AFIO_EXTICR3_OFFSET 0x10
#define AFIO_EXTICR4_OFFSET 0x14

#define AFIO_EXTI_PER_CR 4

struct Stm32Afio {
    /* Inherited */
    SysBusDevice busdev;

    /* Properties */
    void *stm32_rcc_prop;
    void *stm32_exti_prop;

    /* Private */
    MemoryRegion iomem;

    Stm32Rcc *stm32_rcc;
    Stm32Exti *stm32_exti;

    uint32_t
        USART1_REMAP,
        USART2_REMAP,
        USART3_REMAP,
        AFIO_MAPR,
        AFIO_EXTICR[AFIO_EXTICR_COUNT];
};






/* REGISTER IMPLEMENTATION */

static uint32_t stm32_afio_AFIO_MAPR_read(Stm32Afio *s)
{
    return (s->USART1_REMAP << AFIO_MAPR_USART1_REMAP_BIT) |
           (s->USART2_REMAP << AFIO_MAPR_USART2_REMAP_BIT) |
           (s->USART3_REMAP << AFIO_MAPR_USART3_REMAP_START);
}

static void stm32_afio_AFIO_MAPR_write(Stm32Afio *s, uint32_t new_value,
                                        bool init)
{
    s->USART1_REMAP = GET_BIT_VALUE(s->AFIO_MAPR, AFIO_MAPR_USART1_REMAP_BIT);
    s->USART2_REMAP = GET_BIT_VALUE(s->AFIO_MAPR, AFIO_MAPR_USART2_REMAP_BIT);
    s->USART3_REMAP = (new_value & AFIO_MAPR_USART3_REMAP_MASK) >> AFIO_MAPR_USART3_REMAP_START;
}

/* Write the External Interrupt Configuration Register.
 * There are four of these registers, each of which configures
 * four EXTI interrupt lines.  Each line is represented by four bits, which
 * indicate which GPIO the line is connected to.  When the register is
 * written, the changes are propagated to the EXTI module.
 */
static void stm32_afio_AFIO_EXTICR_write(Stm32Afio *s, unsigned index,
                                            uint32_t new_value, bool init)
{
    int i;
    unsigned exti_line;
    unsigned start;
    unsigned old_gpio_index, new_gpio_index;

    assert(index < AFIO_EXTICR_COUNT);

    /* Loop through the four EXTI lines controlled by this register. */
    for(i = 0; i < AFIO_EXTI_PER_CR; i++) {
        /* For each line, notify the EXTI module.  This shouldn't
         * happen often, so we update all four, even if they all don't
         * change. */
        exti_line = (index * AFIO_EXTI_PER_CR) + i;
        start = i * 4;

        if(!init) {
            old_gpio_index = (s->AFIO_EXTICR[index] >> start) & 0xf;
            stm32_exti_reset_gpio(s->stm32_exti, exti_line, STM32_GPIO_PERIPH_FROM_INDEX(old_gpio_index));
        }
        new_gpio_index = (new_value >> start) & 0xf;
        stm32_exti_set_gpio(s->stm32_exti, exti_line, STM32_GPIO_PERIPH_FROM_INDEX(new_gpio_index));
    }

    s->AFIO_EXTICR[index] = new_value;
}


static uint64_t stm32_afio_readw(Stm32Afio *s, target_phys_addr_t offset)
{
    switch (offset) {
        case AFIO_EVCR_OFFSET:
            STM32_NOT_IMPL_REG(offset, WORD_ACCESS_SIZE);
            break;
        case AFIO_MAPR_OFFSET:
            return stm32_afio_AFIO_MAPR_read(s);
        case AFIO_EXTICR1_OFFSET:
            return s->AFIO_EXTICR[0];
        case AFIO_EXTICR2_OFFSET:
            return s->AFIO_EXTICR[1];
        case AFIO_EXTICR3_OFFSET:
            return s->AFIO_EXTICR[2];
        case AFIO_EXTICR4_OFFSET:
            return s->AFIO_EXTICR[3];
        default:
            STM32_BAD_REG(offset, WORD_ACCESS_SIZE);
            return 0;
    }
}

static void stm32_afio_writew(Stm32Afio *s, target_phys_addr_t offset,
                          uint64_t value)
{
    switch (offset) {
        case AFIO_EVCR_OFFSET:
            STM32_NOT_IMPL_REG(offset, WORD_ACCESS_SIZE);
            break;
        case AFIO_MAPR_OFFSET:
            stm32_afio_AFIO_MAPR_write(s, value, false);
            break;
        case AFIO_EXTICR1_OFFSET:
            stm32_afio_AFIO_EXTICR_write(s, 0, value, false);
            break;
        case AFIO_EXTICR2_OFFSET:
            stm32_afio_AFIO_EXTICR_write(s, 1, value, false);
            break;
        case AFIO_EXTICR3_OFFSET:
            stm32_afio_AFIO_EXTICR_write(s, 2, value, false);
            break;
        case AFIO_EXTICR4_OFFSET:
            stm32_afio_AFIO_EXTICR_write(s, 3, value, false);
            break;
        default:
            STM32_BAD_REG(offset, WORD_ACCESS_SIZE);
            break;
    }
}

static uint64_t stm32_afio_read(void *opaque, target_phys_addr_t offset,
                          unsigned size)
{
    Stm32Afio *s = (Stm32Afio *)opaque;

    stm32_rcc_check_periph_clk((Stm32Rcc *)s->stm32_rcc, STM32_AFIO);

    switch(size) {
        case 4:
            return stm32_afio_readw(opaque, offset);
        default:
            STM32_BAD_REG(offset, size);
            return 0;
    }
}

static void stm32_afio_write(void *opaque, target_phys_addr_t offset,
                       uint64_t value, unsigned size)
{
    Stm32Afio *s = (Stm32Afio *)opaque;

    stm32_rcc_check_periph_clk((Stm32Rcc *)s->stm32_rcc, STM32_AFIO);

    switch(size) {
        case 4:
            stm32_afio_writew(opaque, offset, value);
            break;
        default:
            STM32_BAD_REG(offset, size);
            break;
    }
}

static const MemoryRegionOps stm32_afio_ops = {
    .read = stm32_afio_read,
    .write = stm32_afio_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void stm32_afio_reset(DeviceState *dev)
{
    Stm32Afio *s = FROM_SYSBUS(Stm32Afio, sysbus_from_qdev(dev));

    stm32_afio_AFIO_MAPR_write(s, 0x00000000, true);
    stm32_afio_AFIO_EXTICR_write(s, 0, 0x00000000, true);
    stm32_afio_AFIO_EXTICR_write(s, 1, 0x00000000, true);
    stm32_afio_AFIO_EXTICR_write(s, 2, 0x00000000, true);
    stm32_afio_AFIO_EXTICR_write(s, 3, 0x00000000, true);
}






/* PUBLIC FUNCTIONS */

uint32_t stm32_afio_get_periph_map(Stm32Afio *s, stm32_periph_t periph)
{
    switch(periph) {
        case STM32_UART1:
            return s->USART1_REMAP;
        case STM32_UART2:
            return s->USART2_REMAP;
        case STM32_UART3:
            return s->USART3_REMAP;
        default:
            hw_error("Invalid peripheral");
            break;
    }
}




/* DEVICE INITIALIZATION */

static int stm32_afio_init(SysBusDevice *dev)
{
    Stm32Afio *s = FROM_SYSBUS(Stm32Afio, dev);

    s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;
    s->stm32_exti = (Stm32Exti *)s->stm32_exti_prop;

    memory_region_init_io(&s->iomem, &stm32_afio_ops, s,
                          "afio", 0x03ff);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static Property stm32_afio_properties[] = {
    DEFINE_PROP_PTR("stm32_rcc", Stm32Afio, stm32_rcc_prop),
    DEFINE_PROP_PTR("stm32_exti", Stm32Afio, stm32_exti_prop),
    DEFINE_PROP_END_OF_LIST()
};

static void stm32_afio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_afio_init;
    dc->reset = stm32_afio_reset;
    dc->props = stm32_afio_properties;
}

static TypeInfo stm32_afio_info = {
    .name  = "stm32_afio",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(Stm32Afio),
    .class_init = stm32_afio_class_init
};

static void stm32_afio_register_types(void)
{
    type_register_static(&stm32_afio_info);
}

type_init(stm32_afio_register_types)
