/*
 * Kendryte K230 GSDMA
 *
 * Copyright (c) 2026 Tao Ding <dingtao0430@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GSDMA includes SDMA (System Direct Memory Access, K230 TRM section 10.2) and
 * GDMA (Graphic Direct Memory Access, K230 TRM section 2.5.2).
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "hw/dma/k230_gsdma.h"
#include "hw/core/irq.h"
#include "trace.h"

static void k230_gsdma_update_irq(K230GSDMAState *s)
{
    qemu_set_irq(s->irq, !!(s->dma_int_stat & ~s->dma_int_mask));
}

static bool k230_gsdma_decomp_enabled(K230GSDMAState *s)
{
    /* Only sdma channel 0 has decomp enable flag */
    return !!(s->channels[0].cfg & K230_GSDMA_CH0_CFG_DECOMP_CTRL_EN);
}

static bool k230_gsdma_read_sdma_llt(hwaddr addr, K230GSDMALLT *llt)
{
    bool ok = address_space_read(&address_space_memory, addr,
                                 MEMTXATTRS_UNSPECIFIED, llt,
                                 sizeof(*llt)) == MEMTX_OK;

    if (ok) {
        trace_k230_gsdma_read_sdma_llt(addr, le32_to_cpu(llt->cfg),
                                       le32_to_cpu(llt->src_addr),
                                       le32_to_cpu(llt->line_size),
                                       le32_to_cpu(llt->line_cfg),
                                       le32_to_cpu(llt->dst_addr),
                                       le32_to_cpu(llt->next_llt_addr));
    } else {
        trace_k230_gsdma_read_sdma_llt_failed(addr);
    }

    return ok;
}

static unsigned int k230_gsdma_usr_data_size(uint32_t cfg)
{
    /* decode usr_dat_size in CH_CFG register */
    switch (extract32(cfg, K230_GSDMA_CH_CFG_USR_DATA_SIZE_SHIFT, 2)) {
    case 0:
        return 1;
    case 1:
        return 2;
    default:
        return 4;
    }
}

static void k230_gsdma_convert_endian(uint8_t *buf, uint32_t len, uint32_t cfg)
{
    unsigned int mode = extract32(cfg,
                                  K230_GSDMA_CH_CFG_DAT_ENDIAN_SHIFT, 2);
    unsigned int unit_size;
    unsigned int offset;

    if (mode == 0) {
        return;
    }

    unit_size = 1U << mode;
    for (offset = 0; offset + unit_size <= len; offset += unit_size) {
        unsigned int i;

        for (i = 0; i < unit_size / 2; i++) {
            uint8_t tmp = buf[offset + i];

            buf[offset + i] = buf[offset + unit_size - i - 1];
            buf[offset + unit_size - i - 1] = tmp;
        }
    }
}

static bool k230_gsdma_transfer_llt(K230GSDMAChannel *c, const K230GSDMALLT *llt)
{
    uint32_t llt_cfg = le32_to_cpu(llt->cfg);
    hwaddr src = le32_to_cpu(llt->src_addr);
    hwaddr dst = le32_to_cpu(llt->dst_addr);
    uint32_t line_size = le32_to_cpu(llt->line_size);
    uint32_t line_cfg = le32_to_cpu(llt->line_cfg);
    /* Number of lines required for 2d mode */
    uint32_t line_num =
            (llt_cfg & K230_GSDMA_LLT_2D_MODE) ? extract32(line_cfg, 0, 16) : 1;
    /* Stride line size required for 2d mode */
    uint32_t line_space = extract32(line_cfg, 16, 16);

    bool dat_mode = c->cfg & K230_GSDMA_CH_CFG_DAT_MODE;
    bool src_fixed = c->cfg & K230_GSDMA_CH_CFG_SRC_FIXED;
    bool dst_fixed = c->cfg & K230_GSDMA_CH_CFG_DST_FIXED;

    uint8_t buf[8]; /* K230 sdma axi data width is 64-bit */
    uint8_t fill[4];    /* Usr data is 32-bit*/
    unsigned int fill_len = k230_gsdma_usr_data_size(c->cfg);
    uint32_t i;

    memcpy(fill, &c->usr_data, sizeof(fill));

    for (i = 0; i < line_num; i++) {
        uint32_t remaining = line_size;
        hwaddr line_src = src;
        hwaddr line_dst = dst;

        while (remaining) {
            uint32_t chunk = MIN(remaining, (uint32_t)sizeof(buf));

            if (dat_mode) {
                uint32_t j;

                for (j = 0; j < chunk; j++) {
                    buf[j] = fill[j % fill_len];
                }
            } else if (address_space_read(&address_space_memory, line_src,
                                          MEMTXATTRS_UNSPECIFIED, buf,
                                          chunk) != MEMTX_OK) {
                return false;
            }

            k230_gsdma_convert_endian(buf, chunk, c->cfg);

            if (address_space_write(&address_space_memory, line_dst,
                                    MEMTXATTRS_UNSPECIFIED, buf,
                                    chunk) != MEMTX_OK) {
                return false;
            }

            if (!dat_mode && !src_fixed) {
                line_src += chunk;
            }
            if (!dst_fixed) {
                line_dst += chunk;
            }
            remaining -= chunk;
        }

        if (!dat_mode && !src_fixed) {
            if (llt_cfg & K230_GSDMA_LLT_2D_MODE) { /* 2d mode */
                src += line_size + line_space;
            } else {
                src += line_size;
            }
        }

        if (!dst_fixed) {
            dst += line_size;
        }
    }

    return true;
}

