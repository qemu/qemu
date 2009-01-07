/*
 * Luminary Micro Stellaris Ethernet Controller
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */
#include "hw.h"
#include "arm-misc.h"
#include "net.h"
#include <zlib.h>

//#define DEBUG_STELLARIS_ENET 1

#ifdef DEBUG_STELLARIS_ENET
#define DPRINTF(fmt, args...) \
do { printf("stellaris_enet: " fmt , ##args); } while (0)
#define BADF(fmt, args...) \
do { fprintf(stderr, "stellaris_enet: error: " fmt , ##args); exit(1);} while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#define BADF(fmt, args...) \
do { fprintf(stderr, "stellaris_enet: error: " fmt , ##args);} while (0)
#endif

#define SE_INT_RX       0x01
#define SE_INT_TXER     0x02
#define SE_INT_TXEMP    0x04
#define SE_INT_FOV      0x08
#define SE_INT_RXER     0x10
#define SE_INT_MD       0x20
#define SE_INT_PHY      0x40

#define SE_RCTL_RXEN    0x01
#define SE_RCTL_AMUL    0x02
#define SE_RCTL_PRMS    0x04
#define SE_RCTL_BADCRC  0x08
#define SE_RCTL_RSTFIFO 0x10

#define SE_TCTL_TXEN    0x01
#define SE_TCTL_PADEN   0x02
#define SE_TCTL_CRC     0x04
#define SE_TCTL_DUPLEX  0x08

typedef struct {
    uint32_t ris;
    uint32_t im;
    uint32_t rctl;
    uint32_t tctl;
    uint32_t thr;
    uint32_t mctl;
    uint32_t mdv;
    uint32_t mtxd;
    uint32_t mrxd;
    uint32_t np;
    int tx_frame_len;
    int tx_fifo_len;
    uint8_t tx_fifo[2048];
    /* Real hardware has a 2k fifo, which works out to be at most 31 packets.
       We implement a full 31 packet fifo.  */
    struct {
        uint8_t data[2048];
        int len;
    } rx[31];
    uint8_t *rx_fifo;
    int rx_fifo_len;
    int next_packet;
    VLANClientState *vc;
    qemu_irq irq;
    uint8_t macaddr[6];
} stellaris_enet_state;

static void stellaris_enet_update(stellaris_enet_state *s)
{
    qemu_set_irq(s->irq, (s->ris & s->im) != 0);
}

/* TODO: Implement MAC address filtering.  */
static void stellaris_enet_receive(void *opaque, const uint8_t *buf, int size)
{
    stellaris_enet_state *s = (stellaris_enet_state *)opaque;
    int n;
    uint8_t *p;
    uint32_t crc;

    if ((s->rctl & SE_RCTL_RXEN) == 0)
        return;
    if (s->np >= 31) {
        DPRINTF("Packet dropped\n");
        return;
    }

    DPRINTF("Received packet len=%d\n", size);
    n = s->next_packet + s->np;
    if (n >= 31)
        n -= 31;
    s->np++;

    s->rx[n].len = size + 6;
    p = s->rx[n].data;
    *(p++) = (size + 6);
    *(p++) = (size + 6) >> 8;
    memcpy (p, buf, size);
    p += size;
    crc = crc32(~0, buf, size);
    *(p++) = crc;
    *(p++) = crc >> 8;
    *(p++) = crc >> 16;
    *(p++) = crc >> 24;
    /* Clear the remaining bytes in the last word.  */
    if ((size & 3) != 2) {
        memset(p, 0, (6 - size) & 3);
    }

    s->ris |= SE_INT_RX;
    stellaris_enet_update(s);
}

static int stellaris_enet_can_receive(void *opaque)
{
    stellaris_enet_state *s = (stellaris_enet_state *)opaque;

    if ((s->rctl & SE_RCTL_RXEN) == 0)
        return 1;

    return (s->np < 31);
}

static uint32_t stellaris_enet_read(void *opaque, target_phys_addr_t offset)
{
    stellaris_enet_state *s = (stellaris_enet_state *)opaque;
    uint32_t val;

    switch (offset) {
    case 0x00: /* RIS */
        DPRINTF("IRQ status %02x\n", s->ris);
        return s->ris;
    case 0x04: /* IM */
        return s->im;
    case 0x08: /* RCTL */
        return s->rctl;
    case 0x0c: /* TCTL */
        return s->tctl;
    case 0x10: /* DATA */
        if (s->rx_fifo_len == 0) {
            if (s->np == 0) {
                BADF("RX underflow\n");
                return 0;
            }
            s->rx_fifo_len = s->rx[s->next_packet].len;
            s->rx_fifo = s->rx[s->next_packet].data;
            DPRINTF("RX FIFO start packet len=%d\n", s->rx_fifo_len);
        }
        val = s->rx_fifo[0] | (s->rx_fifo[1] << 8) | (s->rx_fifo[2] << 16)
              | (s->rx_fifo[3] << 24);
        s->rx_fifo += 4;
        s->rx_fifo_len -= 4;
        if (s->rx_fifo_len <= 0) {
            s->rx_fifo_len = 0;
            s->next_packet++;
            if (s->next_packet >= 31)
                s->next_packet = 0;
            s->np--;
            DPRINTF("RX done np=%d\n", s->np);
        }
        return val;
    case 0x14: /* IA0 */
        return s->macaddr[0] | (s->macaddr[1] << 8)
               | (s->macaddr[2] << 16) | (s->macaddr[3] << 24);
    case 0x18: /* IA1 */
        return s->macaddr[4] | (s->macaddr[5] << 8);
    case 0x1c: /* THR */
        return s->thr;
    case 0x20: /* MCTL */
        return s->mctl;
    case 0x24: /* MDV */
        return s->mdv;
    case 0x28: /* MADD */
        return 0;
    case 0x2c: /* MTXD */
        return s->mtxd;
    case 0x30: /* MRXD */
        return s->mrxd;
    case 0x34: /* NP */
        return s->np;
    case 0x38: /* TR */
        return 0;
    case 0x3c: /* Undocuented: Timestamp? */
        return 0;
    default:
        cpu_abort (cpu_single_env, "stellaris_enet_read: Bad offset %x\n",
                   (int)offset);
        return 0;
    }
}

static void stellaris_enet_write(void *opaque, target_phys_addr_t offset,
                        uint32_t value)
{
    stellaris_enet_state *s = (stellaris_enet_state *)opaque;

    switch (offset) {
    case 0x00: /* IACK */
        s->ris &= ~value;
        DPRINTF("IRQ ack %02x/%02x\n", value, s->ris);
        stellaris_enet_update(s);
        /* Clearing TXER also resets the TX fifo.  */
        if (value & SE_INT_TXER)
            s->tx_frame_len = -1;
        break;
    case 0x04: /* IM */
        DPRINTF("IRQ mask %02x/%02x\n", value, s->ris);
        s->im = value;
        stellaris_enet_update(s);
        break;
    case 0x08: /* RCTL */
        s->rctl = value;
        if (value & SE_RCTL_RSTFIFO) {
            s->rx_fifo_len = 0;
            s->np = 0;
            stellaris_enet_update(s);
        }
        break;
    case 0x0c: /* TCTL */
        s->tctl = value;
        break;
    case 0x10: /* DATA */
        if (s->tx_frame_len == -1) {
            s->tx_frame_len = value & 0xffff;
            if (s->tx_frame_len > 2032) {
                DPRINTF("TX frame too long (%d)\n", s->tx_frame_len);
                s->tx_frame_len = 0;
                s->ris |= SE_INT_TXER;
                stellaris_enet_update(s);
            } else {
                DPRINTF("Start TX frame len=%d\n", s->tx_frame_len);
                /* The value written does not include the ethernet header.  */
                s->tx_frame_len += 14;
                if ((s->tctl & SE_TCTL_CRC) == 0)
                    s->tx_frame_len += 4;
                s->tx_fifo_len = 0;
                s->tx_fifo[s->tx_fifo_len++] = value >> 16;
                s->tx_fifo[s->tx_fifo_len++] = value >> 24;
            }
        } else {
            s->tx_fifo[s->tx_fifo_len++] = value;
            s->tx_fifo[s->tx_fifo_len++] = value >> 8;
            s->tx_fifo[s->tx_fifo_len++] = value >> 16;
            s->tx_fifo[s->tx_fifo_len++] = value >> 24;
            if (s->tx_fifo_len >= s->tx_frame_len) {
                /* We don't implement explicit CRC, so just chop it off.  */
                if ((s->tctl & SE_TCTL_CRC) == 0)
                    s->tx_frame_len -= 4;
                if ((s->tctl & SE_TCTL_PADEN) && s->tx_frame_len < 60) {
                    memset(&s->tx_fifo[s->tx_frame_len], 0, 60 - s->tx_frame_len);
                    s->tx_fifo_len = 60;
                }
                qemu_send_packet(s->vc, s->tx_fifo, s->tx_frame_len);
                s->tx_frame_len = -1;
                s->ris |= SE_INT_TXEMP;
                stellaris_enet_update(s);
                DPRINTF("Done TX\n");
            }
        }
        break;
    case 0x14: /* IA0 */
        s->macaddr[0] = value;
        s->macaddr[1] = value >> 8;
        s->macaddr[2] = value >> 16;
        s->macaddr[3] = value >> 24;
        break;
    case 0x18: /* IA1 */
        s->macaddr[4] = value;
        s->macaddr[5] = value >> 8;
        break;
    case 0x1c: /* THR */
        s->thr = value;
        break;
    case 0x20: /* MCTL */
        s->mctl = value;
        break;
    case 0x24: /* MDV */
        s->mdv = value;
        break;
    case 0x28: /* MADD */
        /* ignored.  */
        break;
    case 0x2c: /* MTXD */
        s->mtxd = value & 0xff;
        break;
    case 0x30: /* MRXD */
    case 0x34: /* NP */
    case 0x38: /* TR */
        /* Ignored.  */
    case 0x3c: /* Undocuented: Timestamp? */
        /* Ignored.  */
        break;
    default:
        cpu_abort (cpu_single_env, "stellaris_enet_write: Bad offset %x\n",
                   (int)offset);
    }
}

static CPUReadMemoryFunc *stellaris_enet_readfn[] = {
   stellaris_enet_read,
   stellaris_enet_read,
   stellaris_enet_read
};

static CPUWriteMemoryFunc *stellaris_enet_writefn[] = {
   stellaris_enet_write,
   stellaris_enet_write,
   stellaris_enet_write
};
static void stellaris_enet_reset(stellaris_enet_state *s)
{
    s->mdv = 0x80;
    s->rctl = SE_RCTL_BADCRC;
    s->im = SE_INT_PHY | SE_INT_MD | SE_INT_RXER | SE_INT_FOV | SE_INT_TXEMP
            | SE_INT_TXER | SE_INT_RX;
    s->thr = 0x3f;
    s->tx_frame_len = -1;
}

static void stellaris_enet_save(QEMUFile *f, void *opaque)
{
    stellaris_enet_state *s = (stellaris_enet_state *)opaque;
    int i;

    qemu_put_be32(f, s->ris);
    qemu_put_be32(f, s->im);
    qemu_put_be32(f, s->rctl);
    qemu_put_be32(f, s->tctl);
    qemu_put_be32(f, s->thr);
    qemu_put_be32(f, s->mctl);
    qemu_put_be32(f, s->mdv);
    qemu_put_be32(f, s->mtxd);
    qemu_put_be32(f, s->mrxd);
    qemu_put_be32(f, s->np);
    qemu_put_be32(f, s->tx_frame_len);
    qemu_put_be32(f, s->tx_fifo_len);
    qemu_put_buffer(f, s->tx_fifo, sizeof(s->tx_fifo));
    for (i = 0; i < 31; i++) {
        qemu_put_be32(f, s->rx[i].len);
        qemu_put_buffer(f, s->rx[i].data, sizeof(s->rx[i].data));

    }
    qemu_put_be32(f, s->next_packet);
    qemu_put_be32(f, s->rx_fifo - s->rx[s->next_packet].data);
    qemu_put_be32(f, s->rx_fifo_len);
}

static int stellaris_enet_load(QEMUFile *f, void *opaque, int version_id)
{
    stellaris_enet_state *s = (stellaris_enet_state *)opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    s->ris = qemu_get_be32(f);
    s->im = qemu_get_be32(f);
    s->rctl = qemu_get_be32(f);
    s->tctl = qemu_get_be32(f);
    s->thr = qemu_get_be32(f);
    s->mctl = qemu_get_be32(f);
    s->mdv = qemu_get_be32(f);
    s->mtxd = qemu_get_be32(f);
    s->mrxd = qemu_get_be32(f);
    s->np = qemu_get_be32(f);
    s->tx_frame_len = qemu_get_be32(f);
    s->tx_fifo_len = qemu_get_be32(f);
    qemu_get_buffer(f, s->tx_fifo, sizeof(s->tx_fifo));
    for (i = 0; i < 31; i++) {
        s->rx[i].len = qemu_get_be32(f);
        qemu_get_buffer(f, s->rx[i].data, sizeof(s->rx[i].data));

    }
    s->next_packet = qemu_get_be32(f);
    s->rx_fifo = s->rx[s->next_packet].data + qemu_get_be32(f);
    s->rx_fifo_len = qemu_get_be32(f);

    return 0;
}

void stellaris_enet_init(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    stellaris_enet_state *s;
    int iomemtype;

    s = (stellaris_enet_state *)qemu_mallocz(sizeof(stellaris_enet_state));
    iomemtype = cpu_register_io_memory(0, stellaris_enet_readfn,
                                       stellaris_enet_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    s->irq = irq;
    memcpy(s->macaddr, nd->macaddr, 6);

    if (nd->vlan) {
        s->vc = qemu_new_vlan_client(nd->vlan, nd->model, nd->name,
                                     stellaris_enet_receive, stellaris_enet_can_receive, s);
        qemu_format_nic_info_str(s->vc, s->macaddr);
    }

    stellaris_enet_reset(s);
    register_savevm("stellaris_enet", -1, 1,
                    stellaris_enet_save, stellaris_enet_load, s);
}
