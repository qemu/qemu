/*
 * Arm PrimeCell PL022 Synchronous Serial Port
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "primecell.h"

//#define DEBUG_PL022 1

#ifdef DEBUG_PL022
#define DPRINTF(fmt, args...) \
do { printf("pl022: " fmt , ##args); } while (0)
#define BADF(fmt, args...) \
do { fprintf(stderr, "pl022: error: " fmt , ##args); exit(1);} while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#define BADF(fmt, args...) \
do { fprintf(stderr, "pl022: error: " fmt , ##args);} while (0)
#endif

#define PL022_CR1_LBM 0x01
#define PL022_CR1_SSE 0x02
#define PL022_CR1_MS  0x04
#define PL022_CR1_SDO 0x08

#define PL022_SR_TFE  0x01
#define PL022_SR_TNF  0x02
#define PL022_SR_RNE  0x04
#define PL022_SR_RFF  0x08
#define PL022_SR_BSY  0x10

#define PL022_INT_ROR 0x01
#define PL022_INT_RT  0x04
#define PL022_INT_RX  0x04
#define PL022_INT_TX  0x08

typedef struct {
    uint32_t cr0;
    uint32_t cr1;
    uint32_t bitmask;
    uint32_t sr;
    uint32_t cpsr;
    uint32_t is;
    uint32_t im;
    /* The FIFO head points to the next empty entry.  */
    int tx_fifo_head;
    int rx_fifo_head;
    int tx_fifo_len;
    int rx_fifo_len;
    uint16_t tx_fifo[8];
    uint16_t rx_fifo[8];
    qemu_irq irq;
    int (*xfer_cb)(void *, int);
    void *opaque;
} pl022_state;

