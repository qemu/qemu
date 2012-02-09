/*
 * SMSC 91C111 Ethernet interface emulation
 *
 * Copyright (c) 2005 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL
 */

#include "sysbus.h"
#include "net.h"
#include "devices.h"
/* For crc32 */
#include <zlib.h>

/* Number of 2k memory pages available.  */
#define NUM_PACKETS 4

typedef struct {
    SysBusDevice busdev;
    NICState *nic;
    NICConf conf;
    uint16_t tcr;
    uint16_t rcr;
    uint16_t cr;
    uint16_t ctr;
    uint16_t gpr;
    uint16_t ptr;
    uint16_t ercv;
    qemu_irq irq;
    int bank;
    int packet_num;
    int tx_alloc;
    /* Bitmask of allocated packets.  */
    int allocated;
    int tx_fifo_len;
    int tx_fifo[NUM_PACKETS];
    int rx_fifo_len;
    int rx_fifo[NUM_PACKETS];
    int tx_fifo_done_len;
    int tx_fifo_done[NUM_PACKETS];
    /* Packet buffer memory.  */
    uint8_t data[NUM_PACKETS][2048];
    uint8_t int_level;
    uint8_t int_mask;
    MemoryRegion mmio;
} smc91c111_state;

static const VMStateDescription vmstate_smc91c111 = {
    .name = "smc91c111",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT16(tcr, smc91c111_state),
        VMSTATE_UINT16(rcr, smc91c111_state),
        VMSTATE_UINT16(cr, smc91c111_state),
        VMSTATE_UINT16(ctr, smc91c111_state),
        VMSTATE_UINT16(gpr, smc91c111_state),
        VMSTATE_UINT16(ptr, smc91c111_state),
        VMSTATE_UINT16(ercv, smc91c111_state),
        VMSTATE_INT32(bank, smc91c111_state),
        VMSTATE_INT32(packet_num, smc91c111_state),
        VMSTATE_INT32(tx_alloc, smc91c111_state),
        VMSTATE_INT32(allocated, smc91c111_state),
        VMSTATE_INT32(tx_fifo_len, smc91c111_state),
        VMSTATE_INT32_ARRAY(tx_fifo, smc91c111_state, NUM_PACKETS),
        VMSTATE_INT32(rx_fifo_len, smc91c111_state),
        VMSTATE_INT32_ARRAY(rx_fifo, smc91c111_state, NUM_PACKETS),
        VMSTATE_INT32(tx_fifo_done_len, smc91c111_state),
        VMSTATE_INT32_ARRAY(tx_fifo_done, smc91c111_state, NUM_PACKETS),
        VMSTATE_BUFFER_UNSAFE(data, smc91c111_state, 0, NUM_PACKETS * 2048),
        VMSTATE_UINT8(int_level, smc91c111_state),
        VMSTATE_UINT8(int_mask, smc91c111_state),
        VMSTATE_END_OF_LIST()
    }
};

#define RCR_SOFT_RST  0x8000
#define RCR_STRIP_CRC 0x0200
#define RCR_RXEN      0x0100

#define TCR_EPH_LOOP  0x2000
#define TCR_NOCRC     0x0100
#define TCR_PAD_EN    0x0080
#define TCR_FORCOL    0x0004
#define TCR_LOOP      0x0002
#define TCR_TXEN      0x0001

#define INT_MD        0x80
#define INT_ERCV      0x40
#define INT_EPH       0x20
#define INT_RX_OVRN   0x10
#define INT_ALLOC     0x08
#define INT_TX_EMPTY  0x04
#define INT_TX        0x02
#define INT_RCV       0x01

#define CTR_AUTO_RELEASE  0x0800
#define CTR_RELOAD        0x0002
#define CTR_STORE         0x0001

#define RS_ALGNERR      0x8000
#define RS_BRODCAST     0x4000
#define RS_BADCRC       0x2000
#define RS_ODDFRAME     0x1000
#define RS_TOOLONG      0x0800
#define RS_TOOSHORT     0x0400
#define RS_MULTICAST    0x0001

