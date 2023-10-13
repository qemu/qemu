/*
 * QEMU SPARC CPU QOM header (target agnostic)
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef QEMU_SPARC_CPU_QOM_H
#define QEMU_SPARC_CPU_QOM_H

#include "hw/core/cpu.h"

#ifdef TARGET_SPARC64
#define TYPE_SPARC_CPU "sparc64-cpu"
#else
#define TYPE_SPARC_CPU "sparc-cpu"
#endif

OBJECT_DECLARE_CPU_TYPE(SPARCCPU, SPARCCPUClass, SPARC_CPU)

#define SPARC_CPU_TYPE_SUFFIX "-" TYPE_SPARC_CPU
#define SPARC_CPU_TYPE_NAME(model) model SPARC_CPU_TYPE_SUFFIX

#endif
