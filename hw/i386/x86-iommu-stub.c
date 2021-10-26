/*
 * Stubs for X86 IOMMU emulation
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/i386/x86-iommu.h"

void x86_iommu_iec_register_notifier(X86IOMMUState *iommu,
                                     iec_notify_fn fn, void *data)
{
}

X86IOMMUState *x86_iommu_get_default(void)
{
    return NULL;
}

bool x86_iommu_ir_supported(X86IOMMUState *s)
{
    return false;
}
