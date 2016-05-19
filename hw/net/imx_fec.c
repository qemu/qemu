/*
 * i.MX Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2013 Jean-Christophe Dubois. <jcd@tribudubois.net>
 *
 * Based on Coldfire Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/net/imx_fec.h"
#include "sysemu/dma.h"
#include "qemu/log.h"

/* For crc32 */
#include <zlib.h>

#ifndef DEBUG_IMX_FEC
#define DEBUG_IMX_FEC 0
#endif

#define FEC_PRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_FEC) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_FEC, \
                                             __func__, ##args); \
        } \
    } while (0)

#ifndef DEBUG_IMX_PHY
#define DEBUG_IMX_PHY 0
#endif

#define PHY_PRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_PHY) { \
            fprintf(stderr, "[%s.phy]%s: " fmt , TYPE_IMX_FEC, \
                                                 __func__, ##args); \
        } \
    } while (0)

static const VMStateDescription vmstate_imx_fec = {
    .name = TYPE_IMX_FEC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(irq_state, IMXFECState),
        VMSTATE_UINT32(eir, IMXFECState),
        VMSTATE_UINT32(eimr, IMXFECState),
        VMSTATE_UINT32(rx_enabled, IMXFECState),
        VMSTATE_UINT32(rx_descriptor, IMXFECState),
        VMSTATE_UINT32(tx_descriptor, IMXFECState),
        VMSTATE_UINT32(ecr, IMXFECState),
        VMSTATE_UINT32(mmfr, IMXFECState),
        VMSTATE_UINT32(mscr, IMXFECState),
        VMSTATE_UINT32(mibc, IMXFECState),
        VMSTATE_UINT32(rcr, IMXFECState),
        VMSTATE_UINT32(tcr, IMXFECState),
        VMSTATE_UINT32(tfwr, IMXFECState),
        VMSTATE_UINT32(frsr, IMXFECState),
        VMSTATE_UINT32(erdsr, IMXFECState),
        VMSTATE_UINT32(etdsr, IMXFECState),
        VMSTATE_UINT32(emrbr, IMXFECState),
        VMSTATE_UINT32(miigsk_cfgr, IMXFECState),
        VMSTATE_UINT32(miigsk_enr, IMXFECState),

        VMSTATE_UINT32(phy_status, IMXFECState),
        VMSTATE_UINT32(phy_control, IMXFECState),
        VMSTATE_UINT32(phy_advertise, IMXFECState),
        VMSTATE_UINT32(phy_int, IMXFECState),
        VMSTATE_UINT32(phy_int_mask, IMXFECState),
        VMSTATE_END_OF_LIST()
    }
};

#define PHY_INT_ENERGYON            (1 << 7)
#define PHY_INT_AUTONEG_COMPLETE    (1 << 6)
#define PHY_INT_FAULT               (1 << 5)
#define PHY_INT_DOWN                (1 << 4)
#define PHY_INT_AUTONEG_LP          (1 << 3)
#define PHY_INT_PARFAULT            (1 << 2)
#define PHY_INT_AUTONEG_PAGE        (1 << 1)

static void imx_fec_update(IMXFECState *s);

/*
 * The MII phy could raise a GPIO to the processor which in turn
 * could be handled as an interrpt by the OS.
 * For now we don't handle any GPIO/interrupt line, so the OS will
 * have to poll for the PHY status.
 */
static void phy_update_irq(IMXFECState *s)
{
    imx_fec_update(s);
}

static void phy_update_link(IMXFECState *s)
{
    /* Autonegotiation status mirrors link status.  */
    if (qemu_get_queue(s->nic)->link_down) {
        PHY_PRINTF("link is down\n");
        s->phy_status &= ~0x0024;
        s->phy_int |= PHY_INT_DOWN;
    } else {
        PHY_PRINTF("link is up\n");
        s->phy_status |= 0x0024;
        s->phy_int |= PHY_INT_ENERGYON;
        s->phy_int |= PHY_INT_AUTONEG_COMPLETE;
    }
    phy_update_irq(s);
}

