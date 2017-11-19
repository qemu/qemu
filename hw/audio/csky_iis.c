/*
 * Csky i2s emulation.
 * Written by wanghb
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
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "audio/audio.h"
#include "hw/dma/csky_dma.h"
#include "qemu/log.h"

#define RX_MODE 0x0
#define TX_MODE 0x1

#define RX_FIFO_FULL (1 << 5)
#define RX_FIFO_NOT_EMPTY (1 << 4)

#define RX_FIFO_UNDERFLOW (1 << 2)
#define TX_FIFO_OVERFLOW (1 << 1)
#define TX_FIFO_EMPTY (1 << 0)

#define TX_FIFO_ENTRY   0x10000

#define TYPE_CSKY_IIS   "csky_iis"
#define CSKY_IIS(obj)   OBJECT_CHECK(csky_iis_state, (obj), TYPE_CSKY_IIS)

static int csky_dma_can_work(csky_dma_state *s);
static void csky_dma_update(csky_dma_state *s);

typedef struct {
    QEMUSoundCard card;
    SWVoiceOut *out_voice;
    uint8_t tx_fifo[TX_FIFO_ENTRY];
    int read_pos;
    int write_pos;
    int len;
} csky_codec_state;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    int enable;
    int func_mode;
    uint32_t iis_cnf_in;
    uint32_t fssta;
    uint32_t iis_cnf_out;
    uint32_t fadtlr;
    uint32_t compress_ctrl;
    uint32_t tx_fifo_thr;
    uint32_t rx_fifo_thr;
    uint32_t status;
    uint32_t int_mask;
    uint32_t raw_int_status;
    uint32_t dma_ctrl;
    uint32_t dma_tx_data_lvl;
    uint32_t dma_rx_data_lvl;
    uint32_t mode_int_mask;
    uint32_t raw_mode_int_status;

    csky_codec_state codec;

    csky_dma_state *dma;
} csky_iis_state;

static const VMStateDescription vmstate_csky_codec = {
    .name = "csky_codec",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(tx_fifo, csky_codec_state, TX_FIFO_ENTRY),
        VMSTATE_INT32(read_pos, csky_codec_state),
        VMSTATE_INT32(write_pos, csky_codec_state),
        VMSTATE_INT32(len, csky_codec_state),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_csky_iis = {
    .name = TYPE_CSKY_IIS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(enable, csky_iis_state),
        VMSTATE_INT32(func_mode, csky_iis_state),
        VMSTATE_UINT32(iis_cnf_in, csky_iis_state),
        VMSTATE_UINT32(fadtlr, csky_iis_state),
        VMSTATE_UINT32(compress_ctrl, csky_iis_state),
        VMSTATE_UINT32(tx_fifo_thr, csky_iis_state),
        VMSTATE_UINT32(rx_fifo_thr, csky_iis_state),
        VMSTATE_UINT32(status, csky_iis_state),
        VMSTATE_UINT32(int_mask, csky_iis_state),
        VMSTATE_UINT32(raw_int_status, csky_iis_state),
        VMSTATE_UINT32(dma_ctrl, csky_iis_state),
        VMSTATE_UINT32(dma_tx_data_lvl, csky_iis_state),
        VMSTATE_UINT32(dma_tx_data_lvl, csky_iis_state),
        VMSTATE_UINT32(mode_int_mask, csky_iis_state),
        VMSTATE_UINT32(raw_mode_int_status, csky_iis_state),

        VMSTATE_STRUCT(codec, csky_iis_state, 0,
                       vmstate_csky_codec, csky_codec_state),
        VMSTATE_END_OF_LIST()
    }
};

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

static int csky_dma_can_work(csky_dma_state *s)
{
    return s->dma_enable
        && (s->chan[0].chan_enable
            || s->chan[1].chan_enable
            || s->chan[2].chan_enable
            || s->chan[3].chan_enable);
}


static void csky_iis_set_format(csky_iis_state *s);

/**************************************************************************
 * Description:
 *     Update the interrupt flag according the IIS state
 *     and give the flag to interrupt controller.
 * Argument:
 *     s  --- the pointer to the IIS state
 * Return:
 *     void
 **************************************************************************/
static void csky_iis_update(csky_iis_state *s)
{
    /* Update interrupts.  */
    int int_req;

    int_req = (s->raw_int_status & s->int_mask)
        || (s->raw_mode_int_status & s->mode_int_mask);
    qemu_set_irq(s->irq, int_req);
}

