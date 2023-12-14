/*
 * TI OMAP processors UART emulation.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "chardev/char.h"
#include "hw/arm/omap.h"
#include "hw/char/serial.h"
#include "exec/address-spaces.h"

/* UARTs */
struct omap_uart_s {
    MemoryRegion iomem;
    hwaddr base;
    SerialMM *serial; /* TODO */
    struct omap_target_agent_s *ta;
    omap_clk fclk;
    qemu_irq irq;

    uint8_t eblr;
    uint8_t syscontrol;
    uint8_t wkup;
    uint8_t cfps;
    uint8_t mdr[2];
    uint8_t scr;
    uint8_t clksel;
};

void omap_uart_reset(struct omap_uart_s *s)
{
    s->eblr = 0x00;
    s->syscontrol = 0;
    s->wkup = 0x3f;
    s->cfps = 0x69;
    s->clksel = 0;
}

struct omap_uart_s *omap_uart_init(hwaddr base,
                qemu_irq irq, omap_clk fclk, omap_clk iclk,
                qemu_irq txdma, qemu_irq rxdma,
                const char *label, Chardev *chr)
{
    struct omap_uart_s *s = g_new0(struct omap_uart_s, 1);

    s->base = base;
    s->fclk = fclk;
    s->irq = irq;
    s->serial = serial_mm_init(get_system_memory(), base, 2, irq,
                               omap_clk_getrate(fclk)/16,
                               chr ?: qemu_chr_new(label, "null", NULL),
                               DEVICE_NATIVE_ENDIAN);
    return s;
}

static uint64_t omap_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    struct omap_uart_s *s = opaque;

    if (size == 4) {
        return omap_badwidth_read8(opaque, addr);
    }

    switch (addr) {
    case 0x20:	/* MDR1 */
        return s->mdr[0];
    case 0x24:	/* MDR2 */
        return s->mdr[1];
    case 0x40:	/* SCR */
        return s->scr;
    case 0x44:	/* SSR */
        return 0x0;
    case 0x48:	/* EBLR (OMAP2) */
        return s->eblr;
    case 0x4C:	/* OSC_12M_SEL (OMAP1) */
        return s->clksel;
    case 0x50:	/* MVR */
        return 0x30;
    case 0x54:	/* SYSC (OMAP2) */
        return s->syscontrol;
    case 0x58:	/* SYSS (OMAP2) */
        return 1;
    case 0x5c:	/* WER (OMAP2) */
        return s->wkup;
    case 0x60:	/* CFPS (OMAP2) */
        return s->cfps;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_uart_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_uart_s *s = opaque;

    if (size == 4) {
        omap_badwidth_write8(opaque, addr, value);
        return;
    }

    switch (addr) {
    case 0x20:	/* MDR1 */
        s->mdr[0] = value & 0x7f;
        break;
    case 0x24:	/* MDR2 */
        s->mdr[1] = value & 0xff;
        break;
    case 0x40:	/* SCR */
        s->scr = value & 0xff;
        break;
    case 0x48:	/* EBLR (OMAP2) */
        s->eblr = value & 0xff;
        break;
    case 0x4C:	/* OSC_12M_SEL (OMAP1) */
        s->clksel = value & 1;
        break;
    case 0x44:	/* SSR */
    case 0x50:	/* MVR */
    case 0x58:	/* SYSS (OMAP2) */
        OMAP_RO_REG(addr);
        break;
    case 0x54:	/* SYSC (OMAP2) */
        s->syscontrol = value & 0x1d;
        if (value & 2)
            omap_uart_reset(s);
        break;
    case 0x5c:	/* WER (OMAP2) */
        s->wkup = value & 0x7f;
        break;
    case 0x60:	/* CFPS (OMAP2) */
        s->cfps = value & 0xff;
        break;
    default:
        OMAP_BAD_REG(addr);
    }
}

static const MemoryRegionOps omap_uart_ops = {
    .read = omap_uart_read,
    .write = omap_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

struct omap_uart_s *omap2_uart_init(MemoryRegion *sysmem,
                struct omap_target_agent_s *ta,
                qemu_irq irq, omap_clk fclk, omap_clk iclk,
                qemu_irq txdma, qemu_irq rxdma,
                const char *label, Chardev *chr)
{
    hwaddr base = omap_l4_attach(ta, 0, NULL);
    struct omap_uart_s *s = omap_uart_init(base, irq,
                    fclk, iclk, txdma, rxdma, label, chr);

    memory_region_init_io(&s->iomem, NULL, &omap_uart_ops, s, "omap.uart", 0x100);

    s->ta = ta;

    memory_region_add_subregion(sysmem, base + 0x20, &s->iomem);

    return s;
}