/* Update interrupt status.  */
static void smc91c111_update(smc91c111_state *s)
{
    int level;

    if (s->tx_fifo_len == 0)
        s->int_level |= INT_TX_EMPTY;
    if (s->tx_fifo_done_len != 0)
        s->int_level |= INT_TX;
    level = (s->int_level & s->int_mask) != 0;
    qemu_set_irq(s->irq, level);
}

/* Try to allocate a packet.  Returns 0x80 on failure.  */
static int smc91c111_allocate_packet(smc91c111_state *s)
{
    int i;
    if (s->allocated == (1 << NUM_PACKETS) - 1) {
        return 0x80;
    }

    for (i = 0; i < NUM_PACKETS; i++) {
        if ((s->allocated & (1 << i)) == 0)
            break;
    }
    s->allocated |= 1 << i;
    return i;
}


/* Process a pending TX allocate.  */
static void smc91c111_tx_alloc(smc91c111_state *s)
{
    s->tx_alloc = smc91c111_allocate_packet(s);
    if (s->tx_alloc == 0x80)
        return;
    s->int_level |= INT_ALLOC;
    smc91c111_update(s);
}

/* Remove and item from the RX FIFO.  */
static void smc91c111_pop_rx_fifo(smc91c111_state *s)
{
    int i;

    s->rx_fifo_len--;
    if (s->rx_fifo_len) {
        for (i = 0; i < s->rx_fifo_len; i++)
            s->rx_fifo[i] = s->rx_fifo[i + 1];
        s->int_level |= INT_RCV;
    } else {
        s->int_level &= ~INT_RCV;
    }
    smc91c111_update(s);
}

/* Remove an item from the TX completion FIFO.  */
static void smc91c111_pop_tx_fifo_done(smc91c111_state *s)
{
    int i;

    if (s->tx_fifo_done_len == 0)
        return;
    s->tx_fifo_done_len--;
    for (i = 0; i < s->tx_fifo_done_len; i++)
        s->tx_fifo_done[i] = s->tx_fifo_done[i + 1];
}

/* Release the memory allocated to a packet.  */
static void smc91c111_release_packet(smc91c111_state *s, int packet)
{
    s->allocated &= ~(1 << packet);
    if (s->tx_alloc == 0x80)
        smc91c111_tx_alloc(s);
}

/* Flush the TX FIFO.  */
static void smc91c111_do_tx(smc91c111_state *s)
{
    int i;
    int len;
    int control;
    int packetnum;
    uint8_t *p;

    if ((s->tcr & TCR_TXEN) == 0)
        return;
    if (s->tx_fifo_len == 0)
        return;
    for (i = 0; i < s->tx_fifo_len; i++) {
        packetnum = s->tx_fifo[i];
        p = &s->data[packetnum][0];
        /* Set status word.  */
        *(p++) = 0x01;
        *(p++) = 0x40;
        len = *(p++);
        len |= ((int)*(p++)) << 8;
        len -= 6;
        control = p[len + 1];
        if (control & 0x20)
            len++;
        /* ??? This overwrites the data following the buffer.
           Don't know what real hardware does.  */
        if (len < 64 && (s->tcr & TCR_PAD_EN)) {
            memset(p + len, 0, 64 - len);
            len = 64;
        }
#if 0
        {
            int add_crc;

            /* The card is supposed to append the CRC to the frame.
               However none of the other network traffic has the CRC
               appended.  Suspect this is low level ethernet detail we
               don't need to worry about.  */
            add_crc = (control & 0x10) || (s->tcr & TCR_NOCRC) == 0;
            if (add_crc) {
                uint32_t crc;

                crc = crc32(~0, p, len);
                memcpy(p + len, &crc, 4);
                len += 4;
            }
        }
#endif
        if (s->ctr & CTR_AUTO_RELEASE)
            /* Race?  */
            smc91c111_release_packet(s, packetnum);
        else if (s->tx_fifo_done_len < NUM_PACKETS)
            s->tx_fifo_done[s->tx_fifo_done_len++] = packetnum;
        qemu_send_packet(&s->nic->nc, p, len);
    }
    s->tx_fifo_len = 0;
    smc91c111_update(s);
}

