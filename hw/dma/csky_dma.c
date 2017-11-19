/*
 * CSKY DMA controller
 *
 * Written by wanghb <huibin_wang@c-sky.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"

#define NR_DMA_CHAN       4   /* the total number of DMA channels */

#define TYPE_CSKY_DMA   "csky_dma"
#define CSKY_DMA(obj)   OBJECT_CHECK(csky_dma_state, (obj), TYPE_CSKY_DMA)

typedef struct {
    uint32_t src;
    uint32_t dest;
    uint32_t ctrl[2];
    uint32_t conf[2];
    int chan_enable;
} csky_dma_channel;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    int dma_enable;
    uint32_t tfr_int;
    uint32_t block_int;
    uint32_t srctran_int;
    uint32_t dsttran_int;
    uint32_t err_int;
    uint32_t tfr_int_mask;
    uint32_t block_int_mask;
    uint32_t srctran_int_mask;
    uint32_t dsttran_int_mask;
    uint32_t err_int_mask;
    uint32_t status_int;
    csky_dma_channel chan[NR_DMA_CHAN];
} csky_dma_state;

static const VMStateDescription vmstate_csky_dma_channel = {
    .name = "csky_dma_channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(src, csky_dma_channel),
        VMSTATE_UINT32(dest, csky_dma_channel),
        VMSTATE_UINT32_ARRAY(ctrl, csky_dma_channel, 2),
        VMSTATE_UINT32_ARRAY(conf, csky_dma_channel, 2),
        VMSTATE_INT32(chan_enable, csky_dma_channel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_csky_dma = {
    .name = "csky_dma",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(tfr_int, csky_dma_state),
        VMSTATE_UINT32(block_int, csky_dma_state),
        VMSTATE_UINT32(srctran_int, csky_dma_state),
        VMSTATE_UINT32(dsttran_int, csky_dma_state),
        VMSTATE_UINT32(err_int, csky_dma_state),
        VMSTATE_UINT32(tfr_int_mask, csky_dma_state),
        VMSTATE_UINT32(block_int_mask, csky_dma_state),
        VMSTATE_UINT32(srctran_int_mask, csky_dma_state),
        VMSTATE_UINT32(dsttran_int_mask, csky_dma_state),
        VMSTATE_UINT32(err_int_mask, csky_dma_state),
        VMSTATE_UINT32(status_int, csky_dma_state),
        VMSTATE_STRUCT_ARRAY(chan, csky_dma_state, NR_DMA_CHAN,
                             1, vmstate_csky_dma_channel, csky_dma_channel),
        VMSTATE_END_OF_LIST()
    }
};

/**************************************************************************
 * Description:
 *     Update the interrupt flag according the DMA state
 *     and give the flag to interrupt controller.
 * Argument:
 *     s  --- the pointer to the DMA state
 * Return:
 *     void
 **************************************************************************/
static void csky_dma_update(csky_dma_state *s)
{
    if (s->err_int & s->err_int_mask) {
        s->status_int |= 1 << 4;
    } else {
        s->status_int &= ~(1 << 4);
    }

    if (s->dsttran_int & s->dsttran_int_mask) {
        s->status_int |= 1 << 3;
    } else {
        s->status_int &= ~(1 << 3);
    }

    if (s->srctran_int & s->srctran_int_mask) {
        s->status_int |= 1 << 2;
    } else {
        s->status_int &= ~(1 << 2);
    }

    if (s->block_int & s->block_int_mask) {
        s->status_int |= 1 << 1;
    } else {
        s->status_int &= ~(1 << 1);
    }

    if (s->tfr_int & s->tfr_int_mask) {
        s->status_int |= 1 << 0;
    } else {
        s->status_int &= ~(1 << 0);
    }

    if (s->status_int) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

/**************************************************************************
 * Description:
 *     DMAC register read function.
 * Argument:
 *     s  --- the pointer to the DMA state
 *     offset -- the address offset of the register
 * Return:
 *     the value of the corresponding register
 **************************************************************************/
static uint64_t csky_dma_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_dma_state *s = (csky_dma_state *) opaque;
    unsigned int channel;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: 0x%x must word align read\n",
                      __func__, (int)offset);
    }

    switch (offset) {
    case 0x000 ... 0x14c:
        channel = offset / 0x58;
        switch (offset % 0x58) {
        case 0x0:
            ret = s->chan[channel].src;
            break;
        case 0x8:
            ret = s->chan[channel].dest;
            break;
        case 0x18:
            ret = s->chan[channel].ctrl[0];
            break;
        case 0x1c:
            ret = s->chan[channel].ctrl[1];
            break;
        case 0x40:
            ret = s->chan[channel].conf[0] | (1 << 9);
            break;
        case 0x44:
            ret = s->chan[channel].conf[1];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n", __func__, (int)offset);
            break;
        }
        break;
    case 0x2c0:
        ret = s->tfr_int;
        break;
    case 0x2c8:
        ret = s->block_int;
        break;
    case 0x2d0:
        ret = s->srctran_int;
        break;
    case 0x2d8:
        ret = s->dsttran_int;
        break;
    case 0x2e0:
        ret = s->err_int;
        break;
    case 0x2e8:
        ret = s->tfr_int & s->tfr_int_mask;
        break;
    case 0x2f0:
        ret = s->block_int & s->block_int_mask;
        break;
    case 0x2f8:
        ret = s->srctran_int & s->srctran_int_mask;
        break;
    case 0x300:
        ret = s->dsttran_int & s->dsttran_int_mask;
        break;
    case 0x308:
        ret = s->err_int & s->err_int_mask;
        break;
    case 0x310:
        ret = s->tfr_int_mask;
        break;
    case 0x318:
        ret = s->block_int_mask;
        break;
    case 0x320:
        ret = s->srctran_int_mask;
        break;
    case 0x328:
        ret = s->dsttran_int_mask;
        break;
    case 0x330:
        ret = s->err_int_mask;
        break;
    case 0x338:
    case 0x340:
    case 0x348:
    case 0x350:
    case 0x358:
        break;
    case 0x360:
        ret = s->status_int;
        break;
    case 0x368:
    case 0x370:
    case 0x378:
    case 0x380:
    case 0x388:
    case 0x390:
        break;
    case 0x398:
        ret = s->dma_enable;
        break;
    case 0x3a0:
        ret = (s->chan[3].chan_enable << 3)
            | (s->chan[2].chan_enable << 2)
            | (s->chan[1].chan_enable << 1)
            | (s->chan[0].chan_enable << 0);
        break;
    case 0x3b0:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n", __func__, (int)offset);
        break;
    }
    return ret;
}

