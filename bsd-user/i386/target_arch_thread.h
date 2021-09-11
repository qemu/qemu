/*
 *  i386 thread support
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
#ifndef _TARGET_ARCH_THREAD_H_
#define _TARGET_ARCH_THREAD_H_

/* Compare to vm_machdep.c cpu_set_upcall_kse() */
static inline void target_thread_set_upcall(CPUX86State *regs, abi_ulong entry,
    abi_ulong arg, abi_ulong stack_base, abi_ulong stack_size)
{
    /* XXX */
}

static inline void target_thread_init(struct target_pt_regs *regs,
        struct image_info *infop)
{
    regs->esp = infop->start_stack;
    regs->eip = infop->entry;

    /*
     * SVR4/i386 ABI (pages 3-31, 3-32) says that when the program starts %edx
     * contains a pointer to a function which might be registered using
     * `atexit'.  This provides a mean for the dynamic linker to call DT_FINI
     * functions for shared libraries that have been loaded before the code
     * runs.
     *
     * A value of 0 tells we have no such handler.
     */
    regs->edx = 0;
}

#endif /* !_TARGET_ARCH_THREAD_H_ */
