/*
 *  x86_64 cpu related code
 *
 * Copyright (c) 2013 Stacey Son <sson@FreeBSD.org>
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

#include "qemu/osdep.h"

#include "cpu.h"
#include "qemu.h"
#include "qemu/timer.h"

#include "target_arch.h"

static uint64_t *idt_table;

uint64_t cpu_get_tsc(CPUX86State *env)
{
    return cpu_get_host_ticks();
}

void bsd_x86_64_write_dt(void *ptr, unsigned long addr,
        unsigned long limit, int flags)
{
    unsigned int e1, e2;
    uint32_t *p;
    e1 = (addr << 16) | (limit & 0xffff);
    e2 = ((addr >> 16) & 0xff) | (addr & 0xff000000) | (limit & 0x000f0000);
    e2 |= flags;
    p = ptr;
    p[0] = tswap32(e1);
    p[1] = tswap32(e2);
}

static void set_gate64(void *ptr, unsigned int type, unsigned int dpl,
        uint64_t addr, unsigned int sel)
{
    uint32_t *p, e1, e2;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    p = ptr;
    p[0] = tswap32(e1);
    p[1] = tswap32(e2);
    p[2] = tswap32(addr >> 32);
    p[3] = 0;
}

/* only dpl matters as we do only user space emulation */
void bsd_x86_64_set_idt(int n, unsigned int dpl)
{
    set_gate64(idt_table + n * 2, 0, dpl, 0, 0);
}

void bsd_x86_64_set_idt_base(uint64_t base)
{
    idt_table = g2h_untagged(base);
}
