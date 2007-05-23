/* 
 * Motorola ColdFire MCF5206 SoC embedded peripheral emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GPL
 */
#include "vl.h"

/* General purpose timer module.  */
typedef struct {
    uint16_t tmr;
    uint16_t trr;
    uint16_t tcr;
    uint16_t ter;
    ptimer_state *timer;
    qemu_irq irq;
    int irq_state;
} m5206_timer_state;

#define TMR_RST 0x01
#define TMR_CLK 0x06
#define TMR_FRR 0x08
#define TMR_ORI 0x10
#define TMR_OM  0x20
#define TMR_CE  0xc0

#define TER_CAP 0x01
#define TER_REF 0x02

static void m5206_timer_update(m5206_timer_state *s)
{
    if ((s->tmr & TMR_ORI) != 0 && (s->ter & TER_REF))
        qemu_irq_raise(s->irq);
    else
        qemu_irq_lower(s->irq);
}

static void m5206_timer_reset(m5206_timer_state *s)
{
    s->tmr = 0;
    s->trr = 0;
}

static void m5206_timer_recalibrate(m5206_timer_state *s)
{
    int prescale;
    int mode;

    ptimer_stop(s->timer);

    if ((s->tmr & TMR_RST) == 0)
        return;

    prescale = (s->tmr >> 8) + 1;
    mode = (s->tmr >> 1) & 3;
    if (mode == 2)
        prescale *= 16;

    if (mode == 3 || mode == 0)
        cpu_abort(cpu_single_env, 
                  "m5206_timer: mode %d not implemented\n", mode);
    if ((s->tmr & TMR_FRR) == 0)
        cpu_abort(cpu_single_env,
                  "m5206_timer: free running mode not implemented\n");

    /* Assume 66MHz system clock.  */
    ptimer_set_freq(s->timer, 66000000 / prescale);

    ptimer_set_limit(s->timer, s->trr, 0);

    ptimer_run(s->timer, 0);
}

static void m5206_timer_trigger(void *opaque)
{
    m5206_timer_state *s = (m5206_timer_state *)opaque;
    s->ter |= TER_REF;
    m5206_timer_update(s);
}

static uint32_t m5206_timer_read(m5206_timer_state *s, uint32_t addr)
{
    switch (addr) {
    case 0:
        return s->tmr;
    case 4:
        return s->trr;
    case 8:
        return s->tcr;
    case 0xc:
        return s->trr - ptimer_get_count(s->timer);
    case 0x11:
        return s->ter;
    default:
        return 0;
    }
}

static void m5206_timer_write(m5206_timer_state *s, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case 0:
        if ((s->tmr & TMR_RST) != 0 && (val & TMR_RST) == 0) {
            m5206_timer_reset(s);
        }
        s->tmr = val;
        m5206_timer_recalibrate(s);
        break;
    case 4:
        s->trr = val;
        m5206_timer_recalibrate(s);
        break;
    case 8:
        s->tcr = val;
        break;
    case 0xc:
        ptimer_set_count(s->timer, val);
        break;
    case 0x11:
        s->ter &= ~val;
        break;
    default:
        break;
    }
    m5206_timer_update(s);
}

static m5206_timer_state *m5206_timer_init(qemu_irq irq)
{
    m5206_timer_state *s;
    QEMUBH *bh;

    s = (m5206_timer_state *)qemu_mallocz(sizeof(m5206_timer_state));
    bh = qemu_bh_new(m5206_timer_trigger, s);
    s->timer = ptimer_init(bh);
    s->irq = irq;
    m5206_timer_reset(s);
    return s;
}

/* UART */

typedef struct {
    uint8_t mr[2];
    uint8_t sr;
    uint8_t isr;
    uint8_t imr;
    uint8_t bg1;
    uint8_t bg2;
    uint8_t fifo[4];
    uint8_t tb;
    int current_mr;
    int fifo_len;
    int tx_enabled;
    int rx_enabled;
    qemu_irq irq;
    CharDriverState *chr;
} m5206_uart_state;