static void imx_fec_set_link(NetClientState *nc)
{
    phy_update_link(IMX_FEC(qemu_get_nic_opaque(nc)));
}

static void phy_reset(IMXFECState *s)
{
    s->phy_status = 0x7809;
    s->phy_control = 0x3000;
    s->phy_advertise = 0x01e1;
    s->phy_int_mask = 0;
    s->phy_int = 0;
    phy_update_link(s);
}

static uint32_t do_phy_read(IMXFECState *s, int reg)
{
    uint32_t val;

    if (reg > 31) {
        /* we only advertise one phy */
        return 0;
    }

    switch (reg) {
    case 0:     /* Basic Control */
        val = s->phy_control;
        break;
    case 1:     /* Basic Status */
        val = s->phy_status;
        break;
    case 2:     /* ID1 */
        val = 0x0007;
        break;
    case 3:     /* ID2 */
        val = 0xc0d1;
        break;
    case 4:     /* Auto-neg advertisement */
        val = s->phy_advertise;
        break;
    case 5:     /* Auto-neg Link Partner Ability */
        val = 0x0f71;
        break;
    case 6:     /* Auto-neg Expansion */
        val = 1;
        break;
    case 29:    /* Interrupt source.  */
        val = s->phy_int;
        s->phy_int = 0;
        phy_update_irq(s);
        break;
    case 30:    /* Interrupt mask */
        val = s->phy_int_mask;
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "[%s.phy]%s: reg %d not implemented\n",
                      TYPE_IMX_FEC, __func__, reg);
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s.phy]%s: Bad address at offset %d\n",
                      TYPE_IMX_FEC, __func__, reg);
        val = 0;
        break;
    }

    PHY_PRINTF("read 0x%04x @ %d\n", val, reg);

    return val;
}

static void do_phy_write(IMXFECState *s, int reg, uint32_t val)
{
    PHY_PRINTF("write 0x%04x @ %d\n", val, reg);

    if (reg > 31) {
        /* we only advertise one phy */
        return;
    }

    switch (reg) {
    case 0:     /* Basic Control */
        if (val & 0x8000) {
            phy_reset(s);
        } else {
            s->phy_control = val & 0x7980;
            /* Complete autonegotiation immediately.  */
            if (val & 0x1000) {
                s->phy_status |= 0x0020;
            }
        }
        break;
    case 4:     /* Auto-neg advertisement */
        s->phy_advertise = (val & 0x2d7f) | 0x80;
        break;
    case 30:    /* Interrupt mask */
        s->phy_int_mask = val & 0xff;
        phy_update_irq(s);
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "[%s.phy)%s: reg %d not implemented\n",
                      TYPE_IMX_FEC, __func__, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s.phy]%s: Bad address at offset %d\n",
                      TYPE_IMX_FEC, __func__, reg);
        break;
    }
}

static void imx_fec_read_bd(IMXFECBufDesc *bd, dma_addr_t addr)
{
    dma_memory_read(&address_space_memory, addr, bd, sizeof(*bd));
}

static void imx_fec_write_bd(IMXFECBufDesc *bd, dma_addr_t addr)
{
    dma_memory_write(&address_space_memory, addr, bd, sizeof(*bd));
}

static void imx_fec_update(IMXFECState *s)
{
    uint32_t active;
    uint32_t changed;

    active = s->eir & s->eimr;
    changed = active ^ s->irq_state;
    if (changed) {
        qemu_set_irq(s->irq, active);
    }
    s->irq_state = active;
}

