/*
 * QEMU Xen support
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/* header to be included in non-Xen-specific code */

#ifndef SYSEMU_XEN_H
#define SYSEMU_XEN_H

#ifdef CONFIG_USER_ONLY
#error Cannot include sysemu/xen.h from user emulation
#endif

#include "exec/cpu-common.h"

#ifdef NEED_CPU_H
# ifdef CONFIG_XEN
#  define CONFIG_XEN_IS_POSSIBLE
# endif
#else
# define CONFIG_XEN_IS_POSSIBLE
#endif

#ifdef CONFIG_XEN_IS_POSSIBLE

extern bool xen_allowed;

#define xen_enabled()           (xen_allowed)

void xen_hvm_modified_memory(ram_addr_t start, ram_addr_t length);
void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size,
                   struct MemoryRegion *mr, Error **errp);

#else /* !CONFIG_XEN_IS_POSSIBLE */

#define xen_enabled() 0
static inline void xen_hvm_modified_memory(ram_addr_t start, ram_addr_t length)
{
    /* nothing */
}
static inline void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size,
                                 MemoryRegion *mr, Error **errp)
{
    g_assert_not_reached();
}

#endif /* CONFIG_XEN_IS_POSSIBLE */

#endif