/* UART Status Register bits.  */
#define M5206_UART_RxRDY  0x01
#define M5206_UART_FFULL  0x02
#define M5206_UART_TxRDY  0x04
#define M5206_UART_TxEMP  0x08
#define M5206_UART_OE     0x10
#define M5206_UART_PE     0x20
#define M5206_UART_FE     0x40
#define M5206_UART_RB     0x80

/* Interrupt flags.  */
#define M5206_UART_TxINT  0x01
#define M5206_UART_RxINT  0x02
#define M5206_UART_DBINT  0x04
#define M5206_UART_COSINT 0x80

/* UMR1 flags.  */
#define M5206_UART_BC0    0x01
#define M5206_UART_BC1    0x02
#define M5206_UART_PT     0x04
#define M5206_UART_PM0    0x08
#define M5206_UART_PM1    0x10
#define M5206_UART_ERR    0x20
#define M5206_UART_RxIRQ  0x40
#define M5206_UART_RxRTS  0x80

static void m5206_uart_update(m5206_uart_state *s)
{
    s->isr &= ~(M5206_UART_TxINT | M5206_UART_RxINT);
    if (s->sr & M5206_UART_TxRDY)
        s->isr |= M5206_UART_TxINT;
    if ((s->sr & ((s->mr[0] & M5206_UART_RxIRQ)
                  ? M5206_UART_FFULL : M5206_UART_RxRDY)) != 0)
        s->isr |= M5206_UART_RxINT;

    qemu_set_irq(s->irq, (s->isr & s->imr) != 0);
}

static uint32_t m5206_uart_read(m5206_uart_state *s, uint32_t addr)
{
    switch (addr) {
    case 0x00:
        return s->mr[s->current_mr];
    case 0x04:
        return s->sr;
    case 0x0c:
        {
            uint8_t val;
            int i;

            if (s->fifo_len == 0)
                return 0;

            val = s->fifo[0];
            s->fifo_len--;
            for (i = 0; i < s->fifo_len; i++)
                s->fifo[i] = s->fifo[i + 1];
            s->sr &= ~M5206_UART_FFULL;
            if (s->fifo_len == 0)
                s->sr &= ~M5206_UART_RxRDY;
            m5206_uart_update(s);
            return val;
        }
    case 0x10:
        /* TODO: Implement IPCR.  */
        return 0;
    case 0x14:
        return s->isr;
    case 0x18:
        return s->bg1;
    case 0x1c:
        return s->bg2;
    default:
        return 0;
    }
}

/* Update TxRDY flag and set data if present and enabled.  */
static void m5206_uart_do_tx(m5206_uart_state *s)
{
    if (s->tx_enabled && (s->sr & M5206_UART_TxEMP) == 0) {
        if (s->chr)
            qemu_chr_write(s->chr, (unsigned char *)&s->tb, 1);
        s->sr |= M5206_UART_TxEMP;
    }
    if (s->tx_enabled) {
        s->sr |= M5206_UART_TxRDY;
    } else {
        s->sr &= ~M5206_UART_TxRDY;
    }
}

static void m5206_do_command(m5206_uart_state *s, uint8_t cmd)
{
    /* Misc command.  */
    switch ((cmd >> 4) & 3) {
    case 0: /* No-op.  */
        break;
    case 1: /* Reset mode register pointer.  */
        s->current_mr = 0;
        break;
    case 2: /* Reset receiver.  */
        s->rx_enabled = 0;
        s->fifo_len = 0;
        s->sr &= ~(M5206_UART_RxRDY | M5206_UART_FFULL);
        break;
    case 3: /* Reset transmitter.  */
        s->tx_enabled = 0;
        s->sr |= M5206_UART_TxEMP;
        s->sr &= ~M5206_UART_TxRDY;
        break;
    case 4: /* Reset error status.  */
        break;
    case 5: /* Reset break-change interrupt.  */
        s->isr &= ~M5206_UART_DBINT;
        break;
    case 6: /* Start break.  */
    case 7: /* Stop break.  */
        break;
    }

    /* Transmitter command.  */
    switch ((cmd >> 2) & 3) {
    case 0: /* No-op.  */
        break;
    case 1: /* Enable.  */
        s->tx_enabled = 1;
        m5206_uart_do_tx(s);
        break;
    case 2: /* Disable.  */
        s->tx_enabled = 0;
        m5206_uart_do_tx(s);
        break;
    case 3: /* Reserved.  */
        fprintf(stderr, "m5206_uart: Bad TX command\n");
        break;
    }

    /* Receiver command.  */
    switch (cmd & 3) {
    case 0: /* No-op.  */
        break;
    case 1: /* Enable.  */
        s->rx_enabled = 1;
        break;
    case 2:
        s->rx_enabled = 0;
        break;
    case 3: /* Reserved.  */
        fprintf(stderr, "m5206_uart: Bad RX command\n");
        break;
    }
}