static void imx_fec_do_tx(IMXFECState *s)
{
    int frame_size = 0;
    uint8_t frame[FEC_MAX_FRAME_SIZE];
    uint8_t *ptr = frame;
    uint32_t addr = s->tx_descriptor;

    while (1) {
        IMXFECBufDesc bd;
        int len;

        imx_fec_read_bd(&bd, addr);
        FEC_PRINTF("tx_bd %x flags %04x len %d data %08x\n",
                   addr, bd.flags, bd.length, bd.data);
        if ((bd.flags & FEC_BD_R) == 0) {
            /* Run out of descriptors to transmit.  */
            break;
        }
        len = bd.length;
        if (frame_size + len > FEC_MAX_FRAME_SIZE) {
            len = FEC_MAX_FRAME_SIZE - frame_size;
            s->eir |= FEC_INT_BABT;
        }
        dma_memory_read(&address_space_memory, bd.data, ptr, len);
        ptr += len;
        frame_size += len;
        if (bd.flags & FEC_BD_L) {
            /* Last buffer in frame.  */
            qemu_send_packet(qemu_get_queue(s->nic), frame, len);
            ptr = frame;
            frame_size = 0;
            s->eir |= FEC_INT_TXF;
        }
        s->eir |= FEC_INT_TXB;
        bd.flags &= ~FEC_BD_R;
        /* Write back the modified descriptor.  */
        imx_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & FEC_BD_W) != 0) {
            addr = s->etdsr;
        } else {
            addr += 8;
        }
    }

    s->tx_descriptor = addr;

    imx_fec_update(s);
}

static void imx_fec_enable_rx(IMXFECState *s)
{
    IMXFECBufDesc bd;
    uint32_t tmp;

    imx_fec_read_bd(&bd, s->rx_descriptor);

    tmp = ((bd.flags & FEC_BD_E) != 0);

    if (!tmp) {
        FEC_PRINTF("RX buffer full\n");
    } else if (!s->rx_enabled) {
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
    }

    s->rx_enabled = tmp;
}

static void imx_fec_reset(DeviceState *d)
{
    IMXFECState *s = IMX_FEC(d);

    /* Reset the FEC */
    s->eir = 0;
    s->eimr = 0;
    s->rx_enabled = 0;
    s->ecr = 0;
    s->mscr = 0;
    s->mibc = 0xc0000000;
    s->rcr = 0x05ee0001;
    s->tcr = 0;
    s->tfwr = 0;
    s->frsr = 0x500;
    s->miigsk_cfgr = 0;
    s->miigsk_enr = 0x6;

    /* We also reset the PHY */
    phy_reset(s);
}

static uint64_t imx_fec_read(void *opaque, hwaddr addr, unsigned size)
{
    IMXFECState *s = IMX_FEC(opaque);

    FEC_PRINTF("reading from @ 0x%" HWADDR_PRIx "\n", addr);

    switch (addr & 0x3ff) {
    case 0x004:
        return s->eir;
    case 0x008:
        return s->eimr;
    case 0x010:
        return s->rx_enabled ? (1 << 24) : 0;   /* RDAR */
    case 0x014:
        return 0;   /* TDAR */
    case 0x024:
        return s->ecr;
    case 0x040:
        return s->mmfr;
    case 0x044:
        return s->mscr;
    case 0x064:
        return s->mibc; /* MIBC */
    case 0x084:
        return s->rcr;
    case 0x0c4:
        return s->tcr;
    case 0x0e4:     /* PALR */
        return (s->conf.macaddr.a[0] << 24)
               | (s->conf.macaddr.a[1] << 16)
               | (s->conf.macaddr.a[2] << 8)
               | s->conf.macaddr.a[3];
        break;
    case 0x0e8:     /* PAUR */
        return (s->conf.macaddr.a[4] << 24)
               | (s->conf.macaddr.a[5] << 16)
               | 0x8808;
    case 0x0ec:
        return 0x10000; /* OPD */
    case 0x118:
        return 0;
    case 0x11c:
        return 0;
    case 0x120:
        return 0;
    case 0x124:
        return 0;
    case 0x144:
        return s->tfwr;
    case 0x14c:
        return 0x600;
    case 0x150:
        return s->frsr;
    case 0x180:
        return s->erdsr;
    case 0x184:
        return s->etdsr;
    case 0x188:
        return s->emrbr;
    case 0x300:
        return s->miigsk_cfgr;
    case 0x308:
        return s->miigsk_enr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_FEC, __func__, addr);
        return 0;
    }
}

