/*
 *  NetBSD setup_initial_stack() implementation.
 *
 *  Copyright (c) 2013-14 Stacey D. Son
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

#ifndef _TARGET_OS_STACK_H_
#define _TARGET_OS_STACK_H_

#include "target_arch_sigtramp.h"

static inline int setup_initial_stack(struct bsd_binprm *bprm, abi_ulong *p,
    abi_ulong *stringp)
{
    int i;
    abi_ulong stack_base;

    stack_base = (target_stkbas + target_stksiz) -
                  MAX_ARG_PAGES * TARGET_PAGE_SIZE;
    if (p) {
        *p = stack_base;
    }
    if (stringp) {
        *stringp = stack_base;
    }

    for (i = 0; i < MAX_ARG_PAGES; i++) {
        if (bprm->page[i]) {
            info->rss++;
            if (!memcpy_to_target(stack_base, bprm->page[i],
                        TARGET_PAGE_SIZE)) {
                errno = EFAULT;
                return -1;
            }
            g_free(bprm->page[i]);
        }
        stack_base += TARGET_PAGE_SIZE;
    }

    return 0;
}

#endif /* !_TARGET_OS_STACK_H_ */