static void m5206_uart_write(m5206_uart_state *s, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case 0x00:
        s->mr[s->current_mr] = val;
        s->current_mr = 1;
        break;
    case 0x04:
        /* CSR is ignored.  */
        break;
    case 0x08: /* Command Register.  */
        m5206_do_command(s, val);
        break;
    case 0x0c: /* Transmit Buffer.  */
        s->sr &= ~M5206_UART_TxEMP;
        s->tb = val;
        m5206_uart_do_tx(s);
        break;
    case 0x10:
        /* ACR is ignored.  */
        break;
    case 0x14:
        s->imr = val;
        break;
    default:
        break;
    }
    m5206_uart_update(s);
}

static void m5206_uart_reset(m5206_uart_state *s)
{
    s->fifo_len = 0;
    s->mr[0] = 0;
    s->mr[1] = 0;
    s->sr = M5206_UART_TxEMP;
    s->tx_enabled = 0;
    s->rx_enabled = 0;
    s->isr = 0;
    s->imr = 0;
}

static void m5206_uart_push_byte(m5206_uart_state *s, uint8_t data)
{
    /* Break events overwrite the last byte if the fifo is full.  */
    if (s->fifo_len == 4)
        s->fifo_len--;

    s->fifo[s->fifo_len] = data;
    s->fifo_len++;
    s->sr |= M5206_UART_RxRDY;
    if (s->fifo_len == 4)
        s->sr |= M5206_UART_FFULL;

    m5206_uart_update(s);
}

static void m5206_uart_event(void *opaque, int event)
{
    m5206_uart_state *s = (m5206_uart_state *)opaque;

    switch (event) {
    case CHR_EVENT_BREAK:
        s->isr |= M5206_UART_DBINT;
        m5206_uart_push_byte(s, 0);
        break;
    default:
        break;
    }
}

static int m5206_uart_can_receive(void *opaque)
{
    m5206_uart_state *s = (m5206_uart_state *)opaque;

    return s->rx_enabled && (s->sr & M5206_UART_FFULL) == 0;
}

static void m5206_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    m5206_uart_state *s = (m5206_uart_state *)opaque;

    m5206_uart_push_byte(s, buf[0]);
}

static m5206_uart_state *m5206_uart_init(qemu_irq irq, CharDriverState *chr)
{
    m5206_uart_state *s;

    s = qemu_mallocz(sizeof(m5206_uart_state));
    s->chr = chr;
    s->irq = irq;
    if (chr) {
        qemu_chr_add_handlers(chr, m5206_uart_can_receive, m5206_uart_receive,
                              m5206_uart_event, s);
    }
    m5206_uart_reset(s);
    return s;
}

/* System Integration Module.  */

typedef struct {
    CPUState *env;
    m5206_timer_state *timer[2];
    m5206_uart_state *uart[2];
    uint8_t scr;
    uint8_t icr[14];
    uint16_t imr; /* 1 == interrupt is masked.  */
    uint16_t ipr;
    uint8_t rsr;
    uint8_t swivr;
    uint8_t par;
    /* Include the UART vector registers here.  */
    uint8_t uivr[2];
} m5206_mbar_state;

