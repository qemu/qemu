/*
 *  qemu user ioport functions
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include "qemu.h"
#include "qemu-common.h"
#include "exec/ioport.h"

void cpu_outb(pio_addr_t addr, uint8_t val)
{
    fprintf(stderr, "outb: port=0x%04"FMT_pioaddr", data=%02"PRIx8"\n",
            addr, val);
}

void cpu_outw(pio_addr_t addr, uint16_t val)
{
    fprintf(stderr, "outw: port=0x%04"FMT_pioaddr", data=%04"PRIx16"\n",
            addr, val);
}

void cpu_outl(pio_addr_t addr, uint32_t val)
{
    fprintf(stderr, "outl: port=0x%04"FMT_pioaddr", data=%08"PRIx32"\n",
            addr, val);
}

uint8_t cpu_inb(pio_addr_t addr)
{
    fprintf(stderr, "inb: port=0x%04"FMT_pioaddr"\n", addr);
    return 0;
}

uint16_t cpu_inw(pio_addr_t addr)
{
    fprintf(stderr, "inw: port=0x%04"FMT_pioaddr"\n", addr);
    return 0;
}

uint32_t cpu_inl(pio_addr_t addr)
{
    fprintf(stderr, "inl: port=0x%04"FMT_pioaddr"\n", addr);
    return 0;
}
