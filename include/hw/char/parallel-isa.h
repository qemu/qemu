/*
 * QEMU ISA Parallel PORT emulation
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2007 Marko Kohtala
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_PARALLEL_ISA_H
#define HW_PARALLEL_ISA_H

#include "parallel.h"

#include "system/ioport.h"
#include "hw/isa/isa.h"
#include "qom/object.h"

#define TYPE_ISA_PARALLEL "isa-parallel"
OBJECT_DECLARE_SIMPLE_TYPE(ISAParallelState, ISA_PARALLEL)

struct ISAParallelState {
    ISADevice parent_obj;

    uint32_t index;
    uint32_t iobase;
    uint32_t isairq;
    ParallelState state;
    PortioList portio_list;
};

void isa_parallel_set_iobase(ISADevice *parallel, hwaddr iobase);
void isa_parallel_set_enabled(ISADevice *parallel, bool enabled);

#endif /* HW_PARALLEL_ISA_H */