static void k230_gsdma_sdma_set_idle(K230GSDMAChannel *c)
{
    c->status &= ~(K230_GSDMA_SDMA_STATUS_BUSY | K230_GSDMA_SDMA_STATUS_PAUSE);
    c->next_llt = 0;
}

static void k230_gsdma_sdma_set_paused(K230GSDMAChannel *c, hwaddr next_llt)
{
    c->status &= ~K230_GSDMA_SDMA_STATUS_BUSY;
    c->status |= K230_GSDMA_SDMA_STATUS_PAUSE;
    c->next_llt = next_llt;
}

static void k230_gsdma_run_sdma(K230GSDMAState *s, unsigned int ch, hwaddr addr)
{
    K230GSDMAChannel *c = &s->channels[ch];

    c->status &= ~K230_GSDMA_SDMA_STATUS_PAUSE;
    c->status |= K230_GSDMA_SDMA_STATUS_BUSY;
    c->next_llt = 0;
    c->current_llt = addr;

    while (addr) {
        K230GSDMALLT llt;
        uint32_t cfg;
        hwaddr next;

        if (!k230_gsdma_read_sdma_llt(addr, &llt)) {
            k230_gsdma_sdma_set_idle(c);
            return;
        }

        cfg = le32_to_cpu(llt.cfg);
        next = le32_to_cpu(llt.next_llt_addr);
        c->current_llt = addr;

        if (!k230_gsdma_transfer_llt(c, &llt)) {
            k230_gsdma_sdma_set_idle(c);
            return;
        }

        if (cfg & K230_GSDMA_LLT_NODE_INTR) {
            s->dma_int_stat |= K230_GSDMA_SDMA_ITEM_INT(ch);
        }

        if (cfg & K230_GSDMA_LLT_PAUSE) {
            s->dma_int_stat |= K230_GSDMA_SDMA_PAUSE_INT(ch);
            k230_gsdma_sdma_set_paused(c, next);
            k230_gsdma_update_irq(s);
            return;
        }

        addr = next;
    }

    s->dma_int_stat |= K230_GSDMA_SDMA_DONE_INT(ch);
    s->dma_ch_en &= ~BIT(ch);
    k230_gsdma_sdma_set_idle(c);
    k230_gsdma_update_irq(s);
}

/* Decomp gzip request step sdma */
static void k230_gsdma_step_sdma(K230GSDMAState *s, unsigned int ch, hwaddr addr)
{
    K230GSDMAChannel *c = &s->channels[ch];
    K230GSDMALLT llt;
    uint32_t cfg;
    hwaddr next;

    if (!addr || !k230_gsdma_read_sdma_llt(addr, &llt)) {
        k230_gsdma_sdma_set_idle(c);
        return;
    }

    cfg = le32_to_cpu(llt.cfg);
    next = le32_to_cpu(llt.next_llt_addr);

    c->status |= K230_GSDMA_SDMA_STATUS_BUSY;
    c->status &= ~K230_GSDMA_SDMA_STATUS_PAUSE;
    c->current_llt = addr;

    if (!k230_gsdma_transfer_llt(c, &llt)) {
        k230_gsdma_sdma_set_idle(c);
        return;
    }

    if (cfg & K230_GSDMA_LLT_NODE_INTR) {
        s->dma_int_stat |= K230_GSDMA_SDMA_ITEM_INT(ch);
    }

    c->status &= ~K230_GSDMA_SDMA_STATUS_BUSY;
    c->next_llt = next;
    if (next == 0) {
        s->dma_int_stat |= K230_GSDMA_SDMA_DONE_INT(ch);
        s->dma_ch_en &= ~BIT(ch);
    }

    k230_gsdma_update_irq(s);
}

