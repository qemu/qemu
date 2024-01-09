/*
 * STM32L4x5 SYSCFG (System Configuration Controller)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This work is based on the stm32f4xx_syscfg by Alistair Francis.
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
#include "hw/misc/stm32l4x5_syscfg.h"

#define SYSCFG_MEMRMP 0x00
#define SYSCFG_CFGR1 0x04
#define SYSCFG_EXTICR1 0x08
#define SYSCFG_EXTICR2 0x0C
#define SYSCFG_EXTICR3 0x10
#define SYSCFG_EXTICR4 0x14
#define SYSCFG_SCSR 0x18
#define SYSCFG_CFGR2 0x1C
#define SYSCFG_SWPR 0x20
#define SYSCFG_SKR 0x24
#define SYSCFG_SWPR2 0x28

/* 00000000_00000000_00000001_00000111 */
#define ACTIVABLE_BITS_MEMRP 0x00000107

/* 11111100_11111111_00000001_00000000 */
#define ACTIVABLE_BITS_CFGR1 0xFCFF0100
/* 00000000_00000000_00000000_00000001 */
#define FIREWALL_DISABLE_CFGR1 0x00000001

/* 00000000_00000000_11111111_11111111 */
#define ACTIVABLE_BITS_EXTICR 0x0000FFFF

/* 00000000_00000000_00000000_00000011 */
/* #define ACTIVABLE_BITS_SCSR 0x00000003 */

/* 00000000_00000000_00000000_00001111 */
#define ECC_LOCK_CFGR2 0x0000000F
/* 00000000_00000000_00000001_00000000 */
#define SRAM2_PARITY_ERROR_FLAG_CFGR2 0x00000100

/* 00000000_00000000_00000000_11111111 */
#define ACTIVABLE_BITS_SKR 0x000000FF

#define NUM_LINES_PER_EXTICR_REG 4

static void stm32l4x5_syscfg_hold_reset(Object *obj)
{
    Stm32l4x5SyscfgState *s = STM32L4X5_SYSCFG(obj);

    s->memrmp = 0x00000000;
    s->cfgr1 = 0x7C000001;
    s->exticr[0] = 0x00000000;
    s->exticr[1] = 0x00000000;
    s->exticr[2] = 0x00000000;
    s->exticr[3] = 0x00000000;
    s->scsr = 0x00000000;
    s->cfgr2 = 0x00000000;
    s->swpr = 0x00000000;
    s->skr = 0x00000000;
    s->swpr2 = 0x00000000;
}

static void stm32l4x5_syscfg_set_irq(void *opaque, int irq, int level)
{
    Stm32l4x5SyscfgState *s = opaque;
    const uint8_t gpio = irq / GPIO_NUM_PINS;
    const int line = irq % GPIO_NUM_PINS;

    const int exticr_reg = line / NUM_LINES_PER_EXTICR_REG;
    const int startbit = (line % NUM_LINES_PER_EXTICR_REG) * 4;

    g_assert(gpio < NUM_GPIOS);
    trace_stm32l4x5_syscfg_set_irq(gpio, line, level);

    if (extract32(s->exticr[exticr_reg], startbit, 4) == gpio) {
        trace_stm32l4x5_syscfg_forward_exti(line);
        qemu_set_irq(s->gpio_out[line], level);
    }
}

