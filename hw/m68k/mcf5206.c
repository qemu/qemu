/*
 * Motorola ColdFire MCF5206 SoC embedded peripheral emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "cpu.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/m68k/mcf.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"

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

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);

    if ((s->tmr & TMR_RST) == 0) {
        goto exit;
    }

    prescale = (s->tmr >> 8) + 1;
    mode = (s->tmr >> 1) & 3;
    if (mode == 2)
        prescale *= 16;

    if (mode == 3 || mode == 0) {
        qemu_log_mask(LOG_UNIMP, "m5206_timer: mode %d not implemented\n",
                      mode);
        goto exit;
    }
    if ((s->tmr & TMR_FRR) == 0) {
        qemu_log_mask(LOG_UNIMP,
                      "m5206_timer: free running mode not implemented\n");
        goto exit;
    }

    /* Assume 66MHz system clock.  */
    ptimer_set_freq(s->timer, 66000000 / prescale);

    ptimer_set_limit(s->timer, s->trr, 0);

    ptimer_run(s->timer, 0);
exit:
    ptimer_transaction_commit(s->timer);
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
        ptimer_transaction_begin(s->timer);
        ptimer_set_count(s->timer, val);
        ptimer_transaction_commit(s->timer);
        break;
    case 0x11:
        s->ter &= ~val;
        break;
    default:
        break;
    }
    m5206_timer_update(s);
}

static void m5206_timer_init(m5206_timer_state *s, qemu_irq irq)
{
    s->timer = ptimer_init(m5206_timer_trigger, s, PTIMER_POLICY_LEGACY);
    s->irq = irq;
    m5206_timer_reset(s);
}

/* System Integration Module.  */

typedef struct {
    SysBusDevice parent_obj;

    M68kCPU *cpu;
    MemoryRegion iomem;
    qemu_irq *pic;
    m5206_timer_state timer[2];
    DeviceState *uart[2];
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

#define MCF5206_MBAR(obj) OBJECT_CHECK(m5206_mbar_state, (obj), TYPE_MCF5206_MBAR)

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
                qemu_log_mask(LOG_UNIMP, "%s: Unhandled vector for IRQ %d\n",
                              __func__, irq);
                vector = 0xf;
                break;
            }
        }
    } else {
        level = 0;
        vector = 0;
    }
    m68k_set_irq_level(s->cpu, level, vector);
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

static void m5206_mbar_reset(DeviceState *dev)
{
    m5206_mbar_state *s = MCF5206_MBAR(dev);

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

static uint64_t m5206_mbar_read(m5206_mbar_state *s,
                                uint16_t offset, unsigned size)
{
    if (offset >= 0x100 && offset < 0x120) {
        return m5206_timer_read(&s->timer[0], offset - 0x100);
    } else if (offset >= 0x120 && offset < 0x140) {
        return m5206_timer_read(&s->timer[1], offset - 0x120);
    } else if (offset >= 0x140 && offset < 0x160) {
        return mcf_uart_read(s->uart[0], offset - 0x140, size);
    } else if (offset >= 0x180 && offset < 0x1a0) {
        return mcf_uart_read(s->uart[1], offset - 0x180, size);
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
            while (mask > current_machine->ram_size) {
                mask >>= 1;
            }
            return mask & 0x0ffe0000;
        }
    case 0x5c: return 1; /* DRAM bank 1 empty.  */
    case 0xcb: return s->par;
    case 0x170: return s->uivr[0];
    case 0x1b0: return s->uivr[1];
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad MBAR offset 0x%"PRIx16"\n",
                  __func__, offset);
    return 0;
}

static void m5206_mbar_write(m5206_mbar_state *s, uint16_t offset,
                             uint64_t value, unsigned size)
{
    if (offset >= 0x100 && offset < 0x120) {
        m5206_timer_write(&s->timer[0], offset - 0x100, value);
        return;
    } else if (offset >= 0x120 && offset < 0x140) {
        m5206_timer_write(&s->timer[1], offset - 0x120, value);
        return;
    } else if (offset >= 0x140 && offset < 0x160) {
        mcf_uart_write(s->uart[0], offset - 0x140, value, size);
        return;
    } else if (offset >= 0x180 && offset < 0x1a0) {
        mcf_uart_write(s->uart[1], offset - 0x180, value, size);
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
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad MBAR offset 0x%"PRIx16"\n",
                      __func__, offset);
        break;
    }
}

/* Internal peripherals use a variety of register widths.
   This lookup table allows a single routine to handle all of them.  */
static const uint8_t m5206_mbar_width[] =
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

static uint32_t m5206_mbar_readw(void *opaque, hwaddr offset);
static uint32_t m5206_mbar_readl(void *opaque, hwaddr offset);