/* Add a packet to the TX FIFO.  */
static void smc91c111_queue_tx(smc91c111_state *s, int packet)
{
    if (s->tx_fifo_len == NUM_PACKETS)
        return;
    s->tx_fifo[s->tx_fifo_len++] = packet;
    smc91c111_do_tx(s);
}

static void smc91c111_reset(DeviceState *dev)
{
    smc91c111_state *s = FROM_SYSBUS(smc91c111_state, sysbus_from_qdev(dev));
    s->bank = 0;
    s->tx_fifo_len = 0;
    s->tx_fifo_done_len = 0;
    s->rx_fifo_len = 0;
    s->allocated = 0;
    s->packet_num = 0;
    s->tx_alloc = 0;
    s->tcr = 0;
    s->rcr = 0;
    s->cr = 0xa0b1;
    s->ctr = 0x1210;
    s->ptr = 0;
    s->ercv = 0x1f;
    s->int_level = INT_TX_EMPTY;
    s->int_mask = 0;
    smc91c111_update(s);
}

#define SET_LOW(name, val) s->name = (s->name & 0xff00) | val
#define SET_HIGH(name, val) s->name = (s->name & 0xff) | (val << 8)

static void smc91c111_writeb(void *opaque, target_phys_addr_t offset,
                             uint32_t value)
{
    smc91c111_state *s = (smc91c111_state *)opaque;

    offset = offset & 0xf;
    if (offset == 14) {
        s->bank = value;
        return;
    }
    if (offset == 15)
        return;
    switch (s->bank) {
    case 0:
        switch (offset) {
        case 0: /* TCR */
            SET_LOW(tcr, value);
            return;
        case 1:
            SET_HIGH(tcr, value);
            return;
        case 4: /* RCR */
            SET_LOW(rcr, value);
            return;
        case 5:
            SET_HIGH(rcr, value);
            if (s->rcr & RCR_SOFT_RST)
                smc91c111_reset(&s->busdev.qdev);
            return;
        case 10: case 11: /* RPCR */
            /* Ignored */
            return;
        case 12: case 13: /* Reserved */
            return;
        }
        break;

    case 1:
        switch (offset) {
        case 0: /* CONFIG */
            SET_LOW(cr, value);
            return;
        case 1:
            SET_HIGH(cr,value);
            return;
        case 2: case 3: /* BASE */
        case 4: case 5: case 6: case 7: case 8: case 9: /* IA */
            /* Not implemented.  */
            return;
        case 10: /* Genral Purpose */
            SET_LOW(gpr, value);
            return;
        case 11:
            SET_HIGH(gpr, value);
            return;
        case 12: /* Control */
            if (value & 1)
                fprintf(stderr, "smc91c111:EEPROM store not implemented\n");
            if (value & 2)
                fprintf(stderr, "smc91c111:EEPROM reload not implemented\n");
            value &= ~3;
            SET_LOW(ctr, value);
            return;
        case 13:
            SET_HIGH(ctr, value);
            return;
        }
        break;

    case 2:
        switch (offset) {
        case 0: /* MMU Command */
            switch (value >> 5) {
            case 0: /* no-op */
                break;
            case 1: /* Allocate for TX.  */
                s->tx_alloc = 0x80;
                s->int_level &= ~INT_ALLOC;
                smc91c111_update(s);
                smc91c111_tx_alloc(s);
                break;
            case 2: /* Reset MMU.  */
                s->allocated = 0;
                s->tx_fifo_len = 0;
                s->tx_fifo_done_len = 0;
                s->rx_fifo_len = 0;
                s->tx_alloc = 0;
                break;
            case 3: /* Remove from RX FIFO.  */
                smc91c111_pop_rx_fifo(s);
                break;
            case 4: /* Remove from RX FIFO and release.  */
                if (s->rx_fifo_len > 0) {
                    smc91c111_release_packet(s, s->rx_fifo[0]);
                }
                smc91c111_pop_rx_fifo(s);
                break;
            case 5: /* Release.  */
                smc91c111_release_packet(s, s->packet_num);
                break;
            case 6: /* Add to TX FIFO.  */
                smc91c111_queue_tx(s, s->packet_num);
                break;
            case 7: /* Reset TX FIFO.  */
                s->tx_fifo_len = 0;
                s->tx_fifo_done_len = 0;
                break;
            }
            return;
        case 1:
            /* Ignore.  */
            return;
        case 2: /* Packet Number Register */
            s->packet_num = value;
            return;
        case 3: case 4: case 5:
            /* Should be readonly, but linux writes to them anyway. Ignore.  */
            return;
        case 6: /* Pointer */
            SET_LOW(ptr, value);
            return;
        case 7:
            SET_HIGH(ptr, value);
            return;
        case 8: case 9: case 10: case 11: /* Data */
            {
                int p;
                int n;

                if (s->ptr & 0x8000)
                    n = s->rx_fifo[0];
                else
                    n = s->packet_num;
                p = s->ptr & 0x07ff;
                if (s->ptr & 0x4000) {
                    s->ptr = (s->ptr & 0xf800) | ((s->ptr + 1) & 0x7ff);
                } else {
                    p += (offset & 3);
                }
                s->data[n][p] = value;
            }
            return;
        case 12: /* Interrupt ACK.  */
            s->int_level &= ~(value & 0xd6);
            if (value & INT_TX)
                smc91c111_pop_tx_fifo_done(s);
            smc91c111_update(s);
            return;
        case 13: /* Interrupt mask.  */
            s->int_mask = value;
            smc91c111_update(s);
            return;
        }
        break;

    case 3:
        switch (offset) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            /* Multicast table.  */
            /* Not implemented.  */
            return;
        case 8: case 9: /* Management Interface.  */
            /* Not implemented.  */
            return;
        case 12: /* Early receive.  */
            s->ercv = value & 0x1f;
        case 13:
            /* Ignore.  */
            return;
        }
        break;
    }
    hw_error("smc91c111_write: Bad reg %d:%x\n", s->bank, (int)offset);
}

