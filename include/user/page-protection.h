/*
 * QEMU page protection declarations.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#ifndef USER_PAGE_PROTECTION_H
#define USER_PAGE_PROTECTION_H

#ifndef CONFIG_USER_ONLY
#error Cannot include this header from system emulation
#endif

#include "exec/translation-block.h"

void page_protect(tb_page_addr_t page_addr);
int page_unprotect(tb_page_addr_t address, uintptr_t pc);

#endif
