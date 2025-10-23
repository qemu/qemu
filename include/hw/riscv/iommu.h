/*
 * QEMU emulation of an RISC-V IOMMU
 *
 * Copyright (C) 2022-2023 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_IOMMU_H
#define HW_RISCV_IOMMU_H

#include "qemu/osdep.h"
#include "qom/object.h"

#define TYPE_RISCV_IOMMU "riscv-iommu"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVIOMMUState, RISCV_IOMMU)

#define TYPE_RISCV_IOMMU_MEMORY_REGION "riscv-iommu-mr"
typedef struct RISCVIOMMUSpace RISCVIOMMUSpace;

#define TYPE_RISCV_IOMMU_PCI "riscv-iommu-pci"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVIOMMUStatePci, RISCV_IOMMU_PCI)

#define TYPE_RISCV_IOMMU_SYS "riscv-iommu-device"
OBJECT_DECLARE_SIMPLE_TYPE(RISCVIOMMUStateSys, RISCV_IOMMU_SYS)

#define FDT_IRQ_TYPE_EDGE_LOW 1

#endif
