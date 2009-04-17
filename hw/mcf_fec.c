/*
 * ColdFire Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GPL
 */
#include "hw.h"
#include "net.h"
#include "mcf.h"
/* For crc32 */
#include <zlib.h>

//#define DEBUG_FEC 1

#ifdef DEBUG_FEC
#define DPRINTF(fmt, args...) \
do { printf("mcf_fec: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

#define FEC_MAX_FRAME_SIZE 2032

typedef struct {
    qemu_irq *irq;
    int mmio_index;
    VLANClientState *vc;
    uint32_t irq_state;
    uint32_t eir;
    uint32_t eimr;
    int rx_enabled;
    uint32_t rx_descriptor;
    uint32_t tx_descriptor;
    uint32_t ecr;
    uint32_t mmfr;
    uint32_t mscr;
    uint32_t rcr;
    uint32_t tcr;
    uint32_t tfwr;
    uint32_t rfsr;
    uint32_t erdsr;
    uint32_t etdsr;
    uint32_t emrbr;
    uint8_t macaddr[6];
} mcf_fec_state;

#define FEC_INT_HB   0x80000000
#define FEC_INT_BABR 0x40000000
#define FEC_INT_BABT 0x20000000
#define FEC_INT_GRA  0x10000000
#define FEC_INT_TXF  0x08000000
#define FEC_INT_TXB  0x04000000
#define FEC_INT_RXF  0x02000000
#define FEC_INT_RXB  0x01000000
#define FEC_INT_MII  0x00800000
#define FEC_INT_EB   0x00400000
#define FEC_INT_LC   0x00200000
#define FEC_INT_RL   0x00100000
#define FEC_INT_UN   0x00080000

#define FEC_EN      2
#define FEC_RESET   1

/* Map interrupt flags onto IRQ lines.  */
#define FEC_NUM_IRQ 13
static const uint32_t mcf_fec_irq_map[FEC_NUM_IRQ] = {
    FEC_INT_TXF,
    FEC_INT_TXB,
    FEC_INT_UN,
    FEC_INT_RL,
    FEC_INT_RXF,
    FEC_INT_RXB,
    FEC_INT_MII,
    FEC_INT_LC,
    FEC_INT_HB,
    FEC_INT_GRA,
    FEC_INT_EB,
    FEC_INT_BABT,
    FEC_INT_BABR
};

/* Buffer Descriptor.  */
typedef struct {
    uint16_t flags;
    uint16_t length;
    uint32_t data;
} mcf_fec_bd;

#define FEC_BD_R    0x8000
#define FEC_BD_E    0x8000
#define FEC_BD_O1   0x4000
#define FEC_BD_W    0x2000
#define FEC_BD_O2   0x1000
#define FEC_BD_L    0x0800
#define FEC_BD_TC   0x0400
#define FEC_BD_ABC  0x0200
#define FEC_BD_M    0x0100
#define FEC_BD_BC   0x0080
#define FEC_BD_MC   0x0040
#define FEC_BD_LG   0x0020
#define FEC_BD_NO   0x0010
#define FEC_BD_CR   0x0004
#define FEC_BD_OV   0x0002
#define FEC_BD_TR   0x0001

static void mcf_fec_read_bd(mcf_fec_bd *bd, uint32_t addr)
{
    cpu_physical_memory_read(addr, (uint8_t *)bd, sizeof(*bd));
    be16_to_cpus(&bd->flags);
    be16_to_cpus(&bd->length);
    be32_to_cpus(&bd->data);
}

static void mcf_fec_write_bd(mcf_fec_bd *bd, uint32_t addr)
{
    mcf_fec_bd tmp;
    tmp.flags = cpu_to_be16(bd->flags);
    tmp.length = cpu_to_be16(bd->length);
    tmp.data = cpu_to_be32(bd->data);
    cpu_physical_memory_write(addr, (uint8_t *)&tmp, sizeof(tmp));
}

static void mcf_fec_update(mcf_fec_state *s)
{
    uint32_t active;
    uint32_t changed;
    uint32_t mask;
    int i;

    active = s->eir & s->eimr;
    changed = active ^s->irq_state;
    for (i = 0; i < FEC_NUM_IRQ; i++) {
        mask = mcf_fec_irq_map[i];
        if (changed & mask) {
            DPRINTF("IRQ %d = %d\n", i, (active & mask) != 0);
            qemu_set_irq(s->irq[i], (active & mask) != 0);
        }
    }
    s->irq_state = active;
}

static void mcf_fec_do_tx(mcf_fec_state *s)
{
    uint32_t addr;
    mcf_fec_bd bd;
    int frame_size;
    int len;
    uint8_t frame[FEC_MAX_FRAME_SIZE];
    uint8_t *ptr;

    DPRINTF("do_tx\n");
    ptr = frame;
    frame_size = 0;
    addr = s->tx_descriptor;
    while (1) {
        mcf_fec_read_bd(&bd, addr);
        DPRINTF("tx_bd %x flags %04x len %d data %08x\n",
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
        cpu_physical_memory_read(bd.data, ptr, len);
        ptr += len;
        frame_size += len;
        if (bd.flags & FEC_BD_L) {
            /* Last buffer in frame.  */
            DPRINTF("Sending packet\n");
            qemu_send_packet(s->vc, frame, len);
            ptr = frame;
            frame_size = 0;
            s->eir |= FEC_INT_TXF;
        }
        s->eir |= FEC_INT_TXB;
        bd.flags &= ~FEC_BD_R;
        /* Write back the modified descriptor.  */
        mcf_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & FEC_BD_W) != 0) {
            addr = s->etdsr;
        } else {
            addr += 8;
        }
    }
    s->tx_descriptor = addr;
}

static void mcf_fec_enable_rx(mcf_fec_state *s)
{
    mcf_fec_bd bd;

    mcf_fec_read_bd(&bd, s->rx_descriptor);
    s->rx_enabled = ((bd.flags & FEC_BD_E) != 0);
    if (!s->rx_enabled)
        DPRINTF("RX buffer full\n");
}

static void mcf_fec_reset(mcf_fec_state *s)
{
    s->eir = 0;
    s->eimr = 0;
    s->rx_enabled = 0;
    s->ecr = 0;
    s->mscr = 0;
    s->rcr = 0x05ee0001;
    s->tcr = 0;
    s->tfwr = 0;
    s->rfsr = 0x500;
}

static uint32_t mcf_fec_read(void *opaque, target_phys_addr_t addr)
{
    mcf_fec_state *s = (mcf_fec_state *)opaque;
    switch (addr & 0x3ff) {
    case 0x004: return s->eir;
    case 0x008: return s->eimr;
    case 0x010: return s->rx_enabled ? (1 << 24) : 0; /* RDAR */
    case 0x014: return 0; /* TDAR */
    case 0x024: return s->ecr;
    case 0x040: return s->mmfr;
    case 0x044: return s->mscr;
    case 0x064: return 0; /* MIBC */
    case 0x084: return s->rcr;
    case 0x0c4: return s->tcr;
    case 0x0e4: /* PALR */
        return (s->macaddr[0] << 24) | (s->macaddr[1] << 16)
              | (s->macaddr[2] << 8) | s->macaddr[3];
        break;
    case 0x0e8: /* PAUR */
        return (s->macaddr[4] << 24) | (s->macaddr[5] << 16) | 0x8808;
    case 0x0ec: return 0x10000; /* OPD */
    case 0x118: return 0;
    case 0x11c: return 0;
    case 0x120: return 0;
    case 0x124: return 0;
    case 0x144: return s->tfwr;
    case 0x14c: return 0x600;
    case 0x150: return s->rfsr;
    case 0x180: return s->erdsr;
    case 0x184: return s->etdsr;
    case 0x188: return s->emrbr;
    default:
        cpu_abort(cpu_single_env, "mcf_fec_read: Bad address 0x%x\n",
                  (int)addr);
        return 0;
    }
}

static void mcf_fec_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    mcf_fec_state *s = (mcf_fec_state *)opaque;
    switch (addr & 0x3ff) {
    case 0x004:
        s->eir &= ~value;
        break;
    case 0x008:
        s->eimr = value;
        break;
    case 0x010: /* RDAR */
        if ((s->ecr & FEC_EN) && !s->rx_enabled) {
            DPRINTF("RX enable\n");
            mcf_fec_enable_rx(s);
        }
        break;
    case 0x014: /* TDAR */
        if (s->ecr & FEC_EN) {
            mcf_fec_do_tx(s);
        }
        break;
    case 0x024:
        s->ecr = value;
        if (value & FEC_RESET) {
            DPRINTF("Reset\n");
            mcf_fec_reset(s);
        }
        if ((s->ecr & FEC_EN) == 0) {
            s->rx_enabled = 0;
        }
        break;
    case 0x040:
        /* TODO: Implement MII.  */
        s->mmfr = value;
        break;
    case 0x044:
        s->mscr = value & 0xfe;
        break;
    case 0x064:
        /* TODO: Implement MIB.  */
        break;
    case 0x084:
        s->rcr = value & 0x07ff003f;
        /* TODO: Implement LOOP mode.  */
        break;
    case 0x0c4: /* TCR */
        /* We transmit immediately, so raise GRA immediately.  */
        s->tcr = value;
        if (value & 1)
            s->eir |= FEC_INT_GRA;
        break;
    case 0x0e4: /* PALR */
        s->macaddr[0] = value >> 24;
        s->macaddr[1] = value >> 16;
        s->macaddr[2] = value >> 8;
        s->macaddr[3] = value;
        break;
    case 0x0e8: /* PAUR */
        s->macaddr[4] = value >> 24;
        s->macaddr[5] = value >> 16;
        break;
    case 0x0ec:
        /* OPD */
        break;
    case 0x118:
    case 0x11c:
    case 0x120:
    case 0x124:
        /* TODO: implement MAC hash filtering.  */
        break;
    case 0x144:
        s->tfwr = value & 3;
        break;
    case 0x14c:
        /* FRBR writes ignored.  */
        break;
    case 0x150:
        s->rfsr = (value & 0x3fc) | 0x400;
        break;
    case 0x180:
        s->erdsr = value & ~3;
        s->rx_descriptor = s->erdsr;
        break;
    case 0x184:
        s->etdsr = value & ~3;
        s->tx_descriptor = s->etdsr;
        break;
    case 0x188:
        s->emrbr = value & 0x7f0;
        break;
    default:
        cpu_abort(cpu_single_env, "mcf_fec_write Bad address 0x%x\n",
                  (int)addr);
    }
    mcf_fec_update(s);
}

