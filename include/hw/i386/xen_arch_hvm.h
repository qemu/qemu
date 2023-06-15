#ifndef HW_XEN_ARCH_I386_HVM_H
#define HW_XEN_ARCH_I386_HVM_H

#include <xen/hvm/ioreq.h>
#include "hw/xen/xen-hvm-common.h"

void arch_handle_ioreq(XenIOState *state, ioreq_t *req);
void arch_xen_set_memory(XenIOState *state,
                         MemoryRegionSection *section,
                         bool add);
#endif