/* Interrupt controller.  */

static int m5206_find_pending_irq(m5206_mbar_state *s)
{
    int level;
    int vector;
    uint16_t active;
    int i;

    level = 0;
    vector = 0;
    active = s->ipr & ~s->imr;
    if (!active)
        return 0;

    for (i = 1; i < 14; i++) {
        if (active & (1 << i)) {
            if ((s->icr[i] & 0x1f) > level) {
                level = s->icr[i] & 0x1f;
                vector = i;
            }
        }
    }

    if (level < 4)
        vector = 0;

    return vector;
}

static void m5206_mbar_update(m5206_mbar_state *s)
{
    int irq;
    int vector;
    int level;

    irq = m5206_find_pending_irq(s);
    if (irq) {
        int tmp;
        tmp = s->icr[irq];
        level = (tmp >> 2) & 7;
        if (tmp & 0x80) {
            /* Autovector.  */
            vector = 24 + level;
        } else {
            switch (irq) {
            case 8: /* SWT */
                vector = s->swivr;
                break;
            case 12: /* UART1 */
                vector = s->uivr[0];
                break;
            case 13: /* UART2 */
                vector = s->uivr[1];
                break;
            default:
                /* Unknown vector.  */
                fprintf(stderr, "Unhandled vector for IRQ %d\n", irq);
                vector = 0xf;
                break;
            }
        }
    } else {
        level = 0;
        vector = 0;
    }
    m68k_set_irq_level(s->env, level, vector);
}

static void m5206_mbar_set_irq(void *opaque, int irq, int level)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    if (level) {
        s->ipr |= 1 << irq;
    } else {
        s->ipr &= ~(1 << irq);
    }
    m5206_mbar_update(s);
}

/* System Integration Module.  */

static void m5206_mbar_reset(m5206_mbar_state *s)
{
    s->scr = 0xc0;
    s->icr[1] = 0x04;
    s->icr[2] = 0x08;
    s->icr[3] = 0x0c;
    s->icr[4] = 0x10;
    s->icr[5] = 0x14;
    s->icr[6] = 0x18;
    s->icr[7] = 0x1c;
    s->icr[8] = 0x1c;
    s->icr[9] = 0x80;
    s->icr[10] = 0x80;
    s->icr[11] = 0x80;
    s->icr[12] = 0x00;
    s->icr[13] = 0x00;
    s->imr = 0x3ffe;
    s->rsr = 0x80;
    s->swivr = 0x0f;
    s->par = 0;
}

static uint32_t m5206_mbar_read(m5206_mbar_state *s, uint32_t offset)
{
    if (offset >= 0x100 && offset < 0x120) {
        return m5206_timer_read(s->timer[0], offset - 0x100);
    } else if (offset >= 0x120 && offset < 0x140) {
        return m5206_timer_read(s->timer[1], offset - 0x120);
    } else if (offset >= 0x140 && offset < 0x160) {
        return m5206_uart_read(s->uart[0], offset - 0x140);
    } else if (offset >= 0x180 && offset < 0x1a0) {
        return m5206_uart_read(s->uart[1], offset - 0x180);
    }
    switch (offset) {
    case 0x03: return s->scr;
    case 0x14 ... 0x20: return s->icr[offset - 0x13];
    case 0x36: return s->imr;
    case 0x3a: return s->ipr;
    case 0x40: return s->rsr;
    case 0x41: return 0;
    case 0x42: return s->swivr;
    case 0x50:
        /* DRAM mask register.  */
        /* FIXME: currently hardcoded to 128Mb.  */
        {
            uint32_t mask = ~0;
            while (mask > ram_size)
                mask >>= 1;
            return mask & 0x0ffe0000;
        }
    case 0x5c: return 1; /* DRAM bank 1 empty.  */
    case 0xcb: return s->par;
    case 0x170: return s->uivr[0];
    case 0x1b0: return s->uivr[1];
    }
    cpu_abort(cpu_single_env, "Bad MBAR read offset 0x%x", (int)offset);
    return 0;
}