static const unsigned char pl022_id[8] =
  { 0x22, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl022_update(pl022_state *s)
{
    s->sr = 0;
    if (s->tx_fifo_len == 0)
        s->sr |= PL022_SR_TFE;
    if (s->tx_fifo_len != 8)
        s->sr |= PL022_SR_TNF;
    if (s->rx_fifo_len != 0)
        s->sr |= PL022_SR_RNE;
    if (s->rx_fifo_len == 8)
        s->sr |= PL022_SR_RFF;
    if (s->tx_fifo_len)
        s->sr |= PL022_SR_BSY;
    s->is = 0;
    if (s->rx_fifo_len >= 4)
        s->is |= PL022_INT_RX;
    if (s->tx_fifo_len <= 4)
        s->is |= PL022_INT_TX;

    qemu_set_irq(s->irq, (s->is & s->im) != 0);
}

static void pl022_xfer(pl022_state *s)
{
    int i;
    int o;
    int val;

    if ((s->cr1 & PL022_CR1_SSE) == 0) {
        pl022_update(s);
        DPRINTF("Disabled\n");
        return;
    }

    DPRINTF("Maybe xfer %d/%d\n", s->tx_fifo_len, s->rx_fifo_len);
    i = (s->tx_fifo_head - s->tx_fifo_len) & 7;
    o = s->rx_fifo_head;
    /* ??? We do not emulate the line speed.
       This may break some applications.  The are two problematic cases:
        (a) A driver feeds data into the TX FIFO until it is full,
         and only then drains the RX FIFO.  On real hardware the CPU can
         feed data fast enough that the RX fifo never gets chance to overflow.
        (b) A driver transmits data, deliberately allowing the RX FIFO to
         overflow because it ignores the RX data anyway.

       We choose to support (a) by stalling the transmit engine if it would
       cause the RX FIFO to overflow.  In practice much transmit-only code
       falls into (a) because it flushes the RX FIFO to determine when
       the transfer has completed.  */
    while (s->tx_fifo_len && s->rx_fifo_len < 8) {
        DPRINTF("xfer\n");
        val = s->tx_fifo[i];
        if (s->cr1 & PL022_CR1_LBM) {
            /* Loopback mode.  */
        } else if (s->xfer_cb) {
            val = s->xfer_cb(s->opaque, val);
        } else {
            val = 0;
        }
        s->rx_fifo[o] = val & s->bitmask;
        i = (i + 1) & 7;
        o = (o + 1) & 7;
        s->tx_fifo_len--;
        s->rx_fifo_len++;
    }
    s->rx_fifo_head = o;
    pl022_update(s);
}

static uint32_t pl022_read(void *opaque, target_phys_addr_t offset)
{
    pl022_state *s = (pl022_state *)opaque;
    int val;

    if (offset >= 0xfe0 && offset < 0x1000) {
        return pl022_id[(offset - 0xfe0) >> 2];
    }
    switch (offset) {
    case 0x00: /* CR0 */
      return s->cr0;
    case 0x04: /* CR1 */
      return s->cr1;
    case 0x08: /* DR */
        if (s->rx_fifo_len) {
            val = s->rx_fifo[(s->rx_fifo_head - s->rx_fifo_len) & 7];
            DPRINTF("RX %02x\n", val);
            s->rx_fifo_len--;
            pl022_xfer(s);
        } else {
            val = 0;
        }
        return val;
    case 0x0c: /* SR */
        return s->sr;
    case 0x10: /* CPSR */
        return s->cpsr;
    case 0x14: /* IMSC */
        return s->im;
    case 0x18: /* RIS */
        return s->is;
    case 0x1c: /* MIS */
        return s->im & s->is;
    case 0x20: /* DMACR */
        /* Not implemented.  */
        return 0;
    default:
        cpu_abort (cpu_single_env, "pl022_read: Bad offset %x\n",
                   (int)offset);
        return 0;
    }
}

static void pl022_write(void *opaque, target_phys_addr_t offset,
                        uint32_t value)
{
    pl022_state *s = (pl022_state *)opaque;

    switch (offset) {
    case 0x00: /* CR0 */
        s->cr0 = value;
        /* Clock rate and format are ignored.  */
        s->bitmask = (1 << ((value & 15) + 1)) - 1;
        break;
    case 0x04: /* CR1 */
        s->cr1 = value;
        if ((s->cr1 & (PL022_CR1_MS | PL022_CR1_SSE))
                   == (PL022_CR1_MS | PL022_CR1_SSE)) {
            BADF("SPI slave mode not implemented\n");
        }
        pl022_xfer(s);
        break;
    case 0x08: /* DR */
        if (s->tx_fifo_len < 8) {
            DPRINTF("TX %02x\n", value);
            s->tx_fifo[s->tx_fifo_head] = value & s->bitmask;
            s->tx_fifo_head = (s->tx_fifo_head + 1) & 7;
            s->tx_fifo_len++;
            pl022_xfer(s);
        }
        break;
    case 0x10: /* CPSR */
        /* Prescaler.  Ignored.  */
        s->cpsr = value & 0xff;
        break;
    case 0x14: /* IMSC */
        s->im = value;
        pl022_update(s);
        break;
    case 0x20: /* DMACR */
        if (value)
            cpu_abort (cpu_single_env, "pl022: DMA not implemented\n");
        break;
    default:
        cpu_abort (cpu_single_env, "pl022_write: Bad offset %x\n",
                   (int)offset);
    }
}

static void pl022_reset(pl022_state *s)
{
    s->rx_fifo_len = 0;
    s->tx_fifo_len = 0;
    s->im = 0;
    s->is = PL022_INT_TX;
    s->sr = PL022_SR_TFE | PL022_SR_TNF;
}

static CPUReadMemoryFunc *pl022_readfn[] = {
   pl022_read,
   pl022_read,
   pl022_read
};

static CPUWriteMemoryFunc *pl022_writefn[] = {
   pl022_write,
   pl022_write,
   pl022_write
};

static void pl022_save(QEMUFile *f, void *opaque)
{
    pl022_state *s = (pl022_state *)opaque;
    int i;

    qemu_put_be32(f, s->cr0);
    qemu_put_be32(f, s->cr1);
    qemu_put_be32(f, s->bitmask);
    qemu_put_be32(f, s->sr);
    qemu_put_be32(f, s->cpsr);
    qemu_put_be32(f, s->is);
    qemu_put_be32(f, s->im);
    qemu_put_be32(f, s->tx_fifo_head);
    qemu_put_be32(f, s->rx_fifo_head);
    qemu_put_be32(f, s->tx_fifo_len);
    qemu_put_be32(f, s->rx_fifo_len);
    for (i = 0; i < 8; i++) {
        qemu_put_be16(f, s->tx_fifo[i]);
        qemu_put_be16(f, s->rx_fifo[i]);
    }
}

static int pl022_load(QEMUFile *f, void *opaque, int version_id)
{
    pl022_state *s = (pl022_state *)opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    s->cr0 = qemu_get_be32(f);
    s->cr1 = qemu_get_be32(f);
    s->bitmask = qemu_get_be32(f);
    s->sr = qemu_get_be32(f);
    s->cpsr = qemu_get_be32(f);
    s->is = qemu_get_be32(f);
    s->im = qemu_get_be32(f);
    s->tx_fifo_head = qemu_get_be32(f);
    s->rx_fifo_head = qemu_get_be32(f);
    s->tx_fifo_len = qemu_get_be32(f);
    s->rx_fifo_len = qemu_get_be32(f);
    for (i = 0; i < 8; i++) {
        s->tx_fifo[i] = qemu_get_be16(f);
        s->rx_fifo[i] = qemu_get_be16(f);
    }

    return 0;
}

void pl022_init(uint32_t base, qemu_irq irq, int (*xfer_cb)(void *, int),
                void * opaque)
{
    int iomemtype;
    pl022_state *s;

    s = (pl022_state *)qemu_mallocz(sizeof(pl022_state));
    iomemtype = cpu_register_io_memory(0, pl022_readfn,
                                       pl022_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    s->irq = irq;
    s->xfer_cb = xfer_cb;
    s->opaque = opaque;
    pl022_reset(s);
    register_savevm("pl022_ssp", -1, 1, pl022_save, pl022_load, s);
}