static uint64_t stm32l4x5_syscfg_read(void *opaque, hwaddr addr,
                                      unsigned int size)
{
    Stm32l4x5SyscfgState *s = opaque;

    trace_stm32l4x5_syscfg_read(addr);

    switch (addr) {
    case SYSCFG_MEMRMP:
        return s->memrmp;
    case SYSCFG_CFGR1:
        return s->cfgr1;
    case SYSCFG_EXTICR1...SYSCFG_EXTICR4:
        return s->exticr[(addr - SYSCFG_EXTICR1) / 4];
    case SYSCFG_SCSR:
        return s->scsr;
    case SYSCFG_CFGR2:
        return s->cfgr2;
    case SYSCFG_SWPR:
        return s->swpr;
    case SYSCFG_SKR:
        return s->skr;
    case SYSCFG_SWPR2:
        return s->swpr2;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}
static void stm32l4x5_syscfg_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned int size)
{
    Stm32l4x5SyscfgState *s = opaque;

    trace_stm32l4x5_syscfg_write(addr, value);

    switch (addr) {
    case SYSCFG_MEMRMP:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Changing the memory mapping isn't supported\n",
                      __func__);
        s->memrmp = value & ACTIVABLE_BITS_MEMRP;
        return;
    case SYSCFG_CFGR1:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Functions in CFGRx aren't supported\n",
                      __func__);
        /* bit 0 (firewall dis.) is cleared by software, set only by reset. */
        s->cfgr1 = (s->cfgr1 & value & FIREWALL_DISABLE_CFGR1) |
                   (value & ACTIVABLE_BITS_CFGR1);
        return;
    case SYSCFG_EXTICR1...SYSCFG_EXTICR4:
        s->exticr[(addr - SYSCFG_EXTICR1) / 4] =
                (value & ACTIVABLE_BITS_EXTICR);
        return;
    case SYSCFG_SCSR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Erasing SRAM2 isn't supported\n",
                      __func__);
        /*
         * only non reserved bits are :
         * bit 0 (write-protected by a passkey), bit 1 (meant to be read)
         * so it serves no purpose yet to add :
         * s->scsr = value & 0x3;
         */
        return;
    case SYSCFG_CFGR2:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Functions in CFGRx aren't supported\n",
                      __func__);
        /* bit 8 (SRAM2 PEF) is cleared by software by writing a '1'.*/
        /* bits[3:0] (ECC Lock) are set by software, cleared only by reset.*/
        s->cfgr2 = (s->cfgr2 | (value & ECC_LOCK_CFGR2)) &
                   ~(value & SRAM2_PARITY_ERROR_FLAG_CFGR2);
        return;
    case SYSCFG_SWPR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Write protecting SRAM2 isn't supported\n",
                      __func__);
        /* These bits are set by software and cleared only by reset.*/
        s->swpr |= value;
        return;
    case SYSCFG_SKR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Erasing SRAM2 isn't supported\n",
                      __func__);
        s->skr = value & ACTIVABLE_BITS_SKR;
        return;
    case SYSCFG_SWPR2:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Write protecting SRAM2 isn't supported\n",
                      __func__);
        /* These bits are set by software and cleared only by reset.*/
        s->swpr2 |= value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32l4x5_syscfg_ops = {
    .read = stm32l4x5_syscfg_read,
    .write = stm32l4x5_syscfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .impl.unaligned = false,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void stm32l4x5_syscfg_init(Object *obj)
{
    Stm32l4x5SyscfgState *s = STM32L4X5_SYSCFG(obj);

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_syscfg_ops, s,
                          TYPE_STM32L4X5_SYSCFG, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_in(DEVICE(obj), stm32l4x5_syscfg_set_irq,
                      GPIO_NUM_PINS * NUM_GPIOS);
    qdev_init_gpio_out(DEVICE(obj), s->gpio_out, GPIO_NUM_PINS);
}

static const VMStateDescription vmstate_stm32l4x5_syscfg = {
    .name = TYPE_STM32L4X5_SYSCFG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(memrmp, Stm32l4x5SyscfgState),
        VMSTATE_UINT32(cfgr1, Stm32l4x5SyscfgState),
        VMSTATE_UINT32_ARRAY(exticr, Stm32l4x5SyscfgState,
                             SYSCFG_NUM_EXTICR),
        VMSTATE_UINT32(scsr, Stm32l4x5SyscfgState),
        VMSTATE_UINT32(cfgr2, Stm32l4x5SyscfgState),
        VMSTATE_UINT32(swpr, Stm32l4x5SyscfgState),
        VMSTATE_UINT32(skr, Stm32l4x5SyscfgState),
        VMSTATE_UINT32(swpr2, Stm32l4x5SyscfgState),
        VMSTATE_END_OF_LIST()
    }
};

static void stm32l4x5_syscfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_stm32l4x5_syscfg;
    rc->phases.hold = stm32l4x5_syscfg_hold_reset;
}

static const TypeInfo stm32l4x5_syscfg_info[] = {
    {
        .name          = TYPE_STM32L4X5_SYSCFG,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Stm32l4x5SyscfgState),
        .instance_init = stm32l4x5_syscfg_init,
        .class_init    = stm32l4x5_syscfg_class_init,
    }
};

DEFINE_TYPES(stm32l4x5_syscfg_info)