/**************************************************************************
 * Description:
 *     DMAC register write function.
 * Argument:
 *     s  --- the pointer to the DMA state
 *     offset -- the address offset of the register
 *     value  -- the value that will be written
 * Return:
 *     void
 **************************************************************************/
static void csky_dma_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned size)
{
    csky_dma_state *s = (csky_dma_state *) opaque;
    unsigned int channel;
    uint32_t tmp;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: 0x%x must word align read\n",
                      __func__, (int)offset);
    }

    switch (offset) {
    case 0x000 ... 0x14c:
        channel = offset / 0x58;
        switch (offset % 0x58) {
        case 0x0:
            s->chan[channel].src = value;
            break;
        case 0x8:
            s->chan[channel].dest = value;
            break;
        case 0x18:
            s->chan[channel].ctrl[0] = value;
            break;
        case 0x1c:
            s->chan[channel].ctrl[1] = value;
            break;
        case 0x40:
            s->chan[channel].conf[0] = value;
            break;
        case 0x44:
            s->chan[channel].conf[1] = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n", __func__, (int)offset);
            break;
        }
        break;
    case 0x2c0:
    case 0x2c8:
    case 0x2d0:
    case 0x2d8:
    case 0x2e0:
    case 0x2e8:
    case 0x2f0:
    case 0x2f8:
    case 0x300:
    case 0x308:
        break;
    case 0x310:
        s->tfr_int_mask = ((value & 0x0f00) >> 8) & (value & 0xf);
        break;
    case 0x318:
        s->block_int_mask = ((value & 0x0f00) >> 8) & (value & 0xf);
        break;
    case 0x320:
        s->srctran_int_mask = ((value & 0x0f00) >> 8) & (value & 0xf);
        break;
    case 0x328:
        s->dsttran_int_mask = ((value & 0x0f00) >> 8) & (value & 0xf);
        break;
    case 0x330:
        s->err_int_mask = ((value & 0x0f00) >> 8) & (value & 0xf);
        break;
    case 0x338:
        s->tfr_int &= ~value;
        break;
    case 0x340:
        s->block_int &= ~value;
        break;
    case 0x348:
        s->srctran_int &= ~value;
        break;
    case 0x350:
        s->dsttran_int &= ~value;
        break;
    case 0x358:
        s->err_int &= ~value;
        break;
    case 0x360:
        break;
    case 0x368:
    case 0x370:
    case 0x378:
    case 0x380:
    case 0x388:
    case 0x390:
        break;
    case 0x398:
        s->dma_enable = value & 0x1;
        break;
    case 0x3a0:
        tmp = ((value & 0x0f00) >> 8) & (value & 0xf);
        s->chan[3].chan_enable = tmp >> 3;
        s->chan[2].chan_enable = tmp >> 2;
        s->chan[1].chan_enable = tmp >> 1;
        s->chan[0].chan_enable = tmp >> 0;
        break;
    case 0x3b0:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n", __func__, (int)offset);
        break;
    }
    csky_dma_update(s);
}

static const MemoryRegionOps csky_dma_ops = {
    .read = csky_dma_read,
    .write = csky_dma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void csky_dma_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    csky_dma_state *s = CSKY_DMA(obj);
    int i;

    for (i = 0; i < NR_DMA_CHAN; i++) {
        s->chan[i].ctrl[1] = 0x2;
        s->chan[i].conf[0] = 0xe00;
        s->chan[i].conf[1] = 0x4;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_dma_ops, s,
                          TYPE_CSKY_DMA, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}
/*
csky_dma_state *csky_dma_create(const char *name, hwaddr addr, qemu_irq irq)
{
    DeviceState *dev;
    csky_dma_state *s;

    dev = sysbus_create_simple(name, addr, irq);
    s = FROM_SYSBUS(csky_dma_state, (SysBusDevice *)(dev));
    return s;
}
*/
static void csky_dma_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_csky_dma;
}

static const TypeInfo csky_dma_info = {
    .name          = TYPE_CSKY_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_dma_state),
    .instance_init = csky_dma_init,
    .class_init    = csky_dma_class_init,
};

static void csky_dma_register_types(void)
{
    type_register_static(&csky_dma_info);
}

type_init(csky_dma_register_types)
