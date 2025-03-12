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
#include "hw/char/serial-mm.h"
#include "system/address-spaces.h"

/* UARTs */
struct omap_uart_s {
    MemoryRegion iomem;
    hwaddr base;
    SerialMM *serial; /* TODO */
    omap_clk fclk;
    qemu_irq irq;

    uint8_t eblr;
    uint8_t syscontrol;
    uint8_t wkup;
    uint8_t cfps;
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
                               omap_clk_getrate(fclk) / 16,
                               chr ?: qemu_chr_new(label, "null", NULL),
                               DEVICE_NATIVE_ENDIAN);
    return s;
}