static const VMStateDescription vmstate_k230_gsdma_channel = {
    .name = "k230.gsdma.channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctl, K230GSDMAChannel),
        VMSTATE_UINT32(status, K230GSDMAChannel),
        VMSTATE_UINT32(cfg, K230GSDMAChannel),
        VMSTATE_UINT32(usr_data, K230GSDMAChannel),
        VMSTATE_UINT32(llt_saddr, K230GSDMAChannel),
        VMSTATE_UINT32(current_llt, K230GSDMAChannel),
        VMSTATE_UINT32(next_llt, K230GSDMAChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_k230_gsdma = {
    .name = "k230.gsdma",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(dma_ch_en, K230GSDMAState),
        VMSTATE_UINT32(dma_int_mask, K230GSDMAState),
        VMSTATE_UINT32(dma_int_stat, K230GSDMAState),
        VMSTATE_UINT32(dma_cfg, K230GSDMAState),
        VMSTATE_UINT32(dma_weight, K230GSDMAState),
        VMSTATE_STRUCT_ARRAY(channels, K230GSDMAState,
                             K230_GSDMA_NUM_SDMA_CHANNELS, 2,
                             vmstate_k230_gsdma_channel, K230GSDMAChannel),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t k230_gsdma_read(void *opaque, hwaddr addr, unsigned int size)
{
    K230GSDMAState *s = K230_GSDMA(opaque);
    unsigned int ch;
    hwaddr ch_off;
    uint64_t ret = 0;

    switch (addr) {
    case K230_GSDMA_DMA_CH_EN:
        ret = s->dma_ch_en;
        break;
    case K230_GSDMA_DMA_INT_MASK:
        ret = s->dma_int_mask;
        break;
    case K230_GSDMA_DMA_INT_STAT:
        ret = s->dma_int_stat;
        break;
    case K230_GSDMA_DMA_CFG:
        ret = s->dma_cfg;
        break;
    case K230_GSDMA_DMA_WEIGHT:
        ret = s->dma_weight;
        break;
    default:
        if (addr < K230_GSDMA_CH_BASE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: not implement gdma at offset 0x%" HWADDR_PRIx
                        "\n", __func__, addr);
        }
        break;
    }

    ch = (addr - K230_GSDMA_CH_BASE) / K230_GSDMA_CH_STRIDE;
    ch_off = (addr - K230_GSDMA_CH_BASE) % K230_GSDMA_CH_STRIDE;
    if (ch < K230_GSDMA_NUM_SDMA_CHANNELS) {
        switch (ch_off) {
        case K230_GSDMA_CH_CTL:
            break;
        case K230_GSDMA_CH_STATUS:
            ret = s->channels[ch].status;
            break;
        case K230_GSDMA_CH_CFG:
            ret = s->channels[ch].cfg;
            break;
        case K230_GSDMA_CH_USR_DATA:
            ret = s->channels[ch].usr_data;
            break;
        case K230_GSDMA_CH_LLT_SADDR:
            ret = s->channels[ch].llt_saddr;
            break;
        case K230_GSDMA_CH_CURRENT_LLT:
            ret = s->channels[ch].current_llt;
            break;
        default:
            break;
        }
    }

    trace_k230_gsdma_read(addr, size, ret);
    return ret;
}

static void k230_gsdma_write_channel(K230GSDMAState *s, unsigned int ch,
                                     hwaddr ch_off, uint32_t value)
{
    K230GSDMAChannel *c = &s->channels[ch];
    bool handshake_mode = ch < 2 && k230_gsdma_decomp_enabled(s);

    switch (ch_off) {
    case K230_GSDMA_CH_CTL:
        c->ctl = value;
        if ((value & K230_GSDMA_CTL_STOP) != 0) {
            c->started = false;
            k230_gsdma_sdma_set_idle(c);
            break;
        }
        if ((value & K230_GSDMA_CTL_RESUME) != 0 &&
            (c->status & K230_GSDMA_SDMA_STATUS_PAUSE) && c->next_llt != 0) {
            k230_gsdma_run_sdma(s, ch, c->next_llt);
            break;
        }
        if (handshake_mode && (value & K230_GSDMA_CTL_START) != 0) {
            c->started = true;
            c->status &= ~(K230_GSDMA_SDMA_STATUS_BUSY |
                           K230_GSDMA_SDMA_STATUS_PAUSE);
            c->current_llt = 0;
            c->next_llt = 0;
            break;
        }
        if ((value & K230_GSDMA_CTL_START) != 0 &&
            (s->dma_ch_en & BIT(ch)) && c->llt_saddr != 0) {
            c->started = true;
            k230_gsdma_run_sdma(s, ch, c->llt_saddr);
        }
        break;
    case K230_GSDMA_CH_CFG:
        c->cfg = value;
        break;
    case K230_GSDMA_CH_USR_DATA:
        c->usr_data = value;
        break;
    case K230_GSDMA_CH_LLT_SADDR:
        c->llt_saddr = value;
        break;
    case K230_GSDMA_CH_CURRENT_LLT:
        break;
    default:
        break;
    }
}

static void k230_gsdma_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned int size)
{
    K230GSDMAState *s = K230_GSDMA(opaque);
    unsigned int ch;
    hwaddr ch_off;
    uint32_t v = value;

    trace_k230_gsdma_write(addr, size, value);

    switch (addr) {
    case K230_GSDMA_DMA_CH_EN:
        s->dma_ch_en = v & K230_GSDMA_DMA_CH_EN_MASK;
        return;
    case K230_GSDMA_DMA_INT_MASK:
        s->dma_int_mask = v;
        k230_gsdma_update_irq(s);
        return;
    case K230_GSDMA_DMA_INT_STAT:
        s->dma_int_stat &= ~v;
        k230_gsdma_update_irq(s);
        return;
    case K230_GSDMA_DMA_CFG:
        s->dma_cfg = v;
        return;
    case K230_GSDMA_DMA_WEIGHT:
        s->dma_weight = v & 0x00ffffff;
        return;
    default:
        break;
    }

    if (addr < K230_GSDMA_CH_BASE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: not implement gdma at offset 0x%" HWADDR_PRIx
                      "\n", __func__, addr);
        return;
    }

    ch = (addr - K230_GSDMA_CH_BASE) / K230_GSDMA_CH_STRIDE;
    ch_off = (addr - K230_GSDMA_CH_BASE) % K230_GSDMA_CH_STRIDE;
    if (ch >= K230_GSDMA_NUM_SDMA_CHANNELS) {
        return;
    }

    k230_gsdma_write_channel(s, ch, ch_off, v);
}

