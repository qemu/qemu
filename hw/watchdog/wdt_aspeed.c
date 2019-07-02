/*
 * ASPEED Watchdog Controller
 *
 * Copyright (C) 2016-2017 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "sysemu/watchdog.h"
#include "hw/misc/aspeed_scu.h"
#include "hw/sysbus.h"
#include "hw/watchdog/wdt_aspeed.h"

#define WDT_STATUS                      (0x00 / 4)
#define WDT_RELOAD_VALUE                (0x04 / 4)
#define WDT_RESTART                     (0x08 / 4)
#define WDT_CTRL                        (0x0C / 4)
#define   WDT_CTRL_RESET_MODE_SOC       (0x00 << 5)
#define   WDT_CTRL_RESET_MODE_FULL_CHIP (0x01 << 5)
#define   WDT_CTRL_1MHZ_CLK             BIT(4)
#define   WDT_CTRL_WDT_EXT              BIT(3)
#define   WDT_CTRL_WDT_INTR             BIT(2)
#define   WDT_CTRL_RESET_SYSTEM         BIT(1)
#define   WDT_CTRL_ENABLE               BIT(0)
#define WDT_RESET_WIDTH                 (0x18 / 4)
#define   WDT_RESET_WIDTH_ACTIVE_HIGH   BIT(31)
#define     WDT_POLARITY_MASK           (0xFF << 24)
#define     WDT_ACTIVE_HIGH_MAGIC       (0xA5 << 24)
#define     WDT_ACTIVE_LOW_MAGIC        (0x5A << 24)
#define   WDT_RESET_WIDTH_PUSH_PULL     BIT(30)
#define     WDT_DRIVE_TYPE_MASK         (0xFF << 24)
#define     WDT_PUSH_PULL_MAGIC         (0xA8 << 24)
#define     WDT_OPEN_DRAIN_MAGIC        (0x8A << 24)

#define WDT_TIMEOUT_STATUS              (0x10 / 4)
#define WDT_TIMEOUT_CLEAR               (0x14 / 4)

#define WDT_RESTART_MAGIC               0x4755

#define SCU_RESET_CONTROL1              (0x04 / 4)
#define    SCU_RESET_SDRAM              BIT(0)

static bool aspeed_wdt_is_enabled(const AspeedWDTState *s)
{
    return s->regs[WDT_CTRL] & WDT_CTRL_ENABLE;
}

static bool is_ast2500(const AspeedWDTState *s)
{
    switch (s->silicon_rev) {
    case AST2500_A0_SILICON_REV:
    case AST2500_A1_SILICON_REV:
        return true;
    case AST2400_A0_SILICON_REV:
    case AST2400_A1_SILICON_REV:
    default:
        break;
    }

    return false;
}

static uint64_t aspeed_wdt_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedWDTState *s = ASPEED_WDT(opaque);

    offset >>= 2;

    switch (offset) {
    case WDT_STATUS:
        return s->regs[WDT_STATUS];
    case WDT_RELOAD_VALUE:
        return s->regs[WDT_RELOAD_VALUE];
    case WDT_RESTART:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from write-only reg at offset 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
        return 0;
    case WDT_CTRL:
        return s->regs[WDT_CTRL];
    case WDT_RESET_WIDTH:
        return s->regs[WDT_RESET_WIDTH];
    case WDT_TIMEOUT_STATUS:
    case WDT_TIMEOUT_CLEAR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: uninmplemented read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

}

static void aspeed_wdt_reload(AspeedWDTState *s, bool pclk)
{
    uint64_t reload;

    if (pclk) {
        reload = muldiv64(s->regs[WDT_RELOAD_VALUE], NANOSECONDS_PER_SECOND,
                          s->pclk_freq);
    } else {
        reload = s->regs[WDT_RELOAD_VALUE] * 1000ULL;
    }

    if (aspeed_wdt_is_enabled(s)) {
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + reload);
    }
}

static void aspeed_wdt_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size)
{
    AspeedWDTState *s = ASPEED_WDT(opaque);
    bool enable = data & WDT_CTRL_ENABLE;

    offset >>= 2;

    switch (offset) {
    case WDT_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only reg at offset 0x%"
                      HWADDR_PRIx "\n", __func__, offset);
        break;
    case WDT_RELOAD_VALUE:
        s->regs[WDT_RELOAD_VALUE] = data;
        break;
    case WDT_RESTART:
        if ((data & 0xFFFF) == WDT_RESTART_MAGIC) {
            s->regs[WDT_STATUS] = s->regs[WDT_RELOAD_VALUE];
            aspeed_wdt_reload(s, !(data & WDT_CTRL_1MHZ_CLK));
        }
        break;
    case WDT_CTRL:
        if (enable && !aspeed_wdt_is_enabled(s)) {
            s->regs[WDT_CTRL] = data;
            aspeed_wdt_reload(s, !(data & WDT_CTRL_1MHZ_CLK));
        } else if (!enable && aspeed_wdt_is_enabled(s)) {
            s->regs[WDT_CTRL] = data;
            timer_del(s->timer);
        }
        break;
    case WDT_RESET_WIDTH:
    {
        uint32_t property = data & WDT_POLARITY_MASK;

        if (property && is_ast2500(s)) {
            if (property == WDT_ACTIVE_HIGH_MAGIC) {
                s->regs[WDT_RESET_WIDTH] |= WDT_RESET_WIDTH_ACTIVE_HIGH;
            } else if (property == WDT_ACTIVE_LOW_MAGIC) {
                s->regs[WDT_RESET_WIDTH] &= ~WDT_RESET_WIDTH_ACTIVE_HIGH;
            } else if (property == WDT_PUSH_PULL_MAGIC) {
                s->regs[WDT_RESET_WIDTH] |= WDT_RESET_WIDTH_PUSH_PULL;
            } else if (property == WDT_OPEN_DRAIN_MAGIC) {
                s->regs[WDT_RESET_WIDTH] &= ~WDT_RESET_WIDTH_PUSH_PULL;
            }
        }
        s->regs[WDT_RESET_WIDTH] &= ~s->ext_pulse_width_mask;
        s->regs[WDT_RESET_WIDTH] |= data & s->ext_pulse_width_mask;
        break;
    }
    case WDT_TIMEOUT_STATUS:
    case WDT_TIMEOUT_CLEAR:
        qemu_log_mask(LOG_UNIMP,
                      "%s: uninmplemented write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }
    return;
}

static WatchdogTimerModel model = {
    .wdt_name = TYPE_ASPEED_WDT,
    .wdt_description = "Aspeed watchdog device",
};

static const VMStateDescription vmstate_aspeed_wdt = {
    .name = "vmstate_aspeed_wdt",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(timer, AspeedWDTState),
        VMSTATE_UINT32_ARRAY(regs, AspeedWDTState, ASPEED_WDT_REGS_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static const MemoryRegionOps aspeed_wdt_ops = {
    .read = aspeed_wdt_read,
    .write = aspeed_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void aspeed_wdt_reset(DeviceState *dev)
{
    AspeedWDTState *s = ASPEED_WDT(dev);

    s->regs[WDT_STATUS] = 0x3EF1480;
    s->regs[WDT_RELOAD_VALUE] = 0x03EF1480;
    s->regs[WDT_RESTART] = 0;
    s->regs[WDT_CTRL] = 0;
    s->regs[WDT_RESET_WIDTH] = 0xFF;

    timer_del(s->timer);
}

static void aspeed_wdt_timer_expired(void *dev)
{
    AspeedWDTState *s = ASPEED_WDT(dev);

    /* Do not reset on SDRAM controller reset */
    if (s->scu->regs[SCU_RESET_CONTROL1] & SCU_RESET_SDRAM) {
        timer_del(s->timer);
        s->regs[WDT_CTRL] = 0;
        return;
    }

    qemu_log_mask(CPU_LOG_RESET, "Watchdog timer expired.\n");
    watchdog_perform_action();
    timer_del(s->timer);
}

