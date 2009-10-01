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
#include "ioport.h"

void cpu_outb(a_pio_addr addr, uint8_t val)
{
    fprintf(stderr, "outb: port=0x%04"FMT_pioaddr", data=%02"PRIx8"\n",
            addr, val);
}

void cpu_outw(a_pio_addr addr, uint16_t val)
{
    fprintf(stderr, "outw: port=0x%04"FMT_pioaddr", data=%04"PRIx16"\n",
            addr, val);
}

void cpu_outl(a_pio_addr addr, uint32_t val)
{
    fprintf(stderr, "outl: port=0x%04"FMT_pioaddr", data=%08"PRIx32"\n",
            addr, val);
}

uint8_t cpu_inb(a_pio_addr addr)
{
    fprintf(stderr, "inb: port=0x%04"FMT_pioaddr"\n", addr);
    return 0;
}

uint16_t cpu_inw(a_pio_addr addr)
{
    fprintf(stderr, "inw: port=0x%04"FMT_pioaddr"\n", addr);
    return 0;
}

uint32_t cpu_inl(a_pio_addr addr)
{
    fprintf(stderr, "inl: port=0x%04"FMT_pioaddr"\n", addr);
    return 0;
}
