/*
 * QEMU display emulation
 *
 * Copyright (c) 2006 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* The Linux kernel contains a driver for an ASCII display.
 * Some kernel variants for MIPS use this display.
 * This emulation creates a virtual display (similar to serial and parallel
 * consoles).
 */

#include <stdio.h>              /* fprintf */
#include "mips_display.h"       /* mips_display_init */

#define logout(fmt, args...) fprintf(stderr, "MIPS\t%-24s" fmt, __func__, ##args)

#define ASCII_DISPLAY_POS_BASE     0x1f000418

static char mips_display_text[8];

static CharDriverState *mips_display;

static void io_writeb(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    logout("??? addr=0x%08x, val=0x%02x\n", addr, value);
}

static uint32_t io_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = 0;
    logout("??? addr=0x%08x, val=0x%02x\n", addr, value);
    return value;
}

static void io_writew(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    logout("??? addr=0x%08x, val=0x%04x\n", addr, value);
}

static uint32_t io_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = 0;
    logout("??? addr=0x%08x, val=0x%04x\n", addr, value);
    return value;
}

static void io_writel(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    if (addr & 0x7) {
        logout("??? addr=0x%08x, val=0x%08x\n", addr, value);
    } else if (addr >= ASCII_DISPLAY_POS_BASE && addr < ASCII_DISPLAY_POS_BASE + 4 * 2 * 8) {
        unsigned index = (addr - ASCII_DISPLAY_POS_BASE) / 4 / 2;
        mips_display_text[index] = (char)value;
        qemu_chr_printf(mips_display, "\r| %-8.8s |", mips_display_text);
    } else {
        logout("??? addr=0x%08x, val=0x%08x\n", addr, value);
    }
}

static uint32_t io_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t value = 0;
    logout("??? addr=0x%08x, val=0x%08x\n", addr, value);
    return value;
}

static CPUWriteMemoryFunc *const io_write[] = {
    io_writeb,
    io_writew,
    io_writel,
};

static CPUReadMemoryFunc *const io_read[] = {
    io_readb,
    io_readw,
    io_readl,
};

void mips_display_init(CPUState *env, const char *devname)
{
    int io_memory = cpu_register_io_memory(0, io_read, io_write, env);
    cpu_register_physical_memory(0x1f000000, 0x00010000, io_memory);
    mips_display = qemu_chr_open(devname);
    if (!strcmp(devname, "vc")) {
        qemu_chr_printf(mips_display, "MIPS Display\r\n");
        qemu_chr_printf(mips_display, "+----------+\r\n");
    }
}