static void m5206_mbar_write(m5206_mbar_state *s, uint32_t offset,
                             uint32_t value)
{
    if (offset >= 0x100 && offset < 0x120) {
        m5206_timer_write(s->timer[0], offset - 0x100, value);
        return;
    } else if (offset >= 0x120 && offset < 0x140) {
        m5206_timer_write(s->timer[1], offset - 0x120, value);
        return;
    } else if (offset >= 0x140 && offset < 0x160) {
        m5206_uart_write(s->uart[0], offset - 0x140, value);
        return;
    } else if (offset >= 0x180 && offset < 0x1a0) {
        m5206_uart_write(s->uart[1], offset - 0x180, value);
        return;
    }
    switch (offset) {
    case 0x03:
        s->scr = value;
        break;
    case 0x14 ... 0x20:
        s->icr[offset - 0x13] = value;
        m5206_mbar_update(s);
        break;
    case 0x36:
        s->imr = value;
        m5206_mbar_update(s);
        break;
    case 0x40:
        s->rsr &= ~value;
        break;
    case 0x41:
        /* TODO: implement watchdog.  */
        break;
    case 0x42:
        s->swivr = value;
        break;
    case 0xcb:
        s->par = value;
        break;
    case 0x170:
        s->uivr[0] = value;
        break;
    case 0x178: case 0x17c: case 0x1c8: case 0x1bc:
        /* Not implemented: UART Output port bits.  */
        break;
    case 0x1b0:
        s->uivr[1] = value;
        break;
    default:
        cpu_abort(cpu_single_env, "Bad MBAR write offset 0x%x", (int)offset);
        break;
    }
}

/* Internal peripherals use a variety of register widths.
   This lookup table allows a single routine to handle all of them.  */
static const int m5206_mbar_width[] = 
{
  /* 000-040 */ 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  2, 2, 2, 2,
  /* 040-080 */ 1, 2, 2, 2,  4, 1, 2, 4,  1, 2, 4, 2,  2, 4, 2, 2,
  /* 080-0c0 */ 4, 2, 2, 4,  2, 2, 4, 2,  2, 4, 2, 2,  4, 2, 2, 4,
  /* 0c0-100 */ 2, 2, 1, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
  /* 100-140 */ 2, 2, 2, 2,  1, 0, 0, 0,  2, 2, 2, 2,  1, 0, 0, 0,
  /* 140-180 */ 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,
  /* 180-1c0 */ 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,
  /* 1c0-200 */ 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,
};

static uint32_t m5206_mbar_readw(void *opaque, target_phys_addr_t offset);
static uint32_t m5206_mbar_readl(void *opaque, target_phys_addr_t offset);

static uint32_t m5206_mbar_readb(void *opaque, target_phys_addr_t offset)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    offset &= 0x3ff;
    if (offset > 0x200) {
        cpu_abort(cpu_single_env, "Bad MBAR read offset 0x%x", (int)offset);
    }
    if (m5206_mbar_width[offset >> 2] > 1) {
        uint16_t val;
        val = m5206_mbar_readw(opaque, offset & ~1);
        if ((offset & 1) == 0) {
            val >>= 8;
        }
        return val & 0xff;
    }
    return m5206_mbar_read(s, offset);
}

static uint32_t m5206_mbar_readw(void *opaque, target_phys_addr_t offset)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset > 0x200) {
        cpu_abort(cpu_single_env, "Bad MBAR read offset 0x%x", (int)offset);
    }
    width = m5206_mbar_width[offset >> 2];
    if (width > 2) {
        uint32_t val;
        val = m5206_mbar_readl(opaque, offset & ~3);
        if ((offset & 3) == 0)
            val >>= 16;
        return val & 0xffff;
    } else if (width < 2) {
        uint16_t val;
        val = m5206_mbar_readb(opaque, offset) << 8;
        val |= m5206_mbar_readb(opaque, offset + 1);
        return val;
    }
    return m5206_mbar_read(s, offset);
}