static uint32_t m5206_mbar_readb(void *opaque, hwaddr offset)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    offset &= 0x3ff;
    if (offset >= 0x200) {
        qemu_log_mask(LOG_GUEST_ERROR, "Bad MBAR read offset 0x%" HWADDR_PRIX,
                      offset);
        return 0;
    }
    if (m5206_mbar_width[offset >> 2] > 1) {
        uint16_t val;
        val = m5206_mbar_readw(opaque, offset & ~1);
        if ((offset & 1) == 0) {
            val >>= 8;
        }
        return val & 0xff;
    }
    return m5206_mbar_read(s, offset, 1);
}

static uint32_t m5206_mbar_readw(void *opaque, hwaddr offset)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset >= 0x200) {
        qemu_log_mask(LOG_GUEST_ERROR, "Bad MBAR read offset 0x%" HWADDR_PRIX,
                      offset);
        return 0;
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
    return m5206_mbar_read(s, offset, 2);
}

static uint32_t m5206_mbar_readl(void *opaque, hwaddr offset)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset >= 0x200) {
        qemu_log_mask(LOG_GUEST_ERROR, "Bad MBAR read offset 0x%" HWADDR_PRIX,
                      offset);
        return 0;
    }
    width = m5206_mbar_width[offset >> 2];
    if (width < 4) {
        uint32_t val;
        val = m5206_mbar_readw(opaque, offset) << 16;
        val |= m5206_mbar_readw(opaque, offset + 2);
        return val;
    }
    return m5206_mbar_read(s, offset, 4);
}

static void m5206_mbar_writew(void *opaque, hwaddr offset,
                              uint32_t value);
static void m5206_mbar_writel(void *opaque, hwaddr offset,
                              uint32_t value);

static void m5206_mbar_writeb(void *opaque, hwaddr offset,
                              uint32_t value)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset >= 0x200) {
        qemu_log_mask(LOG_GUEST_ERROR, "Bad MBAR write offset 0x%" HWADDR_PRIX,
                      offset);
        return;
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
    m5206_mbar_write(s, offset, value, 1);
}

static void m5206_mbar_writew(void *opaque, hwaddr offset,
                              uint32_t value)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset >= 0x200) {
        qemu_log_mask(LOG_GUEST_ERROR, "Bad MBAR write offset 0x%" HWADDR_PRIX,
                      offset);
        return;
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
    m5206_mbar_write(s, offset, value, 2);
}

static void m5206_mbar_writel(void *opaque, hwaddr offset,
                              uint32_t value)
{
    m5206_mbar_state *s = (m5206_mbar_state *)opaque;
    int width;
    offset &= 0x3ff;
    if (offset >= 0x200) {
        qemu_log_mask(LOG_GUEST_ERROR, "Bad MBAR write offset 0x%" HWADDR_PRIX,
                      offset);
        return;
    }
    width = m5206_mbar_width[offset >> 2];
    if (width < 4) {
        m5206_mbar_writew(opaque, offset, value >> 16);
        m5206_mbar_writew(opaque, offset + 2, value & 0xffff);
        return;
    }
    m5206_mbar_write(s, offset, value, 4);
}

static uint64_t m5206_mbar_readfn(void *opaque, hwaddr addr, unsigned size)
{
    switch (size) {
    case 1:
        return m5206_mbar_readb(opaque, addr);
    case 2:
        return m5206_mbar_readw(opaque, addr);
    case 4:
        return m5206_mbar_readl(opaque, addr);
    default:
        g_assert_not_reached();
    }
}

static void m5206_mbar_writefn(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        m5206_mbar_writeb(opaque, addr, value);
        break;
    case 2:
        m5206_mbar_writew(opaque, addr, value);
        break;
    case 4:
        m5206_mbar_writel(opaque, addr, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps m5206_mbar_ops = {
    .read = m5206_mbar_readfn,
    .write = m5206_mbar_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void mcf5206_mbar_realize(DeviceState *dev, Error **errp)
{
    m5206_mbar_state *s = MCF5206_MBAR(dev);

    memory_region_init_io(&s->iomem, NULL, &m5206_mbar_ops, s,
                          "mbar", 0x00001000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    s->pic = qemu_allocate_irqs(m5206_mbar_set_irq, s, 14);
    m5206_timer_init(&s->timer[0], s->pic[9]);
    m5206_timer_init(&s->timer[1], s->pic[10]);
    s->uart[0] = mcf_uart_create(s->pic[12], serial_hd(0));
    s->uart[1] = mcf_uart_create(s->pic[13], serial_hd(1));
}

static Property mcf5206_mbar_properties[] = {
    DEFINE_PROP_LINK("m68k-cpu", m5206_mbar_state, cpu,
                     TYPE_M68K_CPU, M68kCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void mcf5206_mbar_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, mcf5206_mbar_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "MCF5206 system integration module";
    dc->realize = mcf5206_mbar_realize;
    device_class_set_legacy_reset(dc, m5206_mbar_reset);
}

static const TypeInfo mcf5206_mbar_info = {
    .name          = TYPE_MCF5206_MBAR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(m5206_mbar_state),
    .class_init    = mcf5206_mbar_class_init,
};

static void mcf5206_mbar_register_types(void)
{
    type_register_static(&mcf5206_mbar_info);
}

type_init(mcf5206_mbar_register_types)
