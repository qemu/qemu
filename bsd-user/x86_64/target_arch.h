/*
 * Intel x86_64 specific prototypes for bsd-user
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef TARGET_ARCH_H
#define TARGET_ARCH_H

/* target_arch_cpu.c */
void bsd_x86_64_write_dt(void *ptr, unsigned long addr, unsigned long limit,
                int flags);
void bsd_x86_64_set_idt(int n, unsigned int dpl);
void bsd_x86_64_set_idt_base(uint64_t base);

#define target_cpu_set_tls(env, newtls)

#endif /* TARGET_ARCH_H */
