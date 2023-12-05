/*
 * Sparc cpu parameters for qemu.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef SPARC_CPU_PARAM_H
#define SPARC_CPU_PARAM_H

#ifdef TARGET_SPARC64
# define TARGET_LONG_BITS 64
# define TARGET_PAGE_BITS 13 /* 8k */
# define TARGET_PHYS_ADDR_SPACE_BITS  41
# ifdef TARGET_ABI32
#  define TARGET_VIRT_ADDR_SPACE_BITS 32
# else
#  define TARGET_VIRT_ADDR_SPACE_BITS 44
# endif
#else
# define TARGET_LONG_BITS 32
# define TARGET_PAGE_BITS 12 /* 4k */
# define TARGET_PHYS_ADDR_SPACE_BITS 36
# define TARGET_VIRT_ADDR_SPACE_BITS 32
#endif

/*
 * From Oracle SPARC Architecture 2015:
 *
 *   Compatibility notes: The PSO memory model described in SPARC V8 and
 *   SPARC V9 compatibility architecture specifications was never implemented
 *   in a SPARC V9 implementation and is not included in the Oracle SPARC
 *   Architecture specification.
 *
 *   The RMO memory model described in the SPARC V9 specification was
 *   implemented in some non-Sun SPARC V9 implementations, but is not
 *   directly supported in Oracle SPARC Architecture 2015 implementations.
 *
 * Therefore always use TSO in QEMU.
 *
 * D.5 Specification of Partial Store Order (PSO)
 *   ... [loads] are followed by an implied MEMBAR #LoadLoad | #LoadStore.
 *
 * D.6 Specification of Total Store Order (TSO)
 *   ... PSO with the additional requirement that all [stores] are followed
 *   by an implied MEMBAR #StoreStore.
 */
#define TCG_GUEST_DEFAULT_MO  (TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_MO_ST_ST)

#endif
