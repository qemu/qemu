/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef X86HVF_H
#define X86HVF_H
#include "cpu.h"
#include "x86_descr.h"

int hvf_process_events(CPUState *cs);
bool hvf_inject_interrupts(CPUState *cs);
void hvf_set_segment(CPUState *cs, struct vmx_segment *vmx_seg,
                     SegmentCache *qseg, bool is_tr);
void hvf_get_segment(SegmentCache *qseg, struct vmx_segment *vmx_seg);
void hvf_put_xsave(CPUState *cs);
void hvf_put_msrs(CPUState *cs);
void hvf_get_xsave(CPUState *cs);
void hvf_get_msrs(CPUState *cs);
void vmx_clear_int_window_exiting(CPUState *cs);
void vmx_update_tpr(CPUState *cs);
#endif
