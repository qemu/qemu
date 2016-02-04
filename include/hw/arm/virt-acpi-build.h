/*
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO.,LTD.
 *
 * Author: Shannon Zhao <zhaoshenglong@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_VIRT_ACPI_BUILD_H
#define QEMU_VIRT_ACPI_BUILD_H

#include "qemu-common.h"
#include "hw/arm/virt.h"

#define ACPI_GICC_ENABLED 1

typedef struct VirtGuestInfo {
    int smp_cpus;
    FWCfgState *fw_cfg;
    const MemMapEntry *memmap;
    const int *irqmap;
    bool use_highmem;
    int gic_version;
} VirtGuestInfo;


typedef struct VirtGuestInfoState {
    VirtGuestInfo info;
    Notifier machine_done;
} VirtGuestInfoState;

void virt_acpi_setup(VirtGuestInfo *guest_info);

#endif
