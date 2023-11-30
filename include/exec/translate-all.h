/*
 *  Translated block handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TRANSLATE_ALL_H
#define TRANSLATE_ALL_H

#include "exec/exec-all.h"


/* translate-all.c */
void tb_check_watchpoint(CPUState *cpu, uintptr_t retaddr);

#ifdef CONFIG_USER_ONLY
void page_protect(tb_page_addr_t page_addr);
int page_unprotect(target_ulong address, uintptr_t pc);
#endif

#endif /* TRANSLATE_ALL_H */