static void k230_gsdma_handle_signal(void *opaque, int n, int level)
{
    K230GSDMAState *s = opaque;
    K230GSDMAChannel *c;
    hwaddr addr;
    unsigned int ch;

    if (!level || n == K230_GSDMA_GPIO_DECOMP_CTRL_EN) {
        return;
    }

    ch = n - 1;

    c = &s->channels[ch];
    if (!k230_gsdma_decomp_enabled(s) || !(s->dma_ch_en & BIT(ch)) || !c->started) {
        return;
    }

    if (c->current_llt == 0) {
        addr = c->llt_saddr;
    } else {
        addr = c->next_llt;
    }
    if (!addr) {
        return;
    }

    k230_gsdma_step_sdma(s, ch, addr);
    qemu_set_irq(s->handshake_out[n - 1], 1);
    qemu_set_irq(s->handshake_out[n - 1], 0);
}

static const MemoryRegionOps k230_gsdma_ops = {
    .read = k230_gsdma_read,
    .write = k230_gsdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void k230_gsdma_reset_hold(Object *obj, ResetType type)
{
    K230GSDMAState *s = K230_GSDMA(obj);

    memset(s->channels, 0, sizeof(s->channels));
    s->dma_ch_en = 0;
    s->dma_int_mask = 0;
    s->dma_int_stat = 0;
    s->dma_cfg = K230_GSDMA_DMA_CFG_RESET;
    s->dma_weight = 0;
}

static void k230_gsdma_realize(DeviceState *dev, Error **errp)
{
    K230GSDMAState *s = K230_GSDMA(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &k230_gsdma_ops, s,
                          TYPE_K230_GSDMA, K230_GSDMA_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in(DEVICE(dev), k230_gsdma_handle_signal,
                      K230_GSDMA_NUM_GPIOS_IN);
    qdev_init_gpio_out(DEVICE(dev), s->handshake_out,
                       K230_GSDMA_NUM_GPIOS_OUT);
}

static void k230_gsdma_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = k230_gsdma_realize;
    rc->phases.hold = k230_gsdma_reset_hold;
    dc->vmsd = &vmstate_k230_gsdma;
    dc->desc = "Kendryte K230 GSDMA";
}

static const TypeInfo k230_gsdma_info = {
    .name = TYPE_K230_GSDMA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230GSDMAState),
    .class_init = k230_gsdma_class_init,
};

static void k230_gsdma_register_types(void)
{
    type_register_static(&k230_gsdma_info);
}

type_init(k230_gsdma_register_types)