static uint32_t smc91c111_readb(void *opaque, target_phys_addr_t offset)
{
    smc91c111_state *s = (smc91c111_state *)opaque;

    offset = offset & 0xf;
    if (offset == 14) {
        return s->bank;
    }
    if (offset == 15)
        return 0x33;
    switch (s->bank) {
    case 0:
        switch (offset) {
        case 0: /* TCR */
            return s->tcr & 0xff;
        case 1:
            return s->tcr >> 8;
        case 2: /* EPH Status */
            return 0;
        case 3:
            return 0x40;
        case 4: /* RCR */
            return s->rcr & 0xff;
        case 5:
            return s->rcr >> 8;
        case 6: /* Counter */
        case 7:
            /* Not implemented.  */
            return 0;
        case 8: /* Memory size.  */
            return NUM_PACKETS;
        case 9: /* Free memory available.  */
            {
                int i;
                int n;
                n = 0;
                for (i = 0; i < NUM_PACKETS; i++) {
                    if (s->allocated & (1 << i))
                        n++;
                }
                return n;
            }
        case 10: case 11: /* RPCR */
            /* Not implemented.  */
            return 0;
        case 12: case 13: /* Reserved */
            return 0;
        }
        break;

    case 1:
        switch (offset) {
        case 0: /* CONFIG */
            return s->cr & 0xff;
        case 1:
            return s->cr >> 8;
        case 2: case 3: /* BASE */
            /* Not implemented.  */
            return 0;
        case 4: case 5: case 6: case 7: case 8: case 9: /* IA */
            return s->conf.macaddr.a[offset - 4];
        case 10: /* General Purpose */
            return s->gpr & 0xff;
        case 11:
            return s->gpr >> 8;
        case 12: /* Control */
            return s->ctr & 0xff;
        case 13:
            return s->ctr >> 8;
        }
        break;

    case 2:
        switch (offset) {
        case 0: case 1: /* MMUCR Busy bit.  */
            return 0;
        case 2: /* Packet Number.  */
            return s->packet_num;
        case 3: /* Allocation Result.  */
            return s->tx_alloc;
        case 4: /* TX FIFO */
            if (s->tx_fifo_done_len == 0)
                return 0x80;
            else
                return s->tx_fifo_done[0];
        case 5: /* RX FIFO */
            if (s->rx_fifo_len == 0)
                return 0x80;
            else
                return s->rx_fifo[0];
        case 6: /* Pointer */
            return s->ptr & 0xff;
        case 7:
            return (s->ptr >> 8) & 0xf7;
        case 8: case 9: case 10: case 11: /* Data */
            {
                int p;
                int n;

                if (s->ptr & 0x8000)
                    n = s->rx_fifo[0];
                else
                    n = s->packet_num;
                p = s->ptr & 0x07ff;
                if (s->ptr & 0x4000) {
                    s->ptr = (s->ptr & 0xf800) | ((s->ptr + 1) & 0x07ff);
                } else {
                    p += (offset & 3);
                }
                return s->data[n][p];
            }
        case 12: /* Interrupt status.  */
            return s->int_level;
        case 13: /* Interrupt mask.  */
            return s->int_mask;
        }
        break;

    case 3:
        switch (offset) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
            /* Multicast table.  */
            /* Not implemented.  */
            return 0;
        case 8: /* Management Interface.  */
            /* Not implemented.  */
            return 0x30;
        case 9:
            return 0x33;
        case 10: /* Revision.  */
            return 0x91;
        case 11:
            return 0x33;
        case 12:
            return s->ercv;
        case 13:
            return 0;
        }
        break;
    }
    hw_error("smc91c111_read: Bad reg %d:%x\n", s->bank, (int)offset);
    return 0;
}