/**************************************************************************
 * Description:
 *     IIS controller register read function.
 * Argument:
 *     opaque  --- the pointer to the IIS state
 *     offset  --- the address offset of the register
 * Return:
 *     the value of the corresponding register
 **************************************************************************/
static uint64_t csky_iis_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t ret = 0;
    csky_iis_state *s = (csky_iis_state *)opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_iis_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x0:
        ret = s->enable;
        break;
    case 0x4:
        ret = s->func_mode;
        break;
    case 0x8:
        ret = s->iis_cnf_in;
        break;
    case 0xc:
        ret = s->fssta;
        break;
    case 0x10:
        ret = s->iis_cnf_out;
        break;
    case 0x14:
        ret = s->fadtlr;
        break;
    case 0x18:
        ret = s->compress_ctrl;
        break;
    case 0x1c:
        ret = s->tx_fifo_thr;
        break;
    case 0x20:
        ret = s->rx_fifo_thr;
        break;
    case 0x24:   /* tx fifo data level */
        ret = 0;
        break;
    case 0x28:   /* rx fifo data level */
        ret = 1;
        break;
    case 0x2c:
        ret = s->status;
        break;
    case 0x30:
        ret = s->int_mask;
        break;
    case 0x34:
        ret = s->raw_int_status & s->int_mask;
        break;
    case 0x38:
        ret = s->raw_int_status;
        break;
    case 0x3c:
        ret = 0;
        break;
    case 0x4c:
        ret = s->dma_ctrl;
        break;
    case 0x50:
        ret = s->dma_tx_data_lvl;
        break;
    case 0x54:
        ret = s->dma_rx_data_lvl;
        break;
    case 0x60:
        ret = 0;
        break;
    case 0x70:
    case 0x74:
    case 0x78:
    case 0x7c:
        ret = 0;
        break;
    case 0x80:
        ret = s->mode_int_mask;
        break;
    case 0x84:
        ret = s->raw_mode_int_status & s->mode_int_mask;
        break;
    case 0x88:
        ret = s->raw_mode_int_status;
        break;
    case 0x8c:
        ret = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_iis_read: Bad offset %x\n", (int)offset);
        break;
    }

    return ret;
}


/**************************************************************************
 * Description:
 *     IIS controller register write function.
 * Argument:
 *     opaque  --- the pointer to the IIS state
 *     offset  --- the address offset of the register
 *     value   --- the value that will be written
 * Return:
 *     void
 **************************************************************************/
