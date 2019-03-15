/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HVF_X86_DESCR_H
#define HVF_X86_DESCR_H

#include "x86.h"

typedef struct vmx_segment {
    uint16_t sel;
    uint64_t base;
    uint64_t limit;
    uint64_t ar;
} vmx_segment;

/* deal with vmstate descriptors */
void vmx_read_segment_descriptor(struct CPUState *cpu,
                                 struct vmx_segment *desc, enum X86Seg seg);
void vmx_write_segment_descriptor(CPUState *cpu, struct vmx_segment *desc,
                                  enum X86Seg seg);

x68_segment_selector vmx_read_segment_selector(struct CPUState *cpu,
                                               enum X86Seg seg);
void vmx_write_segment_selector(struct CPUState *cpu,
                                x68_segment_selector selector,
                                enum X86Seg seg);

uint64_t vmx_read_segment_base(struct CPUState *cpu, enum X86Seg seg);
void vmx_write_segment_base(struct CPUState *cpu, enum X86Seg seg,
                            uint64_t base);

void x86_segment_descriptor_to_vmx(struct CPUState *cpu,
                                   x68_segment_selector selector,
                                   struct x86_segment_descriptor *desc,
                                   struct vmx_segment *vmx_desc);

uint32_t vmx_read_segment_limit(CPUState *cpu, enum X86Seg seg);
uint32_t vmx_read_segment_ar(CPUState *cpu, enum X86Seg seg);
void vmx_segment_to_x86_descriptor(struct CPUState *cpu,
                                   struct vmx_segment *vmx_desc,
                                   struct x86_segment_descriptor *desc);

#endif