static void smc91c111_writew(void *opaque, target_phys_addr_t offset,
                             uint32_t value)
{
    smc91c111_writeb(opaque, offset, value & 0xff);
    smc91c111_writeb(opaque, offset + 1, value >> 8);
}

static void smc91c111_writel(void *opaque, target_phys_addr_t offset,
                             uint32_t value)
{
    /* 32-bit writes to offset 0xc only actually write to the bank select
       register (offset 0xe)  */
    if (offset != 0xc)
        smc91c111_writew(opaque, offset, value & 0xffff);
    smc91c111_writew(opaque, offset + 2, value >> 16);
}

static uint32_t smc91c111_readw(void *opaque, target_phys_addr_t offset)
{
    uint32_t val;
    val = smc91c111_readb(opaque, offset);
    val |= smc91c111_readb(opaque, offset + 1) << 8;
    return val;
}

static uint32_t smc91c111_readl(void *opaque, target_phys_addr_t offset)
{
    uint32_t val;
    val = smc91c111_readw(opaque, offset);
    val |= smc91c111_readw(opaque, offset + 2) << 16;
    return val;
}

static int smc91c111_can_receive(VLANClientState *nc)
{
    smc91c111_state *s = DO_UPCAST(NICState, nc, nc)->opaque;

    if ((s->rcr & RCR_RXEN) == 0 || (s->rcr & RCR_SOFT_RST))
        return 1;
    if (s->allocated == (1 << NUM_PACKETS) - 1)
        return 0;
    return 1;
}