static void csky_iis_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    csky_iis_state *s = (csky_iis_state *)opaque;
    uint32_t oldval;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_iis_write: 0x%x must word align write\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x0:
        oldval = s->enable;
        s->enable = (value & 0x1);
        if (!oldval && s->enable) {
            s->status = 0xc;
            if (s->func_mode == RX_MODE) {
                s->status |= 1 << 0;
                s->raw_mode_int_status |= 1 << 0;
            } else {
                s->status |= 1 << 1;
                s->raw_int_status |= 1 << 0;
                s->raw_mode_int_status |= 1 << 1;
            }
            if ((s->fssta & 0x1) == 0x1) {
                s->fssta |= 0x1 << 4;
            }
            csky_iis_set_format(s);
        } else if (oldval && !s->enable) {
            s->status &= ~0x3;
            if (s->func_mode == RX_MODE) {
                s->raw_mode_int_status |= 1 << 0;
            } else {
                s->raw_mode_int_status |= 1 << 1;
            }
        }
        break;
    case 0x4:
        if (s->enable) {
            break;
        }
        if ((value & 0x2) == 0) {
            break;
        }
        s->func_mode = value & 0x1;
        break;
    case 0x8:
        if (s->enable) {
            break;
        }
        s->iis_cnf_in = value & 0x117;
        break;
    case 0xc:
        s->fssta = value & 0x7;
        if ((s->fssta & 0x1) == 0) {
            s->fssta |= value & 0xf0;
        }
        break;
    case 0x10:
        if (s->enable) {
            break;
        }
        s->iis_cnf_out = value & 0x1f;
        break;
    case 0x14:
        if (s->enable) {
            break;
        }
        s->fadtlr = value;
        break;
    case 0x18:
        s->compress_ctrl = value;
        break;
    case 0x1c:
        if (s->enable) {
            break;
        }
        s->tx_fifo_thr = value & 0x1f;
        break;
    case 0x20:
        if (s->enable) {
            break;
        }
        s->rx_fifo_thr = value & 0x1f;
        break;
    case 0x24:
        break;
    case 0x28:
        break;
    case 0x2c:
        break;
    case 0x30:
        s->int_mask = value & 0x1f;
        break;
    case 0x34:
        break;
    case 0x38:
        break;
    case 0x3c:
        s->raw_int_status &= ~value;
        break;
    case 0x4c:
        s->dma_ctrl = value & 0x3;
        break;
    case 0x50:
        s->dma_tx_data_lvl = value & 0x1f;
        break;
    case 0x54:
        s->dma_rx_data_lvl = value & 0x1f;
        break;
    case 0x60:
        *((uint32_t *)&s->codec.tx_fifo[s->codec.write_pos]) = value;
        s->codec.write_pos = (s->codec.write_pos + 4) % TX_FIFO_ENTRY;
        s->codec.len += 4;
        s->raw_int_status &= ~TX_FIFO_EMPTY;
        if (s->codec.len >= TX_FIFO_ENTRY) {
            s->raw_int_status |= TX_FIFO_OVERFLOW;
        }
        break;
    case 0x70:  /* spdif registers have not been implemented yet */
    case 0x74:
    case 0x78:
    case 0x7c:
        break;
    case 0x80:
        s->mode_int_mask = value & 0x3f;
        break;
    case 0x84:
        break;
    case 0x88:
        break;
    case 0x8c:
        s->raw_mode_int_status &= ~value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_iis_write: Bad offset 0x%x\n", (int)offset);
    }
    csky_iis_update(s);
}