#define PCLK_HZ 24000000

static void aspeed_wdt_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedWDTState *s = ASPEED_WDT(dev);
    Error *err = NULL;
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "scu", &err);
    if (!obj) {
        error_propagate(errp, err);
        error_prepend(errp, "required link 'scu' not found: ");
        return;
    }
    s->scu = ASPEED_SCU(obj);

    if (!is_supported_silicon_rev(s->silicon_rev)) {
        error_setg(errp, "Unknown silicon revision: 0x%" PRIx32,
                s->silicon_rev);
        return;
    }

    switch (s->silicon_rev) {
    case AST2400_A0_SILICON_REV:
    case AST2400_A1_SILICON_REV:
        s->ext_pulse_width_mask = 0xff;
        break;
    case AST2500_A0_SILICON_REV:
    case AST2500_A1_SILICON_REV:
        s->ext_pulse_width_mask = 0xfffff;
        break;
    default:
        g_assert_not_reached();
    }

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, aspeed_wdt_timer_expired, dev);

    /* FIXME: This setting should be derived from the SCU hw strapping
     * register SCU70
     */
    s->pclk_freq = PCLK_HZ;

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_wdt_ops, s,
                          TYPE_ASPEED_WDT, ASPEED_WDT_REGS_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property aspeed_wdt_properties[] = {
    DEFINE_PROP_UINT32("silicon-rev", AspeedWDTState, silicon_rev, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_wdt_realize;
    dc->reset = aspeed_wdt_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->vmsd = &vmstate_aspeed_wdt;
    dc->props = aspeed_wdt_properties;
}

static const TypeInfo aspeed_wdt_info = {
    .parent = TYPE_SYS_BUS_DEVICE,
    .name  = TYPE_ASPEED_WDT,
    .instance_size  = sizeof(AspeedWDTState),
    .class_init = aspeed_wdt_class_init,
};

static void wdt_aspeed_register_types(void)
{
    watchdog_add_model(&model);
    type_register_static(&aspeed_wdt_info);
}

type_init(wdt_aspeed_register_types)
