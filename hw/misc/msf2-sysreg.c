/*
 * System Register block model of Microsemi SmartFusion2.
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/msf2-sysreg.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "trace.h"

static inline int msf2_divbits(uint32_t div)
{
    int r = ctz32(div);

    return (div < 8) ? r : r + 1;
}

static void msf2_sysreg_reset(DeviceState *d)
{
    MSF2SysregState *s = MSF2_SYSREG(d);

    s->regs[MSSDDR_PLL_STATUS_LOW_CR] = 0x021A2358;
    s->regs[MSSDDR_PLL_STATUS] = 0x3;
    s->regs[MSSDDR_FACC1_CR] = msf2_divbits(s->apb0div) << 5 |
                               msf2_divbits(s->apb1div) << 2;
}

static uint64_t msf2_sysreg_read(void *opaque, hwaddr offset,
    unsigned size)
{
    MSF2SysregState *s = opaque;
    uint32_t ret = 0;

    offset >>= 2;
    if (offset < ARRAY_SIZE(s->regs)) {
        ret = s->regs[offset];
        trace_msf2_sysreg_read(offset << 2, ret);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: Bad offset 0x%08" HWADDR_PRIx "\n", __func__,
                    offset << 2);
    }

    return ret;
}

static void msf2_sysreg_write(void *opaque, hwaddr offset,
                          uint64_t val, unsigned size)
{
    MSF2SysregState *s = opaque;
    uint32_t newval = val;

    offset >>= 2;

    switch (offset) {
    case MSSDDR_PLL_STATUS:
        trace_msf2_sysreg_write_pll_status();
        break;

    case ESRAM_CR:
    case DDR_CR:
    case ENVM_REMAP_BASE_CR:
        if (newval != s->regs[offset]) {
            qemu_log_mask(LOG_UNIMP,
                       TYPE_MSF2_SYSREG": remapping not supported\n");
        }
        break;

    default:
        if (offset < ARRAY_SIZE(s->regs)) {
            trace_msf2_sysreg_write(offset << 2, newval, s->regs[offset]);
            s->regs[offset] = newval;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: Bad offset 0x%08" HWADDR_PRIx "\n", __func__,
                        offset << 2);
        }
        break;
    }
}

static const MemoryRegionOps sysreg_ops = {
    .read = msf2_sysreg_read,
    .write = msf2_sysreg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void msf2_sysreg_init(Object *obj)
{
    MSF2SysregState *s = MSF2_SYSREG(obj);

    memory_region_init_io(&s->iomem, obj, &sysreg_ops, s, TYPE_MSF2_SYSREG,
                          MSF2_SYSREG_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_msf2_sysreg = {
    .name = TYPE_MSF2_SYSREG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MSF2SysregState, MSF2_SYSREG_MMIO_SIZE / 4),
        VMSTATE_END_OF_LIST()
    }
};

static Property msf2_sysreg_properties[] = {
    /* default divisors in Libero GUI */
    DEFINE_PROP_UINT8("apb0divisor", MSF2SysregState, apb0div, 2),
    DEFINE_PROP_UINT8("apb1divisor", MSF2SysregState, apb1div, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void msf2_sysreg_realize(DeviceState *dev, Error **errp)
{
    MSF2SysregState *s = MSF2_SYSREG(dev);

    if ((s->apb0div > 32 || !is_power_of_2(s->apb0div))
        || (s->apb1div > 32 || !is_power_of_2(s->apb1div))) {
        error_setg(errp, "Invalid apb divisor value");
        error_append_hint(errp, "apb divisor must be a power of 2"
                           " and maximum value is 32\n");
    }
}

static void msf2_sysreg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_msf2_sysreg;
    dc->reset = msf2_sysreg_reset;
    device_class_set_props(dc, msf2_sysreg_properties);
    dc->realize = msf2_sysreg_realize;
}

static const TypeInfo msf2_sysreg_info = {
    .name  = TYPE_MSF2_SYSREG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = msf2_sysreg_class_init,
    .instance_size  = sizeof(MSF2SysregState),
    .instance_init = msf2_sysreg_init,
};

static void msf2_sysreg_register_types(void)
{
    type_register_static(&msf2_sysreg_info);
}

type_init(msf2_sysreg_register_types)