static int mcf_fec_can_receive(void *opaque)
{
    mcf_fec_state *s = (mcf_fec_state *)opaque;
    return s->rx_enabled;
}

static void mcf_fec_receive(void *opaque, const uint8_t *buf, int size)
{
    mcf_fec_state *s = (mcf_fec_state *)opaque;
    mcf_fec_bd bd;
    uint32_t flags = 0;
    uint32_t addr;
    uint32_t crc;
    uint32_t buf_addr;
    uint8_t *crc_ptr;
    unsigned int buf_len;

    DPRINTF("do_rx len %d\n", size);
    if (!s->rx_enabled) {
        fprintf(stderr, "mcf_fec_receive: Unexpected packet\n");
    }
    /* 4 bytes for the CRC.  */
    size += 4;
    crc = cpu_to_be32(crc32(~0, buf, size));
    crc_ptr = (uint8_t *)&crc;
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
        mcf_fec_read_bd(&bd, addr);
        if ((bd.flags & FEC_BD_E) == 0) {
            /* No descriptors available.  Bail out.  */
            /* FIXME: This is wrong.  We should probably either save the
               remainder for when more RX buffers are available, or
               flag an error.  */
            fprintf(stderr, "mcf_fec: Lost end of frame\n");
            break;
        }
        buf_len = (size <= s->emrbr) ? size: s->emrbr;
        bd.length = buf_len;
        size -= buf_len;
        DPRINTF("rx_bd %x length %d\n", addr, bd.length);
        /* The last 4 bytes are the CRC.  */
        if (size < 4)
            buf_len += size - 4;
        buf_addr = bd.data;
        cpu_physical_memory_write(buf_addr, buf, buf_len);
        buf += buf_len;
        if (size < 4) {
            cpu_physical_memory_write(buf_addr + buf_len, crc_ptr, 4 - size);
            crc_ptr += 4 - size;
        }
        bd.flags &= ~FEC_BD_E;
        if (size == 0) {
            /* Last buffer in frame.  */
            bd.flags |= flags | FEC_BD_L;
            DPRINTF("rx frame flags %04x\n", bd.flags);
            s->eir |= FEC_INT_RXF;
        } else {
            s->eir |= FEC_INT_RXB;
        }
        mcf_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & FEC_BD_W) != 0) {
            addr = s->erdsr;
        } else {
            addr += 8;
        }
    }
    s->rx_descriptor = addr;
    mcf_fec_enable_rx(s);
    mcf_fec_update(s);
}