static uint32_t m5206_mbar_readl(void *opaque, target_phys_addr_t offset)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset > 0x200) {
        cpu_abort(cpu_single_env, "Bad MBAR read offset 0x%x", (int)offset);
    }
    width = m5206_mbar_width[offset >> 2];
    if (width < 4) {
        uint32_t val;
        val = m5206_mbar_readw(opaque, offset) << 16;
        val |= m5206_mbar_readw(opaque, offset + 2);
        return val;
    }
    return m5206_mbar_read(s, offset);
}

static void m5206_mbar_writew(void *opaque, target_phys_addr_t offset,
                              uint32_t value);
static void m5206_mbar_writel(void *opaque, target_phys_addr_t offset,
                              uint32_t value);

static void m5206_mbar_writeb(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset > 0x200) {
        cpu_abort(cpu_single_env, "Bad MBAR write offset 0x%x", (int)offset);
    }
    width = m5206_mbar_width[offset >> 2];
    if (width > 1) {
        uint32_t tmp;
        tmp = m5206_mbar_readw(opaque, offset & ~1);
        if (offset & 1) {
            tmp = (tmp & 0xff00) | value;
        } else {
            tmp = (tmp & 0x00ff) | (value << 8);
        }
        m5206_mbar_writew(opaque, offset & ~1, tmp);
        return;
    }
    m5206_mbar_write(s, offset, value);
}

static void m5206_mbar_writew(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset > 0x200) {
        cpu_abort(cpu_single_env, "Bad MBAR write offset 0x%x", (int)offset);
    }
    width = m5206_mbar_width[offset >> 2];
    if (width > 2) {
        uint32_t tmp;
        tmp = m5206_mbar_readl(opaque, offset & ~3);
        if (offset & 3) {
            tmp = (tmp & 0xffff0000) | value;
        } else {
            tmp = (tmp & 0x0000ffff) | (value << 16);
        }
        m5206_mbar_writel(opaque, offset & ~3, tmp);
        return;
    } else if (width < 2) {
        m5206_mbar_writeb(opaque, offset, value >> 8);
        m5206_mbar_writeb(opaque, offset + 1, value & 0xff);
        return;
    }
    m5206_mbar_write(s, offset, value);
}

static void m5206_mbar_writel(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset > 0x200) {
        cpu_abort(cpu_single_env, "Bad MBAR write offset 0x%x", (int)offset);
    }
    width = m5206_mbar_width[offset >> 2];
    if (width < 4) {
        m5206_mbar_writew(opaque, offset, value >> 16);
        m5206_mbar_writew(opaque, offset + 2, value & 0xffff);
        return;
    }
    m5206_mbar_write(s, offset, value);
}

static CPUReadMemoryFunc *m5206_mbar_readfn[] = {
   m5206_mbar_readb,
   m5206_mbar_readw,
   m5206_mbar_readl
};

static CPUWriteMemoryFunc *m5206_mbar_writefn[] = {
   m5206_mbar_writeb,
   m5206_mbar_writew,
   m5206_mbar_writel
};

qemu_irq *mcf5206_init(uint32_t base, CPUState *env)
{
    m5206_mbar_state *s;
    qemu_irq *pic;
    int iomemtype;

    s = (m5206_mbar_state *)qemu_mallocz(sizeof(m5206_mbar_state));
    iomemtype = cpu_register_io_memory(0, m5206_mbar_readfn,
                                       m5206_mbar_writefn, s);
    cpu_register_physical_memory(base, 0x00000fff, iomemtype);

    pic = qemu_allocate_irqs(m5206_mbar_set_irq, s, 14);
    s->timer[0] = m5206_timer_init(pic[9]);
    s->timer[1] = m5206_timer_init(pic[10]);
    s->uart[0] = m5206_uart_init(pic[12], serial_hds[0]);
    s->uart[1] = m5206_uart_init(pic[13], serial_hds[1]);
    s->env = env;

    m5206_mbar_reset(s);
    return pic;
}