static void imx_fec_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned size)
{
    IMXFECState *s = IMX_FEC(opaque);

    FEC_PRINTF("writing 0x%08x @ 0x%" HWADDR_PRIx "\n", (int)value, addr);

    switch (addr & 0x3ff) {
    case 0x004: /* EIR */
        s->eir &= ~value;
        break;
    case 0x008: /* EIMR */
        s->eimr = value;
        break;
    case 0x010: /* RDAR */
        if ((s->ecr & FEC_EN) && !s->rx_enabled) {
            imx_fec_enable_rx(s);
        }
        break;
    case 0x014: /* TDAR */
        if (s->ecr & FEC_EN) {
            imx_fec_do_tx(s);
        }
        break;
    case 0x024: /* ECR */
        s->ecr = value;
        if (value & FEC_RESET) {
            imx_fec_reset(DEVICE(s));
        }
        if ((s->ecr & FEC_EN) == 0) {
            s->rx_enabled = 0;
        }
        break;
    case 0x040: /* MMFR */
        /* store the value */
        s->mmfr = value;
        if (extract32(value, 28, 1)) {
            do_phy_write(s, extract32(value, 18, 9), extract32(value, 0, 16));
        } else {
            s->mmfr = do_phy_read(s, extract32(value, 18, 9));
        }
        /* raise the interrupt as the PHY operation is done */
        s->eir |= FEC_INT_MII;
        break;
    case 0x044: /* MSCR */
        s->mscr = value & 0xfe;
        break;
    case 0x064: /* MIBC */
        /* TODO: Implement MIB.  */
        s->mibc = (value & 0x80000000) ? 0xc0000000 : 0;
        break;
    case 0x084: /* RCR */
        s->rcr = value & 0x07ff003f;
        /* TODO: Implement LOOP mode.  */
        break;
    case 0x0c4: /* TCR */
        /* We transmit immediately, so raise GRA immediately.  */
        s->tcr = value;
        if (value & 1) {
            s->eir |= FEC_INT_GRA;
        }
        break;
    case 0x0e4: /* PALR */
        s->conf.macaddr.a[0] = value >> 24;
        s->conf.macaddr.a[1] = value >> 16;
        s->conf.macaddr.a[2] = value >> 8;
        s->conf.macaddr.a[3] = value;
        break;
    case 0x0e8: /* PAUR */
        s->conf.macaddr.a[4] = value >> 24;
        s->conf.macaddr.a[5] = value >> 16;
        break;
    case 0x0ec: /* OPDR */
        break;
    case 0x118: /* IAUR */
    case 0x11c: /* IALR */
    case 0x120: /* GAUR */
    case 0x124: /* GALR */
        /* TODO: implement MAC hash filtering.  */
        break;
    case 0x144: /* TFWR */
        s->tfwr = value & 3;
        break;
    case 0x14c: /* FRBR */
        /* FRBR writes ignored.  */
        break;
    case 0x150: /* FRSR */
        s->frsr = (value & 0x3fc) | 0x400;
        break;
    case 0x180: /* ERDSR */
        s->erdsr = value & ~3;
        s->rx_descriptor = s->erdsr;
        break;
    case 0x184: /* ETDSR */
        s->etdsr = value & ~3;
        s->tx_descriptor = s->etdsr;
        break;
    case 0x188: /* EMRBR */
        s->emrbr = value & 0x7f0;
        break;
    case 0x300: /* MIIGSK_CFGR */
        s->miigsk_cfgr = value & 0x53;
        break;
    case 0x308: /* MIIGSK_ENR */
        s->miigsk_enr = (value & 0x2) ? 0x6 : 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_FEC, __func__, addr);
        break;
    }

    imx_fec_update(s);
}

static int imx_fec_can_receive(NetClientState *nc)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));

    return s->rx_enabled;
}

