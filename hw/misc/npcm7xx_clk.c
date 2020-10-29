/*
 * Nuvoton NPCM7xx Clock Control Registers.
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"

#include "hw/misc/npcm7xx_clk.h"
#include "hw/timer/npcm7xx_timer.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "trace.h"
#include "sysemu/watchdog.h"

#define PLLCON_LOKI     BIT(31)
#define PLLCON_LOKS     BIT(30)
#define PLLCON_PWDEN    BIT(12)

enum NPCM7xxCLKRegisters {
    NPCM7XX_CLK_CLKEN1,
    NPCM7XX_CLK_CLKSEL,
    NPCM7XX_CLK_CLKDIV1,
    NPCM7XX_CLK_PLLCON0,
    NPCM7XX_CLK_PLLCON1,
    NPCM7XX_CLK_SWRSTR,
    NPCM7XX_CLK_IPSRST1         = 0x20 / sizeof(uint32_t),
    NPCM7XX_CLK_IPSRST2,
    NPCM7XX_CLK_CLKEN2,
    NPCM7XX_CLK_CLKDIV2,
    NPCM7XX_CLK_CLKEN3,
    NPCM7XX_CLK_IPSRST3,
    NPCM7XX_CLK_WD0RCR,
    NPCM7XX_CLK_WD1RCR,
    NPCM7XX_CLK_WD2RCR,
    NPCM7XX_CLK_SWRSTC1,
    NPCM7XX_CLK_SWRSTC2,
    NPCM7XX_CLK_SWRSTC3,
    NPCM7XX_CLK_SWRSTC4,
    NPCM7XX_CLK_PLLCON2,
    NPCM7XX_CLK_CLKDIV3,
    NPCM7XX_CLK_CORSTC,
    NPCM7XX_CLK_PLLCONG,
    NPCM7XX_CLK_AHBCKFI,
    NPCM7XX_CLK_SECCNT,
    NPCM7XX_CLK_CNTR25M,
    NPCM7XX_CLK_REGS_END,
};

/*
 * These reset values were taken from version 0.91 of the NPCM750R data sheet.
 *
 * All are loaded on power-up reset. CLKENx and SWRSTR should also be loaded on
 * core domain reset, but this reset type is not yet supported by QEMU.
 */
static const uint32_t cold_reset_values[NPCM7XX_CLK_NR_REGS] = {
    [NPCM7XX_CLK_CLKEN1]        = 0xffffffff,
    [NPCM7XX_CLK_CLKSEL]        = 0x004aaaaa,
    [NPCM7XX_CLK_CLKDIV1]       = 0x5413f855,
    [NPCM7XX_CLK_PLLCON0]       = 0x00222101 | PLLCON_LOKI,
    [NPCM7XX_CLK_PLLCON1]       = 0x00202101 | PLLCON_LOKI,
    [NPCM7XX_CLK_IPSRST1]       = 0x00001000,
    [NPCM7XX_CLK_IPSRST2]       = 0x80000000,
    [NPCM7XX_CLK_CLKEN2]        = 0xffffffff,
    [NPCM7XX_CLK_CLKDIV2]       = 0xaa4f8f9f,
    [NPCM7XX_CLK_CLKEN3]        = 0xffffffff,
    [NPCM7XX_CLK_IPSRST3]       = 0x03000000,
    [NPCM7XX_CLK_WD0RCR]        = 0xffffffff,
    [NPCM7XX_CLK_WD1RCR]        = 0xffffffff,
    [NPCM7XX_CLK_WD2RCR]        = 0xffffffff,
    [NPCM7XX_CLK_SWRSTC1]       = 0x00000003,
    [NPCM7XX_CLK_PLLCON2]       = 0x00c02105 | PLLCON_LOKI,
    [NPCM7XX_CLK_CORSTC]        = 0x04000003,
    [NPCM7XX_CLK_PLLCONG]       = 0x01228606 | PLLCON_LOKI,
    [NPCM7XX_CLK_AHBCKFI]       = 0x000000c8,
};

/* Register Field Definitions */
#define NPCM7XX_CLK_WDRCR_CA9C  BIT(0) /* Cortex A9 Cores */

/* The number of watchdogs that can trigger a reset. */
#define NPCM7XX_NR_WATCHDOGS    (3)

static uint64_t npcm7xx_clk_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCM7xxCLKState *s = opaque;
    int64_t now_ns;
    uint32_t value = 0;

    if (reg >= NPCM7XX_CLK_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%04" HWADDR_PRIx " out of range\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case NPCM7XX_CLK_SWRSTR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: register @ 0x%04" HWADDR_PRIx " is write-only\n",
                      __func__, offset);
        break;

    case NPCM7XX_CLK_SECCNT:
        now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        value = (now_ns - s->ref_ns) / NANOSECONDS_PER_SECOND;
        break;

    case NPCM7XX_CLK_CNTR25M:
        now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        /*
         * This register counts 25 MHz cycles, updating every 640 ns. It rolls
         * over to zero every second.
         *
         * The 4 LSBs are always zero: (1e9 / 640) << 4 = 25000000.
         */
        value = (((now_ns - s->ref_ns) / 640) << 4) % NPCM7XX_TIMER_REF_HZ;
        break;

    default:
        value = s->regs[reg];
        break;
    };

    trace_npcm7xx_clk_read(offset, value);

    return value;
}

