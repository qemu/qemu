/*
 * QEMU Xen support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* header to be included in non-Xen-specific code */

#ifndef SYSTEM_XEN_H
#define SYSTEM_XEN_H

#include "exec/cpu-common.h"

#ifdef COMPILING_PER_TARGET
# ifdef CONFIG_XEN
#  define CONFIG_XEN_IS_POSSIBLE
# endif
#else
# define CONFIG_XEN_IS_POSSIBLE
#endif /* COMPILING_PER_TARGET */

#ifdef CONFIG_XEN_IS_POSSIBLE
extern bool xen_allowed;
#define xen_enabled()           (xen_allowed)
#else /* !CONFIG_XEN_IS_POSSIBLE */
#define xen_enabled() 0
#endif /* CONFIG_XEN_IS_POSSIBLE */

void xen_hvm_modified_memory(ram_addr_t start, ram_addr_t length);
void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size,
                   struct MemoryRegion *mr, Error **errp);
bool xen_mr_is_memory(MemoryRegion *mr);
bool xen_mr_is_grants(MemoryRegion *mr);
#endif