static CPUReadMemoryFunc *mcf_fec_readfn[] = {
   mcf_fec_read,
   mcf_fec_read,
   mcf_fec_read
};

static CPUWriteMemoryFunc *mcf_fec_writefn[] = {
   mcf_fec_write,
   mcf_fec_write,
   mcf_fec_write
};

static void mcf_fec_cleanup(VLANClientState *vc)
{
    mcf_fec_state *s = vc->opaque;

    cpu_unregister_io_memory(s->mmio_index);

    qemu_free(s);
}

void mcf_fec_init(NICInfo *nd, target_phys_addr_t base, qemu_irq *irq)
{
    mcf_fec_state *s;

    qemu_check_nic_model(nd, "mcf_fec");

    s = (mcf_fec_state *)qemu_mallocz(sizeof(mcf_fec_state));
    s->irq = irq;
    s->mmio_index = cpu_register_io_memory(0, mcf_fec_readfn,
                                           mcf_fec_writefn, s);
    cpu_register_physical_memory(base, 0x400, s->mmio_index);

    s->vc = qemu_new_vlan_client(nd->vlan, nd->model, nd->name,
                                 mcf_fec_receive, mcf_fec_can_receive,
                                 mcf_fec_cleanup, s);
    memcpy(s->macaddr, nd->macaddr, 6);
    qemu_format_nic_info_str(s->vc, s->macaddr);
}