static void npcm7xx_clk_write(void *opaque, hwaddr offset,
                              uint64_t v, unsigned size)
{
    uint32_t reg = offset / sizeof(uint32_t);
    NPCM7xxCLKState *s = opaque;
    uint32_t value = v;

    trace_npcm7xx_clk_write(offset, value);

    if (reg >= NPCM7XX_CLK_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: offset 0x%04" HWADDR_PRIx " out of range\n",
                      __func__, offset);
        return;
    }

    switch (reg) {
    case NPCM7XX_CLK_SWRSTR:
        qemu_log_mask(LOG_UNIMP, "%s: SW reset not implemented: 0x%02x\n",
                      __func__, value);
        value = 0;
        break;

    case NPCM7XX_CLK_PLLCON0:
    case NPCM7XX_CLK_PLLCON1:
    case NPCM7XX_CLK_PLLCON2:
    case NPCM7XX_CLK_PLLCONG:
        if (value & PLLCON_PWDEN) {
            /* Power down -- clear lock and indicate loss of lock */
            value &= ~PLLCON_LOKI;
            value |= PLLCON_LOKS;
        } else {
            /* Normal mode -- assume always locked */
            value |= PLLCON_LOKI;
            /* Keep LOKS unchanged unless cleared by writing 1 */
            if (value & PLLCON_LOKS) {
                value &= ~PLLCON_LOKS;
            } else {
                value |= (value & PLLCON_LOKS);
            }
        }
        break;

    case NPCM7XX_CLK_CNTR25M:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: register @ 0x%04" HWADDR_PRIx " is read-only\n",
                      __func__, offset);
        return;
    }

    s->regs[reg] = value;
}

/* Perform reset action triggered by a watchdog */
static void npcm7xx_clk_perform_watchdog_reset(void *opaque, int n,
        int level)
{
    NPCM7xxCLKState *clk = NPCM7XX_CLK(opaque);
    uint32_t rcr;

    g_assert(n >= 0 && n <= NPCM7XX_NR_WATCHDOGS);
    rcr = clk->regs[NPCM7XX_CLK_WD0RCR + n];
    if (rcr & NPCM7XX_CLK_WDRCR_CA9C) {
        watchdog_perform_action();
    } else {
        qemu_log_mask(LOG_UNIMP,
                "%s: only CPU reset is implemented. (requested 0x%" PRIx32")\n",
                __func__, rcr);
    }
}

static const struct MemoryRegionOps npcm7xx_clk_ops = {
    .read       = npcm7xx_clk_read,
    .write      = npcm7xx_clk_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid      = {
        .min_access_size        = 4,
        .max_access_size        = 4,
        .unaligned              = false,
    },
};

static void npcm7xx_clk_enter_reset(Object *obj, ResetType type)
{
    NPCM7xxCLKState *s = NPCM7XX_CLK(obj);

    QEMU_BUILD_BUG_ON(sizeof(s->regs) != sizeof(cold_reset_values));

    switch (type) {
    case RESET_TYPE_COLD:
        memcpy(s->regs, cold_reset_values, sizeof(cold_reset_values));
        s->ref_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        return;
    }

    /*
     * A small number of registers need to be reset on a core domain reset,
     * but no such reset type exists yet.
     */
    qemu_log_mask(LOG_UNIMP, "%s: reset type %d not implemented.",
                  __func__, type);
}

static void npcm7xx_clk_init(Object *obj)
{
    NPCM7xxCLKState *s = NPCM7XX_CLK(obj);

    memory_region_init_io(&s->iomem, obj, &npcm7xx_clk_ops, s,
                          TYPE_NPCM7XX_CLK, 4 * KiB);
    sysbus_init_mmio(&s->parent, &s->iomem);
    qdev_init_gpio_in_named(DEVICE(s), npcm7xx_clk_perform_watchdog_reset,
            NPCM7XX_WATCHDOG_RESET_GPIO_IN, NPCM7XX_NR_WATCHDOGS);
}

static const VMStateDescription vmstate_npcm7xx_clk = {
    .name = "npcm7xx-clk",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, NPCM7xxCLKState, NPCM7XX_CLK_NR_REGS),
        VMSTATE_INT64(ref_ns, NPCM7xxCLKState),
        VMSTATE_END_OF_LIST(),
    },
};

static void npcm7xx_clk_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    QEMU_BUILD_BUG_ON(NPCM7XX_CLK_REGS_END > NPCM7XX_CLK_NR_REGS);

    dc->desc = "NPCM7xx Clock Control Registers";
    dc->vmsd = &vmstate_npcm7xx_clk;
    rc->phases.enter = npcm7xx_clk_enter_reset;
}

static const TypeInfo npcm7xx_clk_info = {
    .name               = TYPE_NPCM7XX_CLK,
    .parent             = TYPE_SYS_BUS_DEVICE,
    .instance_size      = sizeof(NPCM7xxCLKState),
    .instance_init      = npcm7xx_clk_init,
    .class_init         = npcm7xx_clk_class_init,
};

static void npcm7xx_clk_register_type(void)
{
    type_register_static(&npcm7xx_clk_info);
}
type_init(npcm7xx_clk_register_type);