static const MemoryRegionOps csky_iis_ops = {
    .read = csky_iis_read,
    .write = csky_iis_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/**************************************************************************
 * Description:
 *     Set the CODEC volume
 * Argument:
 *     s -- the pointer to the CODEC state
 * Return:
 *     void
 **************************************************************************/
static void csky_codec_set_volume(csky_codec_state *s)
{
    AUD_set_volume_out(s->out_voice, 0, 0xff, 0xff);
    return;
}

static int csky_find_dma_chan_id(csky_dma_state *s)
{
    int i;
    int find = 0;
    for (i = 0; i < 4; i++) {
        if (s->chan[i].dest == 0x1001b060) {
            find = 1;
            break;
        }
    }
    if (find) {
        return i;
    }
    printf("cannot find coresponding DMA channel\n");
    exit(1);
}

static int csky_iis_copy_from_dma(csky_iis_state *s)
{
    int dma_chan_id;
    int len;

    dma_chan_id = csky_find_dma_chan_id(s->dma);
    len = (s->dma->chan[dma_chan_id].ctrl[1])
        << ((s->dma->chan[dma_chan_id].ctrl[0] >> 4) & 0x7);
    if (s->codec.len + len > TX_FIFO_ENTRY) {
        return 0;
    }

    hwaddr source;
    source = s->dma->chan[dma_chan_id].src;
    if (s->codec.write_pos + len < TX_FIFO_ENTRY) {
        cpu_physical_memory_read(source,
                                 &s->codec.tx_fifo[s->codec.write_pos],
                                 len);
    } else {
        cpu_physical_memory_read(source,
                                 &s->codec.tx_fifo[s->codec.write_pos],
                                 TX_FIFO_ENTRY - s->codec.write_pos);
        cpu_physical_memory_read(
                                 source + TX_FIFO_ENTRY - s->codec.write_pos,
                                 &s->codec.tx_fifo[0],
                                 len + s->codec.write_pos - TX_FIFO_ENTRY);
    }
    s->codec.write_pos = (s->codec.write_pos + len) % TX_FIFO_ENTRY;
    s->codec.len += len;

    s->dma->tfr_int     |= 1 << dma_chan_id;
    s->dma->block_int   |= 1 << dma_chan_id;
    csky_dma_update(s->dma);

    return len;
}

static inline void csky_audio_out_flush(csky_codec_state *codec,
                                        int out_pos, int out_len)
{
    int sent = 0;
    while (sent < out_len) {
        sent += AUD_write(codec->out_voice,
                          &codec->tx_fifo[out_pos],
                          out_len - sent) ?: out_len;
    }
}

static void csky_audio_out_cb(void *opaque, int free_b)
{
    csky_iis_state *s = opaque;
    csky_codec_state *codec = &s->codec;

    if (codec->len > free_b) {
        if (codec->read_pos + free_b < TX_FIFO_ENTRY) {
            csky_audio_out_flush(codec, codec->read_pos, free_b);
        } else {
            csky_audio_out_flush(codec, codec->read_pos,
                                 TX_FIFO_ENTRY - codec->read_pos);
            csky_audio_out_flush(codec, 0,
                                 free_b + codec->read_pos - TX_FIFO_ENTRY);
        }
        codec->read_pos = (codec->read_pos + free_b) % TX_FIFO_ENTRY;
        codec->len = codec->len - free_b;
    }
    if (csky_dma_can_work(s->dma)) {
        csky_iis_copy_from_dma(s);
    }
    return;
}

static inline int csky_iis_get_freq(csky_iis_state *s)
{
    int ars, afr;

    ars = (s->fssta >> 6) & 0x3;
    afr = (s->fssta >> 4) & 0x3;
    switch (afr) {
    case 0:
        return 44100 >> ars;
    case 1:
        return 48000 >> ars;
    case 2:
        return 32000 >> ars;
    case 3:
        return 96000;
    default:
        return -1;
    }
}

/**************************************************************************
 * Description:
 *     Set the CODEC control format
 * Argument:
 *     s -- the pointer to the CODEC state
 * Return:
 *     void
 **************************************************************************/
static void csky_iis_set_format(csky_iis_state *s)
{
    csky_codec_state *codec = &s->codec;
    struct audsettings fmt;

    if (codec->out_voice) {
        AUD_set_active_out(codec->out_voice, 0);
        AUD_close_out(&codec->card, codec->out_voice);
        codec->out_voice = NULL;
    }

    fmt.endianness = 0;
    fmt.nchannels = 2;
    fmt.freq = csky_iis_get_freq(s);
    fmt.fmt = AUD_FMT_S16;

    codec->out_voice = AUD_open_out(&codec->card, codec->out_voice,
                                    "csky.codec.out", s,
                                    csky_audio_out_cb, &fmt);

    csky_codec_set_volume(codec);

    AUD_set_active_out(codec->out_voice, 1);
}

/**************************************************************************
 * Description:
 *     Reset the IIS controller.
 * Argument:
 *     s -- the pointer to the IIS state
 * Return:
 *     void
 **************************************************************************/
static void csky_iis_reset(csky_iis_state *s)
{
    s->enable = 0;
    s->tx_fifo_thr = 0x10;
    s->rx_fifo_thr = 0x8;
    s->status = 0xc;
    s->int_mask = 0x1f;
    s->dma_tx_data_lvl = 0x7;
}

static void csky_iis_device_reset(DeviceState *d)
{
    csky_iis_state *s = CSKY_IIS(d);

    csky_iis_reset(s);
}

/**************************************************************************
 * Description:
 *     Initialize the IIS controller.
 * Argument:
 *     dev -- the pointer to a system bus device
 * Return:
 *     success
 **************************************************************************/
static int csky_iis_init(SysBusDevice *dev)
{
    csky_iis_state *s = CSKY_IIS(dev);

    AUD_register_card("csky codec", &s->codec.card);

    csky_iis_reset(s);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_iis_ops, s,
                          TYPE_CSKY_IIS, 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);

    return 0;
}

/*
void csky_iis_create(const char *name, hwaddr addr, qemu_irq irq,
                     csky_dma_state *dma)
{
    DeviceState *dev;
    csky_iis_state *s;

    dev = sysbus_create_simple(name, addr, irq);
    s = FROM_SYSBUS(csky_iis_state, (SysBusDevice *)(dev));
    s->dma = dma;
}
*/

static void csky_iis_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = csky_iis_init;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->reset = csky_iis_device_reset;
    dc->vmsd = &vmstate_csky_iis;
}

static const TypeInfo csky_iis_device_info = {
    .name          = TYPE_CSKY_IIS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_iis_state),
    .class_init    = csky_iis_class_init,
};

static void csky_iis_register_types(void)
{
    type_register_static(&csky_iis_device_info);
}

type_init(csky_iis_register_types)