static ssize_t smc91c111_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    smc91c111_state *s = DO_UPCAST(NICState, nc, nc)->opaque;
    int status;
    int packetsize;
    uint32_t crc;
    int packetnum;
    uint8_t *p;

    if ((s->rcr & RCR_RXEN) == 0 || (s->rcr & RCR_SOFT_RST))
        return -1;
    /* Short packets are padded with zeros.  Receiving a packet
       < 64 bytes long is considered an error condition.  */
    if (size < 64)
        packetsize = 64;
    else
        packetsize = (size & ~1);
    packetsize += 6;
    crc = (s->rcr & RCR_STRIP_CRC) == 0;
    if (crc)
        packetsize += 4;
    /* TODO: Flag overrun and receive errors.  */
    if (packetsize > 2048)
        return -1;
    packetnum = smc91c111_allocate_packet(s);
    if (packetnum == 0x80)
        return -1;
    s->rx_fifo[s->rx_fifo_len++] = packetnum;

    p = &s->data[packetnum][0];
    /* ??? Multicast packets?  */
    status = 0;
    if (size > 1518)
        status |= RS_TOOLONG;
    if (size & 1)
        status |= RS_ODDFRAME;
    *(p++) = status & 0xff;
    *(p++) = status >> 8;
    *(p++) = packetsize & 0xff;
    *(p++) = packetsize >> 8;
    memcpy(p, buf, size & ~1);
    p += (size & ~1);
    /* Pad short packets.  */
    if (size < 64) {
        int pad;

        if (size & 1)
            *(p++) = buf[size - 1];
        pad = 64 - size;
        memset(p, 0, pad);
        p += pad;
        size = 64;
    }
    /* It's not clear if the CRC should go before or after the last byte in
       odd sized packets.  Linux disables the CRC, so that's no help.
       The pictures in the documentation show the CRC aligned on a 16-bit
       boundary before the last odd byte, so that's what we do.  */
    if (crc) {
        crc = crc32(~0, buf, size);
        *(p++) = crc & 0xff; crc >>= 8;
        *(p++) = crc & 0xff; crc >>= 8;
        *(p++) = crc & 0xff; crc >>= 8;
        *(p++) = crc & 0xff;
    }
    if (size & 1) {
        *(p++) = buf[size - 1];
        *p = 0x60;
    } else {
        *(p++) = 0;
        *p = 0x40;
    }
    /* TODO: Raise early RX interrupt?  */
    s->int_level |= INT_RCV;
    smc91c111_update(s);

    return size;
}

static const MemoryRegionOps smc91c111_mem_ops = {
    /* The special case for 32 bit writes to 0xc means we can't just
     * set .impl.min/max_access_size to 1, unfortunately
     */
    .old_mmio = {
        .read = { smc91c111_readb, smc91c111_readw, smc91c111_readl, },
        .write = { smc91c111_writeb, smc91c111_writew, smc91c111_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void smc91c111_cleanup(VLANClientState *nc)
{
    smc91c111_state *s = DO_UPCAST(NICState, nc, nc)->opaque;

    s->nic = NULL;
}

static NetClientInfo net_smc91c111_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = smc91c111_can_receive,
    .receive = smc91c111_receive,
    .cleanup = smc91c111_cleanup,
};

static int smc91c111_init1(SysBusDevice *dev)
{
    smc91c111_state *s = FROM_SYSBUS(smc91c111_state, dev);
    memory_region_init_io(&s->mmio, &smc91c111_mem_ops, s,
                          "smc91c111-mmio", 16);
    sysbus_init_mmio(dev, &s->mmio);
    sysbus_init_irq(dev, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_smc91c111_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->qdev.id, s);
    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);
    /* ??? Save/restore.  */
    return 0;
}

static Property smc91c111_properties[] = {
    DEFINE_NIC_PROPERTIES(smc91c111_state, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void smc91c111_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = smc91c111_init1;
    dc->reset = smc91c111_reset;
    dc->vmsd = &vmstate_smc91c111;
    dc->props = smc91c111_properties;
}

static TypeInfo smc91c111_info = {
    .name          = "smc91c111",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(smc91c111_state),
    .class_init    = smc91c111_class_init,
};

static void smc91c111_register_types(void)
{
    type_register_static(&smc91c111_info);
}

/* Legacy helper function.  Should go away when machine config files are
   implemented.  */
void smc91c111_init(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    qemu_check_nic_model(nd, "smc91c111");
    dev = qdev_create(NULL, "smc91c111");
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

type_init(smc91c111_register_types)
