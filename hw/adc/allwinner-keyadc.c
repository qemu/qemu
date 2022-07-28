/*
 * Allwinner F1 Clock Control Unit emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/adc/allwinner-keyadc.h"

/* ADC register offsets */
enum {
    KEYADC_CTRL_REG  = 0x00, /* Control Register */
    KEYADC_INTC_REG  = 0x04, /* Interrupt Control Register */
    KEYADC_INTS_REG  = 0x08, /* Interrupt Status Register */
    KEYADC_DATA_REG  = 0x0C, /* Data Register */
};

/* ADC register reset values  */
enum {
    KEYADC_CTRL_RST  = 0x01000174, /* Control Register */
    KEYADC_INTC_RST  = 0x00000000, /* Interrupt Control Register */
    KEYADC_INTS_RST  = 0x00000000, /* Interrupt Status Register */
    KEYADC_DATA_RST  = 0x00000000, /* Data Register */
};
#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* ADC register flags */


static uint64_t allwinner_keyadc_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const AwKeyAdcState *s = AW_KEYADC(opaque);
    const uint32_t idx = REG_INDEX(offset);
    
    switch (offset) {
    case KEYADC_CTRL_REG ... KEYADC_INTS_REG:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    case KEYADC_DATA_REG:      /* DRAM Configuration */
        return (s->regs[idx] & ~0x3F) | (s->adc_value & 0x3F);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad read offset 0x%x\n",  __func__, (int)offset);
        break;
    }
    return s->regs[idx];
}

static void allwinner_keyadc_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    AwKeyAdcState *s = AW_KEYADC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case KEYADC_CTRL_REG ... KEYADC_DATA_REG:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad write offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    s->regs[idx] = (uint32_t) value;
}

static const MemoryRegionOps allwinner_keyadc_ops = {
    .read = allwinner_keyadc_read,
    .write = allwinner_keyadc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void allwinner_keyadc_reset(DeviceState *dev)
{
    AwKeyAdcState *s = AW_KEYADC(dev);

    /* Set default values for registers */
    s->regs[REG_INDEX(KEYADC_CTRL_REG)] = KEYADC_CTRL_RST;
    s->regs[REG_INDEX(KEYADC_INTC_REG)] = KEYADC_INTC_RST;
    s->regs[REG_INDEX(KEYADC_INTS_REG)] = KEYADC_INTS_RST;
    s->regs[REG_INDEX(KEYADC_DATA_REG)] = KEYADC_DATA_RST;
}

static void allwinner_keyadc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwKeyAdcState *s = AW_KEYADC(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_keyadc_ops, s,
                          TYPE_AW_KEYADC, AW_KEYADC_REGS_NUM);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription allwinner_keyadc_vmstate = {
    .name = "allwinner-keyadc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(adc_value,  AwKeyAdcState),
        VMSTATE_UINT32_ARRAY(regs, AwKeyAdcState, AW_KEYADC_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_keyadc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = allwinner_keyadc_reset;
    dc->vmsd = &allwinner_keyadc_vmstate;
}

static const TypeInfo allwinner_keyadc_info = {
    .name          = TYPE_AW_KEYADC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_keyadc_init,
    .instance_size = sizeof(AwKeyAdcState),
    .class_init    = allwinner_keyadc_class_init,
};

static void allwinner_keyadc_register(void)
{
    type_register_static(&allwinner_keyadc_info);
}

type_init(allwinner_keyadc_register)