static ssize_t imx_fec_receive(NetClientState *nc, const uint8_t *buf,
                               size_t len)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));
    IMXFECBufDesc bd;
    uint32_t flags = 0;
    uint32_t addr;
    uint32_t crc;
    uint32_t buf_addr;
    uint8_t *crc_ptr;
    unsigned int buf_len;
    size_t size = len;

    FEC_PRINTF("len %d\n", (int)size);

    if (!s->rx_enabled) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Unexpected packet\n",
                      TYPE_IMX_FEC, __func__);
        return 0;
    }

    /* 4 bytes for the CRC.  */
    size += 4;
    crc = cpu_to_be32(crc32(~0, buf, size));
    crc_ptr = (uint8_t *) &crc;

    /* Huge frames are truncted.  */
    if (size > FEC_MAX_FRAME_SIZE) {
        size = FEC_MAX_FRAME_SIZE;
        flags |= FEC_BD_TR | FEC_BD_LG;
    }

    /* Frames larger than the user limit just set error flags.  */
    if (size > (s->rcr >> 16)) {
        flags |= FEC_BD_LG;
    }

    addr = s->rx_descriptor;
    while (size > 0) {
        imx_fec_read_bd(&bd, addr);
        if ((bd.flags & FEC_BD_E) == 0) {
            /* No descriptors available.  Bail out.  */
            /*
             * FIXME: This is wrong. We should probably either
             * save the remainder for when more RX buffers are
             * available, or flag an error.
             */
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Lost end of frame\n",
                          TYPE_IMX_FEC, __func__);
            break;
        }
        buf_len = (size <= s->emrbr) ? size : s->emrbr;
        bd.length = buf_len;
        size -= buf_len;

        FEC_PRINTF("rx_bd 0x%x length %d\n", addr, bd.length);

        /* The last 4 bytes are the CRC.  */
        if (size < 4) {
            buf_len += size - 4;
        }
        buf_addr = bd.data;
        dma_memory_write(&address_space_memory, buf_addr, buf, buf_len);
        buf += buf_len;
        if (size < 4) {
            dma_memory_write(&address_space_memory, buf_addr + buf_len,
                             crc_ptr, 4 - size);
            crc_ptr += 4 - size;
        }
        bd.flags &= ~FEC_BD_E;
        if (size == 0) {
            /* Last buffer in frame.  */
            bd.flags |= flags | FEC_BD_L;
            FEC_PRINTF("rx frame flags %04x\n", bd.flags);
            s->eir |= FEC_INT_RXF;
        } else {
            s->eir |= FEC_INT_RXB;
        }
        imx_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & FEC_BD_W) != 0) {
            addr = s->erdsr;
        } else {
            addr += 8;
        }
    }
    s->rx_descriptor = addr;
    imx_fec_enable_rx(s);
    imx_fec_update(s);
    return len;
}

static const MemoryRegionOps imx_fec_ops = {
    .read = imx_fec_read,
    .write = imx_fec_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void imx_fec_cleanup(NetClientState *nc)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));

    s->nic = NULL;
}

static NetClientInfo net_imx_fec_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = imx_fec_can_receive,
    .receive = imx_fec_receive,
    .cleanup = imx_fec_cleanup,
    .link_status_changed = imx_fec_set_link,
};


static void imx_fec_realize(DeviceState *dev, Error **errp)
{
    IMXFECState *s = IMX_FEC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx_fec_ops, s,
                          TYPE_IMX_FEC, 0x400);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->conf.peers.ncs[0] = nd_table[0].netdev;

    s->nic = qemu_new_nic(&net_imx_fec_info, &s->conf,
                          object_get_typename(OBJECT(dev)), DEVICE(dev)->id,
                          s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static Property imx_fec_properties[] = {
    DEFINE_NIC_PROPERTIES(IMXFECState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void imx_fec_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_imx_fec;
    dc->reset = imx_fec_reset;
    dc->props = imx_fec_properties;
    dc->realize = imx_fec_realize;
    dc->desc = "i.MX FEC Ethernet Controller";
}

static const TypeInfo imx_fec_info = {
    .name = TYPE_IMX_FEC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXFECState),
    .class_init = imx_fec_class_init,
};

static void imx_fec_register_types(void)
{
    type_register_static(&imx_fec_info);
}

type_init(imx_fec_register_types)
